"""Allocate a published district TCP median to booths for the same two candidates.

This is a central benchmark conditional on those two making the final count.
It is used in commentary, not to generate the forecast. Each location retains
its projected TCP turnout, which can differ from FP turnout with exhaustion.
"""
from collections import Counter, defaultdict
from dataclasses import dataclass
import math

from . import booth_fp
from .count_context import mapped_tcp


def split(booth, pair, coalition):
    """A direct projection names its opponents. A TPP projection labels the
    Coalition side '1' even for Nationals. Only use it when the district
    evidence identifies a single Coalition partner facing Labor."""
    direct = booth.get('tcp_votes_projected') or {}
    if direct.keys() == set(pair) and sum(direct.values()) > 0:
        return direct
    classic = booth.get('tpp_votes_projected') or {}
    if ('0' in pair and coalition == set(pair)-{'0'} and classic.keys() == {'0', '1'}
            and sum(classic.values()) > 0):
        return {p: classic['0' if p == '0' else '1'] for p in pair}
    return None


def coalition_opponents(evidence, seat, ballot):
    """When both Coalition partners stand, the simulator exports the actual
    opponent used to calculate each booth's classic preference rates. A
    single shared opponent identifies the otherwise anonymous Coalition
    side of the TPP. Mixed or older accounts still need a direct named TCP."""
    pairs = evidence.get('classic_benchmark_labor_rates', {}).get(seat, {})
    if len(pairs) == 1:
        sides = next(iter(pairs)).split('/')
        if len(sides) == 2 and sides[0] == '0' and sides[1] in ballot:
            return {sides[1]}
    return ballot


@dataclass
class PreferencePool:
    """Votes at one booth in a full-preference election. Only votes whose
    first preference went to someone else can transfer between the finalists."""
    first_primary: float
    donors: float
    first_fraction: float
    minimum: float
    maximum: float
    turnout: float

    def votes_for_first(self, adjustment):
        adjusted = min(1., max(0., self.first_fraction + adjustment))
        return min(self.maximum, max(self.minimum, self.first_primary + self.donors*adjusted))


def preference_pool(pair, expected, seed_fp, seed_tcp, size, floor, rates, district_fraction):
    """Apply the booth's previous preference tendency to its district-aligned
    expected primaries. The original booth FP and TCP estimates establish a
    local transfer split only when they describe compatible ballots. Where
    they cannot, use the district's source-party rates. An older archive
    without these rates can still identify the average transfer share needed
    across the district from its own expected primaries and TCP."""
    if not expected or not math.isclose(sum(expected.values()), size, abs_tol=.01):
        return None
    a, b = pair
    first, second = expected.get(a, 0.), expected.get(b, 0.)
    minimum = max(floor[a], first)
    maximum = size - max(floor[b], second)
    if minimum > maximum + .01:
        return None
    # Differences below one hundredth of a ballot can arise from the
    # precision of the export. A completed TCP fixes both finalists exactly.
    minimum = min(minimum, maximum)
    donors = sum(n for p, n in expected.items() if p not in pair)
    fraction = 0.
    if donors > .01 and maximum - minimum > .01:
        transfers = projected_transfers(pair, seed_tcp, seed_fp, expected, rates)
        if transfers is not None:
            fraction = transfers/donors
        elif rates is not None and all(p in rates and 0 <= rates[p] <= 1
                                       for p, n in expected.items() if p not in pair and n):
            fraction = sum(n*rates[p] for p, n in expected.items() if p not in pair and n)/donors
        elif rates is None and district_fraction is not None:
            fraction = district_fraction
        else:
            return None
    return PreferencePool(first, donors, min(1., max(0., fraction)), minimum, maximum, size)


def reconcile_preferences(pools, pair, target):
    """Make the turnout-weighted booth estimates reproduce the district's TCP
    median. Start by adding the same number of percentage points to the
    transfer share at each booth. A booth with twice as many donor ballots
    can absorb twice as many votes. A share reaching 0% or 100%, or a count
    already recorded for either finalist, limits how far a booth can move.
    Published FP means and a conditional TCP median need not be compatible,
    particularly for an unlikely pairing. Omit an impossible comparison."""
    wanted = sum(row.turnout for row in pools.values())*target/100
    low, high = -1., 1.
    for _ in range(50):
        middle = (low+high)/2
        total = sum(row.votes_for_first(middle) for row in pools.values())
        if abs(total-wanted) <= .001:
            break
        if total < wanted:
            low = middle
        else:
            high = middle
    if abs(total-wanted) > .01:
        return {}
    a, b = pair
    result = {}
    for key, row in pools.items():
        first = row.votes_for_first(middle)
        result[key] = {a:first, b:row.turnout-first}
    return result


