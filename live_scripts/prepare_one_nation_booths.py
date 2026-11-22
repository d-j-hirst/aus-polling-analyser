"""Prepare federal booth comparisons for the Victorian count or a new private SA replay.

The input describes local differences in One Nation's ordinary vote. The live
model supplies the district vote level and turnout. Victoria uses the current
VEC locations on each setup refresh. SA preparation creates a separate project,
inbox and input file for a new run, reading the shared replay sources only.
"""

import argparse
import csv
import io
import json
import re
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'analysis'))
from scripts.elections.federal_booth_geography import Sites, federal_booths, position, source
from scripts.turnout.turnout_federal_prepoll import acquire, CACHE
from prepare_vec_2026_runs import encoded, read_string
from vec_2026_feed import write_complete
from replay_feeder import catalogue

STUDY = ROOT / 'analysis/reports/2026vic-one-nation-booths'
FILENAME = 'one-nation-booths.json'


def victorian_places(manifest):
    """Pool formal ballots at three distinct 2025 House sites near each VEC ordinary venue."""
    if not (CACHE / '2025fed/latest.json').is_file():
        acquire('2025fed')
    federal = Sites(b for b in federal_booths('VIC', manifest) if b.mode == 'ordinary')
    roster = ET.fromstring(source(ROOT / 'downloads/2026vic_booths.xml', manifest))
    result = {}
    for district in roster.findall('{*}PollingDistrict'):
        seat = district.findtext('{*}PollingDistrictIdentifier/{*}Name').removesuffix(' District')
        for booth in district.findall('.//{*}PollingPlace'):
            identifier = booth.find('{*}PollingPlaceIdentifier')
            if identifier.get('Classification') != 'VotingCentre':
                continue
            name = identifier.attrib['Name']
            row = dict(id=int(identifier.attrib['Id']))
            point = position(booth.findtext('.//{*}AddressLatitude'), booth.findtext('.//{*}AddressLongitude'))
            if point:
                counts, distances, _ = federal.nearby(point)
                on, formal = counts[:3].sum(axis=0)
                row.update(one_nation=int(on), formal=int(formal), nearest_km=float(distances[0]))
            if (seat, name) in result:
                raise ValueError(f'Duplicate VEC ordinary place {seat}/{name}')
            result[seat, name] = row
    return result


def sa_places(manifest, ordinary):
    """Join the recorded federal predictors to the pre-election ECSA ballot.

    The research table also contains eventual SA outcomes. Only its federal
    counts, distance and federal area are predictors. The area chooses a
    coefficient fitted outside that area. The six mobile team accounts in the
    turnout roster have no fixed neighbourhood. Closed venues have zero weight.
    """
    rows = csv.DictReader(io.StringIO(source(STUDY / 'sa-booths.csv', manifest).decode('utf-8')))
    locations, areas = {}, {}
    for r in rows:
        if r['mode'] != 'ordinary':
            continue
        seat, name = r['seat'], r['booth']
        if (seat, name) in locations or areas.setdefault(seat, r['federal_area']) != r['federal_area']:
            raise ValueError(f'Inconsistent SA geographic identity {seat}/{name}')
        on, formal = map(int, r['three'].split('/'))
        locations[seat, name] = dict(one_nation=on, formal=formal, nearest_km=float(r['nearest_km']))
    zero = ET.fromstring(source(ROOT / 'downloads/2026sa_zeros.xml', manifest))
    for district in zero.findall('.//{*}district'):
        seat = district.findtext('{*}district_name')
        first_candidate = district.find('{*}first_preferences/{*}candidate')
        for place in first_candidate.findall('{*}polling_places/{*}polling_place'):
            name = place.findtext('{*}polling_place_name')
            key = seat, name
            if key not in ordinary:
                continue
            if key not in locations and name.endswith(' Mobile'):
                locations[key] = dict(mobile=True)
            if key not in locations or 'id' in locations[key]:
                raise ValueError(f'Unrecognised or repeated SA ordinary place {seat}/{name}')
            locations[key]['id'] = int(place.findtext('{*}polling_place_id'))
    return locations, areas


def geographic_input(term):
    """Check the full district/venue roster before publishing a geographic revision.

    Each record identifies a current physical reporting unit. A measured zero
    party count retains its positive formal denominator. A missing position
    instead carries an identity alone. Distances beyond 10 km are retained so
    the reader can apply and audit the district-average fallback.
    """
    manifest = {}
    turnout = json.loads(source(ROOT / f'forecasts/{term}/live-inputs/turnout-prior.json', manifest))
    ordinary = {(turnout['prior']['seats'][u['seat']], u['name'])
                for u in turnout['units'] if u['kind'] == 'ordinary'}
    strengths = csv.DictReader(io.StringIO(source(STUDY / 'baseline-strengths.csv', manifest).decode('utf-8')))
    strength = {r['held_out']: float(r['federal_strength']) for r in strengths}
    if term == '2026sa':
        locations, areas = sa_places(manifest, ordinary)
    else:
        locations = victorian_places(manifest)
        areas = dict.fromkeys(turnout['prior']['seats'], 'none')
    if locations.keys() != ordinary or any('id' not in r for r in locations.values()):
        raise ValueError(f'{term} geographic and turnout rosters differ. Refresh the setup inputs together.')
    districts = {seat: dict(strength=strength[area], places={}) for seat, area in sorted(areas.items())}
    for (seat, name), row in sorted(locations.items()):
        districts[seat]['places'][name] = row
    return dict(schema_version=1, election=term, districts=districts, sources=manifest)


