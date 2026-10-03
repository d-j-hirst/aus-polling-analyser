"""Construct possible final vote counts before live results are counted.

Inputs describe previous turnout and formality, previous category proportions,
and early/postal counts already converted to expected final formal votes.
Parameters describe errors learned from the permitted training elections.
Actual results for the election being predicted are kept outside this module.

Both count interfaces retain inexpensive rate and category transformations.
Their shared count calculation multiplies turnout and formality and makes
the categories add to each district's total. Prepared local summaries and
separate shared responses support the live simulator's reduced preparation
sample. These routines work with category counts, not polling
booths or party vote shares, so their cost is only one part of live forecasting.
"""

import hashlib
import numpy as np


SCHEMA_VERSION = 3
MODEL_VERSION = 'proportional-prior-6'


def rate_log_odds(percent):
    """Express an observed interior percentage as natural logarithmic odds.

    Fit historical rate changes in these coordinates and apply uncertainty in
    the same units. No endpoint clipping is needed for the retained turnout and
    formality data. An exact endpoint needs an explicit substantive treatment,
    rather than silently inventing participation or informal ballots.
    """
    proportion = np.asarray(percent, dtype=float) / 100
    if not np.isfinite(proportion).all() or ((proportion <= 0) | (proportion >= 1)).any():
        raise ValueError('Turnout and formality inputs must be strictly between 0% and 100%.')
    return np.log(proportion) - np.log1p(-proportion)


def rate_percent(log_odds):
    """Return a percentage whose movements taper near either endpoint.

    The stable inverse avoids overflow without capping any rate. Equal changes
    in log odds have smaller percentage effects near 0% or 100%; reaching an
    endpoint requires an infinite change in the mathematical distribution.
    """
    return 100 * np.exp(-np.logaddexp(0, -np.asarray(log_odds, dtype=float)))


def relative_share(reference, ratio):
    """Translate a positive relative amount change into an interior share.

    Match the requested share's response at the central estimate, but taper its
    movements as the available pool fills or empties. Dividing by the starting
    remainder fraction converts a relative count change into log-odds units.
    This keeps early/postal reconciliation and the declaration anchor smooth,
    without replacing an excessive request by an almost-100% allocation.
    Single-category groups and explicitly absent input components stay fixed.
    Input construction must distinguish these from possible observed zeros,
    which receive small count equivalents before entering this calculation.
    """
    reference, ratio = np.broadcast_arrays(reference, ratio)
    result = np.array(reference, dtype=float, copy=True)
    active = (reference > 0) & (reference < 1)
    odds = np.log(reference[active]) - np.log1p(-reference[active])
    result[active] = rate_percent(odds + np.log(ratio[active]) / (1 - reference[active])) / 100
    return result


def starting_rates(inputs, parameters):
    """Apply the pooled turnout change on the same scale used in its fitting."""
    turnout = rate_percent(rate_log_odds(inputs['previous_turnout_pct']) + parameters['rates']['drift_log_odds'])
    formality = np.asarray(inputs['previous_formality_pct'], dtype=float)
    rate_log_odds(formality)  # Require an interior starting rate before producing any counts.
    return turnout, formality


def reconcile_controls(inputs, totals, amounts, central_totals):
    """Allocate uncertain controls and a positive remainder within formal totals.

    Converted counts define the central combined controlled share. Drawn
    controls and formal totals change its log odds, preserving the requested
    count responses to first order. Normalizing the resulting combined amount
    retains the early/postal ratio. A conflicting draw therefore leaves a
    continuous positive remainder rather than a fixed numerical reserve.
    Nominal controls must already describe a possible starting allocation;
    incompatible starting evidence is reported instead of silently capped.
    """
    central_controlled = sum((np.asarray(g['amount']) for g in inputs['controls']), np.zeros_like(central_totals))
    reference = central_controlled / central_totals
    if (reference >= 1).any() or (reference < 0).any():
        raise ValueError('Starting early/postal controls must leave a positive expected remainder.')
    requested = sum(amounts, np.zeros_like(totals))
    ratio = np.divide(requested / totals, reference, out=np.ones_like(totals), where=reference > 0)
    share = relative_share(reference, ratio)
    supplied = totals * share
    scale = np.divide(supplied, requested, out=np.ones_like(totals), where=requested > 0)
    return [amount * scale for amount in amounts], totals * (1 - share)


