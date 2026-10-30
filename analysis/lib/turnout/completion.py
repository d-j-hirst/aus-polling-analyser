"""Estimate near-finished quantities without transferring votes to other services.

A count approaching its expected size may need few further additions. A count
far above that expectation can instead indicate an undersized prior. These
competing explanations change only the local routine-addition component;
existing exceptional batches and postal receipt safeguards remain available.
Separately, gentle local activity can support a positive absent/early-absent
remainder when the original size was too high. These limits are applied after
allocation, retaining every counted vote and the original sibling allowances.
"""

import numpy as np
from scipy.special import expit, logit, ndtr, ndtri

SURPRISE_SCALE = 2.
COUNT_EQUIVALENT = .5
# Broaden the near-finished postal distribution without raising its unbounded
# expected quantity. The original total variance was .4^2+.4^2; subtracting
# half the variance increase from log odds preserves the mean of additions.
# Shared delays retain SD .4; the extra spread is local. These and the following
# settings are conservative Queensland 2024 trials, not cross-election fits.
POSTAL_SHARED_SD = .4
POSTAL_REMAINDER_SD = 1.25
POSTAL_LOCAL_SD = np.sqrt(POSTAL_REMAINDER_SD**2-POSTAL_SHARED_SD**2)
POSTAL_LOG_MEDIAN_SHIFT = (.4**2+.4**2-POSTAL_REMAINDER_SD**2)/2
POSTAL_FINISHING_ODDS = 3.
POSTAL_ROUTINE_SPREAD = 1.5
POSTAL_BATCH_TARGET_SD = 2.
# Starting settings for positive absent/early-absent remainders. These are
# supported by Queensland 2024 diagnostic trials, not an across-election fit.
# Two daily processing quantities set the median, not the completion date.
# Total natural-log-odds SD 1.75 comprises a shared .5 and independent local rest.
DECLARATION_LOCAL_EVENT_SCALE = .05
DECLARATION_REMAINDER_DAYS = 2.
DECLARATION_REMAINDER_SD = 1.75
DECLARATION_SHARED_SD = .5


def postal_remainder_log_odds(days_before_deadline=None):
    """Median uncounted/final formal-postal share for the near-finished path.

    Mature, slowing counts in Federal 2022/2025 and Queensland 2024 motivate
    original medians of 6%, 4%, 2% at four/two/zero calendar days before receipt
    closes. Recentring the broader distribution lowers those medians while
    preserving the expected addition before allocation limits. Linear
    interpolation in log odds keeps shares bounded and counts continuous. The
    early slope continues before day four; afterwards odds halve every two days.
    With no source-clock deadline, retain 6% rather than assume receipt has ended.
    Larger backlogs and late batches are covered outside this finishing path.
    """
    if days_before_deadline is None:
        return float(logit(.06)+POSTAL_LOG_MEDIAN_SHIFT)
    days = days_before_deadline
    if days < 0:
        return float(logit(.02)+days*np.log(2)/2+POSTAL_LOG_MEDIAN_SHIFT)
    if days < 2:
        return float(logit(.02)+days*(logit(.04)-logit(.02))/2+POSTAL_LOG_MEDIAN_SHIFT)
    return float(logit(.04)+(days-2)*(logit(.06)-logit(.04))/2+POSTAL_LOG_MEDIAN_SHIFT)


def days_after_poll_close(history, poll_close):
    """Calendar age of the latest received source; absent dates supply no guard."""
    from datetime import datetime
    if not history or poll_close is None:
        return None
    now = max(datetime.fromisoformat(row['source_time']) for row in history)
    return (now-datetime.fromisoformat(poll_close)).total_seconds()/86400