def replay_project(path, settings):
    """Copy the saved SA model and simulation settings into a separately named watcher project."""
    raw = path.read_bytes()
    _, start = read_string(raw, 4)
    election, start = read_string(raw, start)
    _, body = read_string(raw, start)
    if election != '2026 SA State Election' or struct.unpack_from('<i', raw)[0] not in (70, 71):
        raise ValueError('Expected a version 70 or 71 SA 2026 watcher project.')
    # ProjectFiler appends the watcher JSON as a length-prefixed UTF-8 string.
    # Replace only that entire field, the display name and the startup macro.
    # Work from the end since the model itself can contain many megabytes.
    start = raw.rfind(b'{', body + 4)
    while start >= 0:
        boundary = start - 4
        if struct.unpack_from('<i', raw, boundary)[0] == len(raw) - start:
            old = json.loads(raw[start:])
            if old.get('election') == '2026sa':
                return (raw[:4] + encoded(settings['display_name']) + encoded(election)
                        + encoded('run-model:0;run-projection:0') + raw[body:boundary]
                        + encoded(json.dumps(settings, separators=(',', ':'))))
        start = raw.rfind(b'{', body + 4, start)
    raise ValueError('No recognised SA watcher configuration at the end of the project.')


def prepare_replay(run_id, project, payload):
    """Install a new replay as one directory, leaving existing run identities and feeds intact."""
    if not re.fullmatch('2026sa-[a-z0-9-]+', run_id):
        raise ValueError('Use a new replay ID starting with 2026sa- and lowercase letters, digits or hyphens.')
    directory = ROOT / 'live_runs' / run_id
    if directory.exists():
        raise FileExistsError(f'This replay directory already exists: {directory}')
    settings = dict(version=1, election='2026sa', kind='replay', run_id=run_id, visibility='private',
                    display_name='SA 2026 federal booth geography', election_date='2026-03-21',
                    timezone='Australia/Adelaide', night_end_hour=6, one_nation_booth_geography=True,
                    source_directory=f'live_runs/{run_id}/inbox', commentary=dict(enabled=False))
    captures = catalogue(ROOT / 'forecasts/2026sa/live-snapshots/feeds', '2026sa')
    if not captures or captures[0][0] != '20260321183853':
        raise ValueError('Expected the retained SA sequence to begin with its genuine zero-count capture.')
    data = replay_project(project, settings)
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory.parent) as staging:
        ready = Path(staging) / run_id
        (ready / 'live-inputs').mkdir(parents=True)
        (ready / 'inbox').mkdir()
        (ready / 'live-inputs' / FILENAME).write_bytes(payload)
        (ready / (run_id + '.pol2')).write_bytes(data)
        (ready / 'run.json').write_text(json.dumps(settings, indent=2) + '\n', encoding='utf-8')
        (ready / 'captures.txt').write_text(''.join(code + '\n' for code, _, _ in captures), encoding='ascii')
        (ready / 'Next.ps1').write_bytes((ROOT / 'live_scripts/Step-FederalBoothReplay.ps1').read_bytes())
        ready.rename(directory)
    return directory / 'live-inputs' / FILENAME


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('election', choices=('2026vic', '2026sa'))
    parser.add_argument('--new-replay', metavar='RUN_ID', help='Create a separate numerical SA watcher project')
    parser.add_argument('--project', type=Path, default=ROOT / 'SA State Election 2026 live testing.pol2',
                        help='SA source project whose model and iteration settings the new replay will use')
    args = parser.parse_args()
    if (args.election == '2026sa') != bool(args.new_replay):
        parser.error('SA preparation requires --new-replay RUN_ID. Victoria uses its election live inputs.')
    document = geographic_input(args.election)
    payload = (json.dumps(document, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')
    if args.new_replay:
        path = prepare_replay(args.new_replay, args.project, payload)
    else:
        path = ROOT / 'forecasts/2026vic/live-inputs' / FILENAME
        if not path.exists() or path.read_bytes() != payload:
            write_complete(path, payload)
    n = sum(len(d['places']) for d in document['districts'].values())
    print(f'{len(document["districts"])} districts, {n} polling-place records: {path}')


if __name__ == '__main__':
    main()
