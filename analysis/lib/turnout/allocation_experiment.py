"""Compare ways to release declaration allocations from aggregate targets.

This module is an offline experiment. The normal Python and C++ live models
do not call it. Every alternative preserves current votes and unfinished booth
estimates; it changes only how much of the prior declaration allowance must
still be assigned. The normal late-count layer is applied afterwards.
"""

import hashlib

import numpy as np
from scipy.special import logsumexp

from lib.turnout import allocation, late_counts, live, prior


VERSION = 'allocation-release-experiment-3'


def configurations(experiment='release'):
    """Compare two mechanisms and a small, declared assumption sensitivity.

    The additional 0.5 log-odds standard deviation is an assumption, not a fit.
    It tests whether conclusions depend on treating within-group allocation
    weights as precise. It applies only to finer splits lacking their own
    historical group, leaving supported federal category priors unchanged.
    """
    if experiment == 'directional':
        return [dict(name='current',mode='current',spread=0.,scale=None),
                dict(name='gradual_0.5',mode='gradual',spread=0.,scale=.5),
                *[dict(name='directional_0.5'+('_wider' if spread else ''),
                       mode='directional',spread=spread,scale=.5,direction_scale=.5)
                  for spread in (0.,.5)]]
    if experiment != 'release':
        raise ValueError('Unknown allocation experiment: '+experiment)
    return [dict(name='current',mode='current',spread=0.,scale=None),
            dict(name='immediate',mode='immediate',spread=0.,scale=None),
            dict(name='immediate_wider',mode='immediate',spread=.5,scale=None),
            *[dict(name=f'gradual_{scale:g}'+('_wider' if spread else ''),
                   mode='gradual',spread=spread,scale=scale)
              for spread in (0.,.5) for scale in (.3,.5)]]


def own_additions(draws, prepared, units, template, extra_spread=0., seed=20261002):
    """Recover each declaration's count-conditioned estimate before balancing.

    The broad updater uses these estimates as relative weights, then rescales
    them to exhaust an aggregate allowance. Here they can instead describe the
    amount that remains. Optional extra uncertainty varies a finer category's
    share of formal votes on log odds; no final counts select its size or spread.
    Identity-keyed draws remain fixed when a missing unit is omitted, when a
    snapshot is revisited, or when the count is revised downwards.
    """
    result = prepared['remaining'].copy()
    groups = {}
    for u in template:
        groups.setdefault((u['seat_index'],u['group_index']),[]).append(u)
    for j,u in enumerate(units):
        if u['kind'] != 'declaration' or prepared['complete'][j]:
            continue
        seat,group = u['seat_index'],u['group_index']
        parent = draws['totals'][:,seat]
        share = draws['counts'][:,seat,group]*u['weight']/parent
        finer = len(groups[seat,group]) > 1
        if extra_spread and finer:
            identity = f"{seed}/{u['seat_name']}/{u['name']}".encode()
            rng = np.random.default_rng(int.from_bytes(hashlib.sha256(identity).digest()[:8],'little'))
            share = prior.rate_percent(prior.rate_log_odds(100*share)+extra_spread*rng.normal(size=len(parent)))/100
        if u['counted']:
            parent = prepared['totals'][:,seat]
            result[:,j] = parent*live.condition_share_above(share,u['counted']/parent)-u['counted']
        else:
            result[:,j] = parent*share
    return result


# Both the experiment and the adopted updater use the same measured groups.
# Only the alternative allocation rules and optional extra spread stay here.
group_observation = allocation.group_observation
group_evidence = allocation.group_evidence


def prepare(prepared,inputs,units,own,evidence,mode,scale=None,direction_scale=.5):
    """Release the imposed allowance into unused enrolment on bounded odds.

    Immediate release uses each category's own estimate throughout. Gradual
    release interpolates from the aggregate-balanced estimate towards that
    estimate as the observed group settles. Scale 0.3 responds sooner than 0.5.
    At zero evidence it exactly retains the current account. This is a change
    in continuation estimates, not a second small-addition/completion discount.

    Directional release uses the same progress response but gives more influence
    to estimates below the imposed allowance than to estimates above it. A
    logistic weight varies smoothly with their log-odds difference; direction
    scale 0.5 is a declared experimental assumption, not a calibrated setting.

    All unfinished declarations share unused enrolment capacity. Normalising
    their odds jointly keeps the total inside enrolment while allowing the
    formal total to fall. No revised formal target is forced onto another
    category. Current votes and booth estimates remain separate and unchanged.
    """
    if mode == 'current':
        return prepared,np.zeros(len(units))
    if mode == 'directional':
        return allocation.release(prepared,inputs,units,own,evidence,scale,direction_scale)
    if mode not in ('immediate','gradual') or mode != 'immediate' and (scale is None or scale <= 0):
        raise ValueError('Use immediate release or gradual release with a positive scale.')
    release = np.zeros(len(units))
    for j,u in enumerate(units):
        if u['kind'] == 'declaration' and not prepared['complete'][j]:
            key = 'Postal' if u['group'] == 'postal' else 'allocation:'+u['group']
            strength = evidence.get((u['seat_name'],key),0.)
            release[j] = 1. if mode == 'immediate' else -np.expm1(-(strength/scale)**2)
    if not release.any():
        return prepared,release
    result = dict(prepared,remaining=prepared['remaining'].copy(),unit_counts=prepared['unit_counts'].copy(),
                  counts=prepared['counts'].copy(),totals=prepared['totals'].copy())
    for s in range(len(inputs['seat_names'])):
        columns = [j for j,u in enumerate(units) if u['seat_index'] == s and u['kind'] == 'declaration'
                   and not prepared['complete'][j]]
        if not columns or not release[columns].any():
            continue
        base = prepared['remaining'][:,columns]
        slack = inputs['enrolment'][s]-prepared['totals'][:,s]
        capacity = slack+base.sum(axis=1)
        # Epsilon handles subtraction roundoff in an estimated remainder. It
        # is not a vote floor, an invented observed zero, or an endpoint cap.
        epsilon = np.finfo(float).eps
        old_odds = np.log((base+epsilon)/slack[:,None])
        own_odds = np.log((own[:,columns]+epsilon)/slack[:,None])
        difference = own_odds-old_odds
        odds = old_odds+difference*release[columns]
        denominator = logsumexp(np.column_stack([np.zeros(len(slack)),odds]),axis=1)
        amount = capacity[:,None]*np.exp(odds-denominator[:,None])
        result['remaining'][:,columns] = amount
        result['unit_counts'][:,columns] = np.array([units[j]['counted'] for j in columns])+amount
    for s in range(len(inputs['seat_names'])):
        for g in range(len(inputs['categories'])):
            columns = [j for j,u in enumerate(units) if u['seat_index'] == s and u['group_index'] == g]
            result['counts'][:,s,g] = result['unit_counts'][:,columns].sum(axis=1)
    result['totals'] = result['counts'].sum(axis=2)
    return result,release
