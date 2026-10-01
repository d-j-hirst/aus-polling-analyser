"""Evaluate simple historical turnout/formality expectations before live integration.

Parent context: docs/turnout-data-foundation.md and turnout_changes.py. These
research baselines retain previous geographic rates and estimate common changes
from whole elections. Enrolment is treated as a known pre-election exposure.

Main functions:
* ballot_regime and prepare_changes identify relevant ballot transitions.
* descriptive_statistics separates election, federal-state and seat variation.
* training_changes and predict implement the actual baseline estimators, with
  explicit held-election exclusion rather than seat-wise cross-validation.
* backtest tests complete election folds using leave-one-out and earlier-only data.
* summarize_scores weights election folds equally, irrespective of their seat count.
* render_report/main publish a reviewable report and detailed prediction CSVs.
"""

import argparse
from collections import defaultdict
import math
from pathlib import Path
import statistics

import turnout_changes as changes_report


ROOT = Path(__file__).resolve().parent.parent
METRICS = ('turnout_pct', 'formality_pct')
MODELS = ('carry_forward', 'mean_change', 'ballot_comparable', 'ballot_state')
SCHEMES = ('leave_one_out', 'earlier_only')
MODEL_LABELS = {
    'carry_forward': 'Previous rates',
    'mean_change': 'Average change',
    'ballot_comparable': 'Comparable ballot changes',
    'ballot_state': 'Comparable + federal state',
}
REFORMS = {
    '2017qld': (
        'First general election after Queensland restored full preferential voting in 2016',
        'https://www.ecq.qld.gov.au/about-us/history-of-ecq'),
    '2016fed': (
        'First federal election with the new Senate numbering instructions',
        'https://www.aec.gov.au/About_AEC/research/analysis-informal-voting-2016-election.htm'),
    '2018sa': (
        'First SA election with optional preferential Legislative Council voting',
        'https://www.ecsa.sa.gov.au/html/publications/2018-Election-Report/commissioner.html'),
    '2025wa': (
        'First WA election with the reformed statewide optional-preferential Council ballot',
        'https://www.elections.wa.gov.au/vote/voting-systems-in-wa'),
}


def ballot_regime(code, jurisdiction):
    """Record verified system changes, without assigning a causal effect size.

    Same-jurisdiction comparisons avoid assuming equivalent savings provisions
    across different commissions. Only the covered historical period is used.
    """
    year = int(code[:4])
    lower = 'optional' if jurisdiction == 'nsw' or (jurisdiction == 'qld' and year < 2017) else 'full'
    if jurisdiction == 'fed':
        upper = 'senate_old' if year < 2016 else 'senate_new'
    elif jurisdiction == 'sa':
        upper = 'council_ticket' if year < 2018 else 'council_optional'
    elif jurisdiction == 'wa':
        upper = 'council_ticket' if year < 2025 else 'council_statewide_optional'
    elif jurisdiction == 'qld':
        upper = 'none'
    else:
        upper = 'stable_in_covered_period'
    return lower, upper


def prepare_changes(elections):
    """Reuse the validated aggregation/matching arithmetic from the first report."""
    rows = changes_report.build_tables(elections)['changes']
    for row in rows:
        old = ballot_regime(row['previous'], row['jurisdiction'])
        new = ballot_regime(row['current'], row['jurisdiction'])
        row['previous_lower_regime'], row['previous_upper_regime'] = old
        row['current_lower_regime'], row['current_upper_regime'] = new
        row['ballot_transition'] = old != new
        row['ballot_note'] = REFORMS.get(row['current'], ('', ''))[0] if old != new else ''
    return rows


def mean(values):
    values = list(values)
    return statistics.mean(values) if values else 0.0


def sd(values):
    values = list(values)
    return statistics.stdev(values) if len(values) > 1 else None


def comparable_changes(rows, target):
    """Use stable transitions under the target's lower- and upper-house rules.

    A first-use reform has no independently estimated offset. Falling back to
    unchanged formality is explicit when no stable comparable examples exist.
    """
    regime = ballot_regime(target['current'], target['jurisdiction'])
    return [row for row in rows if not row['ballot_transition']
            and ballot_regime(row['current'], row['jurisdiction']) == regime]


