"""Assess initial vote-count predictions using elections left out of training.

This non-Stan command reuses the audited early/postal evidence and compatible
historical category counts. For each historical test election, it rebuilds
point estimates and uncertainty from the permitted other elections, generates
possible final counts, and compares their intervals with actual final results.
It checks agreement between the two count interfaces and prepares local
summaries and separate responses for subsequent live-simulator integration.

The reusable count calculation lives in lib.turnout.prior. A separate report
module explains completed results. This historical comparison does not run
the C++ application or change the live forecast.
"""

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from time import perf_counter

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import prior
from lib.turnout import category_policy as policy
from scripts.turnout import turnout_category_dynamics as dynamics
from scripts.turnout import turnout_operational_calibration as calibration
from scripts.turnout import turnout_priors as rates
from scripts.turnout import turnout_prior_report as reporting


OUTPUT = REPOSITORY_DIRECTORY / 'docs/turnout-prior-prototype'
ANCHORS = ('zero_drift', 'half_drift', 'full_drift')
VARIANTS = (('previous_shares', 'compact'), ('controlled', 'reference'), ('controlled', 'compact'))


def weighted_second(vectors, weights=None):
    """Summarize the sizes and relationships of a set of observed errors.

    Average each pair's error product, optionally weighting districts by their
    vote counts. Do not subtract the average error: persistent historical bias
    is part of the uncertainty allowance when the point estimate itself is not
    corrected for that bias. The caller controls whether elections or districts
    receive equal weight.
    """
    values = np.asarray(vectors, dtype=float)
    weights = np.ones(len(values)) / len(values) if weights is None else np.asarray(weights) / sum(weights)
    return (values.T * weights) @ values


def paired_matrix(turnout, formality, paired, weights=None):
    """Combine turnout/formality error scales without losing their relationship.

    Turnout changes remain useful even when a ballot-system change makes the
    accompanying formality change unsuitable for training. Estimate each rate's
    squared-error scale from all its eligible observations, but estimate their
    relationship only from observations eligible for both. This avoids either
    discarding useful turnout evidence or treating a ballot-system change as
    ordinary uncertainty about informal votes.
    """
    vt = float(np.mean(np.square(turnout)))
    vf = float(np.mean(np.square(formality)))
    if paired:
        matrix = weighted_second(paired, weights)
        correlation = matrix[0, 1] / np.sqrt(matrix[0, 0] * matrix[1, 1]) if matrix[0, 0] * matrix[1, 1] else 0
    else:
        correlation = 0
    # Keep an estimated near-perfect relationship from collapsing one source
    # of variation completely, especially when only a few paired elections exist.
    correlation = np.clip(correlation, -.95, .95)
    return [[vt, float(correlation * np.sqrt(vt * vf))], [float(correlation * np.sqrt(vt * vf)), vf]]


def transformed_rate_rows(rows):
    """Attach historical log-odds changes once, before fitting prediction tests.

    Keep the original percentages for reporting and exposure calculations.
    Fitting and drawing on the same transformed scale prevents percentage-point
    errors from pushing high-formality or high-turnout districts onto an endpoint.
    Only observed historical rates are transformed here; which observations may
    train a particular election remains the responsibility of fit_rates.
    """
    changes = {metric: prior.rate_log_odds([r['current_' + metric + '_pct'] for r in rows])
        - prior.rate_log_odds([r['previous_' + metric + '_pct'] for r in rows])
        for metric in ('turnout', 'formality')}
    return [dict(row, turnout_change_log_odds=float(changes['turnout'][i]),
                 formality_change_log_odds=float(changes['formality'][i])) for i, row in enumerate(rows)]


