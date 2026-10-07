"""Check whether a booth's implied preference flow disagrees with other booths.

The comparison learns only from the same saved snapshot and the same seat. It
predicts each booth while leaving that booth out of training, so a large PPVC
cannot pull the fitted relationship towards its own erroneous result. Later
counts label subsequent revisions; they never enter the earlier prediction.

An implied flow is (TCP for candidate A minus A's FP) divided by FP for all
other candidates except the two finalists. Exact FP/TCP total agreement is
required: an unfinished preference count must not masquerade as an odd flow.
Ordinary booths and PPVCs are compared; specialist services and declaration
batches are recorded as exclusions rather than treated as equivalent booths.

Two simple, robust regressions are compared. Both use transformed shares. The
first relates preference flow to A's FP share. The second also accounts for the
largest non-finalist party's share of available preferences and PPVC status.
These predictors have different parent groups, deliberately: the response is
a share of transferable preferences, while A's FP share is of all formal votes.
They describe an association, not an identity between those percentages.

The output ranks unusual observations for review. Its scores are descriptive
distances from other booths, not calibrated probabilities or automatic fixes.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.special import expit

from lib.live_analysis_archive import analysis_files

from scripts.diagnostics.live_count_revision_audit import load_frame, revision_record


def transformed_share(count, total):
    """Use a quarter-vote equivalent for possible zeros within a known parent."""
    return float(np.log((count + .25) / (total - count + .25)))


def flow_record(key, booth, pair):
    """Identify an interpretable preference pool before estimating its flow.

    Missing counts, unfinished TCPs and incompatible pairs supply no measured
    preference flow. Impossible candidate gains are reported separately, since
    a regression should never make an impossible count look merely unusual.
    """
    fp, tcp = booth['fp'], booth['tcp']
    if key[2] != 'Ordinary' or booth['booth_type'] not in ('Normal', 'PPVC'):
        return None, 'different_reporting_service'
    if len(pair) != 2 or set(tcp) != set(pair):
        return None, 'missing_or_different_pair'
    total = sum(fp.values())
    if total != sum(tcp.values()):
        return None, 'fp_tcp_totals_differ'
    # Labor is the readable reference in classic seats; in other contests use
    # the non-Liberal finalist where possible. This changes only score direction.
    party_a = 0 if 0 in pair else next((p for p in pair if p != 1), min(pair))
    party_b = next(p for p in pair if p != party_a)
    pool = total - fp.get(party_a, 0) - fp.get(party_b, 0)
    if pool <= 0:
        return None, 'no_other_candidate_preferences'
    gain = tcp[party_a] - fp.get(party_a, 0)
    if gain < 0 or gain > pool:
        return None, 'tcp_contradicts_candidate_fp'
    return dict(seat=key[0], booth=key[1], vote_type=key[2], booth_type=booth['booth_type'],
                party_a=party_a, party_b=party_b, formal_votes=total,
                preference_votes=pool, preference_gain_a=gain,
                fp_a=fp.get(party_a, 0), tcp_a=tcp[party_a],
                primary_share=fp.get(party_a, 0) / total,
                preference_share=gain / pool,
                primary_log_odds=transformed_share(fp.get(party_a, 0), total),
                preference_log_odds=transformed_share(gain, pool)), None


def robust_fit(x, y, weights):
    """Limit an unusual training booth's influence without deleting its result.

    Small preference pools receive less weight, and successive fits smoothly
    downweight large residuals. A weak slope penalty stabilises seats with little
    variation; the intercept is unpenalised. These are exploratory fit settings,
    not a fitted election-night detection policy.
    """
    penalty = np.eye(x.shape[1]) * .1
    penalty[0, 0] = 0
    current_weights = weights.copy()
    for _ in range(12):
        matrix = x.T @ (current_weights[:, None] * x) + penalty
        coefficient = np.linalg.solve(matrix, x.T @ (current_weights * y))
        residual = y - x @ coefficient
        scale = max(.05, 1.4826 * float(np.median(np.abs(residual - np.median(residual)))))
        # Smooth Cauchy weights avoid treating a small score change as a binary
        # decision to include or exclude a booth from its neighbours' comparison.
        current_weights = weights / (1 + (residual / (2 * scale)) ** 2)
    return coefficient, residual, scale, np.linalg.inv(matrix)


def predict_held_out(rows, index, model):
    """Compare one booth with peers, accounting for sparse and distant evidence.

    The score denominator includes observed between-booth scatter, approximate
    small-count noise and uncertainty from fitting/extrapolating the regression.
    It intentionally does not claim that preference ballots are independent
    random draws, or that the resulting score has a standard-normal distribution.
    """
    if len(rows) < 9:
        return None
    x = np.array([[1, r['primary_log_odds']] +
                  ([r['other_party_log_odds'], float(r['booth_type'] == 'PPVC')]
                   if model == 'composition' else []) for r in rows])
    y = np.array([r['preference_log_odds'] for r in rows])
    pools = np.array([r['preference_votes'] for r in rows], dtype=float)
    mask = np.arange(len(rows)) != index
    coefficient, residual, scale, covariance = robust_fit(x[mask], y[mask], pools[mask] / (pools[mask] + 100))
    prediction = float(x[index] @ coefficient)
    probability = float(expit(prediction))
    sampling_variance = 1 / (pools[index] * probability * (1 - probability))
    fitting_variance = scale ** 2 * float(x[index] @ covariance @ x[index])
    denominator = float(np.sqrt(scale ** 2 + sampling_variance + fitting_variance))
    predicted_gain = probability * pools[index]
    return dict(predicted_preference_share=probability,
                preference_error_pp=100 * (rows[index]['preference_share'] - probability),
                candidate_vote_residual=rows[index]['preference_gain_a'] - predicted_gain,
                transformed_residual=float(y[index] - prediction),
                peer_scatter_log_odds=scale, score=float((y[index] - prediction) / denominator),
                training_booths=int(mask.sum()), coefficients=coefficient.tolist())


def analyse_frame(frame, final):
    """Fit contemporaneous comparisons, then attach later changes for evaluation.

    Choose the largest non-finalist party using current FP counts alone. Its
    share describes whether, for example, Greens or One Nation supply most of a
    booth's preferences. Never infer individual party flows from the aggregate.
    """
    by_seat = defaultdict(list)
    excluded = []
    for key, booth in frame['booths'].items():
        pair = frame['seats'][key[0]]['tcp']
        row, reason = flow_record(key, booth, pair)
        if reason:
            excluded.append(dict(seat=key[0], booth=key[1], reason=reason))
        else:
            by_seat[key[0]].append(row)
    output = []
    for name, rows in by_seat.items():
        other_totals = Counter()
        for row in rows:
            fp = frame['booths'][(name, row['booth'], row['vote_type'])]['fp']
            other_totals.update({p: v for p, v in fp.items() if p not in (row['party_a'], row['party_b'])})
        largest_other = max(other_totals, key=other_totals.get)
        for row in rows:
            key = (name, row['booth'], row['vote_type'])
            fp = frame['booths'][key]['fp']
            row['largest_other_party'] = largest_other
            row['other_party_log_odds'] = transformed_share(fp.get(largest_other, 0), row['preference_votes'])
        for index, row in enumerate(rows):
            key = (name, row['booth'], row['vote_type'])
            row['models'] = {model: predict_held_out(rows, index, model) for model in ('primary', 'composition')}
            if key in final['booths']:
                current, later = frame['booths'][key], final['booths'][key]
                revision = revision_record(current['fp'], later['fp'], current['tcp'], later['tcp'], row['party_a'])
                row['later_revision'] = revision
                later_row, reason = flow_record(key, later, current['tcp'])
                row['later_preference_share'] = later_row['preference_share'] if later_row else None
                row['later_primary_share'] = later_row['primary_share'] if later_row else None
            output.append(row)
    return dict(source=frame['source'], rows=output, exclusions=excluded,
                exclusion_counts=dict(Counter(r['reason'] for r in excluded)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--series', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sources', nargs='*', help='Optional source timestamps; final feed remains the evaluation reference.')
    args = parser.parse_args()
    # Filename order selects the latest GUI rerun for each source timestamp.
    paths = {p.name.split('__')[0]: p for p in analysis_files(args.series)}
    final = load_frame(paths[max(paths)])
    frames = [load_frame(p) for k, p in sorted(paths.items()) if not args.sources or any(t in k for t in args.sources)]
    result = dict(definitions=dict(
        preference_share='TCP minus the finalist own FP, divided by FP for all other candidates except the two finalists.',
        primary_share='Finalist FP divided by all formal FP votes. This differs deliberately from the preference-share denominator.',
        eligibility='Ordinary/PPVC records, same two finalists, exact FP/TCP total agreement and possible candidate gains.',
        primary='Robust regression of transformed preference share on transformed finalist FP share, leaving the target booth out.',
        composition='Same regression, adding the current largest non-finalist party share of other FP votes and a PPVC indicator.',
        score='Signed transformed prediction error divided by peer scatter plus small-count and fitting uncertainty; not a calibrated probability.',
        later_reference='Last retained feed labels subsequent changes only; it supplies no earlier regression inputs.'),
        settings=dict(small_pool_weight_scale=100, slope_penalty=.1, minimum_peer_booths=8,
                      smoothing_count=.25, scatter_floor_log_odds=.05),
        sources=[{k: f[k] for k in ('source', 'path', 'sha256')} for f in frames],
        reference={k: final[k] for k in ('source', 'path', 'sha256')},
        code_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        snapshots=[analyse_frame(f, final) for f in frames])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    for snapshot in result['snapshots']:
        print(snapshot['source'], 'eligible booths', len(snapshot['rows']), 'exclusions', snapshot['exclusion_counts'])


if __name__ == '__main__':
    main()
