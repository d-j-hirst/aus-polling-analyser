"""Test whether earlier ordinary booths predict differences between early centres.

Historical tests compare two centres in the same electorate. A uniform swing,
or a shift affecting all early voters in the seat, then does not masquerade
as a geographic effect. Results are scored in percentage points of formal
votes. The application uses the selected method to divide a given district
early-vote expectation among venues.
"""

import argparse
from collections import defaultdict
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path

import numpy as np
from scipy.special import expit, logit

from evidence import (CHOSEN_WINDOW, FIRST_REPORTS, HERE, MIN_VOTES, STRENGTH, WINDOWS, counts, district_rows,
                      read_csv, write_csv)


QUANTITIES = ('Labor TPP', 'Labor FP', 'Coalition FP', 'Greens FP', 'Other FP')


def log_odds(n, d):
    # Half a ballot on each side keeps a genuine zero or unanimous vote finite.
    # A missing count remains missing throughout the calculation.
    return np.log((n + 0.5) / (d - n + 0.5))


@dataclass
class Pairs:
    district: np.ndarray
    has_outside: np.ndarray
    weight: np.ndarray
    first: np.ndarray
    second: np.ndarray
    first_log_odds: np.ndarray
    second_log_odds: np.ndarray
    local_difference: np.ndarray
    local_fp_log_ratio: np.ndarray
    centres: int


def pair_sample(districts, neighbours):
    """Compare substantial centres within seats, keeping each seat equally influential.

    Within a seat, the product of a pair's formal vote totals gives more weight
    to comparisons between substantial centres. These final counts are used to
    score comparisons and fit historical strengths. They never enter the
    local predictor for a new election.
    """
    parts = [[] for _ in range(9)]
    centres = 0
    for seat, sites in districts.items():
        sites = [r for r in sites if float(r['formal']) >= MIN_VOTES and int(r['available']) >= 3]
        if len(sites) < 2:
            continue
        centres += len(sites)
        n, d = (np.stack(x) for x in zip(*(counts(r) for r in sites)))
        local, den = (np.stack(x) for x in zip(*(counts(r, f'n{neighbours}_') for r in sites)))
        shares, odds = n / d, log_odds(n, d)
        local_odds = log_odds(local, den)
        # Four half-ballots (one per group) give a local composition summing
        # to one even when a group received no votes at a nearby booth.
        local_fp = np.log((local[:, 1:] + 0.5) / (den[:, 1:] + 2))
        i, j = np.array(list(combinations(range(len(sites)), 2))).T
        mass = d[i, 1] * d[j, 1]
        outside = np.array([r['catchment'] == 'likely_outside' for r in sites])
        fields = (np.repeat(seat, len(i)), outside[i] | outside[j], mass / mass.sum(), shares[i], shares[j], odds[i], odds[j],
                  local_odds[j] - local_odds[i], local_fp[j] - local_fp[i])
        for result, value in zip(parts, fields):
            result.append(value)
    return Pairs(*(np.concatenate(x) for x in parts), centres)


def fit_strength(samples):
    """Estimate the share of the local difference carried into the early-centre gap.

    A straight line through zero is fitted on log odds. Equal local patterns
    therefore imply equal expected EVC patterns within a seat. A strength
    of one carries across the entire difference. Each seat-election supplies
    the same total weight regardless of the number of centre pairs.
    """
    numerator = sum((p.weight[:, None] * p.local_difference *
                     (p.second_log_odds - p.first_log_odds)).sum(axis=0) for p in samples)
    denominator = sum((p.weight[:, None] * p.local_difference**2).sum(axis=0) for p in samples)
    return np.divide(numerator, denominator, out=np.full(5, np.nan), where=denominator > 0)


def joint_prediction(anchor, log_ratio, strength):
    """Move a reported FP composition towards the next centre's local pattern.

    Multiply each group's observed share by its relative local strength, then
    divide by the sum. All four groups still account for exactly 100% of the
    formal vote, and a group absent from the supplied anchor stays at zero.
    """
    tilt = strength * log_ratio
    adjusted = anchor * np.exp(tilt - tilt.max(axis=1, keepdims=True))
    return adjusted / adjusted.sum(axis=1, keepdims=True)


def primary_prediction(anchor, log_ratio, strength, fixed_other):
    """Optionally reserve the district's combined vote for other parties and independents.

    The group called Other contains different candidates across elections.
    The fixed-Other test holds its reported share at the first centre and uses
    geography to allocate the three remaining groups' combined share.
    """
    if not fixed_other:
        return joint_prediction(anchor, log_ratio, strength)
    result = anchor.copy()
    mass = anchor[:, :3].sum(axis=1)
    active = mass > 0
    result[active, :3] = mass[active, None] * joint_prediction(
        anchor[active, :3], log_ratio[active, :3], strength)
    return result


