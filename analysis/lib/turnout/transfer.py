"""Choose between category transfer and changed district turnout.

Both accounts preserve counted votes. Modest changes to unfinished categories
can explain a district shortfall; extreme changes instead favour accepting a
different total. This uses the current snapshot and immutable prior, not previous
predictions. The portable C++ implementation is in TurnoutModel.cpp.
"""

import numpy as np
from scipy.special import expit, logit

CATEGORY_SPREAD_FLOOR = .20
TRANSFER_PLAUSIBILITY_SPREAD = .25
TURNOUT_SPREAD_FLOOR = .15
PRIOR_ODDS = 3.
TURNOUT_DEGREES_FREEDOM = 4.
UNCOUNTED_ENROLMENT_HEADROOM = .01


def choices(key, count):
    """Stable midpoint permutation, identical to the C++ preparation sampler."""
    mask = (1 << 64)-1
    state = 14695981039346656037
    for byte in key.encode('utf-8'):
        state = ((state ^ byte)*1099511628211) & mask
    state ^= 20261025
    positions = list(range(count))
    for n in range(count, 1, -1):
        state = (state+0x9e3779b97f4a7c15) & mask
        value = state
        value = ((value ^ (value >> 30))*0xbf58476d1ce4e5b9) & mask
        value = ((value ^ (value >> 27))*0x94d049bb133111eb) & mask
        value ^= value >> 31
        k = value % n
        positions[n-1], positions[k] = positions[k], positions[n-1]
    return (np.asarray(positions)+.5)/count


def categories(work, units, seat):
    """Group unfinished services by real category, separating mixed input groups.

    No category names receive special treatment. A missing size baseline or
    explicit reason excludes transfer, without removing that service's votes.
    Unmatched booth identities alone do not identify a newly introduced category.
    """
    grouped = {}
    for j, unit in enumerate(units):
        if (unit['seat_index'] != seat or work.complete[j]
                or unit.get('transfer_exclusion_reason')):
            continue
        key = ('service/'+unit.get('vote_type', unit['name']) if unit['kind'] == 'declaration'
               else unit['kind']+'/'+str(unit['group_index']))
        grouped.setdefault(key, []).append(j)
    result = []
    for key in sorted(grouped):
        columns = grouped[key]
        original = sum(work.group_prior[:, seat, units[j]['group_index']]*units[j]['weight']
                       for j in columns)
        if not ((original > 0) & (original < work.roll[seat])).all():
            continue
        raw_spread = max(CATEGORY_SPREAD_FLOOR, float(np.std(logit(original/work.roll[seat]))))
        # Uncertain small-service sizes must not become permission to absorb
        # large transfers. This common restraint changes only the response;
        # the original size draws and their uncertainty remain intact.
        spread = max(CATEGORY_SPREAD_FLOOR, 1/np.sqrt(1/raw_spread**2+1/TRANSFER_PLAUSIBILITY_SPREAD**2))
        result.append((columns, spread))
    return result


def choose(work, inputs, units):
    """Sample a district-wide explanation in each joint preparation outcome.

    Changes are allocated once, jointly, on odds of remaining votes versus
    available electorate. A category with few remaining votes therefore needs
    a large, unlikely adjustment to absorb a substantial discrepancy. Normal
    size uncertainty allows movement; sampling both explanations preserves the
    extra uncertainty when either is plausible. Main draws reuse these rows.

    The .20 category spread floor, .15 relative-turnout spread floor and 3:1
    starting odds are conservative initial settings, not empirical estimates.
    Turnout's Student-t(4) tail allows exceptional overall size changes without
    forcing still more exceptional changes into the remaining categories.
    """
    work.transfer_probability = np.zeros((work.samples, work.seats))
    work.transfer_selected = np.zeros_like(work.transfer_probability)
    for seat in range(work.seats):
        if work.count_complete[seat] or work.seat_counted[seat] == 0:
            continue
        groups = categories(work, units, seat)
        if not groups:
            continue
        columns = work.seat_indices == seat
        own_total = work.seat_counted[seat]+work.remaining[:, columns].sum(axis=1)
        additions = np.column_stack([work.remaining[:, group].sum(axis=1) for group, _ in groups])
        eligible = additions.sum(axis=1)
        fixed = own_total-eligible
        parent = work.roll[seat]-fixed
        target = work.totals[:, seat]-fixed
        valid = ((eligible > 0) & (parent > eligible) & (target > 0) & (target < parent)
                 & (np.abs(target-eligible) >= 1e-8))
        rows = np.flatnonzero(valid)
        if not len(rows):
            continue
        amounts, capacity, wanted = additions[rows], parent[rows], target[rows]
        positive = amounts > 0
        # Absent amounts stay absent; use a harmless interior value in the
        # unused logarithms so zero-weight services cannot create NaNs.
        transformed = logit(np.where(positive, amounts/capacity[:, None], .5))
        spreads = np.array([spread for _, spread in groups])
        def proposed(shift):
            return np.where(positive, capacity[:, None]*expit(transformed+shift[:, None]*spreads**2), 0.)
        low, high = np.full(len(rows), -1024.), np.full(len(rows), 1024.)
        for _ in range(56):
            middle = (low+high)/2
            below = proposed(middle).sum(axis=1) < wanted
            low = np.where(below, middle, low)
            high = np.where(below, high, middle)
        shift = (low+high)/2
        cost = (amounts/eligible[rows, None]*(shift[:, None]*spreads)**2).sum(axis=1)
        turnout_spread = max(TURNOUT_SPREAD_FLOOR, float(np.std(np.log(work.total_prior[:, seat]))))
        turnout_error = np.log(own_total[rows]/work.totals[rows, seat])/turnout_spread
        probability = expit(np.log(PRIOR_ODDS)-.5*cost
                            +.5*(TURNOUT_DEGREES_FREEDOM+1)*np.log1p(turnout_error**2/TURNOUT_DEGREES_FREEDOM))
        work.transfer_probability[rows, seat] = probability
        key = inputs.get('election_code', '')+'/'+inputs['seat_names'][seat]+'/turnout-transfer'
        selected = choices(key, work.samples)[rows] < probability
        work.transfer_selected[rows, seat] = selected
        replacement = proposed(shift)
        for c, (group, _) in enumerate(groups):
            scale = np.divide(replacement[:, c], amounts[:, c], out=np.ones(len(rows)), where=amounts[:, c] > 0)
            for j in group:
                work.remaining[rows, j] *= np.where(selected, scale, 1.)
