"""Prepare traceable WA replay captures and the complete Legislative Assembly ballot.

The original exports stay unchanged. Captures are copied with election-local
timestamps; consecutive files differing only in record update clocks are put in
a separate folder. A manifest records both clocks, source hashes and all ballot
entries, so minor candidates remain visible even when grouped as Other.
"""

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET
from zoneinfo import ZoneInfo

NS = {'w': 'http://tempuri.org/MediaExportSchema.xsd'}
PARTIES = {'ALP': 'ALP', 'LIB': 'LIB', 'NATS': 'NAT', 'GRN': 'GRN', 'PHON': 'ONP', 'IND': 'IND'}


def read_assembly(path):
    """Keep the lower house only: candidate exports also include the Council."""
    root = ET.parse(path).getroot()
    districts = {d.get('Name'): d for d in root.findall('w:ElectionRegion/w:ElectionDistrict', NS)
                 if d.find('w:LA', NS) is not None}
    if not districts or len(districts) != len(root.findall('w:ElectionRegion/w:ElectionDistrict', NS)):
        raise ValueError('Expected one complete, unambiguous Assembly district list.')
    return root, districts


def candidate_roster(path):
    """Return one official ballot entry per district and order, without promotion.

    A ballot entry lacking both a registered party and an Independent label is
    grouped as Other; a blank affiliation alone does not make it an independent.
    """
    root, districts = read_assembly(path)
    rows = []
    for name, district in districts.items():
        for candidate in district.findall('w:LA/w:Candidate', NS):
            code = candidate.get('RegisteredPartyAbbreviation', '')
            rows.append(dict(district=name, order=int(candidate.get('BallotPaperOrder')),
                name=candidate.get('BallotPaperName'), official_party=code,
                party=PARTIES.get(code, 'OTH'), independent=candidate.get('Independent') == 'true'))
    if len({(r['district'], r['order']) for r in rows}) != len(rows):
        raise ValueError('Duplicate Assembly ballot position.')
    return root.get('ElectionDate'), rows


def data_fingerprint(root):
    """Ignore update clocks only; counts, identities and status changes all matter.

    Comparing consecutive states retains genuine corrections, including a return
    to an earlier count. Percentages are retained conservatively: the filter must
    not conceal a change whose meaning the live reader does not yet understand.
    """
    def canonical(element):
        attributes = sorted((k, v) for k, v in element.attrib.items() if k != 'LastUpdated')
        return element.tag, attributes, (element.text or '').strip(), [canonical(c) for c in element]
    return hashlib.sha256(json.dumps(canonical(root), separators=(',', ':')).encode()).hexdigest()


def capture_time(code, source_timezone):
    """Convert the collector's clock, including its historical daylight saving."""
    observed = datetime.strptime(code, '%y%m%d%H%M%S').replace(tzinfo=ZoneInfo(source_timezone))
    return observed.astimezone(ZoneInfo('Australia/Perth')).strftime('%Y%m%d%H%M%S')


def prepare_captures(source, target, source_timezone, election_date):
    """Copy immutable exports and record where metadata-only repeats were placed."""
    rows, previous, seen = [], None, set()
    for path in sorted(source.glob('*.xml')):
        match = re.search(r' - (\d{12})\.xml$', path.name, re.I)
        if not match:
            continue
        code = capture_time(match[1], source_timezone)
        if code in seen:
            raise ValueError('Duplicate WA observation timestamp: ' + code)
        seen.add(code)
        root, _ = read_assembly(path)
        if root.get('ElectionDate') != election_date:
            raise ValueError('Capture belongs to another election: ' + path.name)
        fingerprint = data_fingerprint(root)
        repeated = fingerprint == previous
        folder = target / 'no-data-change' if repeated else target
        folder.mkdir(parents=True, exist_ok=True)
        output = folder / f'waec-la-verbose-{code}.xml'
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if output.exists() and hashlib.sha256(output.read_bytes()).hexdigest() != digest:
            raise ValueError('An existing prepared capture differs: ' + str(output))
        if not output.exists():
            shutil.copyfile(path, output)
        rows.append(dict(source_filename=path.name, source_code=match[1], source_timezone=source_timezone,
            wa_code=code, sha256=digest, data_fingerprint=fingerprint,
            prepared_path=output.relative_to(target).as_posix(), metadata_only_repeat=repeated))
        previous = fingerprint
    if not rows:
        raise ValueError('No timestamped WA captures found.')
    return rows


def update_running_parties(text, rows):
    """Use ballot multiplicities while preserving authored margins and prominence.

    This deliberately changes only sRunningParties. Existing candidate names and
    prominent-independent flags remain authored decisions; the full official
    roster is written separately rather than promoting every independent.
    """
    counts = {}
    for row in rows:
        counts.setdefault(row['district'], Counter())[row['party']] += 1
    seen, current, output = set(), None, []
    for line in text.splitlines():
        if line.startswith('#'):
            current = line[1:].strip()
        if line.startswith('sRunningParties='):
            if current not in counts or current in seen:
                raise ValueError('Unmatched/duplicate configured WA district: ' + str(current))
            seen.add(current)
            fields = [p + '*' * (counts[current][p] - 1)
                      for p in ('ALP', 'LIB', 'NAT', 'GRN', 'ONP', 'OTH', 'IND') if counts[current][p]]
            line = 'sRunningParties=' + ','.join(fields)
        output.append(line)
    if seen != set(counts):
        raise ValueError('Missing WA district configuration: ' + str(set(counts) - seen))
    return '\n'.join(output) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--source-timezone', required=True)
    parser.add_argument('--election', default='2025wa')
    parser.add_argument('--update-seats', action='store_true')
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[3]
    candidates = root / 'downloads' / f'{args.election}_candidates_current.xml'
    date, roster = candidate_roster(candidates)
    folder = root / 'forecasts' / args.election / 'live-snapshots'
    captures = prepare_captures(args.source, folder / 'feeds', args.source_timezone, date)
    manifest = dict(schema_version=1, election=args.election, election_date=date,
        candidate_source=dict(filename=candidates.name, sha256=hashlib.sha256(candidates.read_bytes()).hexdigest()),
        prepared_timezone='Australia/Perth', candidates=roster, captures=captures)
    (folder / 'preparation-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if args.update_seats:
        path = root / 'analysis' / 'seats' / f'{args.election}.txt'
        path.write_text(update_running_parties(path.read_text(encoding='utf-8'), roster), encoding='utf-8')
    active = sum(not r['metadata_only_repeat'] for r in captures)
    print(f'Prepared {len(roster)} Assembly candidates; {active} changed captures, {len(captures)-active} repeats.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
