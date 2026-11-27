"""Test how a previous district's Coalition ballot should inform a transferred booth.

Run this file from the repository root with the analysis Python. It reads saved
forecast inputs and later booth counts locally and prints its comparisons.
All methods use the same opening district prior and counted ballots. The trial
varies the opening booth geography and, in designated comparisons, whether
the two Coalition parties are updated together or separately from other parties.
Later counts are scoring data. Run score_relative_coalition_split.py for the
complete first-return comparison using the intervening federal feed deliveries.
"""
from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import sys

import numpy as np

from booth_fp_update import adjusted_shares, group_votes, read

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'analysis'))
from lib.live_analysis_archive import analysis_files, load_json
from score_booth_fp_update import official_results


# The two "historical total, seat split" trials start from identical ballots.
# Their only difference is how the live fit learns from subsequent returns.
SEAT_SPLIT = 'historical total, seat split'
SEAT_SPLIT_SEPARATE = 'historical total, seat split (separate fit)'
RELATIVE_SPLIT = 'historical total, relative split'
LIVE_UNIFORM = 'uniform at live seat split'
LIVE_ODDS = 'relative odds at live seat split'
LIVE_POINTS = 'relative points at live seat split'
ONE_PARTNER = 'live seat split only for one former partner'
COMBINED_TRIAL = 'former local lean when measured, live district split otherwise'
HISTORICAL_TOTAL = {'source combined', SEAT_SPLIT, SEAT_SPLIT_SEPARATE, RELATIVE_SPLIT, ONE_PARTNER}
SEPARATE_FIT = {'separate', 'source combined', SEAT_SPLIT_SEPARATE}


def maps(rows):
    return {str(r['party_index']): r['value'] for r in rows}


def key(b):
    return b['seat_name'], b['booth_type'], b['vote_type'], b['name']


@dataclass(frozen=True)
class Transfer:
    former_seat: str
    booth_nat_fraction: float
    combined_share: float
    # A factor of one leaves the receiving seat's split intact. None means
    # only one partner stood, so the former ballot never measured a choice.
    relative_nat_odds: float | None
    former_nat_fraction: float | None

    @property
    def both_stood(self):
        return self.relative_nat_odds is not None


def former_coalition_ballots(current_election):
    """Use the previous election's whole districts, including declaration votes.

    Reconstructing their totals from booths that survived redistribution could
    give the wrong reference, especially where a new district takes only a
    small rural or metropolitan part of an old seat.
    """
    previous = {'2022vic': '2018vic', '2025fed': '2022fed'}[current_election]
    return {seat: {party: votes for _, party, votes in data['fp'] if party in ('LIB', 'NAT')}
            for seat, data in official_results(previous).items()}


def historical_transfer(former_seat, votes, nat, ballot):
    """Compare the booth's choice between the two partners to its old district.

    For example, twice as many NAT votes per Liberal vote as in the former
    district means doubling the receiving district's NAT-to-LIB odds. This
    transfers a local tendency even if the two districts back quite different
    Coalition partners. A half ballot on each side allows a measured zero in
    a small booth without prescribing a zero for that party forever.
    """
    national, liberal = votes.get(nat, 0), votes.get('1', 0)
    coalition = national + liberal
    factor = former_fraction = None
    if 'LIB' in ballot and 'NAT' in ballot:
        booth_odds = (national + .5) / (liberal + .5)
        factor = booth_odds / (ballot['NAT'] / ballot['LIB'])
        former_fraction = ballot['NAT'] / (ballot['NAT'] + ballot['LIB'])
    return Transfer(former_seat, national / coalition, coalition / sum(votes.values()), factor, former_fraction)


def relative_fraction(seat_fraction, transfer):
    """Translate the old local lean to a NAT share among the new seat's Coalition voters."""
    if not transfer.both_stood:
        return seat_fraction
    national = seat_fraction * transfer.relative_nat_odds
    return national / (national + 1 - seat_fraction)


