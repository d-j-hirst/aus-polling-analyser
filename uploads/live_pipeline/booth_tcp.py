"""Allocate a published district TCP median to booths for the same two candidates.

This is a central benchmark conditional on those two making the final count.
It is used in commentary, not to generate the forecast. Each location retains
its projected TCP turnout, which can differ from FP turnout with exhaustion.
"""
from collections import Counter, defaultdict
import math

from .booth_fp import balance_outstanding
from .count_context import mapped_tcp


def split(booth, pair, coalition):
    """A direct projection names its opponents. A TPP projection labels the
    Coalition side '1' even for Nationals. Only use it when the district
    ballot identifies a single Coalition partner facing Labor."""
    direct = booth.get('tcp_votes_projected') or {}
    if direct.keys() == set(pair) and sum(direct.values()) > 0:
        return direct
    classic = booth.get('tpp_votes_projected') or {}
    if ('0' in pair and coalition == set(pair)-{'0'} and classic.keys() == {'0', '1'}
            and sum(classic.values()) > 0):
        return {p: classic['0' if p == '0' else '1'] for p in pair}
    return None


def fit(booths, opening, pair, target, coalition, bounds):
    """Keep measured same-pair TCP fixed. At FP-only booths the two finalists
    already own their primary votes. Scale the votes still assignable until
    the turnout-weighted share equals the district median for the first
    candidate. A missing booth or an impossible target prevents the comparison."""
    fixed, remaining = {}, {}
    turnout = 0.
    for key, booth in booths.items():
        measured = mapped_tcp(booth) or {}
        fp = booth.get('fp_votes_current', {})
        positive_tcp = any(booth.get('tcp_votes_current', {}).values())
        projected = split(booth, pair, coalition)
        if not projected:
            if positive_tcp or any(fp.values()) or key in bounds or 'fp_votes_projected' not in booth or any(
                    any(booth.get(field, {}).values()) for field in
                    ('fp_votes_projected', 'tcp_votes_projected', 'tpp_votes_projected')):
                return {}
            continue
        # Apply the simulator's own lower bounds when a recheck made part
        # of an old TCP uncertain. Counts for different opponents supply
        # no floor for this contest.
        aliases = booth.get('count_to_projection_ids', {})
        saved_floor = None
        for entry in bounds.get(key, ()):
            mapped = {aliases.get(p, p): v for p, v in entry.items()}
            if mapped.keys() == set(pair):
                saved_floor = mapped
        if positive_tcp and not measured:
            return {}
        if any(not math.isfinite(v) or v < 0 for v in projected.values()):
            return {}
        size = sum(projected.values())
        floor = Counter()
        if saved_floor is not None:
            floor.update(saved_floor)
        elif positive_tcp and measured.keys() == set(pair):
            floor.update(measured)
        if sum(floor.values()) < size-.01:
            primary = Counter()
            for raw, votes in fp.items():
                p = aliases.get(raw, raw)
                if p in pair:
                    primary[p] += votes
            for p in primary:
                floor[p] = max(floor[p], primary[p])
        if any(v < 0 or not math.isfinite(v) for v in floor.values()):
            return {}
        free = size-sum(floor.values())
        if free < -.01:
            return {}
        fixed[key] = {p: floor[p] for p in pair}
        turnout += size
        if free <= .01:
            continue
        seed = projected
        if not positive_tcp and saved_floor is None and not any(fp.values()):
            # With no booth count yet, preserve its opening relative strength.
            # Once primaries arrive, the reference model's current preference
            # estimate contains information that the opening estimate lacks.
            first = opening.get(key, {})
            if first.get('fp_votes_projected', {}).keys() == booth.get('fp_votes_projected', {}).keys():
                seed = split(first, pair, coalition) or projected
        pool = {p: max(0., seed[p]-floor[p]) for p in pair}
        mass = sum(pool.values())
        if not mass:
            return {}
        remaining[key] = free, {p: v*free/mass for p, v in pool.items()}
    goals = {p: turnout*share/100-sum(row[p] for row in fixed.values())
             for p, share in zip(pair, (target, 100-target))}
    if min(goals.values()) < -.01 or not balance_outstanding(remaining, {p:max(0., v) for p,v in goals.items()}):
        return {}
    for key, (_, votes) in remaining.items():
        for p in pair:
            fixed[key][p] += votes[p]
    return fixed


def expectations(summary, evidence, wanted, ranked):
    """Fit only pairs needed in this window. Ranked allocations for ballots
    awaiting preferences have a separate same-ballot comparison."""
    by_seat = defaultdict(dict)
    for key, booth in evidence.get('booths', {}).items():
        if booth.get('seat_name') in wanted:
            by_seat[booth['seat_name']][key] = booth
    result = {}
    for seat, booths in by_seat.items():
        coalition = {p for b in booths.values() for p in b.get('fp_votes_projected', {})
                     if summary.get('parties', {}).get(p) in ('LIB', 'LNP', 'LNQ', 'CLP', 'NAT')}
        bands = summary['seats'].get(seat, {}).get('tcp_percentiles', {})
        for label, levels in bands.items():
            pair = label.split('/')
            ids = frozenset(pair)
            if (len(pair) != 2 or ids not in wanted[seat] or (seat, ids) in ranked
                    or levels.get('50') is None or not 0 <= levels['50'] <= 100):
                continue
            fitted = fit(booths, evidence.get('opening_tcp_geography', {}), pair, levels['50'], coalition,
                         evidence.get('booth_tcp_bounds', {}))
            result.update(((key, ids), votes) for key, votes in fitted.items())
    return result


def comparison_estimates(previous, current, old_evidence, new_evidence, changed_fp, ranked):
    """A new TCP count needs its earlier benchmark. A booth reporting FP
    can also illustrate a change in its projected TCP, which requires the
    same identifiable contest at both dates."""
    wanted = defaultdict(set)
    for seat in changed_fp:
        frequencies = current['seats'].get(seat, {}).get('pairing_percent', {})
        if frequencies:
            wanted[seat].add(frozenset(max(frequencies, key=frequencies.get).split('/')))
    old, new = old_evidence.get('booths', {}), new_evidence.get('booths', {})
    for key in old.keys() & new.keys():
        if old[key].get('tcp_votes_current') != new[key].get('tcp_votes_current'):
            measured = mapped_tcp(new[key])
            if measured:
                wanted[new[key]['seat_name']].add(frozenset(measured))
    for seat, contests in wanted.items():
        old_names, new_names = [s['seats'].get(seat, {}).get('candidates', {}) for s in (previous, current)]
        wanted[seat] = {pair for pair in contests if all((old_names.get(p) or {}).get('name')
                        and old_names[p] == new_names.get(p) for p in pair)}
    return (expectations(previous, old_evidence, wanted, ranked),
            expectations(current, new_evidence, {s:wanted[s] for s in changed_fp}, ranked))
