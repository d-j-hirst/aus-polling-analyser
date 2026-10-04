"""Balance a fixed pre-election count distribution against a current snapshot.

This research updater estimates additions to counted votes. Reported ordinary
booths are complete; declaration counts are lower bounds. It deliberately
does not learn a downward district-total change from early reporting booths.
Unreported booths retain size-based estimates rather than inheriting an entire
category shortfall. Reliable larger PPVCs allow conservative compensation.
The bounded marginal conditioning below is an approximation, not a fitted
joint posterior or a calibrated model of declaration counting progress.
"""

import numpy as np
from scipy.special import log_ndtr, ndtri_exp

from lib.turnout import prior


MODEL_VERSION = 'live-counts-6'

# These are prediction slopes in log odds, not correlations or fractions of a
# vote-count deficit. Federal 2025 reliable matches give 95% district-bootstrap
# slope intervals ending at -0.1443, -1.1731 and -0.6358 for these size bands,
# excluding Brand's Rockingham reporting consolidation from paired calibration.
# Choose the end closest to no relationship, rounded towards zero. Smaller
# bands include zero, so receive no compensation. This is exploratory same-
# election calibration, not an independently validated Federal/NSW parameter.
PPVC_COMPENSATION = ((2000, 4000, .14), (4000, 8000, 1.17), (8000, np.inf, .63))


def compensation_configuration():
    """Export the fixed exploratory coefficients with their scope and origin."""
    return dict(source='Federal 2025 reliable non-EAV matches, excluding Brand reporting consolidation; conservative 95% slope endpoint.',
                independently_validated=False, smaller_centres_coefficient=0, eav_coefficient=0,
                bands=[dict(expected_minimum=low, expected_maximum_exclusive=high if np.isfinite(high) else None,
                            coefficient=value) for low, high, value in PPVC_COMPENSATION])


def is_eav(unit):
    """Recognise the small EAV service independently of its estimated size.

    Readers may supply an explicit role; the Federal feed also has a stable
    EAV name prefix. Retain the ordinary PPVC category for accounting, while
    excluding this service from public-centre trend and compensation rules.
    """
    return bool(unit.get('is_eav')) or unit['name'].startswith('EAV ')


def eav_starting_counts(expected, units):
    """Replace unmatched EAV fallbacks using historical EAV services only.

    Ordinary-centre fallback weights are inappropriate for these small central
    services. Use the median starting EAV expectation among reliable historical
    matches, separately in each immutable prior outcome, retaining shared prior
    variation. If an election has no such matches, 25 is an explicit service
    assumption. Current or final counts never choose the replacement size.
    """
    result = expected.copy()
    reference = [j for j, u in enumerate(units) if is_eav(u) and u['matched'] and not u.get('closed_reason')]
    replacement = np.median(expected[:, reference], axis=1) if reference else np.full(expected.shape[0], 25.)
    for j, unit in enumerate(units):
        if is_eav(unit) and not unit['matched']:
            result[:, j] = replacement
    return result


def ppvc_compensation_shifts(units, expected, roll, complete, sizing_expected=None):
    """Learn limited counter-movement from reported centres in the same group.

    The historical comparison used all other centres' final counts. During a
    live update only reliably matched completed centres provide evidence, so
    attenuate their deviation by the fraction of the other expected PPVC vote
    they represent. Unmatched centres and EAV keep their starting expectation.
    Recompute from the immutable prior for every snapshot; reporting order is
    not accumulated as repeated evidence.
    """
    centre = expected.mean(axis=0)
    sizing = centre if sizing_expected is None else sizing_expected.mean(axis=0)
    shifts = np.zeros(len(units))
    groups = {}
    for j, unit in enumerate(units):
        if unit['kind'] == 'ppvc':
            groups.setdefault((unit['seat_index'], unit['group_index']), []).append(j)
    evidence = {}
    for key, members in groups.items():
        reported = [k for k in members if complete[k] and units[k]['matched'] and not is_eav(units[k])
                    and units[k]['counted'] > 0 and not units[k].get('closed_reason')
                    and not units[k].get('calibration_exclusion_reason')]
        evidence[key] = (centre[members].sum(), centre[reported].sum(),
                         sum(units[k]['counted'] for k in reported))
    for j, unit in enumerate(units):
        if complete[j] or unit['kind'] != 'ppvc' or not unit['matched'] or is_eav(unit) or unit.get('calibration_exclusion_reason'):
            continue
        strength = next((value for low, high, value in PPVC_COMPENSATION if low <= sizing[j] < high), 0)
        if not strength:
            continue
        combined, reported_expected, observed = evidence[(unit['seat_index'], unit['group_index'])]
        other_expected = combined - centre[j]
        if not reported_expected or other_expected <= 0:
            continue
        parent = roll[unit['seat_index']]
        deviation = prior.rate_log_odds(100 * observed / parent) - prior.rate_log_odds(100 * reported_expected / parent)
        shifts[j] = -strength * reported_expected / other_expected * deviation
    return shifts


