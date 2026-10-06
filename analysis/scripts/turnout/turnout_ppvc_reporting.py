"""Examine how much vote remains at historically unreported federal PPVCs.

Historical counts set a pre-count size reference. At each retained update,
compare that reference with later formal returns for the same feed identities.
Keep the chance of a later return separate from its size. An empty final
record is an observed absence of a return, not proof that no votes were cast
at the venue. This analysis does not close booths or fit a live decay rule.
"""

import argparse
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from ftplib import FTP
import hashlib
import io
import json
from pathlib import Path

from lib.turnout.paths import archive_directory, download_directory
import re
import xml.etree.ElementTree as ET

import numpy as np

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.turnout import aec_live, ppvc_sizes
from scripts.turnout.turnout_federal_prepoll import csv_rows
from scripts.turnout.turnout_live_prototype import digest


ELECTIONS = {'2019fed': ('24310', '2019-05-18', '2016fed'),
             '2022fed': ('27966', '2022-05-21', '2019fed')}
EVENTS = {'2013fed': '17496', '2016fed': '20499', '2019fed': '24310', '2022fed': '27966'}


def cached_counts(code):
    """Read retained official final counts and coordinates, checking revisions.

    Formal and informal measurements are kept separately. In particular, a
    record with neither is not called a known zero venue total. The lightweight
    booth mapping also supplies the older historical matching reference without
    requiring an additional verbose feed for every election.
    """
    directory = REPOSITORY_DIRECTORY / 'downloads/turnout/federal-prepoll' / code
    manifest_path = directory / 'latest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    contents, files = {}, [manifest_path]
    for key, entry in manifest.items():
        path = directory / entry['sha256'] / entry['url'].rsplit('/', 1)[-1]
        if digest(path) != entry['sha256']:
            raise ValueError('Historical CSV differs from its retained revision: ' + str(path))
        contents[key] = csv_rows(path.read_bytes())
        files.append(path)
    counts, informal = defaultdict(int), defaultdict(int)
    for state, rows in contents.items():
        if state == 'places':
            continue
        for row in rows:
            target = informal if row['PartyNm'] == 'Informal' else counts
            target[(row['DivisionNm'], row['PollingPlaceID'])] += int(row['OrdinaryVotes'])
    booths = {}
    for place in contents['places']:
        key = place['DivisionNm'], place['PollingPlaceID']
        if place['PollingPlaceID'] in booths:
            raise ValueError('Historical polling-place ID is not unique: ' + place['PollingPlaceID'])
        booths[place['PollingPlaceID']] = dict(id=place['PollingPlaceID'], seat_name=place['DivisionNm'],
            name=place['PollingPlaceNm'], counted=counts[key], informal=informal[key],
            classification='PrePollVotingCentre' if place['PollingPlaceTypeID'] == '5' else 'Normal',
            position=ppvc_sizes.csv_position(place))
    return dict(booths=booths, places=contents['places']), files


def acquire_checkpoints(code, refresh=False):
    """Retain a small daily FTP sample, preserving replaced source revisions.

    Fetch midday/evening observations around the first two days and the latest
    daily observations through the first week. Existing byte-addressed files
    are reused unless refresh is requested; a refresh never overwrites an old
    revision. This makes reporting-clock calibration reproducible without
    copying the entire media-feed archive.
    """
    event, date, _ = ELECTIONS[code]
    directory = REPOSITORY_DIRECTORY/'downloads/turnout/federal-ppvc-reporting'/code
    manifest_path = directory/'latest.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    with FTP('mediafeedarchive.aec.gov.au', timeout=30) as ftp:
        ftp.login()
        ftp.cwd('/' + event + '/Detailed/Light')
        candidates = {}
        for name in ftp.nlst():
            match = re.fullmatch(r'aec-mediafeed-Detailed-Light-' + event + r'-(\d{14})\.zip', name)
            if match:
                candidates[datetime.strptime(match[1], '%Y%m%d%H%M%S')] = name
        start = datetime.fromisoformat(date)
        targets = [start + timedelta(days=day, hours=23, minutes=59) for day in range(1, 9)]
        targets.extend(start + timedelta(days=day, hours=hour) for day in (1, 2) for hour in (12, 18))
        names = sorted({candidates[min(candidates, key=lambda t: abs((t-target).total_seconds()))] for target in targets})
        manifest = {}
        for name in names:
            if name in previous and not refresh:
                manifest[name] = previous[name]
                continue
            buffer = io.BytesIO()
            ftp.retrbinary('RETR ' + name, buffer.write)
            data = buffer.getvalue()
            fingerprint = hashlib.sha256(data).hexdigest()
            target = directory/fingerprint/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            manifest[name] = dict(sha256=fingerprint,
                url='ftp://mediafeedarchive.aec.gov.au/' + event + '/Detailed/Light/' + name,
                retrieved_at=datetime.now(timezone.utc).isoformat())
        directory.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')