def covariance_root(matrix):
    """Translate independent random changes into the fitted related changes.

    Multiplying independent unit-normal draws by this small matrix gives them
    the required variances and relationships. Some category changes must sum
    to zero because their group total is fixed, making the covariance singular.
    That is valid here; a factorization requiring strictly positive variances
    in every direction would incorrectly reject those conserved categories.
    """
    covariance = np.asarray(matrix, dtype=float)
    root = np.zeros_like(covariance)
    for i in range(len(root)):
        for j in range(i + 1):
            remainder = covariance[i, j] - root[i, :j] @ root[j, :j]
            if i == j:
                # Squared spreads cannot be negative. Allow tiny negative
                # remainders from rounding when a conserved direction has
                # exactly zero variation, but reject a materially invalid fit.
                if remainder < -1e-9:
                    raise ValueError('Uncertainty covariance is not positive semidefinite.')
                root[i, j] = np.sqrt(max(0, remainder))
            elif root[j, j] > 1e-12:
                root[i, j] = remainder / root[j, j]
    return root


def random_source(seed, identity, source, shape):
    """Give each uncertainty source repeatable draws for this election setup.

    Keying the generator by election and source lets reference and compact
    calculations use identical random changes. Adding another uncertainty
    source, or changing the order of calls, then leaves existing draws intact.
    A difference between count interfaces would then reveal a calculation
    change rather than unrelated random samples.
    """
    digest = hashlib.sha256(f'{seed}/{identity}/{source}'.encode()).digest()
    return np.random.default_rng(int.from_bytes(digest[:8], 'little')).standard_normal(shape)


def local_source(seed, identity, source, names, samples, dimensions=None):
    """Generate repeatable local changes for each named district.

    A district owns its random stream rather than inheriting one from its
    position in an array. Reordering the input districts therefore preserves
    each district's possible outcomes, which matters when comparing fixtures
    or combining district results into geographic totals.
    """
    shape = (samples,) if dimensions is None else (samples, dimensions)
    return np.stack([random_source(seed, identity, source + '/' + name, shape) for name in names], axis=1)


def allocation_weights(group, amount, totals, central_weights=None):
    """Find starting proportions for the amount available in this outcome.

    Most groups retain their previous internal proportions. Federal combined
    pre-polls are different: extra ordinary pre-polls do not imply extra
    declaration pre-polls. Where that split is observed, carry the declaration
    rate per total formal votes and give ordinary pre-polls the balance. The
    supplied group total still determines how many combined pre-polls exist.

    Central expectations retain the previous declaration rate. Uncertain draws
    move that share on the log-odds scale, matching its count response near the
    centre and tapering extreme requests instead of capping the other category
    at a negligible numerical remainder.
    """
    amount = np.asarray(amount, dtype=float)
    weights = np.broadcast_to(np.asarray(group['weights'], dtype=float), amount.shape + (len(group['indices']),)).copy()
    if 'total_anchor' in group:
        anchor = group['total_anchor']
        position = anchor['position']
        desired = np.divide(totals * np.asarray(anchor['fraction']), amount,
                            out=np.zeros_like(amount), where=amount > 0)
        other = 1 - position  # This rule is only used for the observed two-way federal split.
        if central_weights is None:
            active = (weights[..., position] > 0) & (weights[..., other] > 0)
            if ((desired[active] <= 0) | (desired[active] >= 1)).any():
                raise ValueError('The starting declaration anchor must fit inside combined early votes.')
            weights[..., position] = np.where(active, desired, weights[..., position])
        else:
            reference = np.asarray(central_weights)[..., position]
            ratio = np.divide(desired, reference, out=np.ones_like(desired), where=reference > 0)
            weights[..., position] = relative_share(reference, ratio)
        weights[..., other] = 1 - weights[..., position]
    return weights