def trial_openings(run):
    """Select actual two-partner ballots and recover the historical booth identity.

    A name match alone need not be a transfer. Require the simulator's historical
    match, a different named district, and identifiable previous Coalition votes.
    Count geographically identified ordinary booths and stand-alone pre-polls.
    Keep all district vote categories for subsequent balancing.
    """
    summary = read(run / 'packages/f00000001/summary.json')
    evidence = read(run / 'packages/f00000001/evidence.json.gz')
    opening = load_json(analysis_files(run)[0])
    history = {key(b): b for b in opening['booths']}
    old_ballots = former_coalition_ballots(summary['election'])
    nat = next(p for p, label in summary['parties'].items() if label == 'NAT')
    by_seat = defaultdict(list)
    for b in evidence['booths'].values():
        by_seat[b['seat_name']].append(b)
    for seat, booths in by_seat.items():
        district = summary['seats'][seat]
        means = district['fp_mean_percent']
        if not all(p in district['candidates'] and means.get(p, 0) > 0 for p in ('1', nat)):
            continue
        if any(sum(b['fp_votes_current'].values()) for b in booths):
            raise ValueError(f'{seat} already has an opening count')
        parties = sorted((p for p in means if int(p) >= 0 and p in district['candidates'] and means[p] > 0),
                         key=int)
        prior = group_votes(means, parties, {}) / sum(means.values())
        j, k = parties.index('1'), parties.index(nat)
        active = [b for b in booths if sum(b['fp_votes_projected'].values()) > 0]
        original = np.array([group_votes(b['fp_votes_projected'], parties, {}) for b in active])
        sizes = original.sum(axis=1)
        original /= sizes[:, None]
        transfers = {}
        for i, b in enumerate(active):
            old = history[key(b)]
            if (old['same_seat'] or not old.get('previous_district') or b['vote_type'] != 'Ordinary'
                    or b['booth_type'] not in ('Normal', 'PPVC')):
                continue
            old_votes = maps(old['node']['fp_votes_previous'])
            coalition = old_votes.get(nat, 0) + old_votes.get('1', 0)
            if coalition and original[i, j] + original[i, k] > 0:
                former_seat = old['previous_district']
                ballot = old_ballots.get(former_seat)
                # A missing district or an unmeasurable two-candidate split
                # cannot establish a relative lean. Don't interpret a
                # candidate with zero recorded votes as an absent candidate.
                if not ballot or (len(ballot) == 2 and min(ballot.values()) <= 0):
                    continue
                transfers[i] = historical_transfer(former_seat, old_votes, nat, ballot)
        if transfers:
            yield seat, (active, parties, prior, original, sizes, transfers, j, k)


def split_shapes(base, method):
    """Reallocate the booth's existing combined Coalition primary between its candidates.

    The midpoint is on the proportion of Coalition voters choosing NAT. Only
    the historical proportion is clipped to 10–90%. A missing historical
    partner looks the same as zero votes under the literal midpoint. Compare
    using the destination's split alone when the historical split is missing.
    The seat-split trials use that destination split for every transfer, even
    where both Coalition candidates stood on the booth's historical ballot.
    """
    _, _, prior, original, _, transfers, lib, nat = base
    shapes = original.copy()
    seat_fraction = prior[nat] / (prior[lib] + prior[nat])
    if method == 'existing':
        return shapes
    for i, transfer in transfers.items():
        if method == ONE_PARTNER and transfer.both_stood:
            continue
        old_combined = transfer.combined_share
        coalition = shapes[i, lib] + shapes[i, nat]
        if method in HISTORICAL_TOTAL:
            # Sensitivity: the historical combined Coalition vote can be more
            # transferable than the two partners' separate totals. Other
            # parties share the difference in their opening proportions.
            # District balancing still supplies the receiving seat's FP totals.
            if coalition < 1:
                shapes[i] *= (1 - old_combined) / (1 - coalition)
            elif old_combined < 1:
                raise ValueError('No opening non-Coalition votes to apportion the historical remainder')
            coalition = old_combined
        fraction = seat_fraction
        if method == RELATIVE_SPLIT:
            # Apply the historical lean at opening. Later returns update
            # every booth through exactly the same fit as the seat-split trial.
            fraction = relative_fraction(seat_fraction, transfer)
        elif method == 'half' or (method in ('both', 'separate', 'source combined') and transfer.both_stood):
            fraction = (seat_fraction + np.clip(transfer.booth_nat_fraction, .1, .9)) / 2
        shapes[i, nat] = coalition * fraction
        shapes[i, lib] = coalition - shapes[i, nat]
    if method not in HISTORICAL_TOTAL and not np.allclose(shapes[:, lib] + shapes[:, nat],
                                                          original[:, lib] + original[:, nat]):
        raise ValueError('Changing the Coalition split altered its combined opening vote')
    return shapes


