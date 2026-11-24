"""Experimental transfer of ordinary FP evidence to still-uncounted voters.

The control is the replay's split fit. Two independent alternatives ask whether
to move faster at other ordinary booths and whether to transfer less of a
geographically concentrated shift to unobserved vote types. All four versions
use the same capture, and keep its counted ballots in full.
"""
import numpy as np

from scripts.exploratory.booth_fp_update import adjusted_shares, fit_shift


def middle(values, weights):
    order = np.argsort(values)
    cumulative = np.cumsum(weights[order])
    half = cumulative[-1] / 2
    ends = np.searchsorted(cumulative, half, side='left'), np.searchsorted(cumulative, half, side='right')
    return np.mean(values[order[list(ends)]])


def without_two(values, weights):
    """Omit the two biggest signed excesses, sharing an exact tie symmetrically."""
    contributions = values * weights
    boundary = np.partition(contributions, -2)[-2]
    above, tied = contributions > boundary, contributions == boundary
    remaining = np.where(above, 0., weights)
    remaining[tied] *= 1 - (2 - above.sum()) / tied.sum()
    return np.average(values, weights=remaining)


def agreement(deviations, weights, halves, reversal_only):
    """Ask how much of the mean FP surprise survives three different checks.

    Work in share differences against the frozen opening for the same booths.
    Orient gains and losses alike. One diagnostic omits the two largest excess
    ballot contributions. Another finds the middle ballot's booth. The third
    separates places with relatively high and low opening support. The opening
    strength is a proxy for coverage, not a classification of voters' homes.
    """
    mean = np.average(deviations, weights=weights)
    if abs(mean) < 1e-9:
        return mean, 0., 0.
    directed = deviations * np.sign(mean)
    central, omitted = middle(directed, weights), without_two(directed, weights)
    checks = [central, omitted]
    groups = [side for side in (halves, ~halves) if side.sum() >= 2]
    for subset in groups:
        checks.append(np.average(directed[subset], weights=weights[subset]))
    retention = min(checks) / abs(mean)
    # Half the original difference surviving every check supplies full
    # corroboration. Missing coverage cannot by itself be evidence of disagreement.
    support = np.clip(2 * retention, 0, 1) if len(groups) == 2 else 0.
    disagreement = np.clip(1 - 2 * retention, 0, 1)
    if reversal_only:
        # A narrower sensitivity requires two descriptions of the same
        # booths to reverse the mean. Both use the same ballots.
        disagreement = np.clip(-max(central, omitted) / (.25 * abs(mean)), 0, 1)
    return mean, support, disagreement


def candidate_weights(parties, opening, counts, ordinary, outstanding, pair_chances, reversal_only=False):
    """Return candidate-specific trial strengths using evidence available now.

    A faster update requires the focal FP movement and a change in the gap to
    its most likely opponent in the same direction. This counts the rival's
    votes as a cross-check on which shift matters in the race. It does not
    enter them a second time as ballots. Anonymous Others keep their usual fit.
    """
    sizes = counts.sum(axis=1)
    use = ordinary & (sizes > 0)
    fast = np.zeros(len(parties) + 1)
    caution = np.zeros_like(fast)
    if use.sum() < 3:
        return fast, caution
    w = sizes[use]
    departures = counts[use] / w[:, None] - opening[use]
    pairs = sorted(pair_chances, key=lambda p: (-pair_chances[p], p))
    breadth = min(1., (len(w) - 2) / 4)
    for j, party in enumerate(parties):
        cut = middle(opening[ordinary, j], (sizes + outstanding)[ordinary])
        halves = opening[use, j] >= cut
        change, support, disagreement = agreement(departures[:, j], w, halves, reversal_only)
        # The chosen ramps are trial assumptions. A 5 pp disputed shift gets
        # full consideration for restraint. For faster ordinary updating,
        # graduate from no response at 4 pp to full consideration at 10 pp.
        caution[j] = breadth * min(1., abs(change) / .05) * disagreement
        rival = next((other for pair in pairs if party in pair.split('/')
                      for other in pair.split('/') if other != party and other in parties), None)
        if not rival or not support:
            continue
        gap = (departures[:, j] - departures[:, parties.index(rival)]) * np.sign(change)
        surviving_gap = min(without_two(gap, w), middle(gap, w))
        fast[j] = (breadth * support * np.clip((abs(change) - .04) / .06, 0, 1)
                   * np.clip(surviving_gap / max(abs(change) / 2, .02), 0, 1))
    return fast, caution


def blend(first, second, strength):
    """Interpolate the positive candidate shares, then restore the ballot total."""
    log_ratio = np.zeros_like(first)
    np.log(np.divide(second, first, out=np.ones_like(first), where=first > 0),
           out=log_ratio, where=second > 0)
    mix = first * np.exp(log_ratio * strength)
    return mix / mix.sum(axis=1, keepdims=True)


def changes(booths, opening, shapes, counts, ordinary, remaining, prior, parties, pairs, reversal_only=False):
    """Return district FP changes in pp for the two switches separately.

    Trial A reduces the ordinary starting weight from 250 towards 62.5
    effective voters for a corroborated shift. Trial B removes up to three
    quarters of the *local* correction to the 1,000-vote fit in vote types
    with no FP yet. The statewide and regional background remains available.
    Already-counted ballots are identical across all variants. Different
    locations have different remaining turnout, so the two effects add at
    district level before the production simulator's final normalisation.
    """
    sizes = counts.sum(axis=1)
    use = ordinary & (sizes > 0)
    n = len(parties) + 1
    fast, caution = candidate_weights(parties, opening, counts, ordinary, remaining, pairs, reversal_only)
    effects = np.zeros((2, n))
    control = np.zeros_like(shapes)
    alternatives = np.zeros_like(shapes)
    total = sizes.sum() + remaining.sum()
    types = [(b['vote_type'], b['booth_type'] if b['vote_type'] == 'Ordinary' else '') for b in booths]
    reported = {t for t, size in zip(types, sizes) if size}
    unobserved = ~ordinary & np.array([t not in reported for t in types])
    for row, group, weights, usual_strength in (
            (0, ordinary, fast, 250.), (1, ~ordinary, .75 * caution, 1000.)):
        if not np.any(group):
            continue
        control[group] = adjusted_shares(shapes[group], fit_shift(shapes[use], counts[use], prior, usual_strength, 500))
        subset = group if row == 0 else unobserved
        if not weights.any() or not remaining[subset].sum():
            continue
        base = control[subset]
        alternative = (adjusted_shares(shapes[subset], fit_shift(shapes[use], counts[use], prior, 62.5, 500))
                       if row == 0 else shapes[subset])
        moved = blend(base, alternative, weights)
        alternatives[subset] = moved - base
        effects[row] = 100 * remaining[subset] @ alternatives[subset] / total
    return effects, control, alternatives
