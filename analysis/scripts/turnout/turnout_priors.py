"""Pool turnout expectations and measure the uncertainty needed by a live prior.

Parent context: docs/turnout-data-foundation.md. This is offline research, not
production forecasting. It builds on turnout_changes/turnout_expectations while
replacing jurisdiction-specific drift estimates with an across-the-board model.

Main functions:
* prepare_rows separates known previous local rates from observed later changes.
* training_elections and backtest hold out whole elections, not individual seats.
* fit_local_pattern tests a shared rate-gap correction with equal election weight.
* uncertainty measures common, federal-state and seat-local shocks separately.
* seat_histories describes repeated seat patterns without fitting per-seat models.
* gap_variability checks local uncertainty against prior-known seat-rate gaps.
* render_report/main publish the numerical findings and reproducible evidence.
"""

import argparse
from collections import defaultdict
import math
from pathlib import Path
from lib.paths import REPOSITORY_DIRECTORY
import statistics

from scripts.turnout import turnout_changes as changes
from scripts.turnout import turnout_expectations as expectations


ROOT = REPOSITORY_DIRECTORY
METRICS = expectations.METRICS
MODELS = ('carry_forward', 'turnout_drift', 'half_turnout_drift',
          'both_drift', 'local_pattern')
SCHEMES = ('leave_one_out', 'earlier_only', 'leave_jurisdiction_out')
LABELS = {
    'carry_forward': 'Previous rates',
    'turnout_drift': 'Pooled turnout drift',
    'half_turnout_drift': 'Half pooled turnout drift',
    'both_drift': 'Pooled turnout + formality drift',
    'local_pattern': 'Turnout drift + local rate gaps',
}


def mean(values):
    values = list(values)
    return statistics.mean(values) if values else 0.0


def fold_weights(rows):
    """Give each election equal weight, then each geography within it equal weight."""
    counts = defaultdict(int)
    for row in rows:
        counts[row['current']] += 1
    return [1.0 / (len(counts) * counts[row['current']]) for row in rows]


def weighted_quantile(values, weights, fraction):
    """Empirical inverse CDF; local quantiles retain the same election balance."""
    cumulative = 0.0
    for value, weight in sorted(zip(values, weights)):
        cumulative += weight
        if cumulative >= fraction - 1e-12:
            return value
    return max(values)


def moments(rows, key, sample=False):
    """Describe a weighted population, or use sample SD for election-wide changes.

    Local observations are nested, not independent replicates. Their weighted
    SD describes local spread; it is not a standard error based on seat count.
    """
    weights = fold_weights(rows)
    values = [r[key] for r in rows]
    centre = sum(w * v for w, v in zip(weights, values))
    variance = sum(w * (v - centre) ** 2 for w, v in zip(weights, values))
    if sample:
        variance = variance * len(rows) / (len(rows) - 1) if len(rows) > 1 else None
    return dict(elections=len({r['current'] for r in rows}), observations=len(rows),
                mean_pp=centre, sd_pp=math.sqrt(variance) if variance is not None else None,
                rmse_zero_pp=math.sqrt(sum(w * v * v for w, v in zip(weights, values))),
                p10_pp=weighted_quantile(values, weights, .1),
                median_pp=weighted_quantile(values, weights, .5),
                p90_pp=weighted_quantile(values, weights, .9),
                minimum_pp=min(values), maximum_pp=max(values))


def prepare_rows(elections):
    """Attach prior gaps and later residuals without mixing them in prediction.

    Federal seat gaps/residuals use their state as parent; other seats use the
    election. Previous parent rates are available before the target election.
    Actual current parent changes are used only to describe/train local shocks.
    """
    rows = expectations.prepare_changes(elections)
    dates = {e.code: e.date for e in elections}
    election_lookup = {e.code: e for e in elections}
    parents = {(r['current'], r['level'], r['geography']): r
               for r in rows if r['level'] != 'seat'}
    for row in rows:
        row['current_date'] = dates[row['current']]
        row['subdivision'] = ''
        if row['level'] == 'seat':
            row['subdivision'] = election_lookup[row['current']].seats[row['geography']].subdivision
        parent_key = (('state', row['subdivision']) if row['jurisdiction'] == 'fed'
                      and row['level'] == 'seat' else ('election', row['jurisdiction']))
        # build_tables already requires the actual parent to exist; do not
        # silently replace a missing federal state with the national aggregate.
        parent = parents[(row['current'],) + parent_key]
        for metric in METRICS:
            row[metric + '_previous_gap_pp'] = row['previous_' + metric] - parent['previous_' + metric]
            row[metric + '_local_change_pp'] = row[metric + '_change_pp'] - parent[metric + '_change_pp']
    # This centring keeps the shared gap correction from inadvertently moving
    # the common election expectation merely because matched seats are uneven.
    seats = defaultdict(list)
    for row in rows:
        if row['level'] == 'seat':
            seats[row['current']].append(row)
    for fold in seats.values():
        for metric in METRICS:
            gap_mean = mean(r[metric + '_previous_gap_pp'] for r in fold)
            for row in fold:
                row[metric + '_centred_previous_gap_pp'] = row[metric + '_previous_gap_pp'] - gap_mean
    return rows