def balance(shapes, weights, targets):
    """Find the common candidate multipliers used by the live ordinary fit.

    District totals are preserved while turnout-weighted locations retain their
    relative geography. The same equations fit the new results after adding
    prior-equivalent ballots. Unsupported totals are a failed fit.
    """
    factors = np.ones(shapes.shape[1])
    for _ in range(200):
        predicted = weights @ adjusted_shares(shapes, factors)
        if np.max(np.abs(targets - predicted)) <= 1e-9 * sum(weights):
            return factors
        if np.any((predicted == 0) & (targets > 0)):
            break
        factors *= np.divide(targets, predicted, out=np.ones_like(targets), where=predicted > 0)
    raise ValueError('Opening or counted results cannot be reconciled')


def move_partner_share(shares, expected, observed, total):
    """Fit a common change in choice among Coalition voters with a bounded result.

    If NAT gains, apply the same fraction of each location's Liberal support
    to NAT. If it loses, apply the same fraction of its own support to Liberal.
    This trial assumption lets seats with a new Nationals campaign update
    quickly without multiplying an already strong Nationals booth many times.
    The expected, observed and total counts must refer to the same voters.
    """
    if observed > expected:
        return shares + (1 - shares) * (observed - expected) / (total - expected)
    if expected > 0:
        return shares * (observed / expected)
    return shares.copy()


def combine(values, lib, nat):
    pooled = values.copy()
    pooled[..., lib] += pooled[..., nat]
    return np.delete(pooled, nat, axis=-1)


def fit_coalition_separately(sources, weights, targets, destinations, lib, nat, coalition_counts=None):
    """Fit combined Coalition FP across the whole ballot, then the NAT share
    among Coalition voters. At opening the combined vote is estimated. During
    counting the reported Coalition votes supply the denominator at each source.
    Both stages include the same prior ballots and venue reliability.
    """
    pooled = combine(sources, lib, nat)
    shift = balance(pooled, weights, combine(targets, lib, nat))
    combined = adjusted_shares(combine(destinations, lib, nat), shift)
    if coalition_counts is None:
        coalition_counts = weights * adjusted_shares(pooled, shift)[:, lib]

    def national_fraction(rows):
        coalition = rows[:, lib] + rows[:, nat]
        return np.divide(rows[:, nat], coalition, out=np.zeros_like(coalition), where=coalition > 0)

    expected = coalition_counts @ national_fraction(sources)
    split = move_partner_share(national_fraction(destinations), expected, targets[nat], coalition_counts.sum())
    result = np.insert(combined, nat, 0., axis=1)
    result[:, nat] = combined[:, lib] * split
    result[:, lib] = combined[:, lib] * (1 - split)
    return result


