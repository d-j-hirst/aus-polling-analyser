"""Compare a Python LiveV2 count baseline with the Federal 2025 turnout updater.

Read retained AEC snapshots without installing a feed or running the GUI. The
previous final feed supplies baseline sizes; the frozen pre-election fixture
supplies the prototype prior. Final CSV counts enter scoring only.
"""

import argparse
from collections import defaultdict
import copy
from datetime import datetime
import json
from pathlib import Path
import re
import time

import numpy as np
import scipy

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.turnout import aec_live, live, prior, ppvc_sizes, maintained_parameters
from lib.turnout.paths import archive_directory, download_directory
from scripts.turnout import turnout_federal_live_report
from scripts.turnout.turnout_federal_prepoll import csv_rows
from scripts.turnout.turnout_live_prototype import digest, read_json, load_fixture, interval_summary


DEFAULT_OUTPUT = REPOSITORY_DIRECTORY / 'docs/turnout-federal-live-prototype'
CHECKPOINTS = ('20250503171746', '20250503183000', '20250503200000',
               '20250503220000', '20250504010000', '20250507210000',
               '20250514210000', '20250522215429')


def retained_input(explicit, pattern):
    """Find the known retained input; ambiguous revisions require an explicit path."""
    if explicit:
        return explicit
    directories = [REPOSITORY_DIRECTORY / 'downloads', archive_directory(), download_directory()]
    paths = list(dict.fromkeys(p for directory in directories for p in directory.glob(pattern)))
    if len(paths) != 1:
        raise ValueError('Provide an explicit path; expected one retained input for ' + pattern)
    return paths[0]


def starting_size_calibration(_previous=None):
    """Use maintained new-centre tuning without requiring older fitting feeds.

    The installed aggregate sizes were learned from the retained 2019/2022
    comparison. Only the current authored seat classifications are added here;
    recalibrating historical_sizes remains an explicitly run offline experiment.
    """
    types_path = ANALYSIS_DIRECTORY / 'Data/seat-types.csv'
    calibration = maintained_parameters.ppvc_size_parameters()
    calibration['seat_types'] = ppvc_sizes.read_seat_types(types_path)
    return calibration, [maintained_parameters.PARAMETER_PATH, types_path, Path(ppvc_sizes.__file__)]


def select_snapshots(directory, through):
    """Select a short progression using capture timestamps, then retain source times.

    AEC ZIP names identify captures. The reader separately records the embedded
    creation timestamp and hashes the archive, so a later replacement or a
    repeated capture remains identifiable rather than silently becoming a new
    observation. The first capture is required to contain no results.
    """
    paths = {}
    for path in directory.glob('aec-mediafeed-Detailed-Light-31496-*.zip'):
        match = re.fullmatch(r'aec-mediafeed-Detailed-Light-31496-(\d{14})\.zip', path.name)
        if match and match[1][:8] <= through.replace('-', ''):
            paths[match[1]] = path
    if not paths:
        raise ValueError('No retained Federal 2025 snapshots in ' + str(directory))
    distance = lambda s, t: abs((datetime.strptime(s, '%Y%m%d%H%M%S') -
                                 datetime.strptime(t, '%Y%m%d%H%M%S')).total_seconds())
    selected = {min(paths), max(paths)}
    selected.update(min(paths, key=lambda s: distance(s, target)) for target in CHECKPOINTS
                    if target[:8] <= through.replace('-', ''))
    return [paths[s] for s in sorted(selected)]


