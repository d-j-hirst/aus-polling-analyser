"""Replace an almost-none/large declaration split with modest ongoing counts.

Absent and early-absent declarations can keep receiving small batches after
their main processing finishes. Recent processing helps estimate those amounts,
but cannot establish the size of an unopened backlog. We therefore shrink most
routine allowances gradually, retain some unchanged, and move some tiny-addition
possibilities towards a positive amount. Counted-only and exceptional outcomes
keep their original probabilities. Postal receipt has its own treatment.

These helpers describe local additions AFTER ordinary category allocation and
postal uncertainty. Removed allowances become unused turnout. Positive modest
amounts use separately reserved unused enrolment, preserving sibling categories.
The settings were explored in Queensland 2024; they are modelling assumptions,
not a calibration established across elections or known future deliveries.
"""
import numpy as np
from scipy.special import expit, logit


CATEGORIES = ('Absent', 'PrePoll')  # PrePoll is early-absent declaration voting.
PROCESSING_DAYS = 4.
BACKLOG_FRACTION = .2
TOTAL_SD = 1.25  # Natural-log odds of remaining votes within available capacity.
SHARED_SD = .5
LOCAL_SD = np.sqrt(TOTAL_SD**2-SHARED_SD**2)


def parameters(prepared, units, columns, progress, retention):
    """Use the frozen category sizes and observed progress to prepare local terms.

    The returned arrays have one entry per open declaration component. Expected
    amount is four days of the existing decayed absolute count changes, equally
    using local volume and pooled relative volume scaled to the current count.
    It includes rechecks and is a processing proxy, not a delivery forecast.

    Category maturity compares all current counts with their ORIGINAL expected
    total. The smooth phase m²/(m²+max(0,1-m)²) is about .16/.5/.9 at 30/50/75%
    counted. It controls how strongly to use processing evidence; reaching one
    does not declare completion. History coverage and the existing early-count
    safeguard further reduce local support. Missing priors/history, unstarted
    services and other categories supply no intervention.
    """
    n = len(columns)
    support, amount, maturity = np.zeros(n), np.zeros(n), np.zeros(n)
    frozen = prepared.get('original_unit_counts')
    if frozen is None:
        return support, amount, maturity
    phases = {}
    for category in CATEGORIES:
        js = [j for j,u in enumerate(units) if u['kind']=='declaration'
              and u.get('vote_type',u['name'])==category]
        expected = float(frozen[:,js].sum(axis=1).mean()) if js else 0.
        current = sum(units[j]['counted'] for j in js)
        fraction = current/expected if expected>0 else 0.
        phases[category] = fraction**2/(fraction**2+max(0.,1-fraction)**2)
    for k,j in enumerate(columns):
        u,e = units[j],progress[j]
        category = u.get('vote_type',u['name'])
        if category not in CATEGORIES or u['counted']<=0:
            continue
        maturity[k] = phases[category]
        support[k] = e.get('coverage',0.)*e.get('started',0.)*retention[k]*maturity[k]
        if support[k]<=0:
            continue
        amount[k] = max(.25,PROCESSING_DAYS*.5*(e.get('volume',0.)
            +e.get('pooled',{}).get('volume',0.)*(u['counted']+.5)))
    return support, amount, maturity


def alternatives(original, capacity, reference, expected, support, local_normal, shared_normal):
    """Return a smaller routine amount and a modest positive alternative.

    All counts are additional formal votes. Capacity is this allocation plus
    its reserved share of unused enrolment; reference is its original routine
    allocation with sibling odds fixed. The positive target has total SD 1.25,
    including shared SD .5. Subtract half the log variance so expected is an
    approximate MEAN rather than a median followed by a much larger mean.

    Both transformations use log odds within finite capacity. The reduced
    routine amount blends gradually and never increases the original draw.
    The modest alternative never reduces its source tiny addition and cannot
    exceed the counterfactual routine allowance or its reserved physical room.
    Already counted votes are outside this calculation and remain fixed.
    """
    original, capacity, reference = np.broadcast_arrays(original,capacity,reference)
    if support<=0:
        return original.copy(),original.copy()
    safe_capacity = np.where(capacity>0,capacity,1.)
    location = logit(np.clip(original/safe_capacity,1e-15,1-1e-15))
    target = np.log(expected/safe_capacity)-TOTAL_SD**2/2+LOCAL_SD*local_normal+SHARED_SD*shared_normal
    reduced = np.minimum(original,capacity*expit(location+support*np.minimum(0.,target-location)))
    modest = np.maximum(original,np.minimum(reference,capacity*expit(target)))
    return reduced,modest


def choice_weights(point, size_finishing, processing, batch_share, support, bridge):
    """Retain backlog/batch outcomes while transferring some tiny additions.

    Eighty percent of unchanged routine outcomes use the smaller amount; twenty
    percent retain their original allowance. A bridge fraction of tiny outcomes
    uses the modest positive alternative. Existing positive processing outcomes
    and exceptional batches stay fixed. In diagnostic inverse-mixture samples,
    overlapping small/batch draws are split using their conditional densities;
    direct simulation draws know which component was selected instead.
    """
    routine = point&~size_finishing&~processing
    lower = (1-BACKLOG_FRACTION)*routine if support>0 else np.zeros_like(batch_share)
    higher = bridge*(size_finishing+(~point)*(1-batch_share))
    return lower,higher