def training_changes(rows, target, scheme):
    candidates = [row for row in rows if row['level'] == 'election'
                  and row['jurisdiction'] == target['jurisdiction']]
    if scheme == 'leave_one_out':
        # The successor transition subtracts the held result, so simply
        # omitting the target transition would still leak its outcome.
        return [row for row in candidates if target['current'] not in (row['previous'], row['current'])]
    if scheme == 'earlier_only':
        return [row for row in candidates if row['current'] < target['current']]
    raise ValueError('Unknown validation scheme: ' + scheme)


def predict(row, model, training, state_rows):
    """Previous local rates + common mean change + optional mean state residual."""
    result = {}
    for metric in METRICS:
        selected = [] if model == 'carry_forward' else training
        if metric == 'formality_pct' and model in {'ballot_comparable', 'ballot_state'}:
            selected = comparable_changes(selected, row)
        drift = mean(r[metric + '_change_pp'] for r in selected)
        state_offset, state_n = 0.0, 0
        if model == 'ballot_state' and row['jurisdiction'] == 'fed' and row['level'] != 'election':
            state = row['geography'] if row['level'] == 'state' else row['subdivision']
            residuals = []
            for election in selected:
                observed = state_rows.get((election['previous'], election['current'], state))
                if observed is not None:
                    residuals.append(observed[metric + '_change_pp'] - election[metric + '_change_pp'])
            state_offset, state_n = mean(residuals), len(residuals)
        value = row['previous_' + metric] + drift + state_offset
        result['predicted_' + metric] = max(0.0, min(100.0, value))
        result[metric + '_drift_pp'] = drift
        result[metric + '_state_offset_pp'] = state_offset
        result[metric + '_training_elections'] = len(selected)
        result[metric + '_state_training_elections'] = state_n
        result[metric + '_clipped'] = value != result['predicted_' + metric]
    result['expected_ballots'] = row['current_enrolment'] * result['predicted_turnout_pct'] / 100.0
    result['expected_formal_votes'] = result['expected_ballots'] * result['predicted_formality_pct'] / 100.0
    for metric in METRICS:
        result[metric + '_error_pp'] = result['predicted_' + metric] - row['current_' + metric]
    result['formal_votes_error_pct'] = changes_report.percent(
        result['expected_formal_votes'] - row['current_formal_votes'], row['current_formal_votes'])
    return result


def backtest(elections, rows):
    """Keep current enrolment known; do not use current votes to estimate parameters."""
    election_lookup = {e.code: e for e in elections}
    state_rows = {(r['previous'], r['current'], r['geography']): r for r in rows if r['level'] == 'state'}
    by_target = defaultdict(list)
    for row in rows:
        by_target[row['current']].append(row)
    output = []
    for code, target_rows in sorted(by_target.items()):
        target = next(row for row in target_rows if row['level'] == 'election')
        for scheme in SCHEMES:
            training = training_changes(rows, target, scheme)
            for row in target_rows:
                row = dict(row)
                row['subdivision'] = (election_lookup[code].seats[row['geography']].subdivision
                                      if row['level'] == 'seat' else '')
                for model in MODELS:
                    predicted = predict(row, model, training, state_rows)
                    output.append(dict(
                        scheme=scheme, model=model, jurisdiction=row['jurisdiction'],
                        previous=row['previous'], current=code, level=row['level'], geography=row['geography'],
                        subdivision=row['subdivision'], ballot_transition=row['ballot_transition'],
                        ballot_note=row['ballot_note'], current_enrolment=row['current_enrolment'],
                        actual_turnout_pct=row['current_turnout_pct'], actual_formality_pct=row['current_formality_pct'],
                        actual_formal_votes=row['current_formal_votes'],
                        training_targets=';'.join(r['current'] for r in training),
                        **predicted))
    return output