def projections(base, shapes, booths, odds, separate=False, transfer_relevance=1.):
    """Fit the pre-count geography, then use only this capture's FP and turnout.

    The original run's opening geography stands in for the unexported blind
    pass. Match its party totals to the opening district forecast. Follow
    LiveOrdinaryUpdate.h for shared movements, ordinary/PPVC evidence, and
    stronger extrapolation to outstanding ordinary votes. This estimates the
    central shares, before draw-level uncertainty and FP/TCP reconciliation.
    """
    active, parties, prior, _, _, transfers, lib, nat = base
    places = [booths[key(b)] for b in active]
    counts = np.array([group_votes(b['fp_votes_current'], parties, b.get('count_to_projection_ids', {}))
                       for b in places])
    sizes = np.array([sum(b['fp_votes_projected'].values()) for b in places])
    n = counts.sum(axis=1)
    remaining = np.maximum(sizes - n, 0.)
    if separate:
        opening = fit_coalition_separately(shapes, sizes, prior * sizes.sum(), shapes, lib, nat)
    else:
        factors = balance(shapes, sizes, prior * sizes.sum())
        opening = adjusted_shares(shapes, factors)

    if not np.allclose(sizes @ opening / sizes.sum(), prior, atol=2e-8):
        raise ValueError('The opening booth estimates no longer match their district prior')

    def condition(mix):
        shifted = mix * odds / (1 + mix * (odds - 1))
        return shifted / shifted.sum(axis=-1, keepdims=True)

    opening, prior = condition(opening), condition(prior)
    ordinary = np.array([b['booth_type'] == 'Normal' and b['vote_type'] == 'Ordinary' for b in places])
    ppvc = np.array([b['booth_type'] == 'PPVC' and b['vote_type'] == 'Ordinary' for b in places])
    use = (ordinary | ppvc) & (n > 0)
    fitted = opening.copy()
    if np.any(use):
        reliability = np.where(ppvc, np.minimum(1., n / 500.), 1.) / (1 + n / 500.)
        reliability[list(transfers)] *= transfer_relevance
        for destination, strength in [(ordinary, 250.), (~ordinary, 1000.)]:
            strength_at_source = reliability[use] * np.where(ppvc[use], strength / 1000., 1.)
            weights = np.r_[strength, n[use] * strength_at_source]
            targets = strength * prior + strength_at_source @ counts[use]
            sources = np.vstack((prior, opening[use]))
            if separate:
                coalition_counts = np.r_[strength * (prior[lib] + prior[nat]),
                                         (counts[use, lib] + counts[use, nat]) * strength_at_source]
                fitted[destination] = fit_coalition_separately(sources, weights, targets,
                    opening[destination], lib, nat, coalition_counts)
            else:
                shift = balance(sources, weights, targets)
                fitted[destination] = adjusted_shares(opening[destination], shift)
    if not np.allclose(fitted.sum(axis=1), 1.) or np.min(fitted) < -1e-9:
        raise ValueError('Invalid fitted booth ballot')
    return fitted, remaining @ fitted / remaining.sum()


def final_results(run):
    """Use the late archived FP count only to score identically named reporting places."""
    final = load_json(analysis_files(run)[-1])
    return {key(b): maps(b['node']['fp_votes_current']) for b in final['booths']}


def live_midpoint(base, fitted, district, places):
    """A held-out booth check only. Apply the same midpoint after other booths
    have reported, using the fitted whole-district Coalition split at that time.
    Keep the booth's combined Coalition vote from the separate fit. The resulting
    local split has not been fed back into the district fit or used to price a TCP.
    """
    active, parties, _, _, _, transfers, lib, nat = base
    counted = sum((group_votes(places[key(b)]['fp_votes_current'], parties,
                              places[key(b)].get('count_to_projection_ids', {})) for b in active),
                  np.zeros_like(district))
    future = sum(sum(places[key(b)]['fp_votes_projected'].values()) for b in active) - counted.sum()
    projected = counted + max(0., future) * district
    seat_fraction = projected[nat] / (projected[lib] + projected[nat])
    result = fitted.copy()
    for i, transfer in transfers.items():
        fraction = ((seat_fraction + np.clip(transfer.booth_nat_fraction, .1, .9)) / 2
                    if transfer.both_stood else seat_fraction)
        coalition = fitted[i, lib] + fitted[i, nat]
        result[i, nat] = coalition * fraction
        result[i, lib] = coalition * (1 - fraction)
    return result