def remaining_budgets(reference, ratio, amount):
    """Reserve booth votes smoothly, retaining a positive declaration remainder.

    Use the same relative-share transform as the pre-election count controls.
    Calculate each side of the logistic function separately: subtracting an
    almost-one booth share from one would round a small but positive declaration
    remainder to zero when a snapshot is very close to enrolment.
    Exact zero/one references represent an absent unfinished component.
    """
    booth_share, declaration_share = reference.copy(), 1-reference
    active = (reference > 0) & (reference < 1)
    odds = np.log(reference[active]) - np.log1p(-reference[active]) + np.log(ratio[active])/(1-reference[active])
    booth_share[active] = prior.rate_percent(odds)/100
    declaration_share[active] = prior.rate_percent(-odds)/100
    return amount * booth_share, amount * declaration_share


def reserve_booths(base_booth_amount, booth_amount, declaration_amount, budget):
    """Reconcile a size-based booth request with its available remaining pool."""
    reference = np.divide(base_booth_amount, base_booth_amount + declaration_amount,
                          out=np.zeros_like(budget), where=base_booth_amount + declaration_amount > 0)
    requested_share = np.divide(booth_amount, budget, out=np.zeros_like(budget), where=budget > 0)
    ratio = np.divide(requested_share, reference, out=np.ones_like(budget), where=reference > 0)
    return remaining_budgets(reference, ratio, budget)


def condition_share_above(values, lower):
    """Move a positive share distribution above an observed minimum smoothly.

    Fit a normal approximation to the original samples' log odds. Preserve
    each sample's position in that distribution, but map its probability into
    the part above the counted minimum. This removes impossible low outcomes
    without putting them all at the minimum. Log survival probabilities keep
    this calculation usable even when the observation is in a distant tail.

    Columns represent districts or unfinished units; lower can vary by sample
    when a unit's parent is a drawn district total. The mapping retains sample
    ordering for a fixed bound, but does not establish joint calibration.
    """
    values = np.asarray(values, dtype=float)
    lower = np.broadcast_to(lower, values.shape)
    if ((values <= 0) | (values >= 1)).any() or ((lower < 0) | (lower >= 1)).any():
        raise ValueError('Share priors must be interior and counted bounds below their parent.')
    odds = prior.rate_log_odds(values * 100)
    mean, spread = odds.mean(axis=0), odds.std(axis=0)
    active = lower > 0
    deterministic = spread < 1e-12
    if (active & deterministic & (values <= lower)).any():
        raise ValueError('A deterministic prior conflicts with counted votes; revise its evidence.')
    safe_spread = np.where(deterministic, 1, spread)
    observed_odds = np.log(np.where(active, lower, .5)) - np.log1p(-np.where(active, lower, .5))
    threshold = (observed_odds - mean) / safe_spread
    position = (odds - mean) / safe_spread
    conditional_odds = mean - safe_spread * ndtri_exp(log_ndtr(-threshold) + log_ndtr(-position))
    changed = prior.rate_percent(conditional_odds) / 100
    return np.where(active & ~deterministic, changed, values)


