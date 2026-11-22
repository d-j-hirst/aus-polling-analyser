"""Translate the tested differences into district-preserving 2026 EVC examples.

Actual early-vote party shares for 2026 are unknown. These examples use an
even TPP and each district's 2022 named-EVC primary mix. The same functions
take a district's own expected shares and centre weights at forecast time.
"""

from collections import defaultdict

import numpy as np

from evidence import CHOSEN_WINDOW, PRIMARY, STRENGTH, counts, district_rows, write_csv


def balanced_shares(local_counts, supported, weights, target, strength):
    """Allocate one district composition across venues, conserving every group total.

    Raise each local group's share to the measured strength. Common multipliers
    for the groups then move all centres together until their volume-weighted
    mean equals the district target. These multipliers preserve the relative
    odds between any two centres. Rows without a comparison take exactly the
    district mix, and a group with zero district share receives zero everywhere.
    """
    target = np.asarray(target, float)
    weights = np.asarray(weights, float)
    supported = np.asarray(supported, bool)
    local_counts = np.asarray(local_counts, float)
    if (not np.all(np.isfinite(target)) or np.any(target < 0) or not np.isclose(target.sum(), 1, rtol=0, atol=1e-12)
            or not np.all(np.isfinite(weights)) or np.any(weights <= 0)):
        raise ValueError('Expected a district composition summing to one and positive centre weights')
    result = np.tile(target, (len(weights), 1))
    active = target > 0
    if not supported.any() or active.sum() == 1:
        return result
    evidence = local_counts[np.ix_(supported, active)]
    if not np.isfinite(evidence).all() or np.any(evidence < 0):
        raise ValueError('Supported centres need valid nonnegative counts')
    # A row-wide scale cancels when shares are normalised. Counts plus half
    # a ballot therefore give the same local ratios as the historical test.
    adjusted = (evidence + 0.5) ** strength
    for _ in range(1000):
        adjusted /= adjusted.sum(axis=1, keepdims=True)
        average = np.average(adjusted, weights=weights[supported], axis=0)
        if np.max(abs(average - target[active])) < 1e-12:
            result[np.ix_(supported, active)] = adjusted
            return result
        adjusted *= target[active] / average
    raise ValueError('Could not reproduce the supplied district shares')


def primary_shares(local, supported, weights, target, strength):
    """Hold the combined Other primary share fixed and allocate the other three groups."""
    target = np.asarray(target, float)
    mass = target[:3].sum()
    result = np.tile(target, (len(weights), 1))
    if mass:
        result[:, :3] = mass * balanced_shares(local[:, :3], supported, weights, target[:3] / mass, strength)
    return result


def apply_to_roster(rows, state_history, tpp_strength, destination):
    """Record centre offsets, coverage and weight sensitivity for every 2026 district."""
    # Only the named historical EVCs supply this demonstration target. Mixing
    # in residual early or mobile ballots would change the electoral quantity.
    previous = defaultdict(lambda: np.zeros(4))
    for r in state_history:
        previous[r['district']] += counts(r)[0][1:]
    centres, districts = [], []
    for seat, sites in sorted(district_rows(rows).items()):
        weights = np.array([float(s['size_weight']) for s in sites])
        equal = np.ones(len(sites))
        local, denominator = (np.stack(v) for v in zip(*(counts(s, f'n{CHOSEN_WINDOW}_') for s in sites)))
        fp_ok = (denominator[:, 1] > 0) & np.array([int(s['available']) >= CHOSEN_WINDOW for s in sites])
        tpp_ok = fp_ok & (denominator[:, 0] > 0)
        tpp_counts = np.column_stack([local[:, 0], denominator[:, 0] - local[:, 0]])
        tpp = balanced_shares(tpp_counts, tpp_ok, weights, [0.5, 0.5], tpp_strength)[:, 0]
        equal_tpp = balanced_shares(tpp_counts, tpp_ok, equal, [0.5, 0.5], tpp_strength)[:, 0]
        historical = previous[seat]
        target = historical / historical.sum() if historical.sum() else None
        # Without a district FP comparison, a neutral location offset can be
        # specified for any forecast target, even though its level is unknown.
        shifts = equal_shifts = np.zeros((len(sites), 4))
        if target is not None:
            shifts = primary_shares(local[:, 1:], fp_ok, weights, target, STRENGTH) - target
            equal_shifts = primary_shares(local[:, 1:], fp_ok, equal, target, STRENGTH) - target
        if (not np.allclose(np.average(shifts, weights=weights, axis=0), 0, atol=1e-10)
                or abs(np.average(tpp, weights=weights) - 0.5) > 1e-10):
            raise ValueError(f'Local adjustments change the expected district vote in {seat}')
        for i, s in enumerate(sites):
            result = dict(district=seat, centre=s['centre'], suburb=s['suburb'], role=s['role'],
                          vec_estimated_votes=int(weights[i]), fraction_of_district_venue_weights=round(weights[i]/weights.sum(), 6),
                          fp_evidence='local' if fp_ok[i] and target is not None else 'district fallback',
                          tpp_evidence='local' if tpp_ok[i] else 'district fallback',
                          local_labor_tpp_pct=round(100 * local[i, 0] / denominator[i, 0], 3) if tpp_ok[i] else '',
                          labor_tpp_delta_at_50_pp=round(100 * (tpp[i] - 0.5), 4),
                          equal_weight_tpp_delta_at_50_pp=round(100 * (equal_tpp[i] - 0.5), 4))
            result.update((f'{party}_fp_delta_pp', round(100 * shifts[i, j], 4)) for j, party in enumerate(PRIMARY))
            result.update(nearest_five=s['nearest_five'], fifth_distance_km=s['fifth_km'],
                          nearest_prior_evc_km=s['nearest_2022_evc_km'])
            centres.append(result)
        result = dict(district=seat, centres=len(sites), local_fp_centres=sum(fp_ok), direct_tpp_centres=sum(tpp_ok),
                      vec_estimated_votes=int(weights.sum()), tpp_range_at_50_pp=round(100 * np.ptp(tpp), 3),
                      max_tpp_change_equal_weights_pp=round(100 * max(abs(tpp-equal_tpp)), 3),
                      max_fp_change_equal_weights_pp=round(100 * np.max(abs(shifts-equal_shifts)), 3))
        result.update((f'{party}_example_target_pct', round(100 * target[j], 4) if target is not None else '')
                      for j, party in enumerate(PRIMARY))
        districts.append(result)
    write_csv(destination / 'operator' / '2026-centres.csv', centres)
    write_csv(destination / '2026-districts.csv', districts)
