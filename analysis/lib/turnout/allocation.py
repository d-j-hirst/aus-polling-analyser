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


def _available_group_units(units, inputs):
    """Map full observed groups to the progress model's service identities.

    Groups normally include all reported booths and declaration services. Keep
    wholly unavailable groups outside shared evidence, while retaining measured
    counts when an available sibling keeps a mixed group open.
    """
    observed = group_observation(units,inputs)
    available = {(u['seat_name'], 'Postal' if u['group'] == 'postal' else 'allocation:'+u['group'])
                 for u in units if not u.get('closed_reason')}
    pseudo = [dict(seat_name=name,name=key,vote_type=key,kind='declaration',counted=value)
              for name,row in observed.items() for key,value in row['vote_types'].items()
              if (name, key) in available]
    subdivisions = dict(zip(inputs['seat_names'],inputs.get('subdivisions',[None]*len(inputs['seat_names']))))
    return pseudo, subdivisions


def _index_group_strengths(pseudo, evidence):
    """Return each group's response score under its district/category identity."""
    return {(u['seat_name'],u['vote_type']):e['strength'] for u,e in zip(pseudo,evidence)}


def group_evidence(history,units,inputs,postal_deadline=None,progress_options=None):
    """Measure whole-group progress separately from child-category completion.

    History must contain the group measurements recorded at each snapshot;
    ordinary feed totals cannot reconstruct how much belonged to PPVCs. Missing
    group measurements supply no evidence. Groups with no available reporting
    services also supply no evidence, while an open child keeps a mixed group
    eligible. The existing smooth progress rule supplies the response without
    declaring a category finished or introducing another independent uncertainty
    source.
    """
    pseudo, subdivisions = _available_group_units(units, inputs)
    evidence = late_counts.progress_evidence(history, pseudo, subdivisions, progress_options,
        postal_deadline=postal_deadline)
    return _index_group_strengths(pseudo, evidence)


def _allocation_release_weights(prepared, units, evidence, scale, direction_scale):
    """Map whole-group slowing to each unfinished declaration's influence weight.

    Routine active counts retain their original allowance. Larger slowing scores
    increase the influence smoothly; completed services receive no adjustment.
    """
    if not np.isfinite(scale) or scale <= 0 or not np.isfinite(direction_scale) or direction_scale <= 0:
        raise ValueError('Allocation response scales must be finite and positive.')
    weights = np.zeros(len(units))
    for j,u in enumerate(units):
        if u['kind'] == 'declaration' and not prepared['complete'][j]:
            key = 'Postal' if u['group'] == 'postal' else 'allocation:'+u['group']
            strength = evidence.get((u['seat_name'],key),0.)
            weights[j] = -np.expm1(-(strength/scale)**2)
    return weights


def _copy_release_account(prepared):
    """Copy the count arrays this update can change, retaining the fixed origin."""
    result = dict(prepared,remaining=prepared['remaining'].copy(),unit_counts=prepared['unit_counts'].copy(),
                  counts=prepared['counts'].copy(),totals=prepared['totals'].copy())
    return result


def _release_declaration_counts(result, prepared, inputs, units, own, weights, direction_scale):
    """Reduce excessive allowances within each district's finite uncounted parent.

    Most updates retain expectations while counting continues. As evidence
    strengthens, an unfinished category can approach its own prior size without
    receiving all votes missing from the larger aggregate group.
    """
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


def _rebuild_released_counts(result, inputs, units):
    """Rebuild group and district totals from the same adjusted service counts."""
    for s in range(len(inputs['seat_names'])):
        for g in range(len(inputs['categories'])):
            columns = [j for j,u in enumerate(units) if u['seat_index'] == s and u['group_index'] == g]
            result['counts'][:,s,g] = result['unit_counts'][:,columns].sum(axis=1)
    result['totals'] = result['counts'].sum(axis=2)


def release(prepared,inputs,units,own,evidence,scale=PROGRESS_SCALE,direction_scale=DIRECTION_SCALE):
    """Gradually reduce imposed allowances while limiting prior-driven increases.

    Each original category estimate has already been conditioned above its
    counted votes. Whole-group slowing evidence controls how much it can replace
    the balanced allowance. A logistic response favours decreases smoothly;
    both response scales are explicit assumptions from the offline comparison.
    All unfinished declarations share unused enrolment capacity, so reductions
    can lower the total rather than requiring compensation in another category.
    """
    weights = _allocation_release_weights(prepared, units, evidence, scale, direction_scale)
    if not weights.any():
        return prepared, weights
    result = _copy_release_account(prepared)
    _release_declaration_counts(result, prepared, inputs, units, own, weights, direction_scale)
    _rebuild_released_counts(result, inputs, units)
    return result, weights
