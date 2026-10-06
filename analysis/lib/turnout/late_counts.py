"""Condition remaining declaration counts on smoothly changing progress evidence.

The ordinary live updater supplies the broad prediction. This optional layer
mixes it with small additions and a rare, widely distributed late batch. It
rebuilds each snapshot from that prediction and the supplied source history;
it never uses final results, marks a category closed, or recycles a posterior.
"""

from datetime import datetime

import numpy as np
from scipy.special import expit, logsumexp, ndtr, ndtri


MODEL_VERSION = 'live-late-counts-4'
DEFAULTS = dict(activity_decay_hours=24., coverage_hours=48., activity_scale=.1,
                magnitude_scale=.005, unstarted_scale=.1, state_equivalent_districts=20.,
                evidence_scale=.3, batch_probability=.02, small_median_votes=.25,
                small_log_odds_sd=1., batch_log_odds_sd=1.5, batch_current_fraction=.05,
                postal_deadline_transition_hours=48., zero_addition_fraction=.95)


def configuration(overrides=None):
    """Keep the few exploratory constants explicit and shared across districts."""
    values = dict(DEFAULTS, **(overrides or {}))
    if any(not np.isfinite(v) or v <= 0 for v in values.values()):
        raise ValueError('Late-count parameters must be finite and positive.')
    if values['batch_probability'] >= 1 or values['zero_addition_fraction'] >= 1:
        raise ValueError('Batch and zero-addition fractions must be below one.')
    return values


def category_key(unit):
    return unit.get('vote_type', unit['name'])