def current_seat_split(base, fitted, places):
    """Sensitivity for keeping the destination split even after live results arrive.

    Hold every counted FP and every venue's projected combined Coalition FP
    fixed. If all outstanding transferred votes divide at the whole seat's ratio,
    that ratio is determined by the counted votes plus the other venues' remaining
    votes. Adding the transferred votes at that ratio then reproduces it exactly.
    This checks the next booth benchmark without refitting earlier observations.
    """
    active, parties, _, _, _, transfers, lib, nat = base
    fixed = np.zeros(2)
    transferred = np.zeros(2)
    for i, b in enumerate(active):
        place = places[key(b)]
        votes = place['fp_votes_current']
        fixed += [votes.get(parties[lib], 0), votes.get(parties[nat], 0)]
        remaining = max(0., sum(place['fp_votes_projected'].values()) - sum(votes.values()))
        pending = remaining * fitted[i, [lib, nat]]
        if i in transfers:
            transferred += pending
        else:
            fixed += pending
    result = fitted.copy()
    # A new seat might consist entirely of transferred booths. Before any
    # votes arrive, its fitted aggregate supplies the otherwise undetermined split.
    anchor = fixed if fixed.sum() > 0 else transferred
    if anchor.sum() > 0:
        for i in transfers:
            result[i, [lib, nat]] = fitted[i, [lib, nat]].sum() * (anchor / anchor.sum())
    return result


def benchmark_against_live_seat(base, fitted, places, one_partner_only=False):
    """Test carrying the local lean directly to the current district estimate.

    Count every received vote and use the fitted ballot mix for the remainder
    to find the latest district LIB–NAT split. Hold that split and each booth's
    fitted combined Coalition FP fixed when predicting its first return. These
    alternative booth benchmarks are scored only. They do not feed back into
    the district fit or independently reproduce its totals when summed.
    """
    active, parties, _, _, _, transfers, lib, nat = base
    projected = np.zeros(2)
    for i, booth in enumerate(active):
        place = places[key(booth)]
        counts = place['fp_votes_current']
        remaining = max(0., sum(place['fp_votes_projected'].values()) - sum(counts.values()))
        projected += [counts.get(parties[lib], 0), counts.get(parties[nat], 0)]
        projected += remaining * fitted[i, [lib, nat]]
    seat = projected[1] / projected.sum()
    uniform, odds, points = fitted.copy(), fitted.copy(), fitted.copy()
    for i, transfer in transfers.items():
        if one_partner_only and transfer.both_stood:
            continue
        combined = fitted[i, lib] + fitted[i, nat]
        # A fixed difference in percentage points is a second interpretation
        # of the same local lean. Clip to a possible vote share for this
        # snapshot only; later snapshots can move away from either endpoint.
        difference = transfer.booth_nat_fraction - transfer.former_nat_fraction if transfer.both_stood else 0.
        # The uniform control uses the exact same fitted totals and seat
        # estimate, isolating the effect of the local lean at prediction time.
        for result, fraction in ((uniform, seat), (odds, relative_fraction(seat, transfer)),
                                 (points, np.clip(seat + difference, 0., 1.))):
            result[i, [lib, nat]] = combined * np.array([1 - fraction, fraction])
    return {LIVE_UNIFORM: uniform, LIVE_ODDS: odds, LIVE_POINTS: points}


def assess_opening(base, truth, methods):
    """Measure errors per 100 FP ballots, weighting each location by its eventual FP count."""
    active, parties, _, _, _, transfers, lib, nat = base
    records = []
    places = {key(b): b for b in active}
    for method in methods:
        shape = split_shapes(base, method)
        balanced, _ = projections(base, shape, places, np.ones(len(parties) + 1),
                                  method in SEPARATE_FIT)
        for i, b in enumerate(active):
            actual = truth.get(key(b), {})
            n = sum(actual.values())
            if n == 0 or b['booth_type'] not in ('Normal', 'PPVC') or b['vote_type'] != 'Ordinary':
                continue
            for stage, mix in [('opening', shape), ('balanced', balanced)]:
                nat_error = 100 * abs(mix[i, nat] - actual.get(parties[nat], 0) / n)
                lib_error = 100 * abs(mix[i, lib] - actual.get(parties[lib], 0) / n)
                records.append((method, stage, i in transfers, n, nat_error, lib_error, b['name']))
    return records