def scoring_data(seat_names, categories):
    """Join final district/category counts and retained polling-place CSVs.

    Official OrdinaryVotes include ordinary pre-poll booths. The previously
    audited supplement splits that amount into ordinary pre-poll and other
    ordinary votes. Declaration pre-poll remains its own category. CSV booth
    sums are checked against those totals, so mismatched final revisions cannot
    silently change the scoring denominator.
    """
    final_path = ANALYSIS_DIRECTORY / 'Data/Turnout/2025fed.json'
    supplement_path = ANALYSIS_DIRECTORY / 'Data/Turnout/FederalPrepoll/final.json'
    final = read_json(final_path)
    seats = {r['seat_name']: r for r in final['seat_totals']}
    types = defaultdict(dict)
    for row in final['vote_types']:
        types[row['seat_name']][row['canonical_category']] = row['formal_votes']
    supplement = next(e for e in read_json(supplement_path)['elections'] if e['election_code'] == '2025fed')
    early = {r['seat_name']: r for r in supplement['districts']}
    groups = []
    for name in seat_names:
        typed, ppvc = types[name], early[name]['ordinary_early_formal']
        row = dict(absent=typed['absent'], early_declaration=typed['declaration_early'],
                   early_ordinary=ppvc, ordinary=typed['ordinary_combined'] - ppvc,
                   other=typed['provisional'], postal=typed['postal'])
        if sum(row.values()) != seats[name]['formal_votes'] or min(row.values()) < 0:
            raise ValueError('Final federal category accounting differs: ' + name)
        groups.append([row[g] for g in categories])

    # Use byte-addressed cached downloads. Updating latest.json selects a new
    # explicit revision on the next run; both the manifest and bytes are hashed.
    cache = REPOSITORY_DIRECTORY / 'downloads/turnout/federal-prepoll/2025fed'
    manifest_path = cache / 'latest.json'
    manifest = read_json(manifest_path)
    files = [final_path, supplement_path, manifest_path]
    rows = {}
    for key, entry in manifest.items():
        path = cache / entry['sha256'] / entry['url'].rsplit('/', 1)[-1]
        if digest(path) != entry['sha256']:
            raise ValueError('Retained final CSV differs from its source hash: ' + str(path))
        files.append(path)
        rows[key] = csv_rows(path.read_bytes())
    places = {(r['DivisionNm'], r['PollingPlaceID']): r for r in rows['places']}
    booths, informal = defaultdict(int), defaultdict(int)
    for state, state_rows in rows.items():
        if state == 'places':
            continue
        for row in state_rows:
            key = (row['DivisionNm'], row['PollingPlaceID'])
            target = informal if row['PartyNm'] == 'Informal' else booths
            target[key] += int(row['OrdinaryVotes'])
    empty, scored = [], {}
    for key in set(booths) | set(informal):
        place = places[key]
        if booths[key] == 0 and informal[key] == 0:
            # An entirely empty final record cannot distinguish an unused
            # service from missing reporting. Keep it out of booth scores,
            # without closing its current prediction identity retrospectively.
            empty.append(dict(seat=key[0], id=key[1], name=place['PollingPlaceNm']))
        else:
            scored[key] = dict(counted=booths[key], name=place['PollingPlaceNm'],
                               group='early_ordinary' if place['PollingPlaceTypeID'] == '5' else 'ordinary')
    for i, name in enumerate(seat_names):
        for group in ('ordinary', 'early_ordinary'):
            actual = sum(r['counted'] for (seat, _), r in scored.items() if seat == name and r['group'] == group)
            if actual != groups[i][categories.index(group)]:
                raise ValueError('Final booth CSV and category supplement differ: ' + name + '/' + group)
    return dict(totals=np.array([seats[n]['formal_votes'] for n in seat_names]),
                groups=np.array(groups), booths=scored, empty=sorted(empty, key=lambda r: (r['seat'], r['id'])),
                files=files)


def aggregate_units(values, units, seats, groups):
    """Collect baseline leaf sizes into the same categories used by the prototype."""
    result = np.zeros((seats, groups))
    for value, unit in zip(values, units):
        result[unit['seat_index'], unit['group_index']] += value
    return result


def matching_truth(unit, truth):
    """A final booth score needs agreement in district, ID, name and category."""
    row = truth['booths'].get((unit['seat_name'], unit['id']))
    return row if row and row['name'] == unit['name'] and row['group'] == unit['group'] else None


def booth_diagnostics(units, central, truth, baseline, balanced, adapted):
    """Separate unreported-booth accuracy, reporting order and rare revisions.

    Final counts appear only in this retrospective evaluation. In particular,
    they do not determine matching eligibility for the prediction multiplier
    or make a zero current return into a known closure.
    """
    scores, reporting, revisions = [], [], []
    for kind in ('ordinary', 'ppvc'):
        eligible = [u for u in units if u['kind'] == kind and u['matched']
                    and not u.get('calibration_exclusion_reason')]
        expectation = lambda u: central[u['seat_index'], u['group_index']] * u['weight']
        pivot = np.median([expectation(u) for u in eligible]) if eligible else 0
        for size in ('smaller', 'larger'):
            subset = [u for u in eligible if (expectation(u) <= pivot) == (size == 'smaller')]
            observed = [np.log(u['counted'] / expectation(u)) for u in subset if u['counted'] > 0]
            finals = [np.log(r['counted'] / expectation(u)) for u in subset
                      if (r := matching_truth(u, truth)) and r['counted'] > 0]
            reporting.append(dict(kind=kind, size=size, eligible=len(subset), reported=len(observed),
                                  mean_log_ratio=float(np.mean(observed)) if observed else None,
                                  final_mean_log_ratio=float(np.mean(finals)) if finals else None))
        errors = []
        for j, u in enumerate(units):
            if u['kind'] != kind or u['counted'] > 0 or u.get('calibration_exclusion_reason'):
                continue
            actual = matching_truth(u, truth)
            if actual:
                errors.append([abs(values[j] - actual['counted']) for values in (baseline, balanced, adapted)])
        scores.append(dict(kind=kind, booths=len(errors), **dict(zip(('baseline', 'balanced', 'adapted'),
                          np.mean(errors, axis=0).tolist() if errors else [None]*3))))
    for u in units:
        if u['kind'] == 'declaration' or not u['counted']:
            continue
        actual = matching_truth(u, truth)
        if actual and abs(actual['counted'] - u['counted']) >= 100:
            revisions.append(dict(seat=u['seat_name'], name=u['name'], counted=u['counted'],
                                  final=actual['counted'], difference=actual['counted'] - u['counted']))
    return scores, reporting, revisions