def checkpoint_paths(archive, event, election_date, code):
    """Choose a few night updates and the latest available update on each day.

    These archives are selected observations rather than continuous daily
    coverage. Preserve the actual source time; never invent a midnight update
    or assume a booth reported on a day for which no snapshot is available.
    """
    candidates = {}
    for path in archive.iterdir():
        match = re.search(r'Detailed-Light-' + event + r'-(\d{14})', path.name)
        if match:
            candidates[datetime.strptime(match[1], '%Y%m%d%H%M%S')] = path
    # The additional official FTP sample fills gaps in the user's local
    # archive. Retain all its selected intra-day observations as well as the
    # older local checkpoints; all source bytes are hashed in the export.
    directory = REPOSITORY_DIRECTORY/'downloads/turnout/federal-ppvc-reporting'/code
    manifest_path = directory/'latest.json'
    retained = []
    if manifest_path.exists():
        for name, entry in json.loads(manifest_path.read_text()).items():
            path = directory/entry['sha256']/name
            if digest(path) != entry['sha256']:
                raise ValueError('Retained reporting snapshot differs from its source hash.')
            timestamp = datetime.strptime(re.search(r'-(\d{14})\.zip$', name)[1], '%Y%m%d%H%M%S')
            candidates[timestamp] = path
            retained.append(timestamp)
    if event == '27966':
        for path in (REPOSITORY_DIRECTORY / 'downloads').glob('*2022fed.xml'):
            # Short local capture names do not encode a year/month reliably.
            # Read only the root header to retain the embedded creation time.
            with path.open('rb') as stream:
                _, root = next(ET.iterparse(stream, events=('start',)))
            candidates[datetime.fromisoformat(root.get('Created'))] = path
    day = datetime.fromisoformat(election_date).date()
    grouped = defaultdict(list)
    for timestamp in candidates:
        age = (timestamp.date()-day).days
        if 0 <= age <= 14:
            grouped[age].append(timestamp)
    chosen = []
    for age, times in sorted(grouped.items()):
        if age == 0:
            for hour in (20, 22):
                target = datetime.fromisoformat(election_date + f'T{hour:02d}:00:00')
                chosen.append(min(times, key=lambda t: abs((t-target).total_seconds())))
        chosen.append(max(times))
    return [candidates[t] for t in sorted(set(chosen + retained))]