def transformed_partition(amount, weights, error):
    """Apply relative category changes while preserving a positive group's total.

    Add errors to logarithms of proportions, exponentiate, then normalize back
    to the supplied amount. For two categories this is the usual log-odds
    transformation; for more categories it preserves their joint accounting.
    A given error therefore has a smaller count effect on a small category.
    Positive starting categories stay positive, rather than acquiring a mass
    at zero from clipping negative additive proposals. An explicit zero input
    remains absent; input construction supplies positive count equivalents for
    available categories with observed zeros and omits unknown components.
    """
    active = weights > 0
    log_values = np.where(active, np.log(np.maximum(weights, np.finfo(float).tiny)) + error, -np.inf)
    maximum = log_values.max(axis=-1, keepdims=True)
    # Subtract the maximum for numerical stability. The lower exponent limit
    # only prevents floating-point underflow; it is not a fitted count floor.
    relative = np.where(active, np.exp(np.maximum(log_values - maximum, -700)), 0)
    return amount[..., None] * relative / relative.sum(axis=-1, keepdims=True)


def central_counts(inputs, parameters):
    """Calculate the starting counts before any uncertainty is drawn.

    Enrolment, expected turnout and previous formality determine each district's
    formal-vote total. Converted early/postal estimates receive their counts
    first; other categories divide the remaining votes in their previous
    proportions. A 'control' in these inputs is that converted expected count,
    not the original application or vote-cast measurement.
    """
    roll = np.asarray(inputs['enrolment'])
    turnout, formality = starting_rates(inputs, parameters)
    totals = roll * turnout * formality / 10000
    # The central converted counts must fit the formal total. The same smooth
    # allocation used by uncertain draws leaves these valid starting amounts
    # unchanged, apart from floating-point arithmetic.
    amounts = [np.asarray(g['amount']) for g in inputs['controls']]
    amounts, remaining = reconcile_controls(inputs, totals, amounts, totals)
    counts = np.zeros((len(roll), len(inputs['categories'])))
    for group, amount in zip(inputs['controls'], amounts):
        counts[:, group['indices']] = amount[:, None] * allocation_weights(group, amount, totals)
    counts[:, inputs['remainder']['indices']] = remaining[:, None] * np.asarray(inputs['remainder']['weights'])
    return totals, counts