def pooled_booth_changes(units, central_counts, prior_booths=8, *, half_votes=2000000,
                         exponent=.9, formula='power', weighting='booth', legacy=False):
    """Use reliable complete booths to adjust weights of unfinished booths.

    Estimate ordinary and PPVC changes separately from their log count ratios.
    For PPVCs, scale the average using the actual total observed votes.
    Ordinary booths retain the existing eight-booth rule while this experiment
    isolates PPVC changes; their pooled mean and applied treatment are unchanged.
    The power shape is LiveV2's bias-influence formula expressed with a vote
    count at which influence reaches one half. The default two million votes
    is an experimental cautious strength, not a fitted uncertainty estimate.
    Alternative weighting/shapes and the old eight-booth rule are retained
    for controlled replay comparisons, not automatic parameter selection.
    Only matched non-EAV unfinished booths receive this additional trend.
    District totals retain their existing rule; category totals can change.
    """
    result = {}
    if half_votes <= 0 or exponent <= 0 or formula not in ('power', 'exponential') or weighting not in ('booth', 'observed_votes', 'expected_votes'):
        raise ValueError('Use a positive vote scale/exponent and a supported pooled-size formula/weighting.')
    for kind in ('ordinary', 'ppvc'):
        selected = [u for u in units if u['kind'] == kind and u['matched']
                    and u['counted'] > 0 and not u.get('closed_reason') and not is_eav(u)
                    and not u.get('calibration_exclusion_reason')]
        ratios, weights = [], []
        for unit in selected:
            expected = central_counts[unit['seat_index'], unit['group_index']] * unit['weight']
            ratios.append(np.log(unit['counted'] / expected))
            weights.append(1 if weighting == 'booth' else unit['counted'] if weighting == 'observed_votes' else expected)
        votes = sum(u['counted'] for u in selected)
        raw = float(np.average(ratios, weights=weights)) if ratios else 0.
        use_legacy = legacy or kind == 'ordinary'
        if use_legacy:
            strength = len(ratios)/(len(ratios)+prior_booths)
        elif formula == 'power':
            strength = float(prior.rate_percent(exponent*np.log(votes/half_votes))/100) if votes else 0.
        else:
            strength = float(-np.expm1(-np.log(2)*votes/half_votes))
        shift = raw * strength
        result[kind] = dict(multiplier=float(np.exp(shift)), eligible_booths=len(ratios),
                            observed_votes=votes, observation_strength=strength,
                            half_influence_votes=half_votes, exponent=exponent, formula=formula,
                            weighting=weighting, legacy=use_legacy,
                            prior_equivalent_booths=prior_booths,
                            mean_log_ratio=raw if ratios else None)
    return result


def apply_size_changes(expected, units, roll, changes):
    """Shift nominal reliable booth sizes; reported counts remain separate.

    Applying the change to reference sizes of reported booths allows the
    compensation experiment to measure district deviations after accounting
    for a common trend. Actual reported votes are never changed, and completed
    booths still receive no additions in the allocation below.
    """
    result = expected.copy()
    for kind, change in (changes or {}).items():
        # A known closure can carry a structural zero starting weight. It
        # receives no size adaptation and must not enter the interior-share
        # transform. Reporting consolidations likewise retain their own input
        # assumptions instead of receiving this optional learned size change.
        eligible = np.array([u['kind'] == kind and u['matched'] and not is_eav(u)
                             and not u.get('closed_reason') and not u.get('calibration_exclusion_reason')
                             for u in units])
        parent = roll[np.array([u['seat_index'] for u in units])[eligible]]
        result[:, eligible] = parent * prior.rate_percent(
            prior.rate_log_odds(100 * result[:, eligible] / parent) + np.log(change['multiplier'])) / 100
    return result