def summarize_scores(predictions):
    """Pool errors with equal election-fold weight, then equal geography weight.

    Thousands of seat predictions do not provide thousands of independent
    electoral contexts. Rate and count errors are retained in different units.
    """
    grouped = defaultdict(lambda: defaultdict(list))
    for row in predictions:
        for subset in ('all', 'stable_ballot'):
            if subset == 'stable_ballot' and row['ballot_transition']:
                continue
            for metric, error_key in (
                ('turnout', 'turnout_pct_error_pp'), ('formality', 'formality_pct_error_pp'),
                ('formal_votes', 'formal_votes_error_pct'),
            ):
                if row[error_key] is None:
                    continue
                key = (row['scheme'], subset, row['jurisdiction'], row['level'], row['model'], metric)
                grouped[key][row['current']].append(row[error_key])
    output = []
    for key, folds in sorted(grouped.items()):
        output.append(dict(zip(('scheme', 'subset', 'jurisdiction', 'level', 'model', 'metric'), key),
                           elections=len(folds), predictions=sum(map(len, folds.values())),
                           bias=mean(mean(v) for v in folds.values()),
                           mae=mean(mean(abs(e) for e in v) for v in folds.values()),
                           rmse=math.sqrt(mean(mean(e * e for e in v) for v in folds.values()))))
    return output


def descriptive_statistics(rows):
    """Report separate election shocks and local deviations, in percentage points."""
    election_rows = [r for r in rows if r['level'] == 'election']
    state_rows = [r for r in rows if r['level'] == 'state']
    seat_rows = [r for r in rows if r['level'] == 'seat']
    overall, states, seats, regimes, relationships = [], [], [], [], []
    for jurisdiction in sorted({r['jurisdiction'] for r in election_rows}):
        for metric in METRICS:
            for subset in ('all', 'stable_ballot'):
                selected = [r for r in election_rows if r['jurisdiction'] == jurisdiction
                            and (subset == 'all' or not r['ballot_transition'])]
                values = [r[metric + '_change_pp'] for r in selected]
                if not values:
                    continue
                overall.append(dict(jurisdiction=jurisdiction, metric=metric, subset=subset,
                                    elections=len(values), mean_change_pp=mean(values), sd_change_pp=sd(values),
                                    minimum_change_pp=min(values), maximum_change_pp=max(values),
                                    mean_annual_change_pp=mean(r[metric + '_change_pp'] / r['interval_years'] for r in selected)))
                targets = {r['current'] for r in selected}
                selected_seats = [r for r in seat_rows if r['current'] in targets]
                folds = defaultdict(list)
                for r in selected_seats:
                    folds[r['current']].append(r)
                if folds:
                    local_bias = mean(mean(r[metric + '_change_minus_election_pp'] for r in fold) for fold in folds.values())
                    second_moment = mean(mean(r[metric + '_change_minus_election_pp'] ** 2 for r in fold) for fold in folds.values())
                    state_bias = state_second = None
                    if jurisdiction == 'fed':
                        state_bias = mean(mean(r[metric + '_change_minus_state_pp'] for r in fold) for fold in folds.values())
                        state_second = mean(mean(r[metric + '_change_minus_state_pp'] ** 2 for r in fold) for fold in folds.values())
                    seats.append(dict(jurisdiction=jurisdiction, metric=metric, subset=subset,
                                      elections=len(folds), matched_seat_changes=len(selected_seats),
                                      mean_residual_pp=local_bias,
                                      residual_sd_pp=math.sqrt(max(0.0, second_moment - local_bias ** 2)),
                                      state_adjusted_residual_sd_pp=math.sqrt(max(0.0, state_second - state_bias ** 2))
                                      if state_second is not None else None))
        regime_keys = {ballot_regime(r['current'], jurisdiction) for r in election_rows
                       if r['jurisdiction'] == jurisdiction}
        for lower, upper in sorted(regime_keys):
            selected = [r for r in election_rows if r['jurisdiction'] == jurisdiction
                        and not r['ballot_transition']
                        and ballot_regime(r['current'], jurisdiction) == (lower, upper)]
            values = [r['formality_pct_change_pp'] for r in selected]
            regimes.append(dict(jurisdiction=jurisdiction, lower_regime=lower, upper_regime=upper,
                                elections=len(values), mean_formality_change_pp=mean(values) if values else None,
                                sd_formality_change_pp=sd(values),
                                targets=';'.join(r['current'] for r in selected)))
        for subset in ('all', 'stable_ballot'):
            selected = [r for r in election_rows if r['jurisdiction'] == jurisdiction
                        and (subset == 'all' or not r['ballot_transition'])]
            pairs = [(r['turnout_pct_change_pp'], r['formality_pct_change_pp']) for r in selected]
            if not pairs:
                continue
            x_mean, y_mean = mean(x for x, y in pairs), mean(y for x, y in pairs)
            covariance = (sum((x - x_mean) * (y - y_mean) for x, y in pairs) / (len(pairs) - 1)
                          if len(pairs) > 1 else None)
            relationships.append(dict(jurisdiction=jurisdiction, subset=subset,
                                      elections=len(pairs), turnout_formality_covariance_pp_squared=covariance,
                                      turnout_formality_correlation=changes_report.correlation(pairs)))
    national = {r['current']: r for r in election_rows if r['jurisdiction'] == 'fed'}
    for state in sorted({r['geography'] for r in state_rows}):
        for metric in METRICS:
            for subset in ('all', 'stable_ballot'):
                selected = [r for r in state_rows if r['geography'] == state
                            and (subset == 'all' or not r['ballot_transition'])]
                if not selected:
                    continue
                deltas = [r[metric + '_change_pp'] for r in selected]
                residuals = [r[metric + '_change_pp'] - national[r['current']][metric + '_change_pp'] for r in selected]
                states.append(dict(state=state, metric=metric, subset=subset, elections=len(selected),
                                   mean_change_pp=mean(deltas), sd_change_pp=sd(deltas),
                                   mean_minus_national_pp=mean(residuals), sd_minus_national_pp=sd(residuals)))
    return dict(election_statistics=overall, state_statistics=states, seat_statistics=seats,
                regime_statistics=regimes, relationships=relationships)