def fit_rates(rows, target, scheme, anchor):
    """Fit starting turnout and rate uncertainty for one historical prediction.

    Training excludes the test election and successor transitions containing
    its results. The chosen anchor adds zero, half or all of the training
    elections' average log-odds turnout change to previous log odds. Estimate separate
    election-wide and district error scales without fitting district parameters.
    Formality starts at its previous rate; ballot-system transitions are excluded
    from its uncertainty training.
    """
    training = rates.training_elections(rows, target, scheme)
    codes = {r['current'] for r in training}
    fraction = {'zero_drift': 0, 'half_drift': .5, 'full_drift': 1}[anchor]
    drift = fraction * rates.mean(r['turnout_change_log_odds'] for r in training)
    turnout, formality, paired = [], [], []
    # Predict each training election from the other allowed training elections.
    # Its own turnout change must not help set the drift used to predict it.
    # These internal prediction errors estimate election-wide uncertainty without
    # using any outcome from the outer test election.
    for row in training:
        inner = [r for r in training if row['current'] not in (r['previous'], r['current'])]
        error_t = row['turnout_change_log_odds'] - fraction * rates.mean(r['turnout_change_log_odds'] for r in inner)
        turnout.append(error_t)
        if not row['ballot_transition']:
            error_f = row['formality_change_log_odds']
            formality.append(error_f)
            paired.append([error_t, error_f])
    # Very little history does not establish a narrow probability distribution.
    # Defaults retain the former local sizes near 90% turnout and 95% formality,
    # translating percentage-point spreads through the log-odds derivative.
    # They taper naturally at other starting rates, rather than setting new caps.
    default_t, default_f_common, default_f_local = 1.5 / 9, .75 / 4.75, 1 / 4.75
    common = paired_matrix(turnout or [default_t], formality or [default_f_common], paired)
    if len(turnout) < 2:
        common[0][0] = max(common[0][0], default_t ** 2)
    if len(formality) < 2:
        common[1][1] = max(common[1][1], default_f_common ** 2)
    # Subtract each training election's overall change from its district changes
    # so this scale measures differences between districts, not the same common
    # error again. Federal comparisons subtract the national change; state-level
    # variation remains in the local scale because there is no separate shared
    # federal-state component in this prototype.
    parents = {r['current']: r for r in training}
    local_by_election = defaultdict(list)
    for row in rows:
        if row['level'] == 'seat' and row['current'] in codes:
            parent = parents[row['current']]
            local_by_election[row['current']].append((row,
                row['turnout_change_log_odds'] - parent['turnout_change_log_odds'],
                row['formality_change_log_odds'] - parent['formality_change_log_odds']))
    local_t, local_f, local_pairs, local_weights = [], [], [], []
    # Weight districts by enrolment within an election, then give elections equal
    # weight in the pooled squared-error scale. A large federal election should
    # not dominate simply because it supplies more district records.
    for records in local_by_election.values():
        roll = sum(r['current_enrolment'] for r, _, _ in records)
        # Remove the first-order aggregate effect of local transformed changes.
        # These weights match the centring used by the sampler: participation
        # weights for turnout and formal-ballot weights for formality.
        wt = [r['current_enrolment'] * r['previous_turnout_pct'] / 100
              * (1 - r['previous_turnout_pct'] / 100) for r, _, _ in records]
        wf = [r['current_enrolment'] * r['previous_turnout_pct'] / 100
              * r['previous_formality_pct'] / 100 * (1 - r['previous_formality_pct'] / 100)
              for r, _, _ in records]
        centre_t = np.average([t for _, t, _ in records], weights=wt)
        centre_f = np.average([f for _, _, f in records], weights=wf)
        records = [(r, t - centre_t, f - centre_f) for r, t, f in records]
        local_t.append(sum(r['current_enrolment'] * t*t for r, t, _ in records) / roll)
        if not records[0][0]['ballot_transition']:
            local_f.append(sum(r['current_enrolment'] * f*f for r, _, f in records) / roll)
            for row, t, f in records:
                local_pairs.append([t, f])
                local_weights.append(row['current_enrolment'] / roll)
    local = paired_matrix([np.sqrt(v) for v in local_t] or [default_t],
                          [np.sqrt(v) for v in local_f] or [default_f_local], local_pairs, local_weights)
    return dict(drift_log_odds=drift, units='natural log odds', common_covariance=common, local_covariance=local,
                training_elections=sorted(codes), stable_formality_elections=len(formality),
                broad_default=len(turnout) < 2 or len(formality) < 2 or not local_f)


def projected_parts(pair, target):
    """Express a historical pair in the category grouping needed for comparison.

    Detailed published categories can be added into a broader known grouping.
    A combined publication cannot be split retrospectively without evidence.
    Return None where the requested grouping cannot be obtained by combining
    observed counts, so incompatible history is left out of composition training.
    A shared set of category names is insufficient where their reporting basis
    changed. Queensland's 2015/2017 detail cannot train its 2020-onward split,
    even though both contain labels for ordinary and absent early votes.
    """
    mode = target['mode']
    if (mode == 'qld_detail'
            and (int(pair['previous'][:4]) >= 2020) != (int(target['previous'][:4]) >= 2020)):
        return None
    if pair['mode'] == mode:
        project = dict
    elif mode == 'fed_combined_early' and pair['mode'] == 'fed_detail':
        def project(values):
            output = {k: v for k, v in values.items() if not k.startswith('early')}
            output['early'] = sum(v for k, v in values.items() if k.startswith('early'))
            return output
    elif mode == 'qld_combined_early' and pair['mode'] == 'qld_detail':
        def project(values):
            output = {k: v for k, v in values.items() if not k.startswith('early')}
            output['early'] = sum(v for k, v in values.items() if k.startswith('early'))
            return output
    elif mode == 'wa_broad' and pair['mode'] == 'wa':
        # WA 2025's unresolved early/absent distinction permits comparison only
        # after those categories and ordinary votes have been combined.
        def project(values):
            return dict(ordinary_early_absent=sum(values[k] for k in ('ordinary', 'early', 'absent')),
                        postal=values['postal'], other=values['other'])
    else:
        return None
    old = {n: project(p) for n, p in pair['old_parts'].items()}
    current = {n: project(p) for n, p in pair['actual'].items()}
    # A definition-change comparison may use ordinary plus other as a broader
    # observed pool. Detailed compatible history can be added to that pool;
    # a broader publication cannot be split to train a later detailed target.
    keys = target.get('keys')
    if keys:
        broaden = any(k.endswith('_other') for k in keys)
        if broaden:
            def broaden_values(values):
                return policy.merge_other(values) if 'other' in values else values
            old = {n: broaden_values(v) for n, v in old.items()}
            current = {n: broaden_values(v) for n, v in current.items()}
        if any(set(v) != set(keys) for v in list(old.values()) + list(current.values())):
            return None
    return old, current


def training_pairs(pairs, target, scheme):
    """Select category transitions that can train this election's prediction.

    Exclude the test election at either end of a transition: its results are
    also present as previous counts in the next election. Earlier-only training
    additionally excludes every transition finishing on or after the test date.
    """
    return [p for p in pairs if p['usable'] and target['current'] not in (p['previous'], p['current'])
            and (scheme != 'earlier_only' or p['date'] < target['date'])]