def prepare_responses(inputs, parameters):
    """Describe how each uncertainty source changes the central category counts.

    Keep turnout, formality, early/postal amounts and changes within category
    groups separate so shared uncertainty can later be applied through distinct
    sensitivities. These are linear responses around the central allocation;
    the sampler retains the nonlinear transformations beyond that centre.

    A response in category votes is not yet a response in party vote shares.
    First-preference and two-candidate share responses also need the live
    snapshot's party projections for each category, which are outside this
    count-only component.
    """
    totals, counts = central_counts(inputs, parameters)
    roll = np.asarray(inputs['enrolment'])
    turnout, formality = starting_rates(inputs, parameters)
    free = inputs['remainder']['indices']
    free_weights = np.asarray(inputs['remainder']['weights'])
    responses = {}
    # Changing total turnout or formality leaves combined controlled amounts
    # fixed. The extra or missing votes go to the remaining categories. Within
    # federal early votes, the declaration-rate anchor also transfers votes
    # between declaration and ordinary pre-polls without changing their sum.
    for name, slope in [('turnout_log_odds', roll * formality / 100 * (turnout / 100) * (1 - turnout / 100)),
                        ('formality_log_odds', roll * turnout / 100 * (formality / 100) * (1 - formality / 100))]:
        value = np.zeros_like(counts)
        value[:, free] = slope[:, None] * free_weights
        # The federal declaration expectation follows the formal total, while
        # its ordinary pre-poll counterpart receives the compensating change.
        for group in inputs['controls']:
            if 'total_anchor' in group:
                anchor = group['total_anchor']
                delta = slope * np.asarray(anchor['fraction'])
                value[:, group['indices'][anchor['position']]] += delta
                value[:, group['indices'][1 - anchor['position']]] -= delta
        responses[name] = dict(units='category votes per natural-log-odds rate change', values=value.tolist())
    # Increasing an early/postal count transfers votes from the remainder;
    # it does not increase the district's overall total. Include both sides
    # of this transfer in each separate response.
    for group in inputs['controls']:
        response = np.zeros_like(counts)
        response[:, group['indices']] = np.asarray(group['weights'])
        if 'total_anchor' in group:
            response[:, group['indices']] = 0
            response[:, group['indices'][1 - group['total_anchor']['position']]] = 1
        response[:, free] -= free_weights
        responses[group['name'] + '_count'] = dict(units='category votes per extra controlled vote',
                                                  values=response.tolist())
    # Changes in proportions within a group move votes between its categories.
    # Express the fitted common and local changes as count responses at the
    # central group size, while preserving the fixed group total. Applying the
    # logarithmic derivative below converts their units into count responses.
    for group in inputs['controls'] + [inputs['remainder']]:
        indices = group['indices']
        amount = counts[:, indices].sum(axis=1)
        fitted = parameters['compositions'][group['name']]
        for scope in ('common', 'local'):
            root = covariance_root(fitted[scope + '_covariance'])
            values = np.zeros((len(roll), len(inputs['categories']), len(indices)))
            weights = np.divide(counts[:, indices], amount[:, None],
                                out=np.asarray(group['weights'], dtype=float).copy(), where=amount[:, None] > 0)
            # The derivative of normalized exponentials is diag(p) - p p^T.
            # Applying it is essential: transformed uncertainty is not an
            # absolute proportion error and cannot be multiplied by the pool
            # size alone. Export both the relative drivers and count derivative.
            derivative = weights[:, :, None] * (np.eye(len(indices))[None, :, :] - weights[:, None, :])
            values[:, indices, :] = amount[:, None, None] * (derivative @ root)
            responses[group['name'] + '_composition_' + scope] = dict(
                units='category votes per standard-normal composition driver', values=values.tolist(),
                log_proportion_driver_values=root.tolist(),
                drivers=[inputs['categories'][i] for i in indices])
    return dict(schema_version=SCHEMA_VERSION, model_version=MODEL_VERSION,
                central_totals=totals.tolist(), central_counts=counts.tolist(), responses=responses,
                rate_common_covariance_log_odds_squared=parameters['rates']['common_covariance'],
                rate_local_covariance_log_odds_squared=parameters['rates']['local_covariance'],
                aggregate_control_local_rule='Normalize positive district weights to the drawn national count, then reconcile combined controls with district totals in log odds.')