def render_report(elections, rows, predictions, scores, descriptive):
    table = changes_report.table
    lines = [
        '# Simple turnout and formality expectations', '',
        'Reproduce with `python3 -B analysis/turnout_expectations.py`. This report '
        'uses {} final-result elections and their consecutive transitions from '
        '`analysis/Data/Turnout`.'.format(len(elections)), '',
        '## Estimators and validation', '',
        'Expected formal votes = current enrolment x expected turnout x expected '
        'formality (rates as fractions). Current enrolment is treated as a known '
        'exposure; the test measures voting participation/formality uncertainty, '
        'not uncertainty in the size of the roll.', '',
        '* **Previous rates:** retain the previous election/seat/state turnout and formality.',
        '* **Average change:** add the mean same-jurisdiction election-wide change '
        'to each previous geographic rate. Each election receives equal training weight.',
        '* **Comparable ballot changes:** same turnout estimator; formality drift '
        'uses stable transitions under the target\'s lower- and upper-house regimes.',
        '* **Comparable + federal state:** additionally add the historical mean '
        'state deviation from the national change to federal state/seat predictions.',
        '* **Leave-one-out:** exclude every training transition that contains the '
        'held election as either endpoint. Other later elections can be used; '
        'this is retrospective validation, not an as-of forecast.',
        '* **Earlier-only:** train only on transitions completed before the target '
        'election. No training data means zero drift and an explicit carry-forward fallback.',
        '* Test scores give each election fold equal weight, and each geography '
        'inside a fold equal weight. Bias = predicted minus actual; turnout/formality '
        'errors are pp, formal-vote errors are percent of actual formal votes.',
        '* Same-name seat matching remains unadjusted for redistributions. New/renamed '
        'seats are omitted from seat validation; aggregate predictions include all seats.',
        '* Predictions at election, state and seat levels are evaluated independently. '
        'They are not yet reconciled into one hierarchy of booth-size estimates.',
        '* There is no fitted reform coefficient: these few one-off changes cannot '
        'identify a transferable causal effect. Comparable training removes reform '
        'transitions from routine drift; predicting the first reform election still '
        'requires an externally justified offset.',
        '* These point expectations do not assume independent seat errors or a '
        'Gaussian uncertainty distribution. Descriptive SDs separate common and local variation.', '',
        '## Average election-wide changes', '',
        'All values are pp. SD is the sample variation between elections; annual '
        'drift is descriptive and is not used by the tested estimators.', '',
    ]
    lines += table(['Jurisdiction', 'Metric', 'Subset', 'N', 'Mean change', 'SD', 'Min', 'Max', 'Mean/year'], [
        [r['jurisdiction'].upper(), r['metric'], r['subset'], r['elections'], r['mean_change_pp'],
         r['sd_change_pp'], r['minimum_change_pp'], r['maximum_change_pp'], r['mean_annual_change_pp']]
        for r in descriptive['election_statistics']]) + ['']
    lines += ['### Stable formality changes by ballot regime', '',
              'Unlike the combined stable-ballot rows above, these keep the old '
              'and new regimes separate. N=0 or N=1 cannot establish within-regime '
              'variation. The first reform election is not a stable transition.', '']
    lines += table(['Jurisdiction', 'Lower house', 'Upper house', 'N', 'Mean formality delta pp',
                    'SD pp', 'Transition endpoints'], [
        [r['jurisdiction'].upper(), r['lower_regime'], r['upper_regime'], r['elections'],
         r['mean_formality_change_pp'], r['sd_formality_change_pp'], r['targets']]
        for r in descriptive['regime_statistics']]) + ['']
    lines += ['## Ballot changes and observed formality', '',
              'A change occurring alongside a reform is not a causal estimate of '
              'that reform. Candidate counts, intentional informal voting and '
              'elector composition can change at the same time.', '']
    lines += table(['Election', 'Previous formal %', 'Current formal %', 'Delta pp', 'Context'], [
        [r['current'], r['previous_formality_pct'], r['current_formality_pct'], r['formality_pct_change_pp'], r['ballot_note']]
        for r in rows if r['level'] == 'election' and r['ballot_transition']]) + ['']
    lines += [
        'The [AEC informal-ballot study](https://www.aec.gov.au/About_AEC/research/analysis-informal-voting-2016-election.htm) '
        'found evidence of House voters applying Senate numbering instructions in '
        '2016, and identified candidate count as a predictor of some numbering '
        'errors. Nevertheless aggregate House formality increased in that election: '
        'the direction of the overall change alone cannot isolate ballot confusion.', '',
        'ECSA\'s [2018 ballot audit](https://www.ecsa.sa.gov.au/html/publications/2018-Election-Report/chapter6.html) '
        'reported that most informal Assembly ballots in its sample appeared '
        'intentional. SA\'s formality decline should not be assigned entirely to '
        'the new Council instructions.', '',
    ]
    for code, (description, url) in REFORMS.items():
        lines.append('* {}: [{}]({}).'.format(code, description, url))
    lines += ['', '## Federal state and territory variation', '',
              'Mean state minus national is the simple state correction tested below. '
              'SD of that residual describes changing state-wide departures, not '
              'uncertainty in the fitted mean. N remains the number of elections.', '']
    lines += table(['State', 'Metric', 'Subset', 'N', 'Mean change', 'SD change',
                    'Mean minus national', 'SD minus national'], [
        [r['state'].upper(), r['metric'], r['subset'], r['elections'], r['mean_change_pp'],
         r['sd_change_pp'], r['mean_minus_national_pp'], r['sd_minus_national_pp']]
        for r in descriptive['state_statistics']]) + ['']
    lines += ['## Seat variation after common election changes', '',
              'Seat residual = seat change minus observed election-wide change; '
              'federal state-adjusted residual instead subtracts the observed state '
              'change. These descriptive calculations use the actual common shift '
              'to measure local variation; forecast evaluation below uses only '
              'training estimates. Moments give equal weight to each election.', '']
    lines += table(['Jurisdiction', 'Metric', 'Subset', 'Elections', 'Seat changes',
                    'Mean residual pp', 'Residual SD pp', 'After federal state SD pp'], [
        [r['jurisdiction'].upper(), r['metric'], r['subset'], r['elections'], r['matched_seat_changes'],
         r['mean_residual_pp'], r['residual_sd_pp'], r['state_adjusted_residual_sd_pp']]
        for r in descriptive['seat_statistics']]) + ['']
    lines += ['## Turnout/formality co-movement', '',
              'Formal-vote uncertainty depends on both rates. Positive covariance '
              'reinforces their effects; negative covariance offsets them. These '
              'small election samples do not establish a stable causal relationship; '
              'N<3 has no reported correlation.', '']
    lines += table(['Jurisdiction', 'Subset', 'N', 'Covariance (pp squared)', 'Correlation'], [
        [r['jurisdiction'].upper(), r['subset'], r['elections'],
         r['turnout_formality_covariance_pp_squared'], r['turnout_formality_correlation']]
        for r in descriptive['relationships']]) + ['']
    lines += ['## Validation results', '',
              'The compact tables show MAE/RMSE in pp for turnout and formality, '
              'and percent for formal-vote counts. Complete bias, coverage and '
              'stable-ballot subset scores are in `scores.csv`.', '']
    for scheme in SCHEMES:
        lines += ['### ' + ('Leave-one-out' if scheme == 'leave_one_out' else 'Earlier elections only'), '']
        for level in ('election', 'state', 'seat'):
            lines += ['#### ' + level.title(), '']
            selected = [r for r in scores if r['scheme'] == scheme and r['subset'] == 'all' and r['level'] == level]
            grouped = defaultdict(dict)
            for r in selected:
                grouped[r['jurisdiction'], r['model']][r['metric']] = r
            output = []
            for (jurisdiction, model), metrics in sorted(grouped.items()):
                output.append([jurisdiction.upper(), MODEL_LABELS[model], metrics['turnout']['elections']]
                              + [value for key in ('turnout', 'formality', 'formal_votes')
                                 for value in (metrics[key]['mae'], metrics[key]['rmse'])])
            lines += table(['Jurisdiction', 'Estimator', 'Elections', 'Turnout MAE', 'RMSE',
                            'Formality MAE', 'RMSE', 'Formal count MAE %', 'RMSE %'], output) + ['']
    lines += ['## Per-election errors', '',
              'Leave-one-out direct aggregate prediction errors expose single '
              'elections dominating an average. Fitted counts use the actual '
              'current roll size, so enrolment-growth error is excluded.', '']
    lines += table(['Election', 'Reform', 'Estimator', 'Turnout train N', 'Formality train N',
                    'Turnout error pp', 'Formality error pp', 'Formal count error %'], [
        [r['current'], 'yes' if r['ballot_transition'] else '', MODEL_LABELS[r['model']],
         r['turnout_pct_training_elections'], r['formality_pct_training_elections'],
         r['turnout_pct_error_pp'], r['formality_pct_error_pp'], r['formal_votes_error_pct']]
        for r in predictions if r['scheme'] == 'leave_one_out' and r['level'] == 'election']) + ['']
    lines += ['## Reading the evidence', ''] + findings(scores)
    lines += ['', 'Training support matters when choosing a baseline. The table '
              'below counts aggregate folds with no formality training examples; '
              'those folds retain the previous formality rate.', '']
    fallback_rows = []
    for scheme in SCHEMES:
        for jurisdiction in sorted({e.jurisdiction for e in elections}):
            for model in ('mean_change', 'ballot_comparable'):
                selected = [r for r in predictions if r['scheme'] == scheme and r['jurisdiction'] == jurisdiction
                            and r['level'] == 'election' and r['model'] == model]
                fallback_rows.append([scheme, jurisdiction.upper(), MODEL_LABELS[model], len(selected),
                                      sum(r['formality_pct_training_elections'] == 0 for r in selected)])
    lines += table(['Validation', 'Jurisdiction', 'Estimator', 'Folds', 'No formality training'], fallback_rows) + ['']
    lines += [
        '## Next modelling decisions', '',
        '* Retain previous seat turnout/formality as the anchor, then test common '
        'drift and uncertainty by jurisdiction. Choose using earlier-only results '
        'as well as leave-one-out, not in-sample descriptive averages.',
        '* Treat known ballot transitions separately. A direct lower-house rule '
        'change is stronger evidence for a formality shift than an upper-house '
        'change; upper-house effects require ballot audits or candidate-count data.',
        '* Candidate counts are a useful missing covariate: they were not collected '
        'in the turnout schema. Add observed ballot length before trying to fit '
        'a seat formality correction based on it.',
        '* Test shrinkage of state corrections if their historical mean does not '
        'improve held-out predictions. Preserve the correlated state shock even '
        'when the preferred point offset is zero.',
        '* Once total formal-vote expectations are chosen, fit early/postal '
        'conversions using the latest pre-election operational observations and '
        'allocate the remaining turnout between categories. Those conversion '
        'coefficients are not estimated by this rate-only experiment.',
        '* Check expectations in the SA replay and a Victorian hindcast before '
        'replacing live declaration/PPVC sizing. No production forecast changes '
        'are made by this research command.', '',
        '## Detailed outputs', '',
        '* `election_statistics.csv`: average changes and between-election variation.',
        '* `state_statistics.csv`: federal state changes and national residuals.',
        '* `seat_statistics.csv`: seat variation after common election/state shifts.',
        '* `regime_statistics.csv`: stable formality changes under each ballot regime.',
        '* `relationships.csv`: election-wide turnout/formality covariance and correlation.',
        '* `predictions.csv`: every fold, estimator, geographic prediction, training '
        'count, training targets and error.',
        '* `scores.csv`: equally weighted fold bias, MAE and RMSE for all and '
        'stable-ballot subsets.', '',
    ]
    return '\n'.join(lines)