def bendigo(run, base, methods):
    """Compare the two consecutive fits against the saved simulator diagnostics."""
    from live_pipeline import booth_tcp, comparisons

    active, parties, _, _, _, _, _, nat = base
    pyalong = next(i for i, b in enumerate(active) if b['name'] == 'Pyalong')
    for capture in ('f00000033', 'f00000034'):
        package = run / 'packages' / capture
        evidence = comparisons.comparison_evidence(package)
        summary = read(package / 'summary.json')
        keys = {key(b): k for k, b in evidence['booths'].items() if b['seat_name'] == 'Bendigo'}
        places = {key(b): b for b in evidence['booths'].values() if b['seat_name'] == 'Bendigo'}
        fit = evidence['seats']['Bendigo']['fp_integration']
        shared = maps(fit['shared_deviation'])
        odds = np.exp(np.array([shared.get(p, 0) for p in parties] + [0.]) / 25.)
        exported = next(r['fitted_remaining_share'] for r in fit['ordinary_comparisons']
                        if str(r['party_index']) == parties[nat])
        print(capture, 'archived remaining NAT', round(100 * exported, 2))
        for method in methods:
            shape = split_shapes(base, method)
            fitted, district = projections(base, shape, places, odds, method in SEPARATE_FIT)
            print(method, 'opening NAT', round(100 * shape[pyalong, nat], 2),
                  'pre-return ALP/LIB/NAT' if capture.endswith('33') else 'counted',
                  np.round(100 * fitted[pyalong, [parties.index('0'), parties.index('1'), nat]], 2)
                  if capture.endswith('33') else '', 'remaining ALP/LIB/NAT',
                  np.round(100 * district[[parties.index('0'), parties.index('1'), nat]], 2))
            if method in ('separate', 'source combined') and capture.endswith('33'):
                location = live_midpoint(base, fitted, district, places)[pyalong]
                print('midpoint using current district split, pre-return ALP/LIB/NAT',
                      np.round(100 * location[[parties.index('0'), parties.index('1'), nat]], 2))
            if method in (SEAT_SPLIT, SEAT_SPLIT_SEPARATE) and capture.endswith('33'):
                location = current_seat_split(base, fitted, places)[pyalong]
                print('using current district split, pre-return ALP/LIB/NAT',
                      np.round(100 * location[[parties.index('0'), parties.index('1'), nat]], 2))
            if capture.endswith('33'):
                # Exercise the actual Highlights guard while holding the archived
                # district TCP median and preference geography fixed. Refit the
                # booth TCP to the alternative FP inputs using the normal writer.
                replacement = dict(evidence['fit_remaining_fp_percent'])
                for b, mix in zip(active, fitted):
                    k = keys[key(b)]
                    if k in replacement:
                        replacement[k] = dict(zip(parties, 100 * mix))
                revised = dict(evidence, fit_remaining_fp_percent=replacement)
                fp = comparisons.booth_fp.expectations(summary, revised, {'Bendigo'})
                pair = ('0', '1')
                wanted = {'Bendigo': {frozenset(pair)}}
                tcp = booth_tcp.expectations(summary, revised, wanted, set(), fp)
                k = keys[key(active[pyalong])]
                bench = tcp[k, frozenset(pair)]
                rates = evidence['classic_benchmark_labor_rates']['Bendigo']['0/1']
                guard = booth_tcp.requires_dominant_donor_reversal(pair, bench, fp[k],
                                                                  rates)
                # Give Labor every ballot other than LIB and NAT. Whatever
                # shortfall remains must be made up from the Nationals, even
                # if their FP share is under the writer's majority threshold.
                mass = sum(fp[k].values())
                labor_share = bench['0'] / sum(bench.values())
                national_votes = fp[k][parties[nat]]
                minimum = max(0., labor_share * mass - (mass - fp[k]['1'] - national_votes))
                print('  Highlights: ALP-LIB booth benchmark', round(100 * labor_share, 2),
                      'suppressed' if guard else 'available', 'minimum NAT to ALP',
                      round(100 * minimum / national_votes, 2))
        _, discounted = projections(base, split_shapes(base, 'half'), places, odds, transfer_relevance=.1)
        print('half with 10% relevance for transferred returns, remaining NAT', round(100 * discounted[nat], 2))