def training_elections(rows, target, scheme):
    """Pool all jurisdictions; successor transitions must not leak held outcomes."""
    candidates = [r for r in rows if r['level'] == 'election']
    if scheme == 'leave_one_out':
        return [r for r in candidates if target['current'] not in (r['previous'], r['current'])]
    if scheme == 'earlier_only':
        return [r for r in candidates if r['current_date'] < target['current_date']]
    if scheme == 'leave_jurisdiction_out':
        return [r for r in candidates if r['jurisdiction'] != target['jurisdiction']]
    raise ValueError('Unknown validation scheme: ' + scheme)


def fit_local_pattern(seat_rows, metric):
    """Estimate one pooled within-election slope, not separate seat coefficients.

    Demeaning both variables within each election isolates local variation from
    a common shock. Each election contributes one equally weighted moment.
    Stable-ballot transitions are selected by the caller for formality.
    """
    folds = defaultdict(list)
    for row in seat_rows:
        folds[row['current']].append(row)
    covariance, variance = [], []
    for fold in folds.values():
        x = [r[metric + '_previous_gap_pp'] for r in fold]
        y = [r[metric + '_local_change_pp'] for r in fold]
        mx, my = mean(x), mean(y)
        covariance.append(mean((a - mx) * (b - my) for a, b in zip(x, y)))
        variance.append(mean((a - mx) ** 2 for a in x))
    denominator = mean(variance)
    return mean(covariance) / denominator if denominator else 0.0


def prediction(row, model, training, slopes):
    """Previous local rates + pooled drift + an optional known prior-gap correction."""
    output = {}
    for metric in METRICS:
        selected = training if metric == 'turnout_pct' else [r for r in training if not r['ballot_transition']]
        drift = 0.0
        if model != 'carry_forward' and metric == 'turnout_pct':
            drift = mean(r[metric + '_change_pp'] for r in selected)
            if model == 'half_turnout_drift':
                drift *= .5
        elif model == 'both_drift' and metric == 'formality_pct':
            drift = mean(r[metric + '_change_pp'] for r in selected)
        slope = slopes[metric] if model == 'local_pattern' and row['level'] == 'seat' else 0.0
        local = slope * row.get(metric + '_centred_previous_gap_pp', 0.0)
        value = row['previous_' + metric] + drift + local
        output['predicted_' + metric] = max(0.0, min(100.0, value))
        output[metric + '_drift_pp'] = drift
        output[metric + '_local_correction_pp'] = local
        output[metric + '_slope'] = slope
        uses_drift = ((metric == 'turnout_pct' and model != 'carry_forward')
                      or (metric == 'formality_pct' and model == 'both_drift'))
        output[metric + '_drift_training_elections'] = len(selected) if uses_drift else 0
        output[metric + '_error_pp'] = output['predicted_' + metric] - row['current_' + metric]
        output[metric + '_clipped'] = output['predicted_' + metric] != value
    output['expected_formal_votes'] = (row['current_enrolment'] * output['predicted_turnout_pct']
                                       * output['predicted_formality_pct'] / 10000.0)
    output['formal_votes_error_pct'] = changes.percent(
        output['expected_formal_votes'] - row['current_formal_votes'], row['current_formal_votes'])
    return output