def update(draws, inputs, units, finalised_seats=(), changes=None, compensation=True,
           compensation_order='before_pool', unreported_ppvc_factor=1.):
    """Rebuild one snapshot from immutable prior draws and cumulative counts.

    Complete booths and known closures have no estimated additions. Partial
    declarations retain a positive remaining estimate after their lower bound
    is applied on the log-odds scale. Estimate unfinished booths from their own
    sizes, allowing limited PPVC compensation. Groups containing only booths
    can revise their expectations from that evidence. Mixed aggregate groups
    retain their supported prior totals: assumed finer weights must not
    override historical early/postal evidence. Reserve booth sizes smoothly
    within the appropriate remaining pool; declarations divide the rest. This leaves
    the district-total rule unchanged, including its implicit assumption that
    declarations absorb changes in ordinary/PPVC counts.

    There is no previous-update input: revisiting a snapshot, skipping an
    intermediate snapshot or receiving a downward revision uses the same path.
    Finalisation must come from an authoritative FP flag, never elapsed time.
    """
    total_prior = np.asarray(draws['totals'])
    group_prior = np.asarray(draws['counts'])
    roll = np.asarray(inputs['enrolment'], dtype=float)
    samples, seats = total_prior.shape
    counted = np.array([u['counted'] for u in units], dtype=float)
    seat_indices = np.array([u['seat_index'] for u in units])
    group_indices = np.array([u['group_index'] for u in units])
    seat_counted = np.bincount(seat_indices, weights=counted, minlength=seats)
    finalised = np.array([name in finalised_seats for name in inputs['seat_names']])
    if (seat_counted >= roll).any():
        # A conflicting exposure is exceptional input recovery, not a reason
        # to erase counted votes or cap them silently. The archive remains intact.
        raise ValueError('Counted formal votes reach enrolment; review the exposure before updating.')
    totals = roll * condition_share_above(total_prior / roll, seat_counted / roll)
    totals[:, finalised] = seat_counted[finalised]
    complete = np.array([u['kind'] != 'declaration' and u['counted'] > 0
                         or bool(u.get('closed_reason')) or finalised[u['seat_index']] for u in units])
    weights = np.array([u['weight'] for u in units])
    expected = group_prior[:, seat_indices, group_indices] * weights
    original_expected = expected.copy()
    starting = eav_starting_counts(expected, units)
    if compensation_order not in ('before_pool', 'after_pool'):
        raise ValueError('Choose compensation before_pool or after_pool.')
    # The before-pool path retains the established compensation evidence. In
    # the after-pool experiment, common changes first revise reference sizes
    # for both reported and unreported centres; compensation then responds only
    # to their remaining district deviation. Size-band eligibility stays frozen.
    expected = apply_size_changes(starting, units, roll, changes) if compensation_order == 'after_pool' else starting.copy()
    shifts = ppvc_compensation_shifts(units, expected, roll, complete, sizing_expected=starting) if compensation else np.zeros(len(units))
    if np.any(shifts):
        selected = shifts != 0
        parent = roll[seat_indices[selected]]
        expected[:, selected] = parent * prior.rate_percent(
            prior.rate_log_odds(100 * expected[:, selected] / parent) + shifts[selected]) / 100
    if compensation_order == 'before_pool':
        expected = apply_size_changes(expected, units, roll, changes)
    # Delayed public-centre returns can carry a lower expected future amount.
    # Apply the optional clock rule only after size/trend/compensation evidence,
    # preserving counted votes, EAV service treatment and immutable references.
    # Use log odds so the share stays inside its enrolment parent. No identity
    # is marked closed, and a later actual return always overrides this estimate.
    if not 0 < unreported_ppvc_factor <= 1:
        raise ValueError('The delayed PPVC factor must be positive and no greater than one.')
    if unreported_ppvc_factor != 1:
        selected = np.array([u['kind'] == 'ppvc' and not complete[j] and not is_eav(u)
                             for j, u in enumerate(units)])
        parent = roll[seat_indices[selected]]
        expected[:, selected] = parent * prior.rate_percent(
            prior.rate_log_odds(100 * expected[:, selected] / parent) + np.log(unreported_ppvc_factor)) / 100
    additions = expected.copy()
    partial = ~complete & (counted > 0)
    if partial.any():
        parent = totals[:, seat_indices[partial]]
        # Use the immutable expected share of the original parent. Introducing
        # a changed parent in this starting share could make a prior unit exceed
        # its parent before the counted lower-bound calculation even begins.
        share = expected[:, partial] / total_prior[:, seat_indices[partial]]
        conditional = condition_share_above(share, counted[partial] / parent)
        additions[:, partial] = parent * conditional - counted[partial]
    additions[:, complete] = 0
    totals_remaining = totals - seat_counted
    remaining = np.zeros_like(additions)
    booth_kind = np.array([u['kind'] != 'declaration' for u in units])
    group_columns = {}
    group_counted = np.zeros(group_prior.shape[1:])
    group_open = np.zeros(group_prior.shape[1:], dtype=bool)
    pure_booth_group = np.zeros(group_prior.shape[1:], dtype=bool)
    for seat in range(seats):
        for group in range(group_prior.shape[2]):
            columns = (seat_indices == seat) & (group_indices == group)
            group_columns[seat, group] = columns
            group_counted[seat, group] = counted[columns].sum()
            group_open[seat, group] = (~complete[columns]).any()
            pure_booth_group[seat, group] = columns.any() and booth_kind[columns].all()
    # SA's early/remaining groups mix booths with declarations because the old
    # election has no defensible finer splits. Keep those aggregate estimates.
    # Federal ordinary and ordinary-prepoll groups have actual booth support,
    # so they instead use the expected sizes of their remaining centres.
    aggregate_additions = np.zeros_like(group_prior)
    for group in range(group_prior.shape[2]):
        active = group_open[:, group] & ~pure_booth_group[:, group]
        if active.any():
            conditional = condition_share_above(group_prior[:, active, group] / total_prior[:, active],
                                                group_counted[active, group] / totals[:, active])
            aggregate_additions[:, active, group] = totals[:, active] * conditional - group_counted[active, group]
    for seat in range(seats):
        direct = (seat_indices == seat) & ~complete & pure_booth_group[seat, group_indices]
        booth_amount = additions[:, direct].sum(axis=1)
        aggregate_amount = aggregate_additions[:, seat].sum(axis=1)
        base_booth_amount = original_expected[:, direct].sum(axis=1)
        if not finalised[seat] and ((base_booth_amount + aggregate_amount) <= 0).any():
            raise ValueError('An unfinished district has no supported remaining units.')
        booth_budget, aggregate_budget = reserve_booths(base_booth_amount, booth_amount, aggregate_amount,
                                                       totals_remaining[:, seat])
        scale = np.divide(booth_budget, booth_amount, out=np.zeros(samples), where=booth_amount > 0)
        remaining[:, direct] = additions[:, direct] * scale[:, None]
        for group in range(group_prior.shape[2]):
            if pure_booth_group[seat, group] or not group_open[seat, group]:
                continue
            budget = aggregate_budget * np.divide(aggregate_additions[:, seat, group], aggregate_amount,
                                                   out=np.zeros(samples), where=aggregate_amount > 0)
            columns = group_columns[seat, group] & ~complete
            booths, declarations = columns & booth_kind, columns & ~booth_kind
            b, d = additions[:, booths].sum(axis=1), additions[:, declarations].sum(axis=1)
            base = original_expected[:, booths].sum(axis=1)
            b_budget, d_budget = reserve_booths(base, b, d, budget)
            for selected, amount, assigned in ((booths, b, b_budget), (declarations, d, d_budget)):
                scale = np.divide(assigned, amount, out=np.zeros(samples), where=amount > 0)
                remaining[:, selected] = additions[:, selected] * scale[:, None]
    final_counts = counted + remaining
    groups = np.zeros_like(group_prior)
    for group in range(group_prior.shape[2]):
        for seat in range(seats):
            groups[:, seat, group] = final_counts[:, (seat_indices == seat) & (group_indices == group)].sum(axis=1)
    diagnostics = dict(
        prior_total_below_count_fraction=float(np.mean(total_prior < seat_counted)),
        maximum_accounting_error=float(np.max(np.abs(groups.sum(axis=2) - totals))),
        minimum_open_addition=float(remaining[:, ~complete].min()) if (~complete).any() else None,
        complete_booths=int(complete.sum()), finalised_districts=int(finalised.sum()),
        ppvc_compensation_units=int(np.count_nonzero(shifts)),
        compensation_order=compensation_order,
        eav_fallback_overrides=int(sum(is_eav(u) and not u['matched'] for u in units)),
        ppvc_compensation_shift_range=[float(shifts.min()), float(shifts.max())],
        closure_count_conflicts=[u['name'] for u in units if u.get('closed_reason') and u['counted'] > 0])
    # The progress-aware updater needs the estimate before aggregate balancing.
    # Preserve the existing count-conditioned calculation instead of repeating
    # every declaration marginal fit during preparation.
    return dict(totals=totals, counts=groups, unit_counts=final_counts, remaining=remaining,own_remaining=additions,
                counted=seat_counted, complete=complete, diagnostics=diagnostics)