def early_completion_retention(original_z, days):
    """Retain less finishing support only when both young and below expectation.

    z measures current count against the frozen size distribution on log odds of
    count/enrolment. A two-SD shortfall retains about 14% of finishing support
    at polling close, 57% after seven days, and 99% after fourteen. At or above
    expectation it retains everything. The smooth seven-day scale is a trialled
    safeguard against pauses concealing unprocessed batches, not a finish date.
    """
    z = np.asarray(original_z)
    if days is None:
        return np.ones_like(z)
    shortfall = -np.expm1(-np.maximum(0.,-np.nan_to_num(z))**2/2)
    return 1-shortfall/(1+(max(0.,days)/7)**6)


def postal_finishing_weight(fraction, slowing, support):
    """Prefer small positive remainders without declaring postal receipt closed.

    Gentle local quietness supplies three quarters of the support not already
    supplied by ordinary slowing. Then multiply finishing/substantial-count odds
    by three. Zero support stays zero; large above-prior counts still favour an
    undersized prior. These settings reflect the conservative Queensland trial.
    """
    extra = max(0.,support-slowing)/(1-slowing) if slowing < 1 else 0.
    fraction += .75*(1-fraction)*extra
    return POSTAL_FINISHING_ODDS*fraction/(1+(POSTAL_FINISHING_ODDS-1)*fraction)


def guard_probabilities(probabilities, finishing, remainder, retention):
    """Return suppressed finishing mass to substantial additions, leaving batches
    and the separate district counted-only outcome unchanged. Fractions are
    rescaled to their new routine parent; all inputs are probability quantities.
    """
    previous, small, batch = probabilities
    new_previous = previous+small*(1-retention)
    scale = np.divide(previous*retention,new_previous,
                      out=np.zeros_like(new_previous),where=new_previous>0)
    return (new_previous,small*retention,batch),finishing*scale,remainder*scale


def postal_widening_weight(values, reference, small, batch, probabilities, batch_sd=1.5):
    """Identify the routine point and the batch share of continuous draws.

    Routine draws get full widening. Among continuous draws, a tiny amount is
    usually from the small-addition component, whereas a large one is usually a
    batch. Their normal densities give a smooth batch weight without introducing
    a vote threshold or changing component probabilities. This same function is
    used for inverse-mixture diagnostic samples and direct main simulation draws.
    """
    _,small_probability,batch_probability = probabilities
    routine = values == reference
    if batch_probability <= 0:
        return routine,np.where(routine,1.,0.)
    if small_probability <= 0:
        return routine,np.ones_like(values)
    log_ratio = (np.log(batch_probability)-np.log(small_probability)-np.log(batch_sd)
        -.5*((values-batch)/batch_sd)**2+.5*(values-small)**2)
    return routine,np.where(routine,1.,expit(log_ratio))


def allocated_postal_spread(additions, capacity, sd, normal):
    """Bounded smaller/larger possibilities whose mean is each allocated count.

    Each item is one final postal addition after joint allocation and finishing
    limits. Its parent is that addition plus reserved unused enrolment; other
    categories stay fixed. A unit-local normal magnitude moves its log odds up
    and down. The probability of the larger amount is chosen so the weighted
    average is EXACTLY the original addition, even near either physical bound.
    This adds uncertainty without learning or changing an expected count.

    For example, possibilities 60 and 180 around an allocated 100 receive larger
    probability (100-60)/(180-60)=1/3. Their mean remains 100. Averaging over normal
    magnitudes retains that property; no sample-fitted recentering is needed.
    Zero/full-parent draws stay fixed because a bounded mean-preserving spread
    is then impossible. Finishing outcomes receive zero extra SD from the caller.
    """
    additions,capacity,sd,normal = np.broadcast_arrays(
        *(np.asarray(v,dtype=float) for v in (additions,capacity,sd,normal)))
    use = (sd>0)&(additions>0)&(additions<capacity)
    lower,upper = additions.copy(),additions.copy()
    probability = np.zeros_like(additions,dtype=float)
    location = np.log(additions[use]/(capacity[use]-additions[use]))
    magnitude = sd[use]*np.abs(normal[use])
    lower[use] = np.minimum(additions[use],capacity[use]*expit(location-magnitude))
    upper[use] = np.maximum(additions[use],capacity[use]*expit(location+magnitude))
    width = upper-lower
    np.divide(additions-lower,width,out=probability,where=width>0)
    return lower,upper,np.clip(probability,0.,1.)