def fit_composition(pairs, target, scheme, members, total_anchor=None):
    """Estimate uncertainty in dividing a supplied group total between categories.

    Compare final proportions with those predicted after supplying the actual
    training group total. Most groups retain previous internal proportions;
    the federal early split instead carries the declaration rate per all formal
    votes. That prevents growth in ordinary pre-polls from being learned as an
    error in declaration counts. Actual totals here belong only to training
    elections, never to the election being predicted.

    Differences are measured in centred natural logarithms of proportions.
    For two categories their difference is a log-odds error. These relative
    units work at both large and small starting shares and match the sampler's
    transformation. Common errors include bias because no fitted drift is
    added; local errors subtract each training election's common error.
    """
    size = len(members)
    if size == 1:
        return dict(common_covariance=[[0]], local_covariance=[[0]], training_elections=[], support='single category identity')
    common, local, codes, raw_changes = [], [], [], []
    for pair in training_pairs(pairs, target, scheme):
        # Compare category definitions, not just labels. A rate or category
        # introduced by an administrative change cannot train behavioural
        # uncertainty for an unchanged modern category.
        if any(policy.comparison_exclusion(pair['previous'], pair['current'], k)
               or policy.definition_epoch(pair['current'], k) != policy.definition_epoch(target['current'], k)
               for k in members):
            continue
        parts = projected_parts(pair, target)
        if parts is None:
            continue
        old, current = parts
        errors, exposure, changes = [], [], []
        # Compare only districts with observed counts in both elections. Empty
        # groups cannot establish an internal proportion and provide no such
        # evidence. The outcome of the held-out election never enters this loop.
        for name in sorted(old.keys() & current.keys()):
            if any(k not in old[name] or k not in current[name] for k in members):
                continue
            before = np.array([old[name][k] for k in members], dtype=float)
            after = np.array([current[name][k] for k in members], dtype=float)
            if before.sum() and after.sum():
                previous = np.asarray(policy.possible_proportions(before.tolist()))
                final = np.asarray(policy.possible_proportions(after.tolist()))
                predicted = previous.copy()
                if total_anchor is not None:
                    position = members.index(total_anchor)
                    full_weights = policy.possible_proportions(list(old[name].values()))
                    fraction = full_weights[list(old[name]).index(total_anchor)]
                    predicted[position] = fraction * sum(current[name].values()) / after.sum()
                    if not 0 < predicted[position] < 1 and previous[position] > 0:
                        raise ValueError('Historical declaration anchor does not fit its observed early pool.')
                    predicted[1 - position] = 1 - predicted[position]
                # Counts of possible zeros receive a half-vote equivalent.
                # The input floor falls below 0.1% for larger pools, preserving
                # positive rare categories instead of replacing them by dozens
                # of votes. Definition/service exclusions happen before fitting.
                change = np.log(final) - np.log(predicted)
                errors.append(change - change.mean())
                changes.append(final - previous)
                exposure.append(after.sum())
        if not errors:
            continue
        # The vote-weighted average logarithmic prediction error is shared
        # within this training election. Remove it before estimating local error.
        # Later averaging gives each training election equal weight.
        centre = np.average(errors, axis=0, weights=exposure)
        common.append(centre)
        local.append(weighted_second(np.asarray(errors) - centre, exposure))
        codes.append(pair['current'])
        raw_changes.append(np.average(changes, axis=0, weights=exposure).tolist())
    # With fewer than two comparable elections, observed errors alone cannot
    # justify the spread. Add labelled relative uncertainty in the logarithmic
    # coordinates. Its centred directions preserve the group total after the
    # bounded transformation, without fabricating historical category counts.
    shape = np.eye(size) - np.ones((size, size)) / size
    common_cov = weighted_second(common) if common else np.zeros((size, size))
    local_cov = np.mean(local, axis=0) if local else np.zeros((size, size))
    if len(common) >= 2:
        # Apply a modest finite-sample enlargement to the common error scale.
        # The report labels this assumption; it is not a fitted bias correction.
        common_cov *= len(common) / (len(common) - 1)
    else:
        common_cov += shape * np.log(1.5) ** 2
        local_cov += shape * np.log(1.25) ** 2
    return dict(common_covariance=common_cov.tolist(), local_covariance=local_cov.tolist(),
                units='centred natural log proportions', total_anchor=total_anchor,
                training_elections=codes, mean_training_log_errors=[v.tolist() for v in common],
                mean_training_share_changes=raw_changes,
                support='historical moments' if len(codes) >= 2 else 'broad composition default')


def fit_control(summaries, observation, scheme):
    """Fit a conversion multiplier and its uncertainty without the test election.

    Translate a published count, such as postal applications, into expected final
    formal votes in its matched category. Postal applications use separate state
    and federal multipliers where supported; other measures use a shared fit.
    The returned common and local spreads describe uncertainty in that conversion,
    not uncertainty in the whole election's turnout.
    """
    model = 'kind_pooled' if observation['family'] == 'postal_applications_cumulative' else 'pooled'
    allowed = [r for r in summaries if r['election_code'] != observation['election_code']
               and (scheme != 'earlier_only' or r['election_date'] < observation['election_date'])]
    training, fallback = calibration.training_for(allowed, observation, scheme, model)
    fitted = calibration.fit(training)
    if not fitted:
        return None
    errors = []
    # Leave each permitted training election out in turn when predicting its
    # conversion. This keeps the width estimate inside the outer training set;
    # a full-data uncertainty export could include the test election's outcome.
    for row in training:
        inner, _ = calibration.training_for(allowed, row, 'leave_one_out', model)
        estimated = calibration.fit(inner)
        if estimated:
            errors.append(row['conversion'] - estimated['factor'])
    broad = len(training) < 2
    # Use whichever is larger: variation among allowed conversion observations
    # or their internal prediction-error scale. A single election and missing
    # district variation receive explicit assumed spreads instead of zero error.
    common_sd = max(fitted['common_sd'] or 0,
                    float(np.sqrt(np.mean(np.square(errors)))) if errors else 0,
                    .1 * fitted['factor'] if broad else 0)
    local_sd = fitted['local_rms']
    return dict(factor=fitted['factor'], common_sd=common_sd,
                local_sd=local_sd if local_sd is not None else .1 * fitted['factor'],
                training_elections=fitted['training_elections'], fallback=fallback,
                broad_default=broad or local_sd is None)