def update_with_progress(draws, inputs, units, history, *, count_draws=8, seed=20261002,
                         progress_options=None, postal_deadline=None, **update_options):
    """Prepare counts, release excessive allowances, then model late additions.

    The optional count prototype returns compact category/total outcomes and
    unit means, rather than expanding every booth for every cheap count draw.
    The original broad prediction and revised allocation are both retained for
    inspection. Whole-group observations must be recorded alongside declaration
    history to inform release. The source history must end at this snapshot;
    final results are not inputs.
    """
    from lib.turnout import allocation, late_counts
    prepared = update(draws, inputs, units, **update_options)
    subdivisions = dict(zip(inputs['seat_names'], inputs.get('subdivisions', [None]*len(inputs['seat_names']))))
    evidence = late_counts.progress_evidence(history, units, subdivisions, progress_options,
                                             postal_deadline=postal_deadline)
    grouped = allocation.group_evidence(history,units,inputs,postal_deadline,progress_options)
    released,weights = allocation.release(prepared,inputs,units,prepared['own_remaining'],grouped)
    result = late_counts.update(released, inputs, units, evidence, count_draws, seed, progress_options)
    result['broad_prediction'] = prepared
    result['allocation_prediction'] = released
    result['allocation_progress_weights'] = weights
    return result
