"""Explore whether Federal 2025 PPVC size errors offset elsewhere in a district.

Use the saved prototype's original booth estimates and the final AEC counts.
No live progression, model fitting or forecast modification is performed.
Run from analysis with python -B -m scripts.turnout.turnout_ppvc_compensation.
The default JSON output is ignored; the exploratory findings belong in chat.
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path

import numpy as np
from scipy.special import logit
from scipy.stats import spearmanr

from lib.paths import ANALYSIS_DIRECTORY, REPOSITORY_DIRECTORY
from lib.turnout import aec_live
from scripts.turnout.turnout_federal_live import matching_truth, scoring_data
from scripts.turnout.turnout_live_prototype import digest, read_json


DIRECTORY = REPOSITORY_DIRECTORY / 'docs/turnout-federal-live-prototype'
BANDS = ((0, None), (2000, None), (0, 500), (500, 1000),
         (1000, 2000), (2000, 4000), (4000, 8000), (8000, None))


def correlation(x, y, weights=None):
    """Measure association, optionally giving each district equal total weight."""
    weights = np.ones(len(x)) if weights is None else np.asarray(weights)
    x = np.asarray(x) - np.average(x, weights=weights)
    y = np.asarray(y) - np.average(y, weights=weights)
    denominator = np.sqrt(np.sum(weights * x*x) * np.sum(weights * y*y))
    return float(np.sum(weights * x*y) / denominator) if denominator else None


def district_interval(rows, repetitions, seed):
    """Resample whole districts because a booth and its neighbours overlap.

    Treating every booth pair as independent would overstate precision. Each
    resampled district carries all of its qualifying pairs, including repeated
    use of the same other-booth counts. These intervals describe this one
    election's sampling variation, not uncertainty about future elections.
    """
    grouped = defaultdict(list)
    for row in rows:
        grouped[row['seat']].append(row)
    seats = sorted(grouped)
    moments = []
    for seat in seats:
        x = np.array([r['subset_deviation'] for r in grouped[seat]])
        y = np.array([r['other_subset_deviation'] for r in grouped[seat]])
        moments.append([len(x), x.sum(), y.sum(), (x*x).sum(), (y*y).sum(), (x*y).sum()])
    weights = np.random.default_rng(seed).multinomial(len(seats), np.ones(len(seats))/len(seats), repetitions)
    n, sx, sy, sxx, syy, sxy = (weights @ np.array(moments)).T
    values = (sxy-sx*sy/n) / np.sqrt((sxx-sx*sx/n)*(syy-sy*sy/n))
    slopes = (sxy-sx*sy/n) / (syy-sy*sy/n)
    return dict(correlation=np.quantile(values[np.isfinite(values)], [.025, .975]).tolist(),
                prediction_slope=np.quantile(slopes[np.isfinite(slopes)], [.025, .975]).tolist())


def summarise(rows, repetitions, seed):
    """Compare transformed, proportional and absolute errors on the same pairs.

    Log odds uses enrolment as a fixed parent for both expected and actual
    counts. In particular, neither is divided by the realised PPVC total:
    doing that would force compositional opposition into the comparison.
    Log count ratios and untransformed errors show whether the conclusion
    depends on the scale used. Prediction slopes additionally express how
    much the focal deviation changes per unit of other-centre deviation;
    correlations alone cannot be used as those adjustment coefficients.
    """
    if len(rows) < 3:
        return dict(booths=len(rows), districts=len({r['seat'] for r in rows}))
    pairs = {
        'subset_log_odds': ('subset_deviation', 'other_subset_deviation'),
        'log_count_ratio': ('log_ratio', 'other_log_ratio'),
        'relative_count_error': ('relative_error', 'other_relative_error'),
        'vote_count_error': ('error', 'other_error'),
    }
    counts = defaultdict(int)
    for row in rows:
        counts[row['seat']] += 1
    metrics = {}
    for name, (a, b) in pairs.items():
        x, y = [r[a] for r in rows], [r[b] for r in rows]
        metrics[name] = dict(pearson=correlation(x, y), spearman=float(spearmanr(x, y).statistic))
    x = [r['subset_deviation'] for r in rows]
    y = [r['other_subset_deviation'] for r in rows]
    omitted = {seat: correlation([r['subset_deviation'] for r in rows if r['seat'] != seat],
                                [r['other_subset_deviation'] for r in rows if r['seat'] != seat])
               for seat in counts}
    intervals = district_interval(rows, repetitions, seed)
    return dict(booths=len(rows), districts=len(counts), metrics=metrics,
                subset_correlation_95=intervals['correlation'],
                subset_prediction_slope_95=intervals['prediction_slope'],
                equal_district_subset_correlation=correlation(x, y, [1/counts[r['seat']] for r in rows]),
                leaving_one_district_out_range=[min(omitted.values()), max(omitted.values())])


def analyse(origin_path, repetitions=2000, seed=20261003):
    """Join original estimates with finals, retaining unknown booth exclusions.

    The combined actual count at other PPVCs comes from the known district
    total minus the focal booth. Consequently an empty final booth record is
    never invented as zero, and cannot invalidate a known aggregate. A focal
    booth itself needs the existing exact district/ID/name/category match.
    """
    source = read_json(origin_path)
    origin = source['snapshots'][0]
    if source['election'] != '2025fed' or origin['counted'] != 0:
        raise ValueError('Use the Federal 2025 prototype export with a no-results origin.')
    seat_names = [d['seat'] for d in origin['districts']]
    truth = scoring_data(seat_names, source['categories'])
    final_path = ANALYSIS_DIRECTORY / 'Data/Turnout/2025fed.json'
    roll = {r['seat_name']: r['enrolment'] for r in read_json(final_path)['seat_totals']}
    # Require the final revision used by the original analysis. A later source
    # update requires an explicit regenerated origin/scoring comparison.
    for path in truth['files']:
        if source['provenance']['inputs'].get(str(path)) != digest(path):
            raise ValueError('Final scoring source differs from the saved analysis: ' + str(path))
    units = [u for u in origin['units'] if u['group'] == 'early_ordinary']
    expected_totals = defaultdict(float)
    for unit in units:
        expected_totals[unit['seat_name']] += unit['balanced_final']
    actual_totals = dict(zip(seat_names, truth['groups'][:, source['categories'].index('early_ordinary')]))
    # Each pair's counterpart includes all other PPVCs in its district. A
    # consolidation therefore compromises Brand's counterpart comparisons too,
    # not just the two focal identities. Omit Brand from this booth relationship
    # calibration; its valid district total remains in turnout scoring.
    excluded_districts = {u['seat_name'] for u in units if aec_live.calibration_exclusion_reason(
        '31496', u['seat_name'], u['id'])}
    rows, excluded = [], []
    for unit in units:
        expected = unit['balanced_final']
        if unit['seat_name'] in excluded_districts:
            excluded.append(dict(seat=unit['seat_name'], name=unit['name'], expected=expected,
                reason='District counterpart includes the reviewed Rockingham reporting consolidation.'))
            continue
        final = matching_truth(unit, truth)
        if final is None:
            excluded.append(dict(seat=unit['seat_name'], name=unit['name'], expected=expected,
                                 reason='No usable exact final booth match; not treated as zero.'))
            continue
        seat = unit['seat_name']
        actual = final['counted']
        other_expected = expected_totals[seat] - expected
        other_actual = int(actual_totals[seat]) - actual
        if other_expected <= 0:
            excluded.append(dict(seat=seat, name=unit['name'], expected=expected, reason='No other expected PPVC votes.'))
            continue
        # Possible observed zeros receive a small count equivalent only for
        # transformed calculations; original integer counts remain available.
        safe_actual, safe_other = max(actual, .5), max(other_actual, .5)
        if max(expected, other_expected, safe_actual, safe_other) >= roll[seat]:
            raise ValueError('PPVC counts reach their enrolment parent: ' + seat)
        rows.append(dict(seat=seat, name=unit['name'], id=unit['id'], expected=expected, actual=actual,
                         other_expected=other_expected, other_actual=other_actual,
                         reliable_previous_match=unit['matched'], same_seat=unit['same_seat'],
                         previous=unit['previous'], is_eav=unit['name'].startswith('EAV '),
                         subset_deviation=float(logit(safe_actual/roll[seat])-logit(expected/roll[seat])),
                         other_subset_deviation=float(logit(safe_other/roll[seat])-logit(other_expected/roll[seat])),
                         log_ratio=float(np.log(safe_actual/expected)),
                         other_log_ratio=float(np.log(safe_other/other_expected)),
                         relative_error=actual/expected-1, other_relative_error=other_actual/other_expected-1,
                         error=actual-expected, other_error=other_actual-other_expected))
    # EAV services are identifiable before results arrive. Keep their own
    # comparison, and separate them from ordinary centres in the size bands:
    # small central services need not behave like small public voting centres.
    # The requested result includes every usable focal booth. Additional
    # input-based restrictions expose the effect of uncertain historical matches
    # and a very small combined counterpart without choosing on final errors.
    selections = {
        'all': rows,
        'non_eav': [r for r in rows if not r['is_eav']],
        'reliable_focal_match': [r for r in rows if r['reliable_previous_match']],
        'reliable_non_eav': [r for r in rows if r['reliable_previous_match'] and not r['is_eav']],
        'other_expected_at_least_2000': [r for r in rows if r['other_expected'] >= 2000],
    }
    tables = {}
    for selection, selected in selections.items():
        tables[selection] = [dict(expected_minimum=low, expected_maximum_exclusive=high,
            **summarise([r for r in selected if r['expected'] >= low and (high is None or r['expected'] < high)],
                        repetitions, seed)) for low, high in BANDS]
    eav = [r for r in rows if r['is_eav']]
    eav_summary = dict(all=summarise(eav, repetitions, seed),
                       reliable_focal_match=summarise([r for r in eav if r['reliable_previous_match']], repetitions, seed))
    districts = [dict(seat=s, expected=e, actual=int(actual_totals[s]), error=int(actual_totals[s])-e)
                 for s, e in expected_totals.items()]
    return dict(election='2025fed', origin_source_time=origin['source_timestamp'],
                config=dict(bootstrap_repetitions=repetitions, seed=seed, zero_count_equivalent=.5),
                definitions=dict(expected='Mean original no-results prototype booth count.',
                    others='All other ordinary PPVC votes in the district, including booths expected below 2000.',
                    subset_deviation='logit(actual/enrolment) minus logit(expected/enrolment); same fixed district enrolment for both.',
                    intervals='95% percentile intervals from resampling whole districts.',
                    eav='Booth name begins with EAV; grouped separately regardless of expected size.',
                    reliable_match='Same district, exact booth name and type, positive previous count.'),
                provenance={str(p): digest(p) for p in [origin_path, Path(__file__), Path(aec_live.__file__), *truth['files']]},
                tables=tables, eav=eav_summary, excluded=excluded, districts=districts, pairs=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--origin', type=Path, default=DIRECTORY / 'analysis.json')
    parser.add_argument('--output', type=Path, default=DIRECTORY / 'ppvc-compensation.json')
    parser.add_argument('--bootstrap-repetitions', type=int, default=2000)
    args = parser.parse_args()
    if args.bootstrap_repetitions < 100:
        parser.error('Use at least 100 district resamples.')
    result = analyse(args.origin, args.bootstrap_repetitions)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(dict(tables=result['tables'], eav=result['eav'], excluded=result['excluded']), indent=2))


if __name__ == '__main__':
    main()