def backtest(rows):
    """Cache parameters per election fold; current votes never enter the estimators."""
    folds = defaultdict(list)
    for row in rows:
        folds[row['current']].append(row)
    output, local_errors = [], []
    all_seats = [r for r in rows if r['level'] == 'seat']
    for code, fold in sorted(folds.items()):
        target = next(r for r in fold if r['level'] == 'election')
        for scheme in SCHEMES:
            training = training_elections(rows, target, scheme)
            targets = {r['current'] for r in training}
            training_seats = [r for r in all_seats if r['current'] in targets]
            slopes = {metric: fit_local_pattern(
                [r for r in training_seats if metric == 'turnout_pct' or not r['ballot_transition']], metric)
                for metric in METRICS}
            for row in fold:
                identity = dict(scheme=scheme, current=code, previous=row['previous'],
                                level=row['level'], geography=row['geography'],
                                ballot_transition=row['ballot_transition'],
                                training_targets=';'.join(r['current'] for r in training))
                for model in MODELS:
                    output.append(dict(identity, model=model, **prediction(row, model, training, slopes)))
                if row['level'] == 'seat':
                    # Diagnostic local errors remove the actual common shift,
                    # isolating the pattern's usefulness from common forecast error.
                    for metric in METRICS:
                        actual = row[metric + '_local_change_pp']
                        predicted = slopes[metric] * row[metric + '_centred_previous_gap_pp']
                        for model, value in (('no_local_correction', 0.0), ('prior_gap', predicted)):
                            local_errors.append(dict(identity, metric=metric, model=model,
                                                     error_pp=value - actual, slope=slopes[metric]))
    return output, local_errors


def scores(predictions, local=False):
    """Summarize pooled held-out errors without over-weighting larger parliaments."""
    grouped = defaultdict(list)
    for row in predictions:
        metrics = ((row['metric'], row['error_pp']),) if local else (
            ('turnout', row['turnout_pct_error_pp']),
            ('formality', row['formality_pct_error_pp']),
            ('formal_votes', row['formal_votes_error_pct']))
        for subset in ('all', 'stable_ballot'):
            if subset == 'stable_ballot' and row['ballot_transition']:
                continue
            for metric, error in metrics:
                if error is not None:
                    grouped[row['scheme'], subset, row['level'], row['model'], metric].append(
                        dict(row, error=error))
    output = []
    for key, selected in sorted(grouped.items()):
        weights = fold_weights(selected)
        # Unlike rate-only shock tables, forecast scores also contain percent
        # count errors. Use unit-neutral column names and explicit units here.
        summary = moments(selected, 'error')
        names = {'mean_pp': 'bias', 'sd_pp': 'sd', 'rmse_zero_pp': 'rmse',
                 'p10_pp': 'p10', 'median_pp': 'median', 'p90_pp': 'p90',
                 'minimum_pp': 'minimum', 'maximum_pp': 'maximum'}
        output.append(dict(zip(('scheme', 'subset', 'level', 'model', 'metric'), key),
                           **{names.get(k, k): v for k, v in summary.items()},
                           units='% of actual formal votes' if key[-1] == 'formal_votes' else 'pp',
                           mae=sum(w * abs(r['error']) for w, r in zip(weights, selected))))
    return output


def uncertainty(rows):
    """Separate correlated common shifts from geographically local innovations."""
    output, covariance = [], []
    election_rows = {r['current']: r for r in rows if r['level'] == 'election'}
    for subset in ('all', 'stable_ballot'):
        selected = [r for r in rows if subset == 'all' or not r['ballot_transition']]
        groups = {
            'election_change': ([r for r in selected if r['level'] == 'election'], '_change_pp'),
            'federal_state_after_national': ([dict(r, **{
                m + '_residual_pp': r[m + '_change_pp'] - election_rows[r['current']][m + '_change_pp']
                for m in METRICS}) for r in selected if r['level'] == 'state'], '_residual_pp'),
            'seat_change': ([r for r in selected if r['level'] == 'seat'], '_change_pp'),
            'seat_after_election': ([r for r in selected if r['level'] == 'seat'], '_change_minus_election_pp'),
            'seat_local': ([r for r in selected if r['level'] == 'seat'], '_local_change_pp'),
            'federal_seat_local': ([r for r in selected if r['level'] == 'seat'
                                    and r['jurisdiction'] == 'fed'], '_local_change_pp'),
            'state_election_seat_local': ([r for r in selected if r['level'] == 'seat'
                                           and r['jurisdiction'] != 'fed'], '_local_change_pp'),
        }
        for scope, (records, suffix) in groups.items():
            if not records:
                continue
            for metric in METRICS:
                output.append(dict(scope=scope, subset=subset, metric=metric,
                                   **moments(records, metric + suffix, sample=scope == 'election_change')))
            weights = fold_weights(records)
            x = [r['turnout_pct' + suffix] for r in records]
            y = [r['formality_pct' + suffix] for r in records]
            mx, my = sum(w * v for w, v in zip(weights, x)), sum(w * v for w, v in zip(weights, y))
            cov = sum(w * (a - mx) * (b - my) for w, a, b in zip(weights, x, y))
            vx = sum(w * (a - mx) ** 2 for w, a in zip(weights, x))
            vy = sum(w * (b - my) ** 2 for w, b in zip(weights, y))
            covariance.append(dict(scope=scope, subset=subset, elections=len({r['current'] for r in records}),
                                   covariance_pp_squared=cov, correlation=cov / math.sqrt(vx * vy) if vx * vy else None))
    return output, covariance