def count_series(run, districts, later_run=None):
    """Read counts in publication order. Supplement the short federal replay
    with election-night captures from the older archive to cover Western Australia.
    Only FP counts are borrowed from that archive. Use the trial's opening
    geography, with zero shared movement and opening turnout for unreported
    places there. All methods face exactly the same information at each step.
    """
    election_day = None
    latest = ''
    for package in sorted((run / 'packages').glob('f[0-9]*')):
        summary = read(package / 'summary.json')
        # Freeze the short federal trial at Pyalong's first return so that
        # continuing the private replay does not change this comparison.
        if later_run is not None and summary['source_time'] > '2025-05-03T19:07:17':
            break
        date, time = summary['source_time'].split('T')
        if election_day is None:
            election_day = date
        if date != election_day and time[:2] >= '02':
            break
        latest = summary['source_time']
        ev = read(package / 'evidence.json.gz')
        yield latest, {key(b): b for b in ev['booths'].values() if b['seat_name'] in districts}, {
            seat: maps(ev['seats'][seat]['fp_integration']['shared_deviation']) for seat in districts}
    if later_run is None:
        return
    for sidecar in analysis_files(later_run):
        stamp = sidecar.name.split('_')[1]
        if stamp <= latest.translate(str.maketrans('', '', '-T:')) or stamp > '20250504020000':
            continue
        data = load_json(sidecar)
        places = {}
        for b in data['booths']:
            seat = b['seat_name']
            if seat not in districts:
                continue
            initial = districts[seat][0]
            if key(b) not in initial:
                continue
            start = initial[key(b)]
            counts = maps(b['node']['fp_votes_current'])
            counted = sum(counts.values())
            size = max(counted, sum(start['fp_votes_projected'].values()))
            if b['booth_type'] in ('Normal', 'PPVC') and b['vote_type'] == 'Ordinary' and counted:
                size = counted
            places[key(b)] = {**start, 'fp_votes_current': counts, 'fp_votes_projected': {'total': size}}
        yield stamp, places, {seat: {} for seat in districts}


def first_return_predictions(districts, methods, series, transfers_only=False, live_benchmarks=False):
    """Predict a booth from the preceding capture, then reveal its first count.

    Booths arriving together all use the same prior capture. The main trial
    also checks spillover to other booths. A transfer-only check can stop
    reading the archive once every eligible location has returned.
    """
    seen = set()
    wanted = {key(base[0][i]) for _, base in districts.values() for i in base[5]}
    previous = None
    for time, places, shared in series:
        if previous is not None:
            before, movements = previous
            for seat, (start, base) in districts.items():
                active, parties, _, _, _, transfers, lib, nat = base
                arrivals = []
                for i, b in enumerate(active):
                    k = key(b)
                    if transfers_only and k not in wanted:
                        continue
                    if (k not in places or k in seen or b['booth_type'] not in ('Normal', 'PPVC')
                            or b['vote_type'] != 'Ordinary'):
                        continue
                    actual = places[k]['fp_votes_current']
                    if sum(actual.values()):
                        seen.add(k)
                        if k in before and not sum(before[k]['fp_votes_current'].values()):
                            arrivals.append((i, actual))
                if not arrivals:
                    continue
                if not start.keys() <= before.keys():
                    raise ValueError(f'{seat}: missing reference booths at {time}')
                odds = np.exp(np.array([movements[seat].get(p, 0) for p in parties] + [0.]) / 25.)
                estimates = {}
                for method in methods:
                    predicted, district = projections(base, split_shapes(base, method), before, odds,
                                                      method in SEPARATE_FIT)
                    estimates[method] = predicted
                    if method == ONE_PARTNER:
                        estimates[method] = benchmark_against_live_seat(
                            base, predicted, before, one_partner_only=True)[LIVE_UNIFORM]
                    if live_benchmarks and method == RELATIVE_SPLIT:
                        estimates.update(benchmark_against_live_seat(base, predicted, before))
                        # The simulator trial carries a measured local lean at
                        # opening. Only former single-partner ballots take the
                        # district split again once there are live counts.
                        estimates[COMBINED_TRIAL] = benchmark_against_live_seat(
                            base, predicted, before, one_partner_only=True)[LIVE_UNIFORM]
                    if method in ('separate', 'source combined'):
                        label = 'live midpoint' if method == 'separate' else 'source combined + live midpoint'
                        estimates[label] = live_midpoint(base, predicted, district, before)
                    if method in (SEAT_SPLIT, SEAT_SPLIT_SEPARATE):
                        estimates[method + ' kept live'] = current_seat_split(base, predicted, before)
                for method, predicted in estimates.items():
                    for i, actual in arrivals:
                        size = sum(actual.values())
                        observed = np.array([actual.get(parties[p], 0) for p in (lib, nat)]) / size
                        yield seat, i, time, method, size, 100 * predicted[i, [lib, nat]], 100 * observed
        if transfers_only and seen == wanted:
            return
        previous = places, shared