def summarise(rows):
    """Separate later-return frequency, conditional size and feed additions.

    The marginal feed total counts no-return identities as adding no observed
    votes to that identity by the final archive. Their actual venue totals stay
    unknown. This differs from analysing the size of booths with known results,
    and the output makes both measurements explicit rather than mixing zeros.
    """
    expected = sum(r['expected'] for r in rows)
    returned = [r for r in rows if r['status'] == 'later_return']
    known = [r for r in rows if r['venue_formal'] is not None]
    feed = sum(r['final_feed_formal'] for r in rows)
    return dict(centres=len(rows), districts=len({r['seat'] for r in rows}), expected=expected,
        later_returns=len(returned), no_final_return=sum(r['status'] == 'no_final_return' for r in rows),
        known_formal_zero=sum(r['status'] == 'formal_zero_with_informal_votes' for r in rows),
        later_return_percent=100*len(returned)/len(rows) if rows else None,
        eventual_feed_formal=feed, feed_total_relative_to_expected=feed/expected if expected else None,
        mean_feed_ratio=float(np.mean([r['final_feed_formal']/r['expected'] for r in rows])) if rows else None,
        known_count_relative_to_expected=sum(r['venue_formal'] for r in known)/sum(r['expected'] for r in known) if known else None,
        returned_count_relative_to_expected=sum(r['venue_formal'] for r in returned)/sum(r['expected'] for r in returned) if returned else None)