def run(args):
    """Build baseline outputs independently, optionally score the shared live updater."""
    started = time.perf_counter()
    metadata_path = retained_input(args.preload, '*Preload*31496*polling*')
    previous_path = retained_input(args.previous, '*Verbose*27966*.xml')
    metadata = aec_live.booth_metadata(metadata_path)
    previous = aec_live.read_house(previous_path, event_id='27966')
    paths = select_snapshots(args.sources, args.through)
    first = aec_live.read_house(paths[0], metadata, event_id='31496')
    if any(s['counted'] for s in first['seats'].values()):
        raise ValueError('The first retained snapshot must have no results to freeze allocation weights.')
    aliases = read_json(args.seat_aliases) if args.seat_aliases else {}
    categories = ['absent', 'early_declaration', 'early_ordinary', 'ordinary', 'other', 'postal']
    seat_names = sorted(first['seats'])
    files = [metadata_path, previous_path, Path(__file__), Path(aec_live.__file__),
             REPOSITORY_DIRECTORY / 'LiveV2.cpp', REPOSITORY_DIRECTORY / 'ElectionData.cpp']
    if args.seat_aliases:
        files.append(args.seat_aliases)
    template = aec_live.make_units(first, previous, categories, seat_names, aliases)
    initial_counts, _ = aec_live.baseline_counts(template)
    group_sizes = aggregate_units(initial_counts, template, len(seat_names), len(categories))
    weights = {u['id']: value / group_sizes[u['seat_index'], u['group_index']]
               for u, value in zip(template, initial_counts)}
    size_calibration = None
    size_details = {}
    if not args.baseline_only:
        # Starting weights are fixed at the no-results preload. They allocate
        # the prior category total without changing the C++ count baseline or
        # promoting changed centres to reliable trend observations.
        size_calibration, size_files = starting_size_calibration(previous)
        files.extend(size_files)
        weights, size_details = ppvc_sizes.starting_weights(
            template, initial_counts, metadata, size_calibration, args.ppvc_starting_sizes)
    # The analysis has no GUI project seat mapping. Explicit aliases can match
    # previousName/useFpResults settings. Report the fallback rather than guess
    # a predecessor for a new district such as Bullwinkel.
    missing_previous_seats = [name for name in seat_names if not any(
        n in previous['seats'] for n in [name, aliases.get(name, {}).get('previousName'),
                                       aliases.get(name, {}).get('useFpResults')])]
    truth = inputs = draws = central = None
    enrolment_revisions = []
    if not args.baseline_only:
        fixture = load_fixture(args.fixture, '2025fed')
        inputs, parameters = copy.deepcopy(fixture['inputs']), fixture['parameters']
        if inputs['seat_names'] != seat_names or inputs['categories'] != categories:
            raise ValueError('Federal prior fixture and snapshot category identities differ.')
        # The historical fixture uses the final published roll. A live replay
        # instead has the election-day roll, which the AEC can revise later.
        # Keep the fitted parameters and all operational controls frozen, but
        # supply the exposure actually known at the no-results snapshot.
        enrolment_revisions = [dict(seat=n, fixture=e, election_day=first['seats'][n]['enrolment'])
                               for n, e in zip(seat_names, inputs['enrolment'])
                               if e != first['seats'][n]['enrolment']]
        inputs['enrolment'] = [first['seats'][n]['enrolment'] for n in seat_names]
        draws = prior.draw(inputs, parameters, args.samples, args.seed)
        _, central = prior.central_counts(inputs, parameters)
        truth = scoring_data(seat_names, categories)
        files.extend([args.fixture, Path(live.__file__), Path(prior.__file__),
                      ANALYSIS_DIRECTORY / 'scripts/turnout/turnout_live_prototype.py',
                      ANALYSIS_DIRECTORY / 'scripts/turnout/turnout_federal_prepoll.py',
                      Path(turnout_federal_live_report.__file__), *truth['files']])
    # Optional historical diagnostics explain the research clock rule in the
    # report. They never supply current counts or fitted prediction parameters.
    reporting_history = None
    if args.reporting_history and not args.baseline_only:
        reporting_history = read_json(args.reporting_history)
        for source, fingerprint in reporting_history['provenance'].items():
            if digest(Path(source)) != fingerprint:
                raise ValueError('Refresh historical reporting diagnostics after a source change: ' + source)
        files.append(args.reporting_history)
    snapshots, sources = [], {}
    for path in paths:
        current = first if path == paths[0] else aec_live.read_house(path, metadata, event_id='31496')
        units = aec_live.make_units(current, previous, categories, seat_names, aliases)
        if {u['id'] for u in units} != set(weights):
            raise ValueError('Current booth identities changed; review new/removed identities explicitly.')
        for unit in units:
            unit['weight'] = weights[unit['id']]
            unit.update(size_details.get(unit['id'], {}))
        baseline, legacy_change = aec_live.baseline_counts(units)
        legacy_groups = aggregate_units(baseline, units, len(seat_names), len(categories))
        counted = np.array([current['seats'][n]['counted'] for n in seat_names])
        row = dict(source_timestamp=current['source_time'], archive=path.name, counted=int(counted.sum()),
                   baseline_ppvc=legacy_change)
        unit_rows = [dict(u, baseline_final=float(value)) for u, value in zip(units, baseline)]
        district_rows = [dict(seat=name, counted=int(counted[i]), baseline_total=float(legacy_groups[i].sum()),
                              baseline_groups=legacy_groups[i].tolist()) for i, name in enumerate(seat_names)]
        if truth is not None:
            changes = live.pooled_booth_changes(units, central, args.prior_booths, half_votes=args.pooled_half_votes)
            hours = (datetime.fromisoformat(current['source_time']) - datetime(2025, 5, 3, 18)).total_seconds()/3600
            decay = ppvc_sizes.reporting_decay_factor(hours) if args.ppvc_reporting_decay else 1.
            update_started = time.perf_counter()
            balanced = live.update(draws, inputs, units, unreported_ppvc_factor=decay)
            adapted = live.update(draws, inputs, units, changes=changes, compensation_order=args.compensation_order,
                                  unreported_ppvc_factor=decay)
            row['unreported_ppvc_factor'] = decay
            row['update_seconds'] = time.perf_counter() - update_started
            for j, unit in enumerate(unit_rows):
                unit.update(complete=bool(adapted['complete'][j]),
                            balanced_final=float(balanced['unit_counts'][:, j].mean()),
                            adapted_final=float(adapted['unit_counts'][:, j].mean()))
            for i, district in enumerate(district_rows):
                low, high = np.quantile(balanced['totals'][:, i], [.025, .975])
                district.update(actual=int(truth['totals'][i]), mean_total=float(adapted['totals'][:, i].mean()),
                                mean_remaining=float(adapted['totals'][:, i].mean() - counted[i]),
                                low_95=float(low), high_95=float(high), actual_groups=truth['groups'][i].tolist(),
                                mean_groups=balanced['counts'][:, i].mean(axis=0).tolist())
            booth_scores, reporting, revisions = booth_diagnostics(units, central, truth, baseline,
                balanced['unit_counts'].mean(axis=0), adapted['unit_counts'].mean(axis=0))
            row.update(final_remaining=int(truth['totals'].sum() - counted.sum()),
                       baseline_total_mae=float(np.abs(legacy_groups.sum(axis=1) - truth['totals']).mean()),
                       baseline_category_mae=float(np.abs(legacy_groups - truth['groups']).mean()),
                       total_score=interval_summary(adapted['totals'], truth['totals']),
                       category_score=interval_summary(balanced['counts'], truth['groups']),
                       adapted_category_score=interval_summary(adapted['counts'], truth['groups']),
                       changes=changes, booth_scores=booth_scores, reporting=reporting,
                       maximum_adaptation_difference=float(np.abs(adapted['unit_counts'] - balanced['unit_counts']).max()),
                       substantial_complete_booth_differences=revisions, diagnostics=adapted['diagnostics'])
            # A missing project predecessor could otherwise make the current
            # reference unfairly weak. Retain the same-district comparison too,
            # excluding only districts without previous declaration counts.
            matched = np.array([n not in missing_previous_seats for n in seat_names])
            row['previous_district_comparison'] = dict(
                districts=int(matched.sum()),
                baseline_mae=float(np.abs(legacy_groups.sum(axis=1)[matched] - truth['totals'][matched]).mean()),
                prototype_mae=float(np.abs(adapted['totals'].mean(axis=0)[matched] - truth['totals'][matched]).mean()))
        row.update(districts=district_rows, units=unit_rows)
        snapshots.append(row)
        sources[str(path)] = digest(path)
    return dict(election='2025fed', baseline_version=aec_live.BASELINE_VERSION,
                model_version=live.MODEL_VERSION if truth is not None else None,
                compensation=live.compensation_configuration() if truth is not None else None,
                ppvc_starting_size_calibration=size_calibration,
                ppvc_reporting_history=reporting_history,
                ppvc_reporting_decay_knots=[list(k) for k in ppvc_sizes.REPORTING_DECAY_KNOTS] if args.ppvc_reporting_decay else [],
                prior_model=prior.MODEL_VERSION if truth is not None else None,
                categories=categories, missing_previous_seats=missing_previous_seats,
                enrolment_revisions=enrolment_revisions,
                initial_unknown_booth_counts=sum(u['previous'] is None for u in template if u['kind'] != 'declaration'),
                empty_final_booth_placeholders=truth['empty'] if truth is not None else [],
                config=dict(samples=args.samples, seed=args.seed, prior_equivalent_booths=args.prior_booths,
                            pooled_half_votes=args.pooled_half_votes, compensation_order=args.compensation_order,
                            ppvc_starting_sizes=args.ppvc_starting_sizes,
                            ppvc_reporting_decay=args.ppvc_reporting_decay,
                            reporting_history=str(args.reporting_history) if args.reporting_history else None,
                            through=args.through, baseline_only=args.baseline_only),
                libraries=dict(numpy=np.__version__, scipy=scipy.__version__),
                provenance=dict(inputs={str(p): digest(p) for p in files}, snapshots=sources),
                snapshots=snapshots, elapsed_seconds=time.perf_counter() - started)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, default=download_directory())
    parser.add_argument('--preload', type=Path)
    parser.add_argument('--previous', type=Path)
    parser.add_argument('--seat-aliases', type=Path, help='JSON mapping current seats to previousName/useFpResults settings.')
    parser.add_argument('--fixture', type=Path, default=REPOSITORY_DIRECTORY / 'docs/turnout-prior-prototype/fixtures-v3.json')
    parser.add_argument('--output-directory', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--through', default='2025-05-22')
    parser.add_argument('--samples', type=int, default=1024)
    parser.add_argument('--seed', type=int, default=20261002)
    parser.add_argument('--prior-booths', type=float, default=8)
    parser.add_argument('--pooled-half-votes', type=float, default=5000000,
                        help='Observed PPVC votes giving half influence in the diagnostic pooled adjustment.')
    parser.add_argument('--ppvc-starting-sizes', choices=('legacy', 'mean', 'median', 'mean_by_type', 'median_by_type'), default='legacy',
                        help='Historical local new/changed centre size used for initial allocation weights.')
    parser.add_argument('--ppvc-reporting-decay', action='store_true',
                        help='Experiment with the conservative 2019/2022 delayed-public-centre clock rule.')
    parser.add_argument('--reporting-history', type=Path,
                        help='Include previously generated historical reporting diagnostics in the report only.')
    parser.add_argument('--compensation-order', choices=('before_pool', 'after_pool'), default='after_pool',
                        help='Whether the diagnostic compensation responds before or after removing the pooled trend.')
    parser.add_argument('--baseline-only', action='store_true', help='Export deterministic counts without the turnout prior or final scoring data.')
    parser.add_argument('--dry-run', action='store_true', help='Perform the calculation without writing outputs.')
    args = parser.parse_args()
    if args.samples < 2 or args.prior_booths <= 0 or args.pooled_half_votes <= 0:
        parser.error('Use at least two samples and a positive prior-booth strength.')
    result = run(args)
    if not args.dry_run:
        args.output_directory.mkdir(parents=True, exist_ok=True)
        filename = 'baseline.json' if args.baseline_only else 'analysis.json'
        (args.output_directory / filename).write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
        if not args.baseline_only:
            (args.output_directory / 'report.md').write_text(turnout_federal_live_report.render(result), encoding='utf-8')
    print(json.dumps(dict(baseline=result['baseline_version'], snapshots=len(result['snapshots']),
                          seconds=round(result['elapsed_seconds'], 3), dry_run=args.dry_run)))


if __name__ == '__main__':
    main()
