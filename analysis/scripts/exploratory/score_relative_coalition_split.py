"""Check whether a transferred booth's former LIB–NAT lean predicts its first count.

Run this file from the repository root with the analysis Python. The comparison
uses the 2022 Victorian and 2025 federal replays. For each eligible booth it
prints or scores the prediction made immediately before the first FP return.
Errors are percentage points of all FP votes, weighted by first-return size.
"""
from collections import Counter
import xml.etree.ElementTree as ET
from zipfile import ZipFile

import numpy as np
import pandas as pd

from transferred_coalition_split import (
    COMBINED_TRIAL, LIVE_ODDS, LIVE_POINTS, LIVE_UNIFORM, ONE_PARTNER, RELATIVE_SPLIT, ROOT, SEAT_SPLIT, count_series, first_return_predictions,
    key, trial_openings,
)

METHODS = ('existing', SEAT_SPLIT, RELATIVE_SPLIT, ONE_PARTNER)
BENCHMARKS = (*METHODS, LIVE_UNIFORM, LIVE_ODDS, LIVE_POINTS, COMBINED_TRIAL)
PACKAGE_CUTOFF = '2025-05-03T19:07:17'  # Includes the Pyalong return in the original trial.


def federal_feed_index(districts):
    """Link the AEC's candidate and location IDs to the groups in the trial.

    These four seats separately model the same five parties. All other
    candidates contribute to the grouped remainder, including independents.
    """
    codes = {'ALP': '0', 'LP': '1', 'GRN': '2', 'GVIC': '2', 'ON': '7', 'NP': '10'}
    preload = ET.parse(ROOT / 'downloads/2025fed_preload.xml')
    index = {}
    for contest in preload.findall('.//{*}House/{*}Contests/{*}Contest'):
        identifier = contest.find('{*}ContestIdentifier')
        seat = identifier.findtext('{*}ContestName')
        if seat not in districts:
            continue
        start, base = districts[seat]
        if set(base[1]) != set(codes.values()):
            raise ValueError(f'Check modelled parties before using the AEC feed for {seat}')
        parties = {}
        for candidate in contest.findall('{*}FirstPreferences/{*}Candidate'):
            affiliation = candidate.find('{*}AffiliationIdentifier')
            code = affiliation.get('ShortCode') if affiliation is not None else None
            parties[candidate.find('{*}CandidateIdentifier').get('Id')] = codes.get(code, 'other')
        by_name = {b['name']: k for k, b in start.items()}
        if len(by_name) != len(start):
            raise ValueError(f'AEC place names need disambiguation in {seat}')
        places = {p.get('Id'): by_name[p.get('Name')]
                  for p in contest.findall('{*}PollingPlaces/{*}PollingPlace/{*}PollingPlaceIdentifier')
                  if p.get('Name') in by_name}
        if any(key(base[0][i]) not in places.values() for i in base[5]):
            raise ValueError(f'Transferred booth missing from AEC preload in {seat}')
        index[seat] = identifier.get('Id'), parties, places, by_name
    if index.keys() != districts.keys():
        raise ValueError('Missing federal district in AEC preload')
    return index


def reported_votes(xml, seat_index, start):
    """Read the district from one Detailed-Light delivery and update its counts.

    Counted ordinary booths use their reported turnout. Unreported places and
    the eventual size of declaration categories use the trial's opening turnout.
    Restrict XML parsing to the selected House contests in these national files.
    """
    identifier, parties, places, by_name = seat_index
    marker = f'<eml:ContestIdentifier Id="{identifier}"'.encode()
    ident_pos = xml.index(marker)
    begin = xml.rindex(b'<Contest ', 0, ident_pos)
    end = xml.index(b'</Contest>', ident_pos) + len(b'</Contest>')
    contest = ET.fromstring(xml[begin:end].replace(b'eml:', b''))

    def read_candidates(candidates, field):
        votes = Counter()
        for candidate in candidates:
            party = parties[candidate.find('CandidateIdentifier').get('Id')]
            votes[party] += int(candidate.findtext(field, default='0'))
        return votes

    updated = start.copy()
    for place in contest.findall('PollingPlaces/PollingPlace'):
        k = places.get(place.find('PollingPlaceIdentifier').get('Id'))
        if k is None:
            continue
        counts = read_candidates(place.findall('FirstPreferences/Candidate'), 'Votes')
        n = sum(counts.values())
        if n:
            updated[k] = dict(start[k], fp_votes_current=counts, fp_votes_projected={'total': n})
    candidates = contest.findall('FirstPreferences/Candidate')
    for kind in ('Absent', 'PrePoll', 'Provisional', 'Postal'):
        k = by_name.get(kind)
        if k is None:
            continue
        counts = read_candidates(candidates, f"VotesByType/Votes[@Type='{kind}']")
        n = sum(counts.values())
        if n:
            expected = sum(start[k]['fp_votes_projected'].values())
            updated[k] = dict(start[k], fp_votes_current=counts, fp_votes_projected={'total': max(n, expected)})
    return updated