def postal_shared_normals(outcomes, seed, key="postal-remainder/shared"):
    """One independent shared normal per simulated count outcome.

    Mirror TurnoutModel.cpp's stable keyed permutation so portable tests compare
    identical Python/C++ outcomes. A fixed stream name makes the draw independent
    of seat order, category availability and branch selection. This is random
    sampling of shared uncertainty, not new evidence from other districts.
    Explicit alternative keys provide independent local postal-widening streams.
    """
    mask = (1 << 64)-1
    state = 14695981039346656037
    for byte in key.encode('utf-8'):
        state = ((state ^ byte)*1099511628211) & mask
    state ^= seed
    positions = list(range(outcomes))
    for n in range(outcomes,1,-1):
        state = (state+0x9e3779b97f4a7c15) & mask
        value = state
        value = ((value ^ (value >> 30))*0xbf58476d1ce4e5b9) & mask
        value = ((value ^ (value >> 27))*0x94d049bb133111eb) & mask
        value ^= value >> 31
        j = value % n
        positions[n-1], positions[j] = positions[j], positions[n-1]
    return ndtri((np.asarray(positions)+.5)/outcomes)


def evidence(prepared, inputs, units, progress):
    """Measure each open service against its frozen, unconditioned size draws.

    The original account is carried separately through allocation. Never substitute
    the current conditioned account or an earlier snapshot's posterior. A missing,
    structurally zero or fixed prior supplies no new completion evidence.
    """
    weight = np.zeros(len(units))
    z = np.full(len(units), np.nan)
    frozen = prepared.get('original_unit_counts')
    if frozen is None:
        return weight, z
    for j, unit in enumerate(units):
        if (unit['kind'] != 'declaration' or prepared['complete'][j]
                or unit['counted'] <= 0 or not np.any(frozen[:,j] > 0)):
            continue
        parent = inputs['enrolment'][unit['seat_index']]+2*COUNT_EQUIVALENT
        transformed = logit((frozen[:,j]+COUNT_EQUIVALENT)/parent)
        sd = transformed.std()
        if sd <= 1e-8:
            continue
        z[j] = (logit((unit['counted']+COUNT_EQUIVALENT)/parent)-transformed.mean())/sd
        weight[j] = ndtr(z[j])/(1+(max(z[j],0)/SURPRISE_SCALE)**4)
        # Postal size support also concerns main counting being largely done.
        # It now leads to a positive remainder, so receipt need not discount
        # it. Quietness and exact-zero support keep their existing safeguards.
    return weight, z


def limits(reference, small, batch, capacity, weights, probabilities, positive, uniforms,
           small_sd=1., batch_sd=1.5, *, postal_counts=None, postal_log_odds=None, shared_normal=None):
    """Draw local near-finished limits from the routine probability interval.

    The ordinary inverse-mixture sample supplies an unconstrained allocation
    weight. Postal limits enter the shared allocation itself: reserve a binding
    finishing amount before distributing the remaining space. Otherwise a large
    postal weight would suppress other categories and then disappear afterwards.
    Non-postal limits retain their local, after-allocation treatment; their
    released allowance becomes unused turnout. Infinity means no intervention.
    The routine interval's rescaled position supplies local variation. Normally
    this is the existing tiny component; Postal instead uses uncounted/final
    postal share, varying with both that local draw and an election-wide draw.
    Non-postal columns in postal_log_odds are NaN, meaning inapplicable, not zero.
    """
    previous, small_probability, batch_probability = probabilities
    below = (small_probability*ndtr((reference-small)/small_sd)
             +batch_probability*ndtr((reference-batch)/batch_sd))/positive
    mass = previous/positive*weights
    position = np.divide(uniforms-below,mass,out=np.zeros_like(uniforms),where=mass>0)
    selected = (mass>0)&(position>0)&(position<1)
    result = np.full(uniforms.shape,np.inf)
    # Evaluate only interior positions. Values outside the selected probability
    # interval must not produce invalid inverse-normal or logarithm warnings.
    normal = ndtri(np.where(selected,position,.5))
    result[selected] = (capacity*expit(small+small_sd*normal))[selected]
    if postal_log_odds is not None:
        postal = np.isfinite(postal_log_odds)&(postal_counts>0)
        # q of final votes remaining means counted*q/(1-q) additions. Bound
        # before exponentiation for numerical safety. Shared allocation can
        # supply less than this limit; the limit never creates extra capacity.
        log_counted = np.log(np.where(postal,postal_counts,1.))
        log_additions = (log_counted+postal_log_odds+POSTAL_LOCAL_SD*normal
                         +POSTAL_SHARED_SD*shared_normal[:,None])
        use = selected&postal
        result[use] = np.exp(np.minimum(log_additions,np.log(capacity)))[use]
    return result