def aggregate_local_scale(pairs, target, scheme, members):
    """Estimate how uncertain a national category's allocation to districts is.

    Federal early counts describe a national amount. Previous resident category
    counts per elector, scaled to current enrolment, supply its district split.
    Test that split in training elections while supplying the actual national
    amount, so this measures where the votes belong rather than uncertainty in
    how many early votes there are nationally.
    """
    moments, codes = [], []
    for pair in training_pairs(pairs, target, scheme):
        parts = projected_parts(pair, target)
        if parts is None:
            continue
        old, current = parts
        names = sorted(old.keys() & current.keys())
        if any(k not in old[n] or k not in current[n] for n in names for k in members):
            continue
        expected = np.array([sum(old[n][k] for k in members) * pair['seats'][n].enrolment
                             / pair['old_seats'][n].enrolment for n in names])
        actual = np.array([sum(current[n][k] for k in members) for n in names])
        if expected.sum() and actual.sum():
            # Districts partition a national count. Compare their proportions
            # in logarithmic coordinates, removing the common shift that
            # normalization cancels. Half-vote equivalents keep possible zeros
            # finite without confusing them with an unavailable category.
            predicted_shares = np.asarray(policy.possible_proportions(expected))
            actual_shares = np.asarray(policy.possible_proportions(actual))
            errors = np.log(actual_shares) - np.log(predicted_shares)
            errors -= np.average(errors, weights=predicted_shares)
            moments.append(np.average(errors ** 2, weights=predicted_shares))
            codes.append(pair['current'])
    # The count sampler takes a coefficient of variation, then converts it to
    # a logarithmic spread. Supply the equivalent of the fitted log variance.
    return dict(relative_sd=float(np.sqrt(np.expm1(np.mean(moments)))) if moments else .2,
                fitted_units='centered logarithms of resident shares of the national count',
                training_elections=codes, broad_default=not bool(moments))


def group_weights(shares, indices):
    """Normalize previous category shares within the group being allocated.

    A group may contain only some of the district's categories, so their shares
    must sum to one within that group. Input preparation supplies half-vote
    equivalents for possible zeros. An entirely empty or unknown group cannot
    establish an internal division and must be reviewed or regrouped first.
    """
    selected = shares[:, indices]
    sums = selected.sum(axis=1, keepdims=True)
    if not len(indices) or not np.isfinite(selected).all() or (sums <= 0).any():
        raise ValueError('An empty or unknown group cannot supply historical allocation weights.')
    return selected / sums


