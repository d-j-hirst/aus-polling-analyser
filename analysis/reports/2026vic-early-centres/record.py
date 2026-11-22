"""Rebuild the compact study inputs from retained commission data.

The main analysis reads the recorded tables offline. This command is for
checking them against the original releases. Only --fetch contacts the VEC.
The public election-night archive is large and is scanned with --with-night.
"""

import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from zipfile import ZipFile

from evidence import FIRST_REPORTS, HERE, ROOT, MIN_VOTES, YEARS, aec_election, federal_cases, observation, side, write_csv
from victoria import comparison_booths, current_cases, early_2022, historical_votes, locations, venue_weights, workbooks


NIGHTS = {2022: ('27966', '20220521'), 2025: ('31496', '20250503')}
PLACE = re.compile(rb'<PollingPlaceIdentifier\b[^>]*?\bId="(\d+)"')


def reported_pairs(data, eligible, candidate_party):
    """Extract substantial Labor–Coalition counts at eligible House early centres.

    Detailed-Light repeats every national booth at every update. Locate its
    polling-place blocks in the House section and parse the small preference
    count as XML. A different pair of candidates cannot supply a Labor TPP
    observation, even if the division has an official final notional TPP.
    """
    start = data.index(b'<House>')
    end = data.index(b'</House>', start)
    for match in PLACE.finditer(data, start, end):
        key = match[1]
        if key not in eligible:
            continue
        stop = data.index(b'</PollingPlace>', match.end())
        first = data.find(b'<TwoCandidatePreferred', match.end(), stop)
        last = data.find(b'</TwoCandidatePreferred>', first, stop) if first >= 0 else -1
        if last < 0:
            continue
        block = data[first:last + len(b'</TwoCandidatePreferred>')]
        xml = ET.fromstring(b'<Block xmlns:eml="urn:oasis:names:tc:evs:schema:eml">' + block + b'</Block>')
        counts = {}
        candidates = xml.findall('TwoCandidatePreferred/Candidate')
        for c in candidates:
            identifier = c.find('{*}CandidateIdentifier').get('Id')
            counts[side(candidate_party[identifier])] = int(c.findtext('Votes'))
        if len(candidates) == 2 and counts.keys() == {'alp', 'coalition'} and sum(counts.values()) >= MIN_VOTES:
            yield key.decode(), counts['alp'], sum(counts.values())


def first_reports(year, cases, party, sources):
    """Use the first retained election-night update reaching 500 votes per district.

    A tie in an update keeps every centre that qualified together. Final
    centre sizes do not pick the first report. Scoring later uses the final
    counts to identify substantial comparison centres and measure their result.
    """
    event, date = NIGHTS[year]
    files = sorted((ROOT / f'forecasts/{year}fed/live-snapshots/feeds').glob(
        f'aec-mediafeed-Detailed-Light-{event}-{date}*.zip'))
    if not files:
        raise FileNotFoundError(f'No retained AEC election-night captures for {year}')
    eligible = {r['key'].encode(): r['district'] for r in cases}
    seen, output = set(), []
    inventory = hashlib.sha256()
    for path in files:
        with path.open('rb') as stream:
            inventory.update(f'{path.name} {hashlib.file_digest(stream, "sha256").hexdigest()}\n'.encode())
        with ZipFile(path) as archive:
            data = archive.read(f'xml/aec-mediafeed-results-detailed-light-{event}.xml')
        arrived = defaultdict(list)
        for key, labor, total in reported_pairs(data, eligible, party):
            seat = eligible[key.encode()]
            arrived[seat].append(dict(election=f'{year}fed', district=seat, key=key,
                                      timestamp=path.stem.split('-')[-1], labor=labor, formal=total))
        for seat, records in arrived.items():
            output.extend(records)
            seen.add(seat)
        if arrived:
            eligible = {k: v for k, v in eligible.items() if v not in seen}
    sources[f'ftp://mediafeedarchive.aec.gov.au/{event}/Detailed/Light/'] = dict(
        date=date, captures=len(files), first=files[0].name, last=files[-1].name,
        filename_and_sha256_digest=inventory.hexdigest())
    return output


def check_revision(path, revision):
    """Protect a dated record from an updated source or a different set of night captures."""
    if path.exists() and json.loads(path.read_bytes()) != revision:
        raise ValueError(f'Sources differ from {path}. Use --output with a new directory for this revision.')


def record(fetch, with_night, destination, historical_only):
    """Prepare previous-election comparisons, observed first returns and the current roster."""
    sources = {}
    books = workbooks(fetch, sources)
    geo18 = locations(books['locations2018'], 2018)
    geo22 = locations(books['locations2022'], 2022)
    history = historical_votes(2022, sources)
    state_early = early_2022(books['results2022'], geo22, history)
    state_ordinary = comparison_booths(geo18, geo22, historical_votes(2018, sources))
    historical = [observation('2022vic', b, state_ordinary[b.seat])[0] for b in state_early]
    for row in historical:
        row['catchment'] = 'named own-district count'

    reports, night_sources = [], {}
    previous = aec_election(YEARS[0], sources)
    for year in YEARS[1:]:
        current = aec_election(year, sources)
        cases = federal_cases(year, previous, current)
        historical.extend(cases)
        if with_night and year in NIGHTS:
            print(f'Reading {year} election-night updates', flush=True)
            reports.extend(first_reports(year, cases, current[2], night_sources))
        previous = current

    private, current = {}, []
    if not historical_only:
        current = current_cases(geo22, history, state_early, venue_weights(books['venues2026']), private)
        check_revision(destination / 'operator' / 'source-revisions.json', private)
    check_revision(destination / 'sources.json', sources)
    if with_night:
        check_revision(destination / 'archive-sources.json', night_sources)
    key = lambda r: (r['election'], r['district'], r['key'])
    write_csv(destination / 'historical-centres.csv', sorted(historical, key=key))
    if current:
        write_csv(destination / 'operator' / '2026-inputs.csv', sorted(current, key=key))
        (destination / 'operator' / 'source-revisions.json').write_text(
            json.dumps(private, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    (destination / 'sources.json').write_text(json.dumps(sources, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    if with_night:
        write_csv(destination / FIRST_REPORTS, sorted(reports, key=key))
        (destination / 'archive-sources.json').write_text(json.dumps(night_sources, indent=2) + '\n', encoding='utf-8')
    print(f'Recorded {len(historical)} historical centre observations and {len(current)} Victorian roster entries.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='download missing public VEC spreadsheets')
    parser.add_argument('--with-night', action='store_true', help='re-read the retained AEC election-night ZIPs')
    parser.add_argument('--historical-only', action='store_true', help='omit the operator’s current feed setup')
    parser.add_argument('--output', type=Path, default=HERE, help='directory for this source revision')
    args = parser.parse_args()
    record(args.fetch, args.with_night, args.output, args.historical_only)