def draw(inputs, parameters, samples, seed=20261002, representation='reference', common=True, local=True):
    """Generate possible count outcomes for all districts in one election setup.

    Returned arrays have sample, district and category dimensions. A common
    change is shared across districts; local changes describe differences
    between districts. Each outcome has nonnegative category counts adding to
    its district total. All adjustments take a fixed sequence of calculations.

    Both interfaces retain the transformed rates and their inexpensive product.
    A linear product followed by clipping would undo the endpoint behaviour.
    The compact export still prepares local summaries and separate responses.
    The common/local switches also let preparation measure district
    variation without including election-wide uncertainty a second time.
    """
    if representation not in {'reference', 'compact'} or samples < 2:
        raise ValueError('Choose reference/compact and at least two samples.')
    n, k = len(inputs['seat_names']), len(inputs['categories'])
    identity = inputs['identity']
    roll = np.asarray(inputs['enrolment'], dtype=float)
    turnout, formality = starting_rates(inputs, parameters)
    centre_total, centre_counts = central_counts(inputs, parameters)
    # Draw related turnout/formality changes separately for the whole election
    # and for its districts. Their historical relationship is retained within
    # each pair; a new district name can have an enlarged local error scale.
    common_rate = random_source(seed, identity, 'rates.common', (samples, 2)) @ covariance_root(
        parameters['rates']['common_covariance']).T if common else np.zeros((samples, 2))
    local_rate = local_source(seed, identity, 'rates.local', inputs['seat_names'], samples, 2) @ covariance_root(
        parameters['rates']['local_covariance']).T if local else np.zeros((samples, n, 2))
    local_rate *= np.asarray(inputs['local_scale'])[None, :, None]
    # Centre local log-odds changes by their first-order effect on total ballots
    # and formal votes. Rate derivatives shrink near the endpoints, so weighting
    # just by enrolment would no longer remove the local common response.
    for metric, weights in enumerate((roll * turnout / 100 * (1 - turnout / 100),
            roll * turnout / 100 * formality / 100 * (1 - formality / 100))):
        local_rate[:, :, metric] -= (local_rate[:, :, metric] @ weights / weights.sum())[:, None]
    change = common_rate[:, None, :] + local_rate
    # Historical error scales and draws both use natural log odds. Inverting
    # them keeps rates inside their realistic domain without an endpoint mass.
    # Multiplying the two rates is cheap compared with preparing party shares;
    # retain it in both interfaces instead of introducing another clipped total.
    drawn_turnout = rate_percent(rate_log_odds(turnout) + change[:, :, 0])
    drawn_formality = rate_percent(rate_log_odds(formality) + change[:, :, 1])
    ballots = roll * drawn_turnout / 100
    totals = ballots * drawn_formality / 100

    amounts = []
    requested_aggregate = {}
    # Draw the error in each converted early/postal amount. Common error changes
    # every district together, while local error changes individual districts.
    # No party share or within-group composition change is included here.
    for group in inputs['controls']:
        base = np.asarray(group['amount'], dtype=float)
        # Count conversion errors are supplied as count standard deviations.
        # Translate them to relative logarithmic spreads before drawing, so a
        # positive known early/postal amount cannot turn into zero by clipping.
        common_log_sd = np.sqrt(np.log1p(np.divide(group['common_sd'], base,
            out=np.zeros_like(base), where=base > 0) ** 2)) if common else np.zeros(n)
        local_log_sd = np.sqrt(np.log1p(np.divide(group['local_sd'], base,
            out=np.zeros_like(base), where=base > 0) ** 2)) if local else np.zeros(n)
        shared = random_source(seed, identity, group['name'] + '.common', (samples, 1)) * common_log_sd
        individual = local_source(seed, identity, group['name'] + '.local', inputs['seat_names'], samples) * local_log_sd
        # Subtract half the log variance when exponentiating below so the
        # original converted count remains the mean, rather than drifting
        # upward solely because the positive distribution is asymmetric.
        if group['aggregate']:
            # A national control provides the combined count, not independently
            # measured resident counts in every district. Local error must move
            # votes between districts without changing that combined estimate.
            # Normalize positive relative allocations to the drawn national
            # amount instead of clipping negative additive district proposals.
            weights = base / base.sum()
            parent_sd = float(weights @ common_log_sd)
            requested = base.sum() * np.exp(shared @ weights - .5 * parent_sd ** 2)
            amount = transformed_partition(requested, weights, shared + individual - .5 * local_log_sd ** 2)
            requested_aggregate[group['name']] = requested
        else:
            amount = base * np.exp(shared + individual - .5 * (common_log_sd ** 2 + local_log_sd ** 2))
        amounts.append(amount)
    # Reconcile the uncertain combined control share continuously. Its local
    # response preserves converted-count uncertainty while extreme requests
    # approach the available pool gradually, leaving a positive remainder.
    controlled = sum(amounts, np.zeros_like(totals))
    conflicts = controlled > totals
    supplied, remaining = reconcile_controls(inputs, totals, amounts, centre_total)
    control_difference = sum((np.abs(a - b) for a, b in zip(amounts, supplied)), np.zeros_like(totals))
    counts = np.zeros((samples, n, k))
    # Now divide each supplied amount within its historical category group.
    # This composition uncertainty is conditional on the group's total, so it
    # does not repeat the amount uncertainty drawn in the preceding step.
    for group, amount in zip(inputs['controls'] + [inputs['remainder']], supplied + [remaining]):
        indices = group['indices']
        central_amount = centre_counts[:, indices].sum(axis=1)
        central_weights = np.divide(centre_counts[:, indices], central_amount[:, None],
            out=np.asarray(group['weights'], dtype=float).copy(), where=central_amount[:, None] > 0)
        weights = allocation_weights(group, amount, totals, central_weights)
        fitted = parameters['compositions'][group['name']]
        if len(indices) == 1:
            # A group with one category has no uncertain internal division.
            # Its count still varies with the group's total from the steps above.
            counts[:, :, indices] = amount[:, :, None]
            continue
        shared = (random_source(seed, identity, group['name'] + '.composition.common', (samples, len(indices)))
                  @ covariance_root(fitted['common_covariance']).T) if common else np.zeros((samples, len(indices)))
        individual = (local_source(seed, identity, group['name'] + '.composition.local', inputs['seat_names'], samples, len(indices))
                      @ covariance_root(fitted['local_covariance']).T) if local else np.zeros((samples, n, len(indices)))
        error = shared[:, None, :] + individual
        # Both representations retain this inexpensive bounded transformation.
        # Fixed logarithmic drivers already describe relative responses; using
        # a fixed absolute count response here would recreate the scale error
        # when the outcome's group amount differs from its central amount.
        counts[:, :, indices] = transformed_partition(amount, weights, error)
    # Smooth district reconciliation can change a requested national control.
    # Keep that difference visible alongside the basic category-sum check.
    # These diagnostics check the constructed counts, not predictive accuracy.
    discrepancy = {g['name']: float(np.max(np.abs(counts[:, :, g['indices']].sum(axis=(1, 2))
                       - requested_aggregate[g['name']]))) for g in inputs['controls'] if g['aggregate']}
    accounting_error = float(np.max(np.abs(counts.sum(axis=2) - totals)))
    if accounting_error > 1e-6 or not np.isfinite(counts).all() or (counts < 0).any():
        raise ValueError('Prior counts failed bounded accounting.')
    # Retain rate draws in memory for focused endpoint checks. Saved diagnostics
    # summarize them without exporting the full ensemble or putting actual
    # held-election rates into any prediction input.
    return dict(totals=totals, counts=counts, rates=dict(
        turnout_pct=drawn_turnout, formality_pct=drawn_formality,
        turnout_change_log_odds=change[:, :, 0], formality_change_log_odds=change[:, :, 1]), diagnostics=dict(
        maximum_accounting_error_votes=accounting_error,
        rate_endpoint_fraction=float(((drawn_turnout == 0) | (drawn_turnout == 100)
            | (drawn_formality == 0) | (drawn_formality == 100)).mean()),
        maximum_turnout_pct=float(drawn_turnout.max()), maximum_formality_pct=float(drawn_formality.max()),
        control_conflict_fraction=float(conflicts.mean()),
        control_transform_difference_per_1000=float(1000 * control_difference.sum(axis=1).mean() / roll.sum()),
        positive_category_zero_fraction=float(((counts == 0) & (centre_counts[None, :, :] > 1)).sum()
            / max(1, samples * (centre_counts > 1).sum())),
        aggregate_control_maximum_adjustment_votes=discrepancy))


def preparation_statistics(inputs, parameters, samples=288, seed=20261002):
    """Summarize local count uncertainty with a small sample for one election.

    The live simulator's preparation/main-iteration split needs local variation
    to be summarized once and shared effects to be applied separately. Holding
    common changes fixed prevents election-wide error being recorded as local
    noise and then added again through shared sensitivities.

    Save average counts and root mean square differences from the central count,
    including shifts in the sample mean caused by nonlinear transformations.
    These count statistics do not represent the GUI's prepared variation in
    party vote shares.
    """
    values = draw(inputs, parameters, samples, seed, common=False)['counts']
    _, centre = central_counts(inputs, parameters)
    return dict(samples=samples, shared_drivers_fixed=True,
                mean_counts=values.mean(axis=0).tolist(),
                rms_about_central_counts=np.sqrt(((values - centre) ** 2).mean(axis=0)).tolist())