def build_case(pair, pairs, comparisons, summaries, rate_rows, scheme, anchor, model):
    """Assemble one election setup without giving final outcomes to the sampler.

    Use current enrolment, previous rates and category proportions, and supported
    published early/postal evidence to build prediction inputs. Refit uncertainty
    from the permitted training elections. Return actual final counts separately
    for scoring so they cannot accidentally become numerical prediction inputs.
    """
    # Retain every district for national accounting. A name without a matched
    # previous rate uses the previous parent rate and a larger local spread.
    # SA 2026 instead uses three broad groups because old SA publications do
    # not provide a comparable early/postal split of declaration votes.
    names = sorted(pair['seats'])
    special_sa = pair['current'] == '2026sa' and model == 'controlled'
    keys = ['early', 'postal', 'remaining'] if special_sa else pair['keys']
    target = next(r for r in rate_rows if r['current'] == pair['current'] and r['level'] == 'election')
    lookup = {r['geography']: r for r in rate_rows if r['current'] == pair['current'] and r['level'] == 'seat'}
    previous = [lookup.get(n, target) for n in names]
    # Only recorded, available categories reach this point. Regrouping handles
    # incompatible definitions; a small observed zero then receives half a
    # vote on the model scale instead of being frozen in every possible outcome.
    shares = None if special_sa else np.array([policy.possible_proportions(
        [pair['baseline'][n]['observed_counts'][k] for k in keys]) for n in names])
    input_pair = dict(pair, keys=keys) if special_sa else pair
    converted, support = dynamics.control_estimates(input_pair, comparisons, summaries, scheme) if model == 'controlled' else ({}, [])
    controls, fitted_controls, omissions = [], {}, []
    # The preceding category analysis selects compatible published counts and
    # converts them to final-formal expectations. Attach separately trained
    # common/local errors here. Incomplete district coverage is recorded and
    # omitted, rather than treating unpublished district counts as zero.
    for detail in support:
        group = converted[detail['group']]
        if set(group['values']) != set(names):
            omissions.append(detail['group'] + ': incomplete district control coverage')
            continue
        observed = next(r for r in comparisons if r['election_code'] == pair['current'] and r['family'] == detail['measure'])
        fitted = fit_control(summaries, observed, scheme)
        amount = np.array([group['values'][n] for n in names])
        indices = [keys.index(k) for k in group['members']]
        if detail['aggregate']:
            # National evidence cannot supply independently observed resident
            # district controls. Its local uncertainty comes from the historical
            # resident allocation, not from issuing-centre district attendance.
            distribution = aggregate_local_scale(pairs, pair, scheme, group['members'])
            local_sd = amount * distribution['relative_sd']
            fitted['resident_distribution'] = distribution
            # A national observation has no district conversion-ratio spread.
            # Its actual local count uncertainty comes from the resident fit
            # above, so the unused preliminary district allowance must not be
            # reported as an assumed spread used by the sampler. Sparse common
            # conversion history or an assumed resident split still needs a flag.
            fitted['local_sd'] = None
            fitted['broad_default'] = len(fitted['training_elections']) < 2 or distribution['broad_default']
        else:
            local_sd = amount * fitted['local_sd'] / fitted['factor']
        weights = np.ones((len(names), 1)) if special_sa else group_weights(shares, indices)
        controls.append(dict(name=detail['group'], indices=indices, amount=amount.tolist(), weights=weights.tolist(),
            common_sd=(amount * fitted['common_sd'] / fitted['factor']).tolist(), local_sd=local_sd.tolist(),
            aggregate=detail['aggregate']))
        if pair['mode'] == 'fed_detail' and set(group['members']) == {'early_ordinary', 'early_declaration'}:
            # The national early total reflects rapid ordinary pre-poll growth.
            # It should not mechanically inflate declaration pre-polls in each
            # district. Carry their previous proportion of all formal votes;
            # ordinary pre-polls receive the balance of the combined control.
            # The regime selector combines early categories when the previous
            # election predates the 2010 processing change, so this anchor is
            # not applied to that older history. For an otherwise detailed
            # election with little ordinary early voting, the starting anchor
            # must still fit inside the combined pool; allocation_weights rejects
            # an incompatible starting estimate instead of inventing a split.
            controls[-1]['total_anchor'] = dict(position=group['members'].index('early_declaration'),
                fraction=shares[:, keys.index('early_declaration')].tolist())
        fitted_controls[detail['group']] = dict(fitted, measure=detail['measure'],
            observation_status=detail['observation_status'], cutoff=[detail['first_cutoff'], detail['last_cutoff']])
    if special_sa and {g['name'] for g in controls} != {'early', 'postal'}:
        raise ValueError('SA broad budget requires supported early and postal controls.')
    # Categories outside the controlled groups divide the remaining total in
    # previous within-group proportions. Keep this rule identical to the accepted
    # category analysis; uncertainty does not introduce a new allocation rule.
    used = {i for g in controls for i in g['indices']}
    free = [i for i in range(len(keys)) if i not in used]
    remainder = dict(name='remainder', indices=free,
        weights=(np.ones((len(names), 1)) if special_sa else group_weights(shares, free)).tolist())
    inputs = dict(identity=pair['current'] + '/' + scheme, election_code=pair['current'],
        category_mode='sa_budget' if special_sa else pair['mode'], seat_names=names, categories=keys,
        subdivisions=[pair['seats'][n].subdivision or pair['jurisdiction'] for n in names],
        enrolment=[pair['seats'][n].enrolment for n in names],
        previous_turnout_pct=[r['previous_turnout_pct'] for r in previous],
        previous_formality_pct=[r['previous_formality_pct'] for r in previous],
        local_scale=[1 if n in lookup else 1.5 for n in names], controls=controls, remainder=remainder)
    inputs['category_input_policy'] = dict(possible_zero_equivalent_votes=.5,
        share_input_floor='min(0.1%, half a vote / recorded parent)',
        observed_zero_baselines=[dict(seat=n, category=k, reason='Observed zero in an available recorded group.')
            for n in names for k in keys if not special_sa and pair['baseline'][n]['observed_counts'][k] == 0])
    parameters = dict(schema_version=prior.SCHEMA_VERSION, model_version=prior.MODEL_VERSION,
        rates=fit_rates(rate_rows, target, scheme, anchor), controls=fitted_controls, compositions={})
    # Learn composition conditional on each supplied group total, including the
    # remainder, so count-conversion uncertainty is not added again as share error.
    for group in controls + [remainder]:
        parameters['compositions'][group['name']] = fit_composition(
            pairs, pair, scheme, [keys[i] for i in group['indices']],
            'early_declaration' if 'total_anchor' in group else None)
    # Only this scoring-only object receives the test election's final votes.
    # Missing published category partitions are left unscored. SA's three-group
    # diagnostic uses observed early/postal targets plus their actual remainder,
    # without creating a missing historical declaration split.
    actual_total = np.array([pair['seats'][n].formal_votes for n in names])
    if special_sa:
        observed = {g: {r['seat_name']: r['final_formal'] for r in comparisons if r['election_code'] == '2026sa'
            and r['family'] == measure} for g, measure in [('early', 'prepoll_votes_cast_cumulative'),
                                                        ('postal', 'postal_applications_cumulative')]}
        early = np.array([observed['early'].get(n) for n in names], dtype=float)
        postal = np.array([observed['postal'].get(n) for n in names], dtype=float)
        actual_categories = np.stack([early, postal, actual_total - early - postal], axis=1)
    else:
        actual_categories = np.array([[pair['actual'][n].get(k, 0) for k in keys] if n in pair['actual']
                                      else [np.nan] * len(keys) for n in names])
    return inputs, parameters, dict(totals=actual_total, categories=actual_categories), omissions


def interval_metrics(values, actual, roll, coverage):
    """Measure whether a prediction range contains the result and is useful.

    Form the requested central interval from possible count outcomes. Its score
    charges for width and adds 2/(1 - coverage) times the distance of a result
    outside it. This penalizes both unnecessarily broad ranges and narrow ranges
    that miss. Normalize count errors and scores per 1,000 enrolled electors so
    different-sized elections can be compared.
    """
    alpha = 1 - coverage
    low, high = np.quantile(values, [alpha / 2, 1 - alpha / 2], axis=0)
    width = high - low
    interval_score = width + 2 / alpha * (np.maximum(0, low - actual) + np.maximum(0, actual - high))
    valid = np.isfinite(actual)
    error = values.mean(axis=0) - actual
    exposure = np.broadcast_to(np.asarray(roll), actual.shape)
    # Sum category widths/scores within a district, counting its enrolment only
    # once in that denominator. Coverage instead gives equal weight to its
    # reported categories and weights districts by enrolment. Missing final
    # partitions are excluded from both, rather than scored as zero votes.
    denominator = exposure[valid].sum() / actual.shape[1] if actual.ndim == 2 else exposure[valid].sum()
    return dict(coverage=float(np.average(((actual >= low) & (actual <= high))[valid], weights=exposure[valid])),
        width_per_1000=float(1000 * width[valid].sum() / denominator),
        interval_score_per_1000=float(1000 * interval_score[valid].sum() / denominator),
        absolute_mean_error_per_1000=float(1000 * np.abs(error[valid]).sum() / denominator),
        mean_error_per_1000=float(1000 * error[valid].sum() / denominator), observations=int(valid.sum()))