def findings(scores):
    lookup = {(r['scheme'], r['subset'], r['jurisdiction'], r['level'], r['model'], r['metric']): r for r in scores}
    output = []
    for jurisdiction in sorted({r['jurisdiction'] for r in scores}):
        base = lookup['earlier_only', 'all', jurisdiction, 'election', 'carry_forward', 'formal_votes']
        candidates = [lookup['earlier_only', 'all', jurisdiction, 'election', model, 'formal_votes'] for model in MODELS]
        best = min(candidates, key=lambda r: r['rmse'])
        output.append('* {} earlier-only aggregate formal-count RMSE: {:.2f}% '
                      'carrying forward; lowest tested is {:.2f}% with {} '
                      '({} elections). This is exploratory model comparison, '
                      'not independent validation of a selected winner.'.format(
                          jurisdiction.upper(), base['rmse'], best['rmse'], MODEL_LABELS[best['model']], base['elections']))
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-directory', type=Path, default=ROOT / 'analysis/Data/Turnout')
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'docs/turnout-expectations')
    args = parser.parse_args(argv)
    print('Loading validated turnout data...', flush=True)
    elections, excluded = changes_report.load_elections(args.input_directory)
    if not elections:
        parser.error('no final-result elections found')
    rows = prepare_changes(elections)
    print('Computing election/state/seat variation...', flush=True)
    descriptive = descriptive_statistics(rows)
    print('Testing complete election folds (leave-one-out and earlier-only)...', flush=True)
    predictions = backtest(elections, rows)
    scores = summarize_scores(predictions)
    report = render_report(elections, rows, predictions, scores, descriptive)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    for name, records in dict(descriptive, predictions=predictions, scores=scores).items():
        changes_report.write_csv(args.output_directory / (name + '.csv'), records)
    (args.output_directory / 'report.md').write_text(report, encoding='utf-8')
    print('Excluded operational-only elections: {}'.format(', '.join(excluded) or 'none'))
    print('Report written to {}'.format(args.output_directory / 'report.md'))


if __name__ == '__main__':
    main()
