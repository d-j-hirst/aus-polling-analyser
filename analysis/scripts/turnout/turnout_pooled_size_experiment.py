"""Compare cautious PPVC size adjustments on the retained Federal 2025 counts.

Every comparison uses the same prior outcomes, EAV override, current counts,
partial-compensation coefficients and district-total rule. Only the PPVC pooled
formula, its strength, observation weighting and compensation order vary.
Final results score the alternatives; they never enter a snapshot adjustment.
"""

import argparse
import copy
from datetime import datetime
import json
from pathlib import Path
import time

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import aec_live, live, prior, ppvc_sizes
from scripts.turnout import turnout_federal_live, turnout_live_prototype
from scripts.turnout.turnout_federal_live import scoring_data, matching_truth
from scripts.turnout.turnout_live_prototype import digest, load_fixture, read_json, interval_summary


DIRECTORY = REPOSITORY_DIRECTORY / 'docs/turnout-federal-live-prototype'


def configurations():
    """Use a small predetermined grid, with shared vote scales across shapes.

    The half-influence count makes strengths comparable: at that count each
    shape applies half of the raw deviation. Include the C++ formula itself,
    stronger shrinkage, different progression shapes and alternative averages.
    """
    values = [('no_pool', None), ('old_booth_rule', dict(legacy=True))]
    candidates = [('cpp_strength', dict(half_votes=20000**(1/.9)))]
    candidates += [(f'power09_{int(v/1000000)}m', dict(half_votes=v)) for v in (1000000, 2000000, 5000000, 10000000)]
    candidates += [('power1_2m', dict(half_votes=2000000, exponent=1)),
                   ('power2_2m', dict(half_votes=2000000, exponent=2)),
                   ('exponential_2m', dict(half_votes=2000000, formula='exponential')),
                   ('observed_weight_2m', dict(half_votes=2000000, weighting='observed_votes')),
                   ('expected_weight_2m', dict(half_votes=2000000, weighting='expected_votes'))]
    for name, parameters in candidates:
        for order in ('before_pool', 'after_pool'):
            values.append((name + '/' + order, dict(parameters, compensation_order=order)))
    return values