def score_draw(inputs, result, actual):
    """Check prediction intervals at district, category and geographic-total levels.

    First add the simulated district counts within each outcome, then calculate
    intervals for election and federal-state totals. Adding district interval
    endpoints would lose the shared/local uncertainty relationships and would
    not give a valid interval for those totals.
    """
    roll = np.asarray(inputs['enrolment'])
    scores = []
    views = [('district_total', result['totals'], actual['totals'], roll),
             ('categories', result['counts'], actual['categories'], roll[:, None]),
             ('election_total', result['totals'].sum(axis=1)[:, None], np.array([actual['totals'].sum()]), np.array([roll.sum()]))]
    if inputs['election_code'].endswith('fed'):
        states = sorted(set(inputs['subdivisions']))
        indices = [np.array(inputs['subdivisions']) == state for state in states]
        views.append(('federal_state_total', np.stack([result['totals'][:, i].sum(axis=1) for i in indices], axis=1),
                      np.array([actual['totals'][i].sum() for i in indices]), np.array([roll[i].sum() for i in indices])))
    for level, values, truth, exposure in views:
        for coverage in (.8, .95):
            scores.append(dict(level=level, nominal_coverage=coverage,
                               **interval_metrics(values, truth, exposure, coverage)))
    return scores


def score_summary(tests):
    """Average matching prediction comparisons with equal weight per election.

    Keep model, training method, turnout assumption and measured quantity
    separate. Report both all retained elections and the same subset with usable
    controls, so the baseline and controlled allocation are compared on the same
    evidence. SA 2026 cannot enter paired category averages because its historical
    baseline and controlled diagnostic use different observed groupings.
    """
    groups = defaultdict(list)
    for test in tests:
        for scope in ('all', 'controls_available'):
            if scope == 'controls_available' and not test['has_controls']:
                continue
            for score in test['scores']:
                # Retain SA's individual category results for its own diagnostic,
                # without adding an unmatched comparison to the paired averages.
                if test['election_code'] == '2026sa' and score['level'] == 'categories':
                    continue
                key = (scope, test['scheme'], test['anchor'], test['model'], test['representation'],
                       score['level'], score['nominal_coverage'])
                groups[key].append(score)
    fields = ('scope', 'scheme', 'anchor', 'model', 'representation', 'level', 'nominal_coverage')
    return [dict(zip(fields, key), elections=len(records),
        **{field: float(np.mean([r[field] for r in records])) for field in (
            'coverage', 'width_per_1000', 'interval_score_per_1000', 'absolute_mean_error_per_1000')})
        for key, records in sorted(groups.items())]


def approximation_metrics(reference, compact, inputs):
    """Check that both count interfaces preserve the shared transformed draws.

    Both arrays use the same random changes, so their differences isolate the
    calculation rather than random sampling variation. Average absolute count
    changes and interval-endpoint changes per 1,000 electors; also retain the
    largest individual category change so an average cannot hide an extreme.
    These are not prediction errors against actual final results.
    """
    denominator = np.sum(inputs['enrolment'])
    category_difference = np.abs(reference['counts'] - compact['counts'])
    result = dict(category_difference_per_1000=float(1000 * category_difference.sum(axis=(1, 2)).mean() / denominator),
        total_difference_per_1000=float(1000 * np.abs(reference['totals'] - compact['totals']).sum(axis=1).mean() / denominator),
        maximum_category_difference_votes=float(category_difference.max()))
    for coverage, quantiles in ((80, [.1, .9]), (95, [.025, .975])):
        before = np.quantile(reference['counts'], quantiles, axis=0)
        after = np.quantile(compact['counts'], quantiles, axis=0)
        result['category_endpoint_difference_' + str(coverage) + '_per_1000'] = float(
            1000 * np.abs(before - after).sum(axis=(1, 2)).mean() / denominator)
    return result


def gilmore_declaration_diagnostic(pair, inputs, parameters, values):
    """Check the revised declaration distribution against the case that exposed it.

    Gilmore's observed declaration pre-polls stayed near four thousand in 2022
    and 2025. Record its current distribution and fitted logarithmic scales
    beside that history, so agreement between representations cannot conceal
    an implausible distribution shared by both.
    The 10,000/25,000 thresholds describe this example; they do not cap predictions
    or enter training. Actual counts remain outside the prediction calculation.
    """
    seat = inputs['seat_names'].index('Gilmore')
    category = inputs['categories'].index('early_declaration')
    group = next(g for g in inputs['controls'] if category in g['indices'])
    within_group = group['indices'].index(category)
    composition = parameters['compositions'][group['name']]
    _, central = prior.central_counts(inputs, parameters)
    central_group = central[seat, group['indices']].sum()
    central_share = central[seat, category] / central_group
    direction = np.array([1, -1])
    common_sd = float(np.sqrt(direction @ np.array(composition['common_covariance']) @ direction))
    local_sd = float(np.sqrt(direction @ np.array(composition['local_covariance']) @ direction))
    counts = values['counts'][:, seat, category]
    low, high = np.quantile(counts, [.025, .975])
    return dict(district='Gilmore', category='early_declaration',
        previous_formal_votes=pair['old_parts']['Gilmore']['early_declaration'],
        actual_formal_votes=pair['actual']['Gilmore']['early_declaration'],
        central_count=float(central[seat, category]), mean_count=float(counts.mean()),
        previous_group_share_pct=100 * group['weights'][seat][within_group],
        central_group_share_pct=100 * central_share,
        common_sd_log_odds=common_sd, local_sd_log_odds=local_sd,
        common_initial_count_sd=central[seat, category] * (1 - central_share) * common_sd,
        local_initial_count_sd=central[seat, category] * (1 - central_share) * local_sd,
        training_mean_share_changes_pp=[dict(election_code=code, change_pp=float(100 * change[within_group]))
            for code, change in zip(composition['training_elections'], composition['mean_training_share_changes'])],
        lower_95=float(low), upper_95=float(high), zero_fraction=float((counts == 0).mean()),
        over_10000_fraction=float((counts > 10000).mean()), over_25000_fraction=float((counts > 25000).mean()))


