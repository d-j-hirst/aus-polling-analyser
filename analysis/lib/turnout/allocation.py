"""Relax excessive declaration allowances as whole-group counting settles.

The broad count update preserves historical group expectations. This step
allows those imposed allowances to recede before the separate late-batch
mixture runs. It preserves observed counts and booth estimates, uses bounded
joint shares, and gives weaker influence to larger individual category priors.
"""

import numpy as np
from scipy.special import expit, logsumexp

from lib.turnout import late_counts

MODEL_VERSION = 'live-allocation-1'
PROGRESS_SCALE = .5
DIRECTION_SCALE = .5


def group_observation(units, inputs):
    """Record exact supported group totals, including their reported booths.

    These additional history measurements distinguish a quiet whole early-vote
    group from an unstarted small declaration child. Postal retains its actual
    name so the published receipt-window discount still applies. Omitted units
    supply no invented counts; the recorded sums describe the observed account.
    """
    totals = np.zeros((len(inputs['seat_names']),len(inputs['categories'])))
    for u in units:
        totals[u['seat_index'],u['group_index']] += u['counted']
    return {name:dict(vote_types={('Postal' if group == 'postal' else 'allocation:'+group):int(totals[s,g])
                                 for g,group in enumerate(inputs['categories'])})
            for s,name in enumerate(inputs['seat_names'])}


def group_evidence(history,units,inputs,postal_deadline=None,progress_options=None):
    """Measure whole-group progress separately from child-category completion.

    History must contain the group measurements recorded at each snapshot;
    ordinary feed totals cannot reconstruct how much belonged to PPVCs. Missing
    group measurements supply no evidence. The existing smooth progress rule
    supplies the response without declaring a category finished or introducing
    another independent uncertainty source.
    """
    observed = group_observation(units,inputs)
    pseudo = [dict(seat_name=name,name=key,vote_type=key,kind='declaration',counted=value)
              for name,row in observed.items() for key,value in row['vote_types'].items()]
    subdivisions = dict(zip(inputs['seat_names'],inputs.get('subdivisions',[None]*len(inputs['seat_names']))))
    evidence = late_counts.progress_evidence(history,pseudo,subdivisions,progress_options,postal_deadline=postal_deadline)
    return {(u['seat_name'],u['vote_type']):e['strength'] for u,e in zip(pseudo,evidence)}


def release(prepared,inputs,units,own,evidence,scale=PROGRESS_SCALE,direction_scale=DIRECTION_SCALE):
    """Gradually reduce imposed allowances while limiting prior-driven increases.

    Each original category estimate has already been conditioned above its
    counted votes. Whole-group slowing evidence controls how much it can replace
    the balanced allowance. A logistic response favours decreases smoothly;
    both response scales are explicit assumptions from the offline comparison.
    All unfinished declarations share unused enrolment capacity, so reductions
    can lower the total rather than requiring compensation in another category.
    """
    if not np.isfinite(scale) or scale <= 0 or not np.isfinite(direction_scale) or direction_scale <= 0:
        raise ValueError('Allocation response scales must be finite and positive.')
    weights = np.zeros(len(units))
    for j,u in enumerate(units):
        if u['kind'] == 'declaration' and not prepared['complete'][j]:
            key = 'Postal' if u['group'] == 'postal' else 'allocation:'+u['group']
            strength = evidence.get((u['seat_name'],key),0.)
            weights[j] = -np.expm1(-(strength/scale)**2)
    if not weights.any():
        return prepared,weights
    result = dict(prepared,remaining=prepared['remaining'].copy(),unit_counts=prepared['unit_counts'].copy(),
                  counts=prepared['counts'].copy(),totals=prepared['totals'].copy())
    for s in range(len(inputs['seat_names'])):
        columns = [j for j,u in enumerate(units) if u['seat_index'] == s and u['kind'] == 'declaration'
                   and not prepared['complete'][j]]
        if not columns or not weights[columns].any():
            continue
        base = prepared['remaining'][:,columns]
        slack = inputs['enrolment'][s]-prepared['totals'][:,s]
        if (slack <= 0).any() or (base < 0).any() or (own[:,columns] < 0).any():
            raise ValueError('Allocation release needs nonnegative estimates and positive unused enrolment.')
        capacity = slack+base.sum(axis=1)
        # Epsilon represents subtraction roundoff, not an observed-zero count
        # or a substantive vote floor. Counts remain separate exact inputs.
        epsilon = np.finfo(float).eps
        old_odds = np.log((base+epsilon)/slack[:,None])
        own_odds = np.log((own[:,columns]+epsilon)/slack[:,None])
        difference = own_odds-old_odds
        direction = expit(-difference/direction_scale)
        odds = old_odds+difference*weights[columns]*direction
        denominator = logsumexp(np.column_stack([np.zeros(len(slack)),odds]),axis=1)
        amount = capacity[:,None]*np.exp(odds-denominator[:,None])
        result['remaining'][:,columns] = amount
        result['unit_counts'][:,columns] = np.array([units[j]['counted'] for j in columns])+amount
    for s in range(len(inputs['seat_names'])):
        for g in range(len(inputs['categories'])):
            columns = [j for j,u in enumerate(units) if u['seat_index'] == s and u['group_index'] == g]
            result['counts'][:,s,g] = result['unit_counts'][:,columns].sum(axis=1)
    result['totals'] = result['counts'].sum(axis=2)
    return result,weights