def predict_pairs(p, strength, joint=False, fixed_other=False):
    """Predict each end from the other so the arbitrary order of two sites is immaterial."""
    change = p.local_difference * strength
    # Start from the actual reported share for both methods. A zero local
    # difference reproduces that share exactly, including zero or 100%.
    forward = expit(logit(p.first) + change)
    backward = expit(logit(p.second) - change)
    if joint:
        forward[:, 1:] = primary_prediction(p.first[:, 1:], p.local_fp_log_ratio, strength, fixed_other)
        backward[:, 1:] = primary_prediction(p.second[:, 1:], -p.local_fp_log_ratio, strength, fixed_other)
    return forward - p.second, backward - p.first


def score_pairs(election, method, neighbours, p, strength, joint=False, fixed_other=False):
    """Report average absolute error and variation between districts, in percentage points."""
    forward, backward = predict_pairs(p, strength, joint, fixed_other)
    errors = 50 * (abs(forward) + abs(backward))
    square_errors = 5000 * (forward**2 + backward**2)
    baseline = 100 * abs(p.first - p.second)
    records = []
    for j, quantity in enumerate(QUANTITIES):
        valid = np.isfinite(errors[:, j])
        if not valid.any():
            continue
        seats = np.unique(p.district[valid])
        gains = []
        for seat in seats:
            which = valid & (p.district == seat)
            gains.append(np.average(baseline[which, j] - errors[which, j], weights=p.weight[which]))
        # Resample whole districts to describe uncertainty from the mix of
        # seats. Pairs sharing the same centre are not independent trials.
        draws = np.random.default_rng(20261122).choice(gains, (3000, len(seats))).mean(axis=1)
        low, high = np.quantile(draws, [0.025, 0.975])
        beta = 0.0 if fixed_other and quantity == 'Other FP' else np.broadcast_to(strength, (5,))[j]
        records.append(dict(election=election, method=method, neighbours=neighbours, quantity=quantity,
                            strength=round(beta, 5), districts=len(seats), centres=p.centres, pairs=sum(valid),
                            baseline_mae_pp=round(np.average(baseline[valid, j], weights=p.weight[valid]), 4),
                            mae_pp=round(np.average(errors[valid, j], weights=p.weight[valid]), 4),
                            rmse_pp=round(np.sqrt(np.average(square_errors[valid, j], weights=p.weight[valid])), 4),
                            districts_improved=sum(g > 1e-10 for g in gains),
                            mean_gain_low_pp=round(low, 4), mean_gain_high_pp=round(high, 4)))
    return records


def historical_tests(elections):
    """Fit on preceding federal elections and test the next election in isolation.

    November 2022 in Victoria can use the federal election held that May.
    Earlier state FP turnout and behaviour never supply the state test outcome
    to the federal fit. The 2026 TPP strength uses all completed federal tests.
    """
    records, catchments, night_strengths = [], [], {}
    for k in WINDOWS:
        samples = {e: pair_sample(district_rows(rows), k) for e, rows in elections.items()}
        for election in ('2019fed', '2022fed', '2025fed', '2022vic'):
            eligible = [p for e, p in samples.items() if e.endswith('fed') and
                        (e[:4] < election[:4] or (election == '2022vic' and e == '2022fed'))]
            beta = fit_strength(eligible)
            p = samples[election]
            records.extend(score_pairs(election, 'earlier federal fit', k, p, beta))
            records.extend(score_pairs(election, 'joint four-fifths', k, p, STRENGTH, joint=True))
            records.extend(score_pairs(election, 'fixed Other', k, p, STRENGTH, joint=True, fixed_other=True))
            if k == CHOSEN_WINDOW:
                records.extend(score_pairs(election, 'uniform', k, p, 0.0))
                records.extend(score_pairs(election, 'full local difference', k, p, 1.0))
                night_strengths[election] = beta[0]
                if election.endswith('fed'):
                    catchments.extend(catchment_test(election, p, beta[0]))
        if k == CHOSEN_WINDOW:
            latest = fit_strength([p for e, p in samples.items() if e.endswith('fed')])
    return records, catchments, night_strengths, latest


def catchment_test(election, pairs, tpp_strength):
    """Check federal centre pairs where at least one venue appears outside its own division.

    The comparison tests the 2026 method (fitted TPP, fixed Other FP) on a
    proxy for guest-centre behaviour. Keep districts equally weighted within
    each subgroup. A venue near a boundary can have an uncertain classification.
    """
    forward, backward = predict_pairs(pairs, STRENGTH, joint=True, fixed_other=True)
    forward[:, 0], backward[:, 0] = [v[:, 0] for v in predict_pairs(pairs, tpp_strength)]
    errors = 50 * (abs(forward) + abs(backward))
    baseline = 100 * abs(pairs.first - pairs.second)
    result = []
    for outside, label in [(True, 'at least one likely outside'), (False, 'neither likely outside')]:
        mask = pairs.has_outside == outside
        districts = np.unique(pairs.district[mask])
        estimates, comparisons = [], []
        for seat in districts:
            select = mask & (pairs.district == seat)
            estimates.append(np.average(errors[select], weights=pairs.weight[select], axis=0))
            comparisons.append(np.average(baseline[select], weights=pairs.weight[select], axis=0))
        for j, quantity in enumerate(QUANTITIES):
            result.append(dict(election=election, catchment=label, quantity=quantity, districts=len(districts),
                               pairs=sum(mask), uniform_mae_pp=round(np.mean(comparisons, axis=0)[j], 4),
                               location_mae_pp=round(np.mean(estimates, axis=0)[j], 4)))
    return result