def federal_declaration_diagnostic(inputs, values, actual):
    """Check the corrected early split across whole held-out federal elections.

    Gilmore exposed the scale error, but it must not become the sole measure
    of the correction. Compare declaration counts with final results in every
    eligible federal test: national average count and district interval quality
    answer different questions. These final votes are used only for scoring.
    """
    category = inputs['categories'].index('early_declaration')
    counts = values['counts'][:, :, category]
    truth = actual['categories'][:, category]
    return dict(actual_formal_votes=float(np.nansum(truth)),
                mean_formal_votes=float(counts.sum(axis=1).mean()),
                interval_95=interval_metrics(counts, truth, np.asarray(inputs['enrolment']), .95))


def qld_early_absent_diagnostic(pair, inputs, parameters, values):
    """Track the Queensland case exposing transfer across a reporting change.

    A small paired approximation gap can conceal a very broad distribution
    shared by both representations. Record Murrumba's absent early counts and
    the support for their uncertainty to show the effect of excluding older,
    incompatible category history. Actual 2024 votes remain scoring-only.
    """
    district = inputs['seat_names'].index('Murrumba')
    category = inputs['categories'].index('early_declaration')
    counts = values['counts'][:, district, category]
    _, central = prior.central_counts(inputs, parameters)
    lower, upper = np.quantile(counts, [.025, .975])
    return dict(district='Murrumba', category='early_declaration',
        previous_formal_votes=pair['old_parts']['Murrumba']['early_declaration'],
        actual_formal_votes=pair['actual']['Murrumba']['early_declaration'],
        central_count=float(central[district, category]), mean_count=float(counts.mean()),
        lower_95=float(lower), upper_95=float(upper), over_25000_fraction=float((counts > 25000).mean()),
        composition_training=parameters['compositions']['remainder']['training_elections'],
        support=parameters['compositions']['remainder']['support'])