def run(source_path, fixture_path, samples=256, names=None, starting_sizes='legacy', reporting_decay=False):
    """Reuse saved counted accounts without rereading every source ZIP.

    The saved no-results allocation weights and election-day roll are the same
    prediction inputs as the normal replay. The final CSV join is separately
    checked against the saved revision so later source changes are explicit.
    All schemes share a reduced prior sample for an initial comparison; selected
    alternatives can be rerun at the normal 1,024 outcomes using --schemes.
    """
    started = time.perf_counter()
    source = read_json(source_path)
    fixture = load_fixture(fixture_path, '2025fed')
    inputs = copy.deepcopy(fixture['inputs'])
    for row in source['enrolment_revisions']:
        inputs['enrolment'][inputs['seat_names'].index(row['seat'])] = row['election_day']
    draws = prior.draw(inputs, fixture['parameters'], samples, 20261002)
    _, central = prior.central_counts(inputs, fixture['parameters'])
    truth = scoring_data(inputs['seat_names'], inputs['categories'])
    for path in truth['files']:
        if source['provenance']['inputs'].get(str(path)) != digest(path):
            raise ValueError('Final scoring revision differs from the retained comparison: ' + str(path))
    # Rebuild every starting-weight variant from the same baseline leaf sizes,
    # even if the saved source itself was generated with another allocation.
    # Election-day preload geography and historical results are prediction
    # inputs; current/final counts do not classify or size new centres.
    metadata_path = next(Path(p) for p in source['provenance']['inputs'] if 'Preload' in p and 'polling_places' in p)
    previous_path = next(Path(p) for p in source['provenance']['inputs'] if 'Verbose' in p and '27966' in p)
    metadata = aec_live.booth_metadata(metadata_path)
    previous = aec_live.read_house(previous_path, event_id='27966')
    calibration, size_files = turnout_federal_live.starting_size_calibration(previous)
    origin = source['snapshots'][0]
    if origin['counted'] != 0:
        raise ValueError('Starting weights require a no-results origin.')
    weights, details = ppvc_sizes.starting_weights(origin['units'], [u['baseline_final'] for u in origin['units']],
                                                  metadata, calibration, starting_sizes)
    variants = []
    for name, parameters in configurations():
        if names and name not in names:
            continue
        snapshots = []
        for snapshot in source['snapshots']:
            units = [dict(u, weight=weights[u['id']], **details[u['id']]) for u in snapshot['units']]
            # Saved origins may predate a reviewed reporting exception. Apply
            # the current explicit policy without changing their observations.
            for unit in units:
                unit['calibration_exclusion_reason'] = aec_live.calibration_exclusion_reason(
                    '31496', unit['seat_name'], unit['id'])
            order = (parameters or {}).get('compensation_order', 'before_pool')
            arguments = {k: v for k, v in (parameters or {}).items() if k != 'compensation_order'}
            changes = live.pooled_booth_changes(units, central, **arguments) if parameters is not None else None
            # Ordinary adaptation is held off in every scheme. This experiment
            # isolates the PPVC change rather than altering two booth types.
            supplied = {'ppvc': changes['ppvc']} if changes is not None else None
            begin = time.perf_counter()
            hours = (datetime.fromisoformat(snapshot['source_timestamp']) - datetime(2025, 5, 3, 18)).total_seconds()/3600
            decay = ppvc_sizes.reporting_decay_factor(hours) if reporting_decay else 1.
            updated = live.update(draws, inputs, units, changes=supplied, compensation_order=order,
                                  unreported_ppvc_factor=decay)
            unreported = [(j, matching_truth(u, truth)) for j, u in enumerate(units)
                          if u['kind'] == 'ppvc' and not u['counted']]
            errors = [abs(updated['unit_counts'][:, j].mean()-actual['counted'])
                      for j, actual in unreported if actual is not None]
            snapshots.append(dict(source_timestamp=snapshot['source_timestamp'],
                category_score=interval_summary(updated['counts'], truth['groups']),
                total_score=interval_summary(updated['totals'], truth['totals']),
                unreported_ppvc_mae=float(np.mean(errors)) if errors else None,
                pooled=supplied['ppvc'] if supplied else None,
                eav_bullwinkel_mean=float(updated['unit_counts'][:, next(j for j, u in enumerate(units)
                                            if u['name'] == 'EAV Bullwinkel PPVC')].mean()),
                diagnostics=updated['diagnostics'], seconds=time.perf_counter()-begin))
            snapshots[-1]['unreported_ppvc_factor'] = decay
        # Summarise the paired results here, keeping statistical calculations
        # out of the report renderer. Repeated snapshots are one election;
        # their average is a compact comparison, not independent validation.
        summaries = dict(mean_category_mae=float(np.mean([s['category_score']['mean_absolute_error'] for s in snapshots[1:]])),
                         mean_interval_score=float(np.mean([s['category_score']['interval_score_95'] for s in snapshots[1:]])))
        variants.append(dict(name=name, parameters=parameters, snapshots=snapshots, summary=summaries))
        print(name + ' complete', flush=True)
    if names and set(names) != {v['name'] for v in variants}:
        raise ValueError('Unknown scheme in --schemes.')
    return dict(election='2025fed', model=live.MODEL_VERSION, samples=samples,
                ppvc_starting_sizes=starting_sizes, ppvc_starting_size_calibration=calibration,
                ppvc_reporting_decay=reporting_decay,
                ppvc_reporting_decay_knots=[list(k) for k in ppvc_sizes.REPORTING_DECAY_KNOTS] if reporting_decay else [],
                compensation=live.compensation_configuration(),
                held_constant='Current counts, EAV replacement, prior, district totals and ordinary adaptation (off).',
                interpretation='Paired exploratory comparisons in one election; not independent parameter validation.',
                variants=variants, elapsed_seconds=time.perf_counter()-started,
                provenance={str(p): digest(p) for p in [source_path, fixture_path, Path(__file__),
                    Path(live.__file__), Path(prior.__file__), Path(turnout_federal_live.__file__),
                    Path(turnout_live_prototype.__file__), Path(aec_live.__file__), metadata_path, previous_path,
                    *size_files, REPOSITORY_DIRECTORY/'LiveV2.cpp', *truth['files']]})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DIRECTORY/'analysis.json')
    parser.add_argument('--fixture', type=Path, default=REPOSITORY_DIRECTORY/'docs/turnout-prior-prototype/fixtures-v3.json')
    parser.add_argument('--output', type=Path, default=DIRECTORY/'pooled-size-experiment.json')
    parser.add_argument('--samples', type=int, default=256)
    parser.add_argument('--ppvc-starting-sizes', choices=('legacy', 'mean', 'median', 'mean_by_type', 'median_by_type'), default='legacy')
    parser.add_argument('--ppvc-reporting-decay', action='store_true')
    parser.add_argument('--schemes', help='Comma-separated scheme names for a smaller confirmation run.')
    args = parser.parse_args()
    if args.samples < 2:
        parser.error('Use at least two outcomes.')
    result = run(args.source, args.fixture, args.samples, args.schemes.split(',') if args.schemes else None,
                 args.ppvc_starting_sizes, args.ppvc_reporting_decay)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps(dict(schemes=len(result['variants']), samples=args.samples, seconds=result['elapsed_seconds'])))


if __name__ == '__main__':
    main()