def fit(booths, opening, pair, target, coalition, bounds, aligned_fp=None, rates=None, dual_coalition=False):
    """Reconcile all booths to the district median for the first finalist.
    With district-aligned primaries and compulsory preferences, only transfers
    are adjustable. For optional preferences the FP and TCP turnout can differ,
    so use the separate TCP geography and keep each finalist's known votes.
    A missing booth or an impossible target prevents the comparison."""
    fixed, remaining, pools = {}, {}, {}
    district_fraction = None
    if aligned_fp is not None and rates is None:
        seat_fp = Counter()
        for key in booths:
            seat_fp.update(aligned_fp.get(key, {}))
        donors = sum(n for p, n in seat_fp.items() if p not in pair)
        transfers = sum(seat_fp.values())*target/100 - seat_fp[pair[0]]
        if donors and 0 <= transfers <= donors:
            district_fraction = transfers/donors
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
        source = booth
        same_pair_count = positive_tcp and measured.keys() == set(pair)
        if aligned_fp is not None:
            # The arrival of primaries changes who supplies preferences. It
            # supplies no new observation of how those voters rank these two
            # candidates. Keep the same local preference geography across that
            # transition. A measured or retained count for this pair can update it.
            use_opening = not same_pair_count and saved_floor is None
        else:
            use_opening = not positive_tcp and saved_floor is None and not any(fp.values())
        if use_opening:
            first = opening.get(key, {})
            # An unnamed opening TPP may belong to the other Coalition party.
            # A named direct TCP is still usable when the opening TPP is ambiguous.
            if dual_coalition and first.get('classic_pair') != '/'.join(pair):
                first = {k:v for k,v in first.items() if k != 'tpp_votes_projected'}
            if (first.get('fp_votes_projected', {}).keys() == booth.get('fp_votes_projected', {}).keys()
                    and split(first, pair, coalition)):
                source = first
        seed = split(source, pair, coalition)
        if aligned_fp is not None:
            row = preference_pool(pair, aligned_fp.get(key), source.get('fp_votes_projected', {}),
                                  seed, size, floor, rates, district_fraction)
            if row is None:
                return {}
            pools[key] = row
            continue
        if free <= .01:
            continue
        pool = {p: max(0., seed[p]-floor[p]) for p in pair}
        mass = sum(pool.values())
        if not mass:
            return {}
        remaining[key] = free, {p: v*free/mass for p, v in pool.items()}
    if aligned_fp is not None:
        return reconcile_preferences(pools, pair, target)
    goals = {p: turnout*share/100-sum(row[p] for row in fixed.values())
             for p, share in zip(pair, (target, 100-target))}
    if min(goals.values()) < -.01 or not booth_fp.balance_outstanding(remaining, {p:max(0., v) for p,v in goals.items()}):
        return {}
    for key, (_, votes) in remaining.items():
        for p in pair:
            fixed[key][p] += votes[p]
    return fixed


def expectations(summary, evidence, wanted, ranked, reference_fp=None):
    """Fit only pairs needed in this window. Ranked allocations for ballots
    awaiting preferences have a separate same-ballot comparison."""
    by_seat = defaultdict(dict)
    for key, booth in evidence.get('booths', {}).items():
        if booth.get('seat_name') in wanted:
            by_seat[booth['seat_name']][key] = booth
    result = {}
    optional = evidence.get('optional_preferential')
    full_preferences = optional is False or (optional is None and bool(evidence.get('fp_based_tpp')))
    if full_preferences and reference_fp is None:
        reference_fp = booth_fp.expectations(summary, evidence, wanted)
    for seat, booths in by_seat.items():
        coalition = {p for b in booths.values() for p in b.get('fp_votes_projected', {})
                     if summary.get('parties', {}).get(p) in ('LIB', 'LNP', 'LNQ', 'CLP', 'NAT')}
        dual_coalition = len(coalition) > 1
        coalition = coalition_opponents(evidence, seat, coalition)
        bands = summary['seats'].get(seat, {}).get('tcp_percentiles', {})
        for label, levels in bands.items():
            pair = label.split('/')
            ids = frozenset(pair)
            if (len(pair) != 2 or ids not in wanted[seat] or (seat, ids) in ranked
                    or levels.get('50') is None or not 0 <= levels['50'] <= 100):
                continue
            aligned = reference_fp if full_preferences else None
            rates = evidence.get('classic_benchmark_labor_rates', {}).get(seat, {}).get(label)
            fitted = fit(booths, evidence.get('opening_tcp_geography', {}), pair, levels['50'], coalition,
                         evidence.get('booth_tcp_bounds', {}), aligned, rates, dual_coalition)
            result.update(((key, ids), votes) for key, votes in fitted.items())
    return result


def comparison_benchmarks(previous, current, old_evidence, new_evidence, changed_fp, ranked):
    """Find the earlier FP and TCP benchmarks for the changed booths. A new TCP
    is compared directly with it. FP-only returns instead keep its preference
    assumptions fixed when estimating what the new primary mix implies."""
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
    # Fit district FP once, including seats where only a TCP was reported.
    # Both comparisons then start from exactly the same expected booth ballots.
    reference_fp = booth_fp.expectations(previous, old_evidence, changed_fp | wanted.keys())
    return reference_fp, expectations(previous, old_evidence, wanted, ranked, reference_fp)