def main(argv=None):
    """Run a historical comparison and save its report and numerical examples.

    Each retained election is evaluated under two training methods, three
    turnout assumptions and three allocation/representation variants. This is
    an offline sweep of alternative prediction setups, not the workload of one
    live forecast. No Stan fitting or C++ application is invoked.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-directory', type=Path, default=OUTPUT)
    parser.add_argument('--samples', type=int, default=1024)
    parser.add_argument('--preparation-samples', type=int, default=288)
    parser.add_argument('--seed', type=int, default=20261002)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args(argv)
    if args.samples < 128 or args.preparation_samples < 32:
        parser.error('Use at least 128 evaluation and 32 preparation samples.')
    # Fingerprint the retained inputs, relevant source code and numerical
    # configuration. The freshness check compares local revisions only; source
    # adapters are responsible for retrieving and retaining remote corrections.
    datasets, fingerprint = calibration.load_inputs(calibration.audit.INPUT_DIRECTORY)
    for module in (Path(__file__), Path(reporting.__file__), Path(prior.__file__), Path(dynamics.__file__),
                   Path(dynamics.reporting.__file__),
                   Path(rates.__file__), Path(rates.expectations.__file__), Path(policy.__file__)):
        fingerprint['code'][module.name] = hashlib.sha256(module.read_text(encoding='utf-8').encode()).hexdigest()
    config = dict(samples=args.samples, preparation_samples=args.preparation_samples, seed=args.seed,
                  numpy_version=np.__version__, include_nsw=False)
    manifest = args.output_directory / 'analysis.json'
    if args.check:
        saved = json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else {}
        current = saved.get('fingerprint') == fingerprint and saved.get('config') == config
        print('Prior prototype is current.' if current else 'Prior prototype is stale; regenerate it.')
        return 0 if current else 1
    # Reuse the audited count evidence and compatible category definitions. The
    # federal supplement reconstructs resident final early votes; only national
    # operational early counts are added as usable controls for this analysis.
    supplement = calibration.federal_prepoll_analysis(datasets, fingerprint)
    federal = {e['election_code']: {r['seat_name']: r for r in e['districts']}
               for e in json.loads(calibration.federal_source.OUTPUT.read_text(encoding='utf-8'))['elections']}
    pairs = dynamics.prepare_pairs(datasets, federal)
    comparisons, _ = calibration.comparison_rows(datasets, include_unknown_targets=True)
    comparisons += [r for r in supplement['comparisons'] if r['family'] == 'federal_prepoll_national']
    summaries = calibration.election_summaries(comparisons)
    rate_rows = transformed_rate_rows([r for r in rates.prepare_rows(dynamics.election_objects(datasets))
                                      if r['jurisdiction'] != 'nsw'])
    tests, fixtures, skipped = [], [], []
    start = perf_counter()
    # Fit each historical prediction independently under its allowed training
    # set. Include both the baseline and controlled rule on the same elections.
    # Reference/compact controlled outcomes share their named random draws.
    for pair in pairs:
        if pair['jurisdiction'] == 'nsw':
            continue
        # A change in published categories is not necessarily a voting-behaviour
        # change. Omit incompatible transitions rather than learn from that
        # difference. SA 2026 retains only its explicit broad-group diagnostic.
        if not pair['usable'] and pair['current'] != '2026sa':
            skipped.append(dict(election_code=pair['current'], reason='Category definition break'))
            continue
        for scheme in dynamics.SCHEMES:
            control_pair = dict(pair, keys=['early', 'postal', 'remaining']) if pair['current'] == '2026sa' else pair
            available, _ = dynamics.control_estimates(control_pair, comparisons, summaries, scheme)
            for anchor in ANCHORS:
                for model in ('previous_shares', 'controlled'):
                    fitted_start = perf_counter()
                    inputs, parameters, actual, omissions = build_case(
                        pair, pairs, comparisons, summaries, rate_rows, scheme, anchor, model)
                    fitting_seconds = perf_counter() - fitted_start
                    reference = None
                    for representation in ('compact',) if model == 'previous_shares' else ('reference', 'compact'):
                        draw_start = perf_counter()
                        values = prior.draw(inputs, parameters, args.samples, args.seed, representation)
                        draw_seconds = perf_counter() - draw_start
                        scores = score_draw(inputs, values, actual)
                        test = dict(election_code=pair['current'], jurisdiction=pair['jurisdiction'], mode=inputs['category_mode'],
                            scheme=scheme, anchor=anchor, model=model, representation=representation,
                            districts=len(inputs['seat_names']), has_controls=bool(available),
                            used_controls=bool(inputs['controls']),
                            scores=scores, diagnostics=values['diagnostics'], fitting_seconds=fitting_seconds,
                            draw_seconds=draw_seconds, parameters=parameters, omissions=omissions)
                        # This concrete example checks whether the count distribution
                        # itself is believable. Approximation scores alone only check
                        # agreement with the reference, even if both models are wrong.
                        if pair['current'] == '2025fed' and anchor == 'half_drift' and model == 'controlled':
                            test['gilmore_declaration_diagnostic'] = gilmore_declaration_diagnostic(
                                pair, inputs, parameters, values)
                        if (pair['mode'] == 'fed_detail' and anchor == 'half_drift'
                                and model == 'controlled' and any('total_anchor' in g for g in inputs['controls'])):
                            test['federal_declaration_diagnostic'] = federal_declaration_diagnostic(inputs, values, actual)
                        if pair['current'] == '2024qld' and anchor == 'half_drift' and model == 'controlled':
                            test['qld_early_absent_diagnostic'] = qld_early_absent_diagnostic(pair, inputs, parameters, values)
                        if representation == 'reference':
                            reference = values
                        elif reference is not None:
                            # Pair these counts before discarding the ensembles,
                            # keeping approximation error separate from scores
                            # against the actual final election results.
                            test['approximation'] = approximation_metrics(reference, values, inputs)
                        tests.append(test)
                        if (anchor == 'half_drift' and scheme == 'earlier_only' and model == 'controlled'
                                and pair['current'] in {'2022vic', '2025fed', '2026sa', '2025wa'}):
                            existing = next((f for f in fixtures if f['inputs']['election_code'] == pair['current']), None)
                            if existing is None:
                                # Prepare each example only once. Time the local
                                # count summaries and separate count responses
                                # together, excluding fitting, party-share work
                                # and writing the final artifact files.
                                prep_start = perf_counter()
                                prepared = prior.preparation_statistics(inputs, parameters, args.preparation_samples, args.seed)
                                existing = dict(schema_version=prior.SCHEMA_VERSION, model_version=prior.MODEL_VERSION,
                                    seed=args.seed, inputs=inputs, parameters=parameters,
                                    responses=prior.prepare_responses(inputs, parameters), preparation=prepared,
                                    preparation_seconds=perf_counter() - prep_start, scenarios={})
                                fixtures.append(existing)
                            # Save enough repeatable outcomes to reproduce each
                            # representation without exporting full ensembles.
                            # Keep all districts in the inputs so national and
                            # state counts still add from their actual children.
                            existing['scenarios'][representation] = dict(totals=values['totals'][:3].tolist(),
                                                                       counts=values['counts'][:3].tolist())
        print('{}: prior comparisons complete.'.format(pair['current']), flush=True)
    # Preserve detailed scores and training provenance locally, while the
    # separate report formatter explains the consolidated public findings.
    # The dry run performs calculations but leaves all artifact files untouched.
    result = dict(version=prior.SCHEMA_VERSION, model_version=prior.MODEL_VERSION,
        generated_at=datetime.now(timezone.utc).isoformat(), fingerprint=fingerprint, config=config,
        tests=tests, scores=score_summary(tests), skipped=skipped,
        category_definition_changes=policy.DEFINITION_CHANGES, service_exclusions=policy.SERVICE_EXCLUSIONS,
        empty_group_review=policy.empty_groups(datasets), parent_denominator_review=policy.PARENT_REVIEWS,
        reviewed_missing_counts=[r for dataset in datasets.values() for r in policy.missing_count_review(dataset)],
        final_control_zero_review=[r for r in comparisons if r['operational_count'] == 0],
        observed_zero_baselines=[dict(election=p['current'], seat=n, category=k)
            for p in pairs if p['usable'] and p['jurisdiction'] != 'nsw'
            for n, baseline in p['baseline'].items()
            for k, count in baseline['observed_counts'].items() if count == 0],
        federal_early_history=[dict(election_code=code,
            declaration_formal=sum(r['declaration_early_formal'] for r in districts.values()),
            ordinary_early_formal=sum(r['ordinary_early_formal'] for r in districts.values()),
            total_formal=sum(s.formal_votes for s in datasets[code].seat_totals))
            for code, districts in sorted(federal.items())],
        elapsed_seconds=perf_counter() - start,
        fixture_summary=[dict(election_code=f['inputs']['election_code'], districts=len(f['inputs']['seat_names']),
                              preparation_seconds=f['preparation_seconds']) for f in fixtures])
    print('{} prediction setups; {:.2f}s offline evaluation.'.format(len(tests), result['elapsed_seconds']))
    if args.dry_run:
        print('Dry run: no files written.')
        return 0
    args.output_directory.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
    (args.output_directory / 'fixtures-v3.json').write_text(json.dumps(dict(
        schema_version=prior.SCHEMA_VERSION, model_version=prior.MODEL_VERSION,
        fingerprint=fingerprint, config=config, fixtures=fixtures), indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
    (args.output_directory / 'report.md').write_text(reporting.render_report(result), encoding='utf-8')
    print('Report: {}'.format(args.output_directory / 'report.md'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
