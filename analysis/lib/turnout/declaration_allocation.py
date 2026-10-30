"""Allocate declarations using the postal amounts that each scenario retains.

Observed votes and physical-booth forecasts are outside this account. Additional
declarations share finite enrolment with an unused portion representing electors
who contribute no formal vote. A selected postal finishing remainder must limit
its allocation before other categories compete with it. Non-postal completion
rules and mean-preserving postal widening remain separate downstream steps.
"""
import numpy as np
from scipy.special import logsumexp


def allocate(parent, log_odds, postal_limits):
    """Return additional votes for each simulated outcome and local category.

    parent is a vector of available people (declarations plus unused capacity).
    The matrices have one row per outcome and one column per category. log_odds
    are natural logs of allocation weights relative to unused capacity, whose
    weight is one. postal_limits are sampled remainders in votes; infinity is
    unconstrained. They describe possibilities, not known true final counts.

    Normal allocation retains the existing relative weights. If a postal limit
    binds, reserve that amount and divide the remainder in the other categories'
    and unused capacity's existing proportions. For example, reserving 100 rather
    than 2,000 postals releases space to both unused turnout and other categories;
    those other categories no longer compete against the discarded 1,900 votes.
    Each repeat fixes at least one further constrained category in affected rows.
    """
    parent = np.asarray(parent, dtype=float)
    log_odds = np.asarray(log_odds, dtype=float)
    postal_limits = np.broadcast_to(postal_limits, log_odds.shape)
    remaining = parent.copy()
    active = np.ones(log_odds.shape, dtype=bool)
    result = np.zeros(log_odds.shape)
    for _ in range(log_odds.shape[1]+1):
        # Recalculate the stable scale after discarding a large postal weight;
        # otherwise the surviving weights can underflow merely because it existed.
        weights = np.where(active, log_odds, -np.inf)
        denominator = logsumexp(np.column_stack([np.zeros(len(parent)), weights]), axis=1)
        proposed = remaining[:, None]*np.exp(weights-denominator[:, None])
        binding = active & (proposed > postal_limits)
        if not binding.any():
            return result+proposed
        fixed = np.where(binding, postal_limits, 0.)
        result += fixed
        remaining = np.maximum(0., remaining-fixed.sum(axis=1))
        active[binding] = False
    raise RuntimeError('Postal allocation did not resolve its finite set of limits.')