def requires_dominant_donor_reversal(pair, earlier_tcp, expected_fp, source_rates):
    """Check whether the booth benchmark needs a clear preference lean to reverse.

    If one eliminated candidate supplies more than half the expected FP votes,
    their transfers can make up most of the TCP. Use the stronger evidence of a
    60–40 starting lean or more. Give every other transferable ballot to the
    finalist who needs them before judging whether the dominant donor would
    have to favour the opposite finalist. This tests the safety of quoting
    the comparison. The simulator's district forecast is a separate estimate.
    """
    size = sum(expected_fp.values())
    donors = {p: n for p, n in expected_fp.items() if p not in pair and n > size/2}
    if not source_rates or not donors:
        return False
    donor, votes = next(iter(donors.items()))
    rate = source_rates.get(donor)
    if rate is None:
        return False
    first, second = pair
    earlier_first = size * earlier_tcp[first]/sum(earlier_tcp.values())
    minimum = expected_fp.get(first, 0) + votes/2
    maximum = size - expected_fp.get(second, 0) - votes/2
    return ((rate >= .6 and earlier_first < minimum-.01) or
            (rate <= .4 and earlier_first > maximum+.01))


def projected_transfers(pair, earlier_tcp, expected_fp, new_fp, labor_rates):
    """Estimate transfers to the first finalist when the FP mix changes.

    Both endpoints use percentages of the same kind of ballot. Each finalist
    keeps their own primaries. The earlier FP and TCP together determine how
    many of the other voters were expected to reach the first finalist. With
    party rates available, shift their odds together until they reproduce
    that earlier split, preserving differences such as Greens versus Others.
    Apply those rates to the new FP mix, which can be a revised forecast or
    a count. Older archives and non-classic pairs can identify only the
    combined split, assumed common to all donors. Return a number of ballots.
    """
    a = pair[0]
    size, actual = sum(expected_fp.values()), sum(new_fp.values())
    tcp_size = sum(earlier_tcp.values())
    if not size or not actual or not tcp_size or not math.isclose(tcp_size, size, abs_tol=.02):
        return None
    expected_a = size * earlier_tcp[a] / tcp_size
    donors = {p for p in expected_fp.keys() | new_fp.keys()
              if p not in pair and (expected_fp.get(p) or new_fp.get(p))}
    pool = sum(expected_fp.get(p, 0) for p in donors)
    transfers = expected_a - expected_fp.get(a, 0)
    # Independently fitted FP and TCP benchmarks can be incompatible, for
    # example if expected Labor TCP is below its own primary. Allow only
    # numerical rounding (a hundredth of a ballot). No earlier donor ballots
    # means no local basis for predicting newly arrived donor preferences.
    if not -.01 <= transfers <= pool + .01 or (not pool and any(new_fp.get(p) for p in donors)):
        return None
    fraction = max(0., min(1., transfers/pool)) if pool else 0.
    rates = dict.fromkeys(donors, fraction)
    if labor_rates is not None and 0 < fraction < 1:
        if any(p not in labor_rates or not 0 < labor_rates[p] < 1 for p in donors):
            return None
        # Vary a common offset on the log-odds scale. The total is monotone
        # so bisection finds the split in a bounded number of steps.
        odds = {p: labor_rates[p]/(1-labor_rates[p]) for p in donors}
        # The earlier FP totals stay fixed throughout the fit. Only build
        # the per-party rates once the adjustment for this booth is known.
        fitting_counts = [(expected_fp.get(p, 0), q) for p, q in odds.items()]
        low, high = -50., 50.
        for _ in range(45):
            mid = (low + high)/2
            factor = math.exp(-mid)
            if sum(count * (q/(q+factor)) for count, q in fitting_counts) < transfers:
                low = mid
            else:
                high = mid
        rates = {p: q/(q+factor) for p, q in odds.items()}
    return sum(new_fp.get(p, 0)*r for p,r in rates.items())


def fixed_flow_shares(pair, earlier_tcp, expected_fp, measured_fp, labor_rates):
    """Compare the booth benchmark with the TCP its counted primaries imply
    at the same transfer rates. Both shares refer to the same two finalists."""
    transfers = projected_transfers(pair, earlier_tcp, expected_fp, measured_fp, labor_rates)
    if transfers is None:
        return None
    a, b = pair
    before_percent = 100*earlier_tcp[a]/sum(earlier_tcp.values())
    after_percent = 100*(measured_fp.get(a, 0)+transfers)/sum(measured_fp.values())
    return {a: [before_percent, after_percent], b: [100-before_percent, 100-after_percent]}