def analyse_election(code, archive, seat_types):
    """Use earlier elections for sizes and this election only for reporting outcomes."""
    event, election_date, previous_code = ELECTIONS[code]
    older_code = '2013fed' if previous_code == '2016fed' else '2016fed'
    final, files = cached_counts(code)
    previous, previous_files = cached_counts(previous_code)
    older, older_files = cached_counts(older_code)
    files.extend([*previous_files, *older_files])
    calibration = ppvc_sizes.historical_sizes(previous, older, previous['places'], seat_types,
                                             election=previous_code, comparison_election=older_code)
    preload_path = next(archive.glob('*' + event + '*Preload*'))
    preload = aec_live.read_house(preload_path, event_id=event)
    files.append(preload_path)
    reporting_manifest = REPOSITORY_DIRECTORY/'downloads/turnout/federal-ppvc-reporting'/code/'latest.json'
    if reporting_manifest.exists():
        files.append(reporting_manifest)
    # Reconcile the CSV scoring revision with the retained final verbose feed.
    # This verifies that a "no later feed return" is an observed reporting
    # outcome, rather than an invented zero for an unavailable measurement.
    final_paths = list((REPOSITORY_DIRECTORY/'downloads').glob('*Verbose*' + event + '*.xml'))
    if len(final_paths) != 1:
        raise ValueError('Expected one retained final verbose feed for ' + code)
    final_house = aec_live.read_house(final_paths[0], event_id=event)
    files.append(final_paths[0])
    for identifier, booth in final['booths'].items():
        returned = final_house['booths'].get(identifier)
        if returned and returned['counted'] != booth['counted'] or not returned and booth['counted'] > 0:
            raise ValueError('Final CSV and final feed counts differ: ' + code + '/' + identifier)
    # The retained result preload supplies the identities actually present
    # before counting. Historical final-location CSVs screen local/remote roles
    # retrospectively; this is not an exact reconstruction of pre-election GIS.
    locations = final['booths']
    ordinary = [dict(seat_name=r['DivisionNm'], position=ppvc_sizes.csv_position(r))
                for r in final['places'] if r['PollingPlaceTypeID'] == '1']
    screen = ppvc_sizes.LocationScreen(ordinary)
    cohort = []
    for booth in preload['booths'].values():
        if aec_live.live_booth_type(booth) != 'ppvc' or booth['name'].startswith('EAV '):
            continue
        old = previous['booths'].get(booth['id'])
        matched = bool(old and old['seat_name'] == booth['seat_name'] and old['name'] == booth['name']
                       and old['classification'] == booth['classification'] and old['counted'] > 0)
        location = locations.get(booth['id'])
        role = screen.classify(booth['seat_name'], location['position'] if location else None)
        accepted = old if old and old['name'][:4].lower() == booth['name'][:4].lower() else None
        expected = accepted['counted'] if accepted and accepted['counted'] > 0 else 500.
        if not matched and role == 'likely_local':
            expected = calibration['by_seat_type'].get(seat_types.get(booth['seat_name']), calibration)['median']
        cohort.append(dict(id=booth['id'], seat=booth['seat_name'], name=booth['name'], expected=expected,
                           location=role, matched=matched, seat_type=seat_types.get(booth['seat_name'])))
    # Extra identities introduced after the preload help the reader reconcile
    # snapshots, but do not acquire invented initial predictions in the cohort.
    metadata = {k: dict(b) for k, b in final['booths'].items()}
    metadata.update(preload['booths'])
    rows, unresolved = [], []
    for booth in cohort:
        last = final['booths'].get(booth['id'])
        if not last or last['seat_name'] != booth['seat']:
            unresolved.append(booth)
            continue
        status = 'later_return' if last['counted'] else 'formal_zero_with_informal_votes' if last['informal'] else 'no_final_return'
        rows.append(dict(booth, status=status, final_feed_formal=last['counted'],
                         venue_formal=last['counted'] if status != 'no_final_return' else None))
    poll_close = datetime.fromisoformat(election_date + 'T18:00:00')
    snapshots = []
    for path in checkpoint_paths(archive, event, election_date, code):
        current = aec_live.read_house(path, metadata, event_id=event)
        files.append(path)
        timestamp = datetime.fromisoformat(current['source_time'])
        pending = [r for r in rows if r['id'] in current['booths'] and current['booths'][r['id']]['counted'] == 0]
        missing = [r['id'] for r in rows if r['id'] not in current['booths']]
        snapshots.append(dict(source_timestamp=current['source_time'],
            hours_after_eastern_poll_close=(timestamp-poll_close).total_seconds()/3600,
            day_after_election=(timestamp.date()-poll_close.date()).days,
            all_public=summarise(pending), local=summarise([r for r in pending if r['location'] == 'likely_local']),
            outside=summarise([r for r in pending if r['location'] == 'likely_outside']),
            absent_current_ids=missing, pending=pending))
    return dict(election=code, reference_election=previous_code, calibration=calibration,
                initial_cohort=summarise(rows), unresolved_final_identities=unresolved,
                snapshots=snapshots), files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=archive_directory())
    parser.add_argument('--output', type=Path, default=REPOSITORY_DIRECTORY/'docs/turnout-federal-live-prototype/ppvc-reporting-history.json')
    parser.add_argument('--fetch', action='store_true', help='Acquire selected official FTP checkpoints before analysing.')
    parser.add_argument('--refresh', action='store_true', help='Acquire new source revisions, retaining the previous bytes.')
    args = parser.parse_args()
    types_path = ANALYSIS_DIRECTORY/'Data/seat-types.csv'
    types = ppvc_sizes.read_seat_types(types_path)
    elections, files = [], [types_path, Path(__file__), Path(aec_live.__file__), Path(ppvc_sizes.__file__),
                          ANALYSIS_DIRECTORY/'scripts/turnout/turnout_federal_prepoll.py',
                          ANALYSIS_DIRECTORY/'scripts/turnout/turnout_live_prototype.py']
    for code in ELECTIONS:
        if args.fetch or args.refresh:
            acquire_checkpoints(code, refresh=args.refresh)
        result, inputs = analyse_election(code, args.archive, types)
        elections.append(result)
        files.extend(inputs)
        print(code + ' complete', flush=True)
    result = dict(elections=elections,
        research_decay_candidate=dict(knots=[list(k) for k in ppvc_sizes.REPORTING_DECAY_KNOTS],
            interpretation='Conservative judgement from historical feed-addition ratios, not a fitted timing distribution.',
            application='Positive log-scale interpolation; applied to log odds of unreported public-centre share, never counted votes or EAV.'),
        definitions=dict(expected='Earlier formal count, or earlier local new/changed median by seat type; no current total-vote prior or pooled adjustment.',
            feed_total='Formal votes ultimately returned under the same feed identity; no-final-return venue counts remain unknown.',
            time='Hours after 18:00 on election day using the embedded feed clock; state closing times are not separately modelled.',
            geography='Retrospective final-location CSV screen; not an exact historical pre-election geography reconstruction.'),
        provenance={str(p): digest(p) for p in dict.fromkeys(files)})
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