def night_tests(elections, first_reports, strengths):
    """Reconstruct the first observed TPP count and estimate the other centres' combined TPP.

    The substantial-centre comparison uses final sizes to isolate vote-share
    error. The pre-count comparison includes every other listed, geocoded
    centre and weights its prediction by earlier sizes only. In both cases
    the result to be predicted is the actual final combined Labor share.
    """
    by_election = defaultdict(list)
    for r in first_reports:
        by_election[r['election']].append(r)
    records = []
    for election, first in by_election.items():
        districts = district_rows(elections[election])
        for seat, arrivals in district_rows(first).items():
            sites = districts[seat]
            by_id = {r['key']: r for r in sites}
            known = {r['key'] for r in arrivals}
            stamp, = {r['timestamp'] for r in arrivals}
            counts_at_time = np.array([[int(r['labor']), int(r['formal'])] for r in arrivals])
            first_share = counts_at_time[:, 0].sum() / counts_at_time[:, 1].sum()
            def local_odds(site):
                n, d = counts(site, f'n{CHOSEN_WINDOW}_')
                return log_odds(n[0], d[0])
            for comparison in ('substantial, final sizes', 'full roster, prior sizes', 'full roster, equal sizes'):
                remaining = [s for s in sites if s['key'] not in known and
                             (comparison != 'substantial, final sizes' or float(s['formal']) >= MIN_VOTES)]
                if not remaining:
                    continue
                final_votes = np.array([int(s['tpp_formal']) for s in remaining])
                if final_votes.sum() == 0:
                    continue
                actual = sum(int(s['tpp_labor']) for s in remaining) / final_votes.sum()
                local = np.array([local_odds(s) for s in remaining])
                predictions = []
                for r, (labor, total) in zip(arrivals, counts_at_time):
                    change = strengths[election] * (local - local_odds(by_id[r['key']]))
                    predictions.append(expit(logit(labor / total) + change))
                predicted = np.average(predictions, axis=0, weights=counts_at_time[:, 1])
                if comparison == 'substantial, final sizes':
                    weights = final_votes
                elif comparison == 'full roster, prior sizes':
                    weights = [float(s['size_weight']) for s in remaining]
                else:
                    weights = np.ones(len(remaining))
                estimate = np.average(predicted, weights=weights)
                records.append(dict(election=election, district=seat, comparison=comparison, first_time=stamp,
                                    first_centres=len(known), other_centres=len(remaining), other_ballots=sum(final_votes),
                                    actual_pct=round(100 * actual, 5), first_pct=round(100 * first_share, 5),
                                    predicted_pct=round(100 * estimate, 5),
                                    uniform_error_pp=round(100 * abs(first_share - actual), 5),
                                    location_error_pp=round(100 * abs(estimate - actual), 5)))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--historical-only', action='store_true', help='omit the current Victorian roster')
    parser.add_argument('--inputs', type=Path, default=HERE, help='directory containing a recorded input revision')
    parser.add_argument('--output', type=Path, help='directory to receive the research tables (defaults to --inputs)')
    args = parser.parse_args()
    destination = args.output or args.inputs
    elections = defaultdict(list)
    for row in read_csv(args.inputs / 'historical-centres.csv'):
        elections[row['election']].append(row)
    results, catchments, strengths, fitted = historical_tests(elections)
    write_csv(destination / 'scores.csv', results)
    write_csv(destination / 'catchments.csv', catchments)
    # The historical scores above use published final results. Rebuilding the
    # election-night comparison also requires the locally recorded first returns.
    if (args.inputs / FIRST_REPORTS).is_file():
        nights = night_tests(elections, read_csv(args.inputs / FIRST_REPORTS), strengths)
        write_csv(destination / 'operator/count-history/election-night.csv', nights)
        columns = ('election', 'district', 'comparison', 'actual_pct', 'first_pct', 'predicted_pct',
                   'uniform_error_pp', 'location_error_pp')
        write_csv(destination / 'replay-comparisons.csv', [{k: r[k] for k in columns} for r in nights])
        print(f'Wrote {len(nights)} district replay comparisons.')
    else:
        print('Election-night comparisons were not regenerated. Use record.py --with-night with the local AEC archives.')
    if not args.historical_only:
        from application import apply_to_roster
        apply_to_roster(read_csv(args.inputs / 'operator' / '2026-inputs.csv'), elections['2022vic'], fitted[0],
                        destination)
    print(f'Wrote {len(results)} historical comparisons.')
    print(f'Five-booth Labor TPP strength from all completed federal elections: {fitted[0]:.4f}')


if __name__ == '__main__':
    main()