def seat_histories(rows):
    """Describe repeated seat behaviour, without fitting noisy seat-specific priors.

    Stable and reform transitions are kept separate for formality. Persistent
    relative levels are already carried forward by the previous-seat baseline;
    they are not evidence for repeatedly adding another vote-count adjustment.
    """
    grouped = defaultdict(list)
    for row in rows:
        if row['level'] == 'seat':
            grouped[row['jurisdiction'], row['geography']].append(row)
    output = []
    for (jurisdiction, seat), history in sorted(grouped.items()):
        history.sort(key=lambda r: r['current_date'])
        for metric in METRICS:
            selected = [r for r in history if metric == 'turnout_pct' or not r['ballot_transition']]
            if not selected:
                continue
            residuals = [r[metric + '_local_change_pp'] for r in selected]
            latest = history[-1]
            latest_gap = latest[metric + '_previous_gap_pp'] + latest[metric + '_local_change_pp']
            output.append(dict(jurisdiction=jurisdiction, seat=seat, metric=metric,
                               transitions=len(selected), latest=latest['current'],
                               latest_rate_pct=latest['current_' + metric], latest_gap_pp=latest_gap,
                               mean_previous_gap_pp=mean(r[metric + '_previous_gap_pp'] for r in selected),
                               mean_local_change_pp=mean(residuals),
                               local_change_sd_pp=statistics.stdev(residuals) if len(selected) > 1 else None,
                               local_change_rmse_pp=math.sqrt(mean(v * v for v in residuals)),
                               minimum_local_change_pp=min(residuals), maximum_local_change_pp=max(residuals),
                               positive_changes=sum(v > 0 for v in residuals),
                               negative_changes=sum(v < 0 for v in residuals),
                               comparison_targets=';'.join(r['current'] for r in selected)))
    return output


def gap_variability(rows):
    """Inspect heteroscedasticity using prior-known gaps, not current outcomes.

    A shared local SD can understate uncertainty in unusually low-participation
    seats. Fixed bins describe that risk without fitting a variance to each
    seat's small history; bin-level scales still need future validation.
    """
    output = []
    bins = (('below -5', -math.inf, -5), ('-5 to -2', -5, -2),
            ('-2 to 0', -2, 0), ('0 to 2', 0, 2), ('2 or above', 2, math.inf))
    for metric in METRICS:
        candidates = [r for r in rows if r['level'] == 'seat'
                      and (metric == 'turnout_pct' or not r['ballot_transition'])]
        for label, lower, upper in bins:
            selected = [r for r in candidates if lower <= r[metric + '_previous_gap_pp'] < upper]
            if selected:
                output.append(dict(metric=metric, previous_gap_bin_pp=label,
                                   **moments(selected, metric + '_local_change_pp')))
    return output


