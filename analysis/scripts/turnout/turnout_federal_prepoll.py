"""Recover final federal ordinary pre-poll counts from official polling-place CSVs.

This small supplement separates the pre-poll component of OrdinaryVotes without
changing the existing normalized vote-category partitions. Final candidate votes
are joined to AEC polling-place type 5 (PrePollVotingCentre), then declaration
pre-polls are added once using the normalized final vote-type records.
"""

import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
from urllib.request import Request, urlopen

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.shared import turnout_data
from scripts.turnout import turnout_aec


OUTPUT = ANALYSIS_DIRECTORY / 'Data/Turnout/FederalPrepoll/final.json'
CACHE = REPOSITORY_DIRECTORY / 'downloads/turnout/federal-prepoll'
ELECTIONS = tuple(code for code in turnout_aec.ELECTIONS if int(code[:4]) >= 2010)
STATES = ('ACT', 'NSW', 'NT', 'QLD', 'SA', 'TAS', 'VIC', 'WA')


def csv_rows(data):
    text = data.decode('utf-8-sig')
    # Official result downloads have one metadata line before the CSV header.
    header = text.splitlines()[0]
    if 'DivisionID' not in header:
        text = text.split('\n', 1)[1]
    return list(csv.DictReader(io.StringIO(text)))


def build_election(dataset, files):
    """Check the recovered polling-place counts against the final ordinary pool."""
    definition = dataset.elections[0]
    by_id = {r.source_seat_id: r for r in dataset.seat_totals}
    places = {(r['DivisionID'], r['PollingPlaceID']): r['PollingPlaceTypeID']
              for r in csv_rows(files['places'])}
    ordinary, early, informal_early = defaultdict(int), defaultdict(int), defaultdict(int)
    for state in STATES:
        for row in csv_rows(files[state]):
            division = row['DivisionID']
            identity = division, row['PollingPlaceID']
            if identity not in places or division not in by_id:
                raise turnout_data.TurnoutDataError('AEC polling-place identity does not match final district data.')
            votes = int(row['OrdinaryVotes'])
            if row['PartyNm'] == 'Informal':
                if places[identity] == '5':
                    informal_early[division] += votes
            else:
                ordinary[division] += votes
                if places[identity] == '5':
                    early[division] += votes
    if ordinary.keys() != by_id.keys():
        raise turnout_data.TurnoutDataError('AEC polling-place results have incomplete district coverage.')
    rows = []
    for division, seat in sorted(by_id.items()):
        categories = [r for r in dataset.vote_types if r.seat_name == seat.seat_name]
        expected = sum(r.formal_votes for r in categories if r.canonical_category == 'ordinary_combined')
        if ordinary[division] != expected:
            raise turnout_data.TurnoutDataError('{} {} ordinary polling-place votes {} differ from final {}.'.format(
                definition.election_code, seat.seat_name, ordinary[division], expected))
        declarations = sum(r.formal_votes for r in categories if r.canonical_category == 'declaration_early')
        rows.append(dict(seat_name=seat.seat_name, division_id=division,
                         all_ordinary_formal=ordinary[division],
                         ordinary_early_formal=early[division], ordinary_early_informal=informal_early[division],
                         declaration_early_formal=declarations, final_early_formal=early[division] + declarations))
    return dict(election_code=definition.election_code, election_date=definition.election_date, districts=rows)


def acquire(code, refresh=False):
    """Reuse retained sources; a refresh keeps each changed raw revision by hash."""
    election = turnout_aec.ELECTIONS[code]
    root = 'https://results.aec.gov.au/{}/Website/Downloads/'.format(election.event_id)
    names = {'places': 'GeneralPollingPlacesDownload-{}.csv'.format(election.event_id)}
    names.update({state: 'HouseStateFirstPrefsByPollingPlaceDownload-{}-{}.csv'.format(election.event_id, state)
                  for state in STATES})
    manifest_path = CACHE / code / 'latest.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}

    def get(item):
        key, name = item
        retained = previous.get(key)
        if retained and not refresh:
            return key, (CACHE / code / retained['sha256'] / name).read_bytes(), retained
        url = root + name
        raw = urlopen(Request(url, headers={'User-Agent': 'AEF turnout analysis'}), timeout=45).read()
        digest = hashlib.sha256(raw).hexdigest()
        directory = CACHE / code / digest
        directory.mkdir(parents=True, exist_ok=True)
        (directory / name).write_bytes(raw)
        return key, raw, dict(url=url, sha256=digest,
                              retrieved_at=datetime.now(timezone.utc).isoformat())

    with ThreadPoolExecutor(max_workers=4) as pool:
        downloaded = list(pool.map(get, names.items()))
    files = {key: raw for key, raw, source in downloaded}
    sources = {key: source for key, raw, source in downloaded}
    normalized = ANALYSIS_DIRECTORY / 'Data/Turnout/{}.json'.format(code)
    dataset = turnout_data.load_dataset(normalized)
    result = build_election(dataset, files)
    result['sources'] = sources
    result['normalized_snapshot_sha256'] = hashlib.sha256(json.dumps(
        json.loads(normalized.read_text(encoding='utf-8')), sort_keys=True,
        separators=(',', ':'), ensure_ascii=False).encode()).hexdigest()
    (CACHE / code).mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(sources, indent=2) + '\n', encoding='utf-8')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh', action='store_true', help='download current sources while retaining old revisions')
    parser.add_argument('--election', choices=ELECTIONS, nargs='+', default=list(ELECTIONS))
    args = parser.parse_args(argv)
    existing = json.loads(OUTPUT.read_text(encoding='utf-8')) if OUTPUT.exists() else {}
    elections = {e['election_code']: e for e in existing.get('elections', [])}
    for code in args.election:
        result = acquire(code, args.refresh)
        elections[code] = result
        print('{}: {} districts; {:,} final formal pre-poll votes.'.format(
            code, len(result['districts']), sum(r['final_early_formal'] for r in result['districts'])), flush=True)
    payload = dict(schema_version=1, adapter='aec-polling-place-prepoll-v1',
                   elections=sorted(elections.values(), key=lambda e: e['election_date']),
                   target_definition='Formal ordinary votes at type-5 polling places plus final declaration pre-polls.')
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUTPUT.with_suffix('.tmp')
    temporary.write_text(json.dumps(payload, indent=2) + '\n', encoding='utf-8')
    temporary.replace(OUTPUT)
    print('Wrote {}'.format(OUTPUT))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