def assess_arrivals(run, bases, methods, later_run):
    """Compare absolute FP errors on the original sequence of archived forecasts."""
    districts = {seat: ({key(b): b for b in base[0]}, base) for seat, base in bases.items()}
    scores = defaultdict(list)
    for seat, i, _, method, size, predicted, observed in first_return_predictions(
            districts, methods, count_series(run, districts, later_run)):
        scores[seat, i in bases[seat][5], method].append((size, *np.abs(predicted - observed)))
    for seat in [*bases, 'All seats']:
        for transfer in (True, False):
            line = []
            for method in (*methods, 'live midpoint', 'source combined + live midpoint',
                           SEAT_SPLIT + ' kept live', SEAT_SPLIT_SEPARATE + ' kept live'):
                rows = [row for (s, t, m), group in scores.items()
                        if (s == seat or seat == 'All seats') and t == transfer and m == method for row in group]
                if rows:
                    weights, lib_error, nat_error = np.array(rows).T
                    line.append(f'{method} {np.average(nat_error, weights=weights):.2f}/'
                                f'{np.average(lib_error, weights=weights):.2f}')
            if line:
                print('First returns', seat, 'transferred' if transfer else 'other', len(rows),
                      int(weights.sum()), 'votes, NAT/LIB absolute FP error (pp)', *line)


def main():
    methods = ('existing', 'half', 'both', 'seat', 'separate', 'source combined', SEAT_SPLIT,
               SEAT_SPLIT_SEPARATE, RELATIVE_SPLIT)
    for start, end in [('2025fed-2-prelim', '2025fed-preferences11'), ('2022vic-public', '2022vic-public')]:
        run = ROOT / 'live_runs' / start
        truth = final_results(ROOT / 'live_runs' / end)
        bases = dict(trial_openings(run))
        for seat, base in bases.items():
            if seat == 'Bendigo':
                bendigo(run, base, methods)
            rows = assess_opening(base, truth, methods)
            for transfer in (True, False):
                sample = [r for r in rows if r[1] == 'balanced' and r[2] == transfer]
                if sample:
                    print(start, seat, 'transferred' if transfer else 'other', len(sample) // len(methods),
                          'NAT/LIB absolute FP error (pp)', end=' ')
                    for method in methods:
                        group = [r for r in sample if r[0] == method]
                        errors = np.average([r[4:6] for r in group], axis=0, weights=[r[3] for r in group])
                        print(method, '/'.join(f'{v:.2f}' for v in errors), end='  ')
                    print()
        assess_arrivals(run, bases, methods, ROOT / 'live_runs' / end if run.name.startswith('2025') else None)


if __name__ == '__main__':
    main()