def render_report(elections, rows, uncertainty_rows, covariance, score_rows,
                  local_scores, histories, gap_rows):
    table = changes.table
    stats = {(r['scope'], r['subset'], r['metric']): r for r in uncertainty_rows}
    election_rows = [r for r in rows if r['level'] == 'election']
    turnout = stats['election_change', 'all', 'turnout_pct']
    formality = stats.get(('election_change', 'stable_ballot', 'formality_pct'),
                         stats['election_change', 'all', 'formality_pct'])
    score_lookup = {(r['scheme'], r['subset'], r['level'], r['model'], r['metric']): r for r in score_rows}
    def rmse(scheme, model, metric, level='election', subset='all'):
        return score_lookup[scheme, subset, level, model, metric]['rmse']
    local_turnout = stats['seat_local', 'all', 'turnout_pct']
    local_formality = stats.get(('seat_local', 'stable_ballot', 'formality_pct'),
                               stats['seat_local', 'all', 'formality_pct'])
    lines = ['# Pooled turnout expectations and prior variability', '',
             'This report assesses whether historical changes in turnout and '
             'formality improve estimates of final formal votes, and describes '
             'the variation those estimates need to account for.', '',
             'It compares assumptions learned across jurisdictions with results '
             'from held-out elections, then separates variation shared by '
             'elections and federal states from local district variation.', '',
             'Reproduce with `cd analysis && python3 -B -m scripts.turnout.turnout_priors`. '
             'All drift estimates are pooled across jurisdictions. '
             'These research comparisons do not change live forecasting assumptions.', '',
             '## Main findings', '',
             '* Across {} election transitions, turnout changed by **{:.2f} pp** on '
             'average, with a **{:.2f} pp between-election sample SD**. The previous-rate '
             'zero-drift RMSE is {:.2f} pp.'.format(turnout['elections'], turnout['mean_pp'],
                                                    turnout['sd_pp'], turnout['rmse_zero_pp']),
             '* On {} {} transitions, formality changed by **{:+.2f} pp**, '
             'with **{:.2f} pp sample SD**. A mean near zero does not imply low '
             'uncertainty: carry-forward still needs a common formality shock.'.format(
                 formality['elections'], 'stable-ballot' if formality['subset'] == 'stable_ballot' else 'available',
                 formality['mean_pp'], formality['sd_pp']),
             '* Pooled turnout drift reduces election turnout RMSE from **{:.2f} to '
             '{:.2f} pp** in leave-one-out, and to **{:.2f} pp** using earlier '
             'elections only. This supports a modest decline assumption, but the '
             'as-of improvement is small. Half drift scores {:.2f} pp earlier-only; '
             'the data do not strongly distinguish full from partially shrunk drift.'.format(
                 rmse('leave_one_out', 'carry_forward', 'turnout'),
                 rmse('leave_one_out', 'turnout_drift', 'turnout'),
                 rmse('earlier_only', 'turnout_drift', 'turnout'),
                 rmse('earlier_only', 'half_turnout_drift', 'turnout')),
             '* Keeping previous formality is the stronger tested baseline: '
             'earlier-only formality RMSE is **{:.2f} pp**, versus **{:.2f} pp** '
             'with pooled formality drift. Do not use the historical mean as '
             'evidence for a formality point correction.'.format(
                 rmse('earlier_only', 'carry_forward', 'formality'),
                 rmse('earlier_only', 'both_drift', 'formality')),
             '* After common shifts, pooled seat-local SD is **{:.2f} pp turnout** '
             'and **{:.2f} pp formality** (stable-ballot formality where available). '
             'These are starting scales, not universal values for every seat.'.format(
                 local_turnout['sd_pp'], local_formality['sd_pp']),
             '* Variability is hierarchical: an election-wide shock, a pooled '
             'federal-state deviation, and a seat-local innovation. Do not apply '
             'the full raw seat-change variation independently to every seat.',
             '* Previous seat turnout/formality already preserve persistent local '
             'levels. The local-pattern test below asks whether those levels predict '
             '**additional change**, rather than adding the same low/high level twice.', '',
             '## Validation and estimator definitions', '',
             'Expected formal votes = known current enrolment x expected turnout x '
             'expected formality / 10,000. Rate errors are percentage points (pp); '
             'formal-count errors are percent of actual formal votes. Error signs '
             'are predicted minus actual.', '',
             '* Every election has equal training and scoring weight. Seats/states '
             'are weighted equally inside their election, not as independent elections.',
             '* Previous rates: no drift. Pooled turnout drift: add the mean '
             'training election turnout change, retaining previous formality. Half '
             'drift applies a predeclared 0.5 shrink factor, not a tuned optimum.',
             '* Pooled turnout + formality drift also adds the mean formality change '
             'from stable-ballot transitions only. Reform transitions are stress '
             'tests, not routine training examples for formality.',
             '* Turnout drift + local rate gaps adds one shared within-election '
             'slope per metric to prior local gaps. Federal gaps are relative to '
             'the state; other seat gaps are relative to the election. Target gaps '
             'are centred over matched seats; no seat/state-specific coefficients '
             'are fitted. Formality slope training excludes ballot transitions.',
             '* Leave-one-out excludes both the target transition and any successor '
             'which subtracts the held election. Earlier-only uses actual dates, '
             'not election-code order. Leave-jurisdiction-out tests transfer from '
             'all other jurisdictions; later data can enter this retrospective test.',
             '* No historical transitions means zero drift/slope. Seats match by '
             'name only, without redistribution correction. Roll size is treated '
             'as known; rate predictions are clipped to [0,100].',
             '* Election/state/seat predictions are evaluated separately and are '
             'not reconciled to consistent vote totals. Centring equal-seat '
             'corrections in this research test does not ensure consistency '
             'with population-weighted totals.',
             '* Election transitions share endpoints and national influences. '
             'The transitions are not fully independent experimental trials; '
             'validation differences are exploratory, not precise significance tests.', '',
             '## Does pooled drift improve prediction?', '',
             'MAE/RMSE are shown together. The seat-pattern estimator is listed only '
             'for seats, since it has no effect on aggregate predictions.', '']
    for scheme in SCHEMES:
        lines += ['### ' + scheme.replace('_', ' ').title(), '']
        for level in ('election', 'state', 'seat'):
            lookup = {(r['model'], r['metric']): r for r in score_rows
                      if r['scheme'] == scheme and r['subset'] == 'all' and r['level'] == level}
            if not lookup:
                continue
            lines += ['**' + ('Federal states pooled' if level == 'state' else level.title()) + '**', '']
            output = []
            for model in MODELS:
                if model == 'local_pattern' and level != 'seat':
                    continue
                r = lookup[model, 'turnout']
                output.append([LABELS[model], r['elections']]
                              + [v for metric in ('turnout', 'formality', 'formal_votes')
                                 for v in (lookup[model, metric]['mae'], lookup[model, metric]['rmse'])])
            lines += table(['Estimator', 'Elections', 'Turnout MAE', 'RMSE', 'Formality MAE', 'RMSE',
                            'Formal-count MAE %', 'RMSE %'], output) + ['']
    lines += ['### Stable-ballot formality check', '',
              'This comparison excludes known ballot-change test elections, as well '
              'as excluding them from formality drift training.', '']
    lines += table(['Validation', 'Estimator', 'Elections', 'Formality RMSE pp', 'Formal-count RMSE %'], [
        [scheme, LABELS[model], lookup['formality']['elections'], lookup['formality']['rmse'],
         lookup['formal_votes']['rmse']]
        for scheme in SCHEMES for model in ('carry_forward', 'turnout_drift', 'both_drift')
        for lookup in [{r['metric']: r for r in score_rows if r['scheme'] == scheme
                       and r['subset'] == 'stable_ballot' and r['level'] == 'election' and r['model'] == model}]
        if lookup]) + ['']
    lines += ['## Variability for priors', '',
              'Election-change SDs use the sample correction. Nested state/seat '
              'SDs are weighted descriptive spread, not errors on a mean. RMS '
              'around zero is the error scale when retaining the previous mean; '
              'SD is spread around the fitted historical mean. Empirical P10/P90 '
              'are not validated future credible intervals. Federal-seat and '
              'state-election-seat rows pool each structural group, without '
              'fitting individual state-election models. Equal geographic '
              'weighting differs from population weighting, so a residual mean '
              'need not be zero and is not a recommended common correction. '
              'These components can be correlated: do not simply sum their '
              'variances and claim a calibrated total.', '']
    lines += table(['Component', 'Subset', 'Metric', 'Elections', 'Observations', 'Mean pp',
                    'SD pp', 'RMS zero pp', 'P10 pp', 'P90 pp'], [
        [r['scope'], r['subset'], r['metric'], r['elections'], r['observations'], r['mean_pp'],
         r['sd_pp'], r['rmse_zero_pp'], r['p10_pp'], r['p90_pp']]
        for r in uncertainty_rows]) + ['']
    lines += ['### Turnout/formality co-movement', '',
              'Covariance is in pp squared. Seat-local uses the observed state '
              'shift for federal seats and the election shift elsewhere. These '
              'descriptive relationships warn against assuming independent turnout '
              'and formality shocks; they do not identify a causal mechanism.', '']
    lines += table(['Component', 'Subset', 'Elections', 'Covariance', 'Correlation'], [
        [r['scope'], r['subset'], r['elections'], r['covariance_pp_squared'], r['correlation']]
        for r in covariance]) + ['']
    lines += ['## Seat patterns', '',
              'A negative shared slope indicates mean reversion: seats above '
              'their previous parent rate tend to fall back relative to the next '
              'common shift. These are within-election weighted slopes, not '
              'regressions dominated by large federal seat counts.', '']
    all_seats = [r for r in rows if r['level'] == 'seat']
    lines += table(['Metric', 'Training elections', 'Pooled slope', 'Correction for prior +1 pp gap'], [
        [metric, len({r['current'] for r in selected}), fit_local_pattern(selected, metric),
         fit_local_pattern(selected, metric)]
        for metric in METRICS for selected in [[r for r in all_seats
                                                if metric == 'turnout_pct' or not r['ballot_transition']]]]) + ['']
    lines += ['### Held-out local-change prediction', '',
              'For this diagnostic only, subtract the actual common shift from '
              'the outcome. This isolates local prediction skill; it is not a '
              'pre-election estimate of that common shift. Local errors remain '
              'equally weighted by election.', '']
    lines += table(['Validation', 'Metric', 'Subset', 'Estimator', 'Elections', 'MAE pp', 'RMSE pp'], [
        [r['scheme'], r['metric'], r['subset'], r['model'], r['elections'], r['mae'], r['rmse']]
        for r in local_scores if r['subset'] == ('all' if r['metric'] == 'turnout_pct' else 'stable_ballot')]) + ['']
    lines += ['The turnout-gap correction does not reliably improve held-out '
              'local errors; retaining the previous local turnout anchor is '
              'preferable to adding this slope. The formality-gap correction '
              'has a small improvement in these tests, but ballot length, '
              'redistributions and regression to the mean remain competing '
              'explanations. These tests do not establish it as a production '
              'correction.', '',
              '### Does local variability depend on the previous seat level?', '',
              'Fixed bins use the previous seat gap from its state/election, '
              'not its current result. Each bin weights contributing elections '
              'equally; bins therefore cannot be added to recover the overall '
              'pooled variance. Formality excludes ballot transitions. Sparse '
              'extreme bins are descriptive evidence, not calibrated priors.', '']
    lines += table(['Metric', 'Prior gap pp', 'Elections', 'Seat changes', 'Local mean pp',
                    'Local SD pp', 'RMS zero pp', 'P10 pp', 'P90 pp'], [
        [r['metric'], r['previous_gap_bin_pp'], r['elections'], r['observations'],
         r['mean_pp'], r['sd_pp'], r['rmse_zero_pp'], r['p10_pp'], r['p90_pp']]
        for r in gap_rows]) + ['']
    for metric in METRICS:
        eligible = [r for r in histories if r['metric'] == metric and r['transitions'] >= 3]
        lines += ['### ' + metric.replace('_pct', '').title() + ': persistent levels and volatile seats', '',
                  'Listings require at least three transitions. The complete seat '
                  'histories are in `seat_histories.csv`. These are flags for '
                  'inspection, not fitted seat adjustments; boundary/roll changes '
                  'can explain apparent local patterns.', '']
        for title, selected in (
            ('Lowest historical relative levels', sorted(eligible, key=lambda r: r['mean_previous_gap_pp'])[:8]),
            ('Largest local-change variation', sorted(eligible, key=lambda r: r['local_change_sd_pp'], reverse=True)[:8]),
            ('Largest repeated average local changes', sorted(eligible, key=lambda r: abs(r['mean_local_change_pp']), reverse=True)[:8]),
        ):
            lines += ['**' + title + '**', '']
            lines += table(['Seat', 'Transitions', 'Mean prior gap pp', 'Latest gap pp',
                            'Mean local change pp', 'Local SD pp', '+/- changes'], [
                [r['seat'] + ' (' + r['jurisdiction'] + ')', r['transitions'], r['mean_previous_gap_pp'],
                 r['latest_gap_pp'], r['mean_local_change_pp'], r['local_change_sd_pp'],
                 '{}/{}'.format(r['positive_changes'], r['negative_changes'])] for r in selected]) + ['']
    lines += ['## Election-level observations and drift sensitivity', '',
              'A decline assumption is per election transition, not per calendar '
              'year. Unequal term lengths and COVID-period shifts remain sources '
              'of heterogeneity. The last column flags known ballot changes, '
              'not a fitted causal offset.', '']
    lines += table(['Transition', 'Years', 'Turnout change pp', 'Formality change pp', 'Ballot change'], [
        [r['previous'] + ' -> ' + r['current'], r['interval_years'], r['turnout_pct_change_pp'],
         r['formality_pct_change_pp'], r['ballot_note']] for r in election_rows]) + ['']
    leave_group_means = [mean(r['turnout_pct_change_pp'] for r in selected)
                        for j in {r['jurisdiction'] for r in election_rows}
                        for selected in [[r for r in election_rows if r['jurisdiction'] != j]] if selected]
    if leave_group_means:
        lines += ['Removing any one jurisdiction leaves pooled turnout drift between '
              '{:.2f} and {:.2f} pp. This is a sensitivity check on the pooled mean, '
              'not a separate model for each state.'.format(min(leave_group_means), max(leave_group_means)), '']
    lines += [
              '## Interpretation', '',
              'Whole-election out-of-sample results measure prediction quality; '
              'the historical mean alone does not establish an improvement. '
              'A zero point correction still has uncertainty. The variability '
              'tables describe common election and federal-state changes as well '
              'as local variation; they are not a calibrated joint forecast.', '',
              'Local gap slopes and outlier histories remain exploratory. '
              'Individual seats have only a few observed transitions. '
              'Unmodelled redistributions, ballot length and roll composition may '
              'explain part of the observed local variation.', '',
              'Operational early/postal counts and changes in vote-category '
              'allocation are outside these rate-prediction comparisons.', '',
              '## Detailed outputs', '',
              '* `uncertainty.csv`: hierarchical shock scales and weighted quantiles.',
              '* `covariance.csv`: turnout/formality co-movement at each level.',
              '* `predictions.csv` and `scores.csv`: pooled whole-election predictions '
              'and equally weighted errors, including training-election identities.',
              '* `local_scores.csv`: held-out prediction of local residual changes.',
              '* `gap_variability.csv`: local shock variation grouped by known '
              'previous seat-rate gaps.',
              '* `seat_histories.csv`: every matched seat history and descriptive '
              'local levels, repeated changes and variation.', '',
              'Data coverage: {} final-result elections, {} election transitions. '
              'Operational pre-election counts are not used in these rate tests.'.format(
                  len(elections), len(election_rows)), '']
    return '\n'.join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-directory', type=Path, default=ROOT / 'analysis/Data/Turnout')
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'docs/turnout-priors')
    args = parser.parse_args(argv)
    print('Loading validated turnout data...', flush=True)
    elections, excluded = changes.load_elections(args.input_directory)
    rows = prepare_rows(elections)
    if not any(r['level'] == 'election' for r in rows):
        parser.error('at least one consecutive election transition is required')
    print('Measuring pooled common and seat-local variability...', flush=True)
    uncertainty_rows, covariance = uncertainty(rows)
    histories = seat_histories(rows)
    gap_rows = gap_variability(rows)
    print('Testing pooled whole-election folds and shared local patterns...', flush=True)
    predictions, local_errors = backtest(rows)
    score_rows, local_scores = scores(predictions), scores(local_errors, local=True)
    report = render_report(elections, rows, uncertainty_rows, covariance, score_rows,
                           local_scores, histories, gap_rows)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    for name, records in dict(uncertainty=uncertainty_rows, covariance=covariance,
                              predictions=predictions, scores=score_rows,
                              local_scores=local_scores, seat_histories=histories,
                              gap_variability=gap_rows).items():
        changes.write_csv(args.output_directory / (name + '.csv'), records)
    (args.output_directory / 'report.md').write_text(report, encoding='utf-8')
    print('Excluded operational-only elections: ' + (', '.join(excluded) or 'none'))
    print('Report written to {}'.format(args.output_directory / 'report.md'))


if __name__ == '__main__':
    main()