def shared_measurements(rows):
    """Limit exceptional districts' influence on shared completion evidence.

    Drop up to two observations at each end of each measurement: one per ten
    measured districts, leaving small pools untrimmed. This protects typical
    reporting progress from erroneous zeros or unusually large corrections.
    Local counts are never trimmed. Sorted-value trimmed means remain continuous
    when districts exchange order; there is no vote-count rejection threshold.
    Movement is measured relative to each district's current category count
    before averaging, giving districts equal influence rather than allowing an
    unusually large batch in one district to dominate the shared measurement.
    """
    trim = min(2, len(rows)//10)
    def central(values):
        ordered = sorted(values)
        return ordered[trim:len(rows)-trim] if trim else ordered
    return dict(activity=float(np.mean(central([r['activity'] for r in rows]))),
                started=float(np.mean(central([r['started'] for r in rows]))),
                volume=float(np.mean(central([r['volume']/(r['current']+.5) for r in rows]))))


def progress_evidence(history, units, subdivisions=None, options=None, *, postal_deadline=None,
                      counting_clock=None, event_scale=.001, shared_deadline=None):
    """Measure counting activity with decaying memory and continuous support.

    Absolute changes include reversals and rechecks. A large change contributes
    at most one activity event, then gradually loses influence with age. Recent
    vote volume is measured separately. Long gaps discount observation coverage;
    they are not treated as repeated unchanged measurements. Missing categories
    are omitted, while explicit unstarted zeros weaken shared starting evidence.
    Federal state measurements blend gradually with national measurements using
    their sample size, without a minimum-district switch. An explicitly supplied
    postal receipt deadline discounts local quietness while returns can still
    arrive from voters, and then releases that discount gradually. It is not a
    completion date: ongoing activity still argues against finalisation.
    An optional counting clock reduces weekend elapsed time. The event scale
    specifies the fraction of the current category used to judge change size.
    A shared deadline discounts late activity's influence elsewhere while
    preserving its local evidence and recording support from small late batches.
    """
    config = configuration(options)
    elapsed = counting_clock or (lambda before,after: (after-before).total_seconds()/3600)
    by_time = {datetime.fromisoformat(s['source_time']): s for s in history}
    ordered = sorted(by_time.items())
    if not ordered:
        return [dict(strength=0., reason='No source history.') for _ in units]
    now = ordered[-1][0]
    categories = {category_key(u) for u in units if u['kind'] == 'declaration'}
    states = subdivisions or {}
    measured = {}
    for name, current in ordered[-1][1]['seats'].items():
        for category in categories:
            value = current['vote_types'].get(category)
            if value is None:
                continue
            if value < 0:
                raise ValueError('Progress history contains a negative category count.')
            observations = [(stamp, snap['seats'].get(name, {}).get('vote_types', {}).get(category))
                            for stamp, snap in ordered]
            activity = volume = coverage = shared_activity = shared_volume = small_support = changed_support = 0.
            tolerance = np.hypot(10., event_scale*value)
            for (before, old), (after, new) in zip(observations, observations[1:]):
                if old is None or new is None:
                    continue
                hours = elapsed(before,after)
                age = elapsed(after,now)
                decay = np.exp(-age/config['activity_decay_hours'])
                change = abs(new-old)
                ratio = change/tolerance
                activity += ratio*ratio/(1+ratio*ratio)*decay
                volume += change*decay
                # A backlog after receipt closes remains local evidence, while
                # its influence on other seats' progress recedes continuously.
                shared = 1. if shared_deadline is None else expit(-(after-datetime.fromisoformat(shared_deadline)).total_seconds()/3600/24.)
                shared_activity += ratio*ratio/(1+ratio*ratio)*decay*shared
                shared_volume += change*decay*shared
                if shared_deadline is not None:
                    post = expit((before-datetime.fromisoformat(shared_deadline)).total_seconds()/3600/12.)
                    event = post*np.exp(-age/72.)*change**2/(change**2+50.**2)
                    changed_support += event
                    if new>old:
                        small_support += event/(1+(change/(.05*(value+.5)))**2)
                # Integrate covered time with fading memory. An isolated pair
                # a week apart supplies much less support than daily returns.
                covered = np.exp(-age/config['coverage_hours'])*(-np.expm1(-hours/config['coverage_hours']))
                coverage += covered/(1+(hours/config['coverage_hours'])**2)
            measured[name, category] = dict(current=value, activity=-np.expm1(-activity),
                volume=volume, coverage=coverage, started=value/(value+10.),
                shared_activity=-np.expm1(-shared_activity),shared_volume=shared_volume,
                small_batch_support=small_support/(1+changed_support),
                state=current.get('state', states.get(name)))

    pools = {}
    for category in categories:
        rows = [r for (name, c), r in measured.items() if c == category]
        if rows:
            pools[category] = shared_measurements([dict(r,activity=r['shared_activity'],volume=r['shared_volume']) for r in rows])
    # Aggregate multiple reporting batches before attaching category evidence
    # to individual units. No final-category data enters this reconciliation.
    current_units = {}
    for unit in units:
        if unit['kind'] == 'declaration':
            key = unit['seat_name'], category_key(unit)
            current_units[key] = current_units.get(key, 0)+unit['counted']
    result = []
    for unit in units:
        key = unit['seat_name'], category_key(unit)
        local = measured.get(key)
        if unit['kind'] != 'declaration' or local is None:
            result.append(dict(strength=0., reason='No applicable declaration measurement.'))
            continue
        if current_units[key] != local['current']:
            raise ValueError('Current unit counts and progress history differ: '+str(key))
        pooled = dict(pools[key[1]])
        region = [r for (name,c),r in measured.items() if c == key[1] and r['state'] == local['state']]
        region_weight = len(region)/(len(region)+config['state_equivalent_districts']) if local['state'] else 0.
        if region_weight:
            regional = shared_measurements([dict(r,activity=r['shared_activity'],volume=r['shared_volume']) for r in region])
            pooled = {k:(1-region_weight)*v+region_weight*regional[k] for k,v in pooled.items()}
        own_quiet = 1/(1+(local['activity']/config['activity_scale'])**2)
        broad_quiet = 1/(1+(pooled['activity']/config['activity_scale'])**2)
        small_movement = 1/(1+(pooled['volume']/config['magnitude_scale'])**2)
        starting_support = 1/(1+((1-pooled['started'])/config['unstarted_scale'])**2)
        strength = local['coverage']*local['started']*own_quiet*starting_support*(.75*broad_quiet+.25*small_movement)
        receipt_support = 1.
        if key[1] == 'Postal' and postal_deadline is not None:
            hours = (now-datetime.fromisoformat(postal_deadline)).total_seconds()/3600
            receipt_support = float(expit(hours/config['postal_deadline_transition_hours']))
            strength *= receipt_support
        result.append(dict(strength=float(strength), **local, pooled=pooled,
                           state_weight=region_weight, postal_receipt_support=receipt_support))
    return result


def component_probabilities(weight, zero_probability, config, schedule=None):
    """Transfer non-batch probability to a shared finished-count outcome.

    A seat-wide zero event replaces part of the previous and small-addition
    branches. Preserve each category's unconditional late-batch probability:
    admitting a finished count must not also make exceptional batches vanish.
    With a receipt schedule, blend towards a separately fading batch chance;
    only the routine amount recedes rapidly after the processing allowance.
    """
    batch = weight*config['batch_probability']
    if schedule is not None:
        positive = 1-np.asarray(zero_probability)
        requested = config['batch_probability']*schedule['batch_survival']
        ratio = np.divide(requested,positive,out=np.zeros_like(positive,dtype=float),where=positive>0)
        possible = positive*(-np.expm1(-ratio))
        phase = schedule['processing_phase']
        batch = (1-phase)*batch+phase*possible
        small_share = weight*(1-config['batch_probability'])/(1-weight*config['batch_probability'])
        remaining = positive-batch
        return (1-small_share)*remaining,small_share*remaining,batch
    retained = (1-zero_probability-batch)/(1-batch)
    return (1-weight)*retained, weight*(1-config['batch_probability'])*retained, batch


def no_addition_probabilities(prepared, units, evidence, config):
    """Share support for a finished count across the district's categories.

    Weight slowing evidence by counted declaration volume, so an unstarted
    tiny category cannot veto a largely completed count. Outstanding broad
    estimates, including unreported booths, weaken that support continuously.
    A known postal receipt window weakens it further. These are exploratory
    shared settings, not fitted completion probabilities or closure flags.
    """
    result = np.zeros(len(prepared['counted']))
    for seat in range(len(result)):
        columns = [j for j,u in enumerate(units) if u['seat_index'] == seat]
        if columns and all(prepared['complete'][j] for j in columns):
            result[seat] = 1.
            continue
        declarations = [j for j in columns if units[j]['kind'] == 'declaration' and not prepared['complete'][j]]
        counted = sum(units[j]['counted'] for j in declarations)
        if not counted:
            continue
        weights = -np.expm1(-(np.array([evidence[j]['strength'] for j in declarations])/config['evidence_scale'])**2)
        support = sum(units[j]['counted']*w for j,w in zip(declarations,weights))/counted
        remaining = float(np.mean(prepared['totals'][:,seat]-prepared['counted'][seat]))
        coverage = counted/(counted+remaining)
        receipt = min((evidence[j].get('postal_receipt_support',1.) for j in declarations
                       if category_key(units[j]) == 'Postal'),default=1.)
        nonbatch = np.prod(1-weights*config['batch_probability'])
        result[seat] = config['zero_addition_fraction']*support*coverage**2*nonbatch*receipt
    return result


def prediction_mean(result, field='totals'):
    """Combine conditional positive-count samples with the exact counted atom."""
    counted = result['counted'] if field == 'totals' else result['counted_groups']
    probability = result['no_addition_probability']
    if field == 'counts':
        probability = probability[:,None]
    return counted+(1-probability)*(result[field].mean(axis=0)-counted)


def prediction_quantiles(result, probabilities, field='totals'):
    """Read intervals from the mixture, retaining its exact zero-addition mass.

    The small preparation sample describes the branch with further votes.
    Analytic mixing avoids estimates jumping when a sampled Bernoulli outcome
    switches branch, and retains rare possibilities independently of sampling.
    """
    values = result[field]
    counted = result['counted'] if field == 'totals' else result['counted_groups']
    pzero = result['no_addition_probability']
    output = np.empty((len(probabilities),)+values.shape[1:])
    for index in np.ndindex(values.shape[1:]):
        p = pzero[index[0]]
        for k,q in enumerate(probabilities):
            output[(k,)+index] = counted[index] if q <= p or p == 1 else np.quantile(values[(slice(None),)+index],(q-p)/(1-p))
    return output


def mixture_log_odds(reference, small, batch, weight, uniforms, config, zero_probability=0., schedule=None):
    """Invert the explicit mixture without switching predictions at a score.

    Conditional on one preparation outcome, its broad reference is a point;
    across all preparation outcomes it retains the existing broad distribution.
    Both new components have continuous, overlapping support in log odds.
    Crossing the reference's probability mass approaches that same reference
    value continuously. A fixed bisection count bounds cost and precision.
    """
    previous, small_weight, batch_weight = component_probabilities(weight,zero_probability,config,schedule)
    positive = 1-zero_probability
    previous, small_weight, batch_weight = previous/positive, small_weight/positive, batch_weight/positive
    def continuous_cdf(value):
        return small_weight*ndtr((value-small)/config['small_log_odds_sd'])+batch_weight*ndtr((value-batch)/config['batch_log_odds_sd'])
    below = continuous_cdf(reference)
    above = below+previous
    on_reference = (uniforms >= below) & (uniforms <= above)
    target = np.where(uniforms < below, uniforms, uniforms-previous)
    # Most election-night outcomes remain on their broad reference. Inverting
    # a normal mixture for those unchanged outcomes would waste the count-only
    # work that is meant to be cheaper than party-composition preparation.
    # Select only outcomes that actually need an inverse, retaining the same
    # continuous CDF and fixed quantiles for the others.
    result = np.broadcast_to(reference,uniforms.shape).copy()
    selected = ~on_reference
    if not selected.any():
        return result
    small = np.broadcast_to(small,uniforms.shape)[selected]
    batch = np.broadcast_to(batch,uniforms.shape)[selected]
    small_weight = np.broadcast_to(small_weight,uniforms.shape)[selected]
    batch_weight = np.broadcast_to(batch_weight,uniforms.shape)[selected]
    target = target[selected]
    # Twelve standard deviations cover the interior stratified probabilities.
    # These are numerical brackets, not caps on simulated votes or evidence.
    low = np.minimum(small-12*config['small_log_odds_sd'], batch-12*config['batch_log_odds_sd'])
    high = np.maximum(small+12*config['small_log_odds_sd'], batch+12*config['batch_log_odds_sd'])
    for _ in range(40):
        middle = (low+high)/2
        lower = continuous_cdf(middle) < target
        low = np.where(lower, middle, low)
        high = np.where(lower, high, middle)
    result[selected] = (low+high)/2
    return result


def update(prepared, inputs, units, evidence, count_draws=8, seed=20261002, options=None, *, uniforms=None, schedule=None):
    """Reweight declarations while preserving counted votes and booth estimates.

    Each expensive/pre-existing preparation outcome supplies several cheap count
    draws. Explicit component parameters and conditional evaluations retain rare
    batches even when posterior samples contain few. A shared finite pool of
    declarations and non-formal/unsubmitted capacity is represented in log odds;
    normalizing it preserves the enrolment bound without clipping. Ordinary and
    PPVC additions stay fixed within the positive branch. A separate shared
    zero-addition probability leaves the entire current account unchanged.
    Removed declaration votes mostly become unused
    capacity, rather than being forced into an unchanged formal turnout target.
    """
    config = configuration(options)
    if count_draws < 1 or int(count_draws) != count_draws or len(evidence) != len(units):
        raise ValueError('Use a positive integer count-draw count and one evidence record per unit.')
    samples, seats = prepared['totals'].shape
    no_addition = no_addition_probabilities(prepared,units,evidence,config)
    outer = np.repeat(np.arange(samples), count_draws)
    counts = prepared['counts'][outer].copy()
    totals = prepared['totals'][outer].copy()
    unit_means = prepared['unit_counts'].mean(axis=0).copy()
    columns = np.array([j for j,u in enumerate(units) if u['kind'] == 'declaration' and not prepared['complete'][j]], dtype=int)
    details = []
    component_preparation = None
    if len(columns):
        indexes = np.array([units[j]['seat_index'] for j in columns])
        strengths = np.array([evidence[j]['strength'] for j in columns])
        weights = -np.expm1(-(strengths/config['evidence_scale'])**2)
        base = prepared['remaining'][:,columns]
        slack = np.asarray(inputs['enrolment'])-prepared['totals']
        if (slack <= 0).any() or (base < 0).any():
            raise ValueError('Open declarations require nonnegative additions and positive unused capacity.')
        # A count-conditioned broad estimate can become smaller than the
        # precision of the subtraction that produced it. Represent its zero
        # log odds at machine precision; this is not an observed-zero count or
        # a substantive minimum vote estimate. Neutral evidence still returns
        # the exact broad account. The conditional batch gets the same small
        # count equivalent as the small-addition component when both its
        # starting estimate and current count are negligible.
        reference = np.log((base+np.finfo(float).eps)/slack[:,indexes])
        normal_shifts = np.zeros(len(columns)) if schedule is None else np.asarray(schedule['usual_log_shifts'])[columns]
        phase = 0. if schedule is None else schedule['processing_phase']
        reference += normal_shifts[None,:]
        small = np.log(config['small_median_votes']/slack[:,indexes])
        batch = np.log((base+config['small_median_votes']+config['batch_current_fraction']*
                        np.array([units[j]['counted'] for j in columns]))/slack[:,indexes])
        if phase:
            own_batch = np.log((prepared['own_remaining'][:,columns]+config['small_median_votes']+config['batch_current_fraction']*
                              np.array([units[j]['counted'] for j in columns]))/slack[:,indexes])
            batch = (1-phase)*batch+phase*own_batch
        # Keep conditional distributions separate from the mixed samples.
        # A subsequent party-composition preparation can evaluate a rare batch
        # deliberately, rather than depending on whether that branch happened
        # to occur among the sampled outcomes or replacing it by one deviation.
        component_preparation = dict(unit_indexes=columns, unused_capacity=slack[:,indexes],
            reference_additions=base, reference_log_odds=reference, small_log_odds=small,
            batch_log_odds=batch, slowing_weight=weights)
        # Cross-language comparison supplies the same interior quantiles to
        # both implementations. Ordinary Python replays retain their established
        # generator, so introducing this comparison hook does not alter scores.
        if uniforms is None:
            rng = np.random.default_rng(seed)
            uniforms = np.stack([(rng.permutation(len(outer))+.5)/len(outer) for _ in columns], axis=1)
        else:
            uniforms = np.asarray(uniforms, dtype=float)
            if uniforms.shape != (len(outer), len(columns)) or not np.isfinite(uniforms).all() or ((uniforms <= 0) | (uniforms >= 1)).any():
                raise ValueError('Supply one interior quantile per outcome and open declaration unit.')
        varied = mixture_log_odds(reference[outer], small[outer], batch[outer], weights, uniforms, config,no_addition[indexes],schedule)
        # Evaluate both conditional distributions explicitly at a fixed set of
        # quantiles. This is preparation of a rare branch, not Bernoulli selection.
        quantiles = ndtri((np.arange(count_draws)+.5)/count_draws)
        for k,j in enumerate(columns):
            parent = slack[:,indexes[k]]+base[:,k]
            conditional = {}
            for name, location, sd in (('small',small[:,k],config['small_log_odds_sd']),
                                      ('batch',batch[:,k],config['batch_log_odds_sd'])):
                values = parent[:,None]*expit(location[:,None]+sd*quantiles)
                conditional[name+'_mean_if_other_counts_fixed'] = float(values.mean())
            previous, small_probability, batch_probability = component_probabilities(weights[k],no_addition[indexes[k]],config,schedule)
            details.append(dict(unit=int(j), seat=units[j]['seat_name'], category=category_key(units[j]),
                evidence=evidence[j], weight=float(weights[k]),
                usual_log_shift=float(normal_shifts[k]),
                probabilities=dict(no_additions=float(no_addition[indexes[k]]),previous=float(previous),
                                   small=float(small_probability),batch=float(batch_probability)),
                reference_mean=float(base[:,k].mean()), **conditional))
        for seat in range(seats):
            local = np.flatnonzero(indexes == seat)
            if not len(local) or (not np.any(weights[local]) and not np.any(normal_shifts[local]) and not phase):
                continue
            parent = slack[:,seat]+base[:,local].sum(axis=1)
            odds = varied[:,local]
            denominator = logsumexp(np.column_stack([np.zeros(len(outer)),odds]), axis=1)
            additions = parent[outer,None]*np.exp(odds-denominator[:,None])
            for k, position in enumerate(local):
                j = columns[position]
                group = units[j]['group_index']
                counts[:,seat,group] += additions[:,k]-base[outer,position]
                unit_means[j] = units[j]['counted']+additions[:,k].mean()
            totals[:,seat] = counts[:,seat].sum(axis=1)
    counted_groups = np.zeros(prepared['counts'].shape[1:])
    for u in units:
        counted_groups[u['seat_index'],u['group_index']] += u['counted']
    # A common zero branch applies to every unfinished unit, rather than asking
    # several independent positive-only category draws to approximate zero.
    # Completed booths and all already counted votes remain unchanged.
    for j,u in enumerate(units):
        unit_means[j] = u['counted']+(1-no_addition[u['seat_index']])*(unit_means[j]-u['counted'])
    diagnostics = dict(maximum_accounting_error=float(np.max(np.abs(counts.sum(axis=2)-totals))),
        minimum_category_addition=float(np.min(counts-counted_groups)),
        minimum_unused_enrolment=float(np.min(np.asarray(inputs['enrolment'])-totals)),
        preparation_samples=samples, count_draws_per_preparation=count_draws, posterior_count_outcomes=len(outer),
        conditional_component_evaluations=2*samples*count_draws*len(columns))
    return dict(model_version=MODEL_VERSION, totals=totals, counts=counts, unit_means=unit_means,
                counted=prepared['counted'],counted_groups=counted_groups,no_addition_probability=no_addition,
                complete=prepared['complete'], components=details,
                component_preparation=component_preparation, config=config, diagnostics=diagnostics)


def reweight_shares(counted_by_group, final_group_counts):
    """Apply the same category-count outcomes to every candidate or two-party share.

    This mechanical illustration keeps each currently observed category mix
    fixed. It supplies the cheap count-to-share operation, not new estimates of
    late-voter preferences. Unknown category composition is an explicit error;
    callers must omit unsupported districts rather than invent candidate votes.
    """
    observed = np.asarray(counted_by_group, dtype=float)
    current = observed.sum(axis=1)
    future = np.asarray(final_group_counts)-current
    if np.any((current == 0) & (np.max(future,axis=0) > 1e-8)):
        raise ValueError('An outstanding category has no observed candidate composition.')
    mix = np.divide(observed,current[:,None],out=np.zeros_like(observed),where=current[:,None] > 0)
    votes = np.asarray(final_group_counts) @ mix
    return votes/votes.sum(axis=1,keepdims=True)