def positive_remainder_parameters(units, progress, weights, completion_weights):
    """Select extra routine outcomes and estimate their positive remaining votes.

    At this stage, size-based completion, exact-zero and exceptional-batch
    outcomes keep their probabilities; the early guard is applied afterwards.
    For example, 50% ordinary quietness versus 90%
    gentler local support diverts 80% of the still-routine portion. Only absent
    and early-absent declarations were trialled; Postal keeps its receipt rule.
    Recent volume includes absolute rechecks and decays over 24 counting hours.
    Equal local/pooled weight and a two-day quantity are modelling assumptions.
    """
    fractions = np.zeros(len(units))
    medians = np.zeros(len(units))
    for k,(u,e) in enumerate(zip(units,progress)):
        if (u.get('vote_type',u['name']) not in ('Absent','PrePoll')
                or u['counted'] <= 0 or weights[k] >= 1):
            continue
        extra = max(0.,e.get('positive_remainder_support',0.)-weights[k])/(1-weights[k])
        fractions[k] = (1-completion_weights[k])*extra
        medians[k] = max(.25,DECLARATION_REMAINDER_DAYS*.5*(e.get('volume',0.)
            +e.get('pooled',{}).get('volume',0.)*(u['counted']+.5)))
    return fractions, medians


def positive_remainder_limits(reference, small, batch, capacity, completion_weights,
                              fractions, medians, probabilities, positive, uniforms,
                              shared_normal, small_sd=1., batch_sd=1.5):
    """Cap only newly selected routine allowances after their ordinary allocation.

    Remaining/final vote share has median odds M/C for median quantity M and
    current count C. Varying log odds gives additions M*exp(variation). A wide
    local spread allows further batches; shared variation does not average away
    across seats. Infinity leaves all other outcomes unchanged. The caller takes
    the smaller of this amount and the original allocation, without transferring
    removed voters to another category. Quiet counts can still conceal backlogs.
    """
    previous, small_probability, batch_probability = probabilities
    below = (small_probability*ndtr((reference-small)/small_sd)
             +batch_probability*ndtr((reference-batch)/batch_sd))/positive
    start = below+previous/positive*completion_weights
    mass = previous/positive*fractions
    position = np.divide(uniforms-start,mass,out=np.zeros_like(uniforms),where=mass>0)
    selected = (mass>0)&(position>0)&(position<1)
    normal = ndtri(np.where(selected,position,.5))
    local_sd = np.sqrt(DECLARATION_REMAINDER_SD**2-DECLARATION_SHARED_SD**2)
    log_addition = (np.log(np.where(medians>0,medians,1.))+local_sd*normal
                    +DECLARATION_SHARED_SD*shared_normal[:,None])
    result = np.full(uniforms.shape,np.inf)
    result[selected] = np.exp(np.minimum(log_addition,np.log(capacity)))[selected]
    return result