def federal_captures(run, districts):
    """Use replay evidence while available, then every subsequent archived feed.

    The feeds supply the order of WA returns that the sparse forecast archive
    skips. Regional and statewide movements are held at zero after the last
    replay package, identically for all methods. No later count sets a prediction.
    """
    latest = None
    for time, places, shared in count_series(run, districts):
        if time > PACKAGE_CUTOFF:
            break
        latest = time
        yield time, places, shared
    if latest != PACKAGE_CUTOFF:
        raise ValueError('The opening federal replay does not cover the Pyalong return')
    latest_stamp = latest.translate(str.maketrans('', '', '-T:'))
    index = federal_feed_index(districts)
    archive = ROOT / 'forecasts/2025fed/live-snapshots/feeds'
    for path in sorted(archive.glob('*Light-31496-*.zip')):
        stamp = path.stem[-14:]
        if stamp <= latest_stamp:
            continue
        with ZipFile(path) as feed:
            xml = feed.read(next(name for name in feed.namelist() if name.lower().endswith('.xml')))
        places = {}
        for seat, (start, _) in districts.items():
            places.update(reported_votes(xml, index[seat], start))
        yield stamp, places, {seat: {} for seat in districts}


def compare_run(election, run_name):
    """Score the same first batch once under every method, including one-party fallbacks."""
    run = ROOT / 'live_runs' / run_name
    districts = {seat: ({key(b): b for b in base[0]}, base) for seat, base in trial_openings(run)}
    series = federal_captures(run, districts) if election == '2025fed' else count_series(run, districts)
    records = []
    for seat, i, stamp, method, size, predicted, observed in first_return_predictions(
            districts, METHODS, series, transfers_only=True, live_benchmarks=True):
        if method not in BENCHMARKS:
            continue
        base = districts[seat][1]
        transfer = base[5][i]
        lib_error, nat_error = abs(predicted - observed)
        records.append(dict(election=election, seat=seat, booth=base[0][i]['name'],
                            former=transfer.former_seat, both=transfer.both_stood, stamp=stamp,
                            method=method, votes=size, lib=predicted[0], nat=predicted[1],
                            observed_lib=observed[0], observed_nat=observed[1],
                            lib_error=lib_error, nat_error=nat_error,
                            combined_error=abs(predicted.sum() - observed.sum())))
    expected = {(seat, base[0][i]['name'], method) for seat, (_, base) in districts.items()
                for i in base[5] for method in BENCHMARKS}
    observed = {(r['seat'], r['booth'], r['method']) for r in records}
    if expected != observed or len(records) != len(expected):
        raise ValueError(f'Missing or repeated first returns: {expected - observed}')
    return records


def score_table(rows):
    """Weight FP errors by all ballots and partner-split errors by Coalition ballots."""
    groups = [('All', rows), *rows.groupby('seat'),
              ('Both previously stood', rows[rows.both]), ('One previously stood', rows[~rows.both])]
    for label, group in groups:
        result = {}
        for method, data in group.groupby('method', sort=False):
            errors = data[['nat_error', 'lib_error', 'combined_error']]
            fp_errors = np.average(errors, weights=data.votes, axis=0)
            observed_combined = data.observed_nat + data.observed_lib
            coalition = observed_combined > 0
            observed_split = data.observed_nat[coalition] / observed_combined[coalition]
            predicted_split = (data.nat / (data.lib + data.nat))[coalition]
            split_error = np.average(100 * abs(predicted_split - observed_split),
                                     weights=(observed_combined * data.votes)[coalition])
            result[method] = [*fp_errors, split_error]
        sample = group[group.method == 'existing']
        print(label, len(sample), 'booths,', sample.votes.sum(), 'votes, NAT/LIB/combined FP/split error (pp)')
        for method, errors in result.items():
            print(f'  {method}: ' + ' / '.join(f'{error:.2f}' for error in errors))


def main():
    rows = pd.DataFrame(compare_run('2025fed', '2025fed-2-prelim') +
                        compare_run('2022vic', '2022vic-public'))
    score_table(rows)
    # A modest statewide average can hide an impossible-looking estimate
    # at a single place. Count booths once if either partner missed its first
    # return by the indicated number of percentage points of the entire FP.
    comparison = rows[rows.method.isin(['existing', ONE_PARTNER, COMBINED_TRIAL])]
    for (both, method), group in comparison.groupby(['both', 'method']):
        worst = group[['lib_error', 'nat_error']].max(axis=1)
        booth = group.loc[worst.idxmax(), 'booth']
        ballot = 'Both previously stood' if both else 'One previously stood'
        print(f'{ballot}, {method}: {sum(worst > 20)} booths over 20 pp, {sum(worst > 30)} over 30 pp. '
              f'Largest error {worst.max():.1f} pp at {booth}.')
    examples = rows[rows.booth.isin(['Pyalong', 'Rochester', 'Tooborac', 'Northam', 'Northam PPVC',
                                     'Beverley', 'Koorda', 'Charlton'])]
    print(examples[['seat', 'booth', 'stamp', 'method', 'votes', 'nat', 'lib', 'observed_nat',
                    'observed_lib']].round(2).to_string(index=False))


if __name__ == '__main__':
    main()
