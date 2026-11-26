"""Estimate FP shares for individual reporting places from a published seat forecast.

Use the local forecast for voters still to report at each booth when the
reference snapshot recorded it. Older forecasts can supply a booth estimate
by distributing the projected district FP across its reporting locations.
Both methods keep votes already counted at the reference time fixed.
"""
from collections import Counter, defaultdict
from functools import lru_cache
import math

from .storage import read


@lru_cache(maxsize=4)
def opening_geography(package):
    """Keep just the initial booth composition for districts with no local
    returns. This prevents a large early booth from determining both the
    district trend and the relative strength of other uncounted booths."""
    evidence = read(package/'evidence.json.gz')
    opening = evidence['booths']
    counted = {b.get('seat_name') for b in opening.values()
               if any(sum(b.get(field, {}).values()) for field in ('fp_votes_current', 'tcp_votes_current'))}
    shapes = {key: b['fp_votes_projected'] for key, b in opening.items()
              if b.get('seat_name') not in counted and b.get('fp_votes_projected')}
    summary = read(package/'summary.json')
    tcp_shapes = {key: {field: b[field] for field in ('tcp_votes_projected', 'tpp_votes_projected', 'fp_votes_projected')
                        if field in b} for key, b in opening.items() if key in shapes}
    for key, shape in tcp_shapes.items():
        pairs = evidence.get('classic_benchmark_labor_rates', {}).get(opening[key].get('seat_name'), {})
        if len(pairs) == 1:
            shape['classic_pair'] = next(iter(pairs))
    return summary['input_revision'], summary['source_time'], shapes, tcp_shapes


def balance_outstanding(rows, targets):
    """Repeatedly scale each party to its outstanding district total and
    each booth back to its turnout. This preserves the geographic contrasts
    in the starting mix and keeps genuine zeros. If the two sets of totals
    cannot be met together, there is no supported comparison."""
    # One hundredth of a ballot is well below the precision of a published
    # share. Bound the work for incompatible or nearly exhausted counts.
    for _ in range(100):
        totals = Counter()
        for size, votes in rows.values():
            totals.update(votes)
        if all(abs(totals[p]-goal) < .01 for p, goal in targets.items()):
            return True
        if any(goal > .01 and not totals[p] for p, goal in targets.items()):
            return False
        scales = {p: goal/totals[p] if totals[p] else 0 for p, goal in targets.items()}
        for size, votes in rows.values():
            for p in votes:
                votes[p] *= scales[p]
            total = sum(votes.values())
            if not total:
                return False
            for p in votes:
                votes[p] *= size/total
    return False


def district_expectations(booths, means, opening):
    """Return final expected candidate votes by booth for one reference seat.
    Named parties use their published mean FP percentages. The simulator
    also reports grouped Others and hypothetical entrants. Their combined
    remainder is shared among the ballot candidates without separate means,
    in proportion to those candidates' booth estimates."""
    if not means or any(not math.isfinite(v) or v < 0 for v in means.values()) or abs(sum(means.values())-100) > .02:
        return {}
    known = {p for p in means if int(p) >= 0 or p == '-4'}
    rows, patterns, counted = {}, {}, {}
    fixed = Counter()
    for key, booth in booths.items():
        forecast = booth.get('fp_votes_projected', {})
        aliases = booth.get('count_to_projection_ids', {})
        actual = Counter()
        for p, n in booth.get('fp_votes_current', {}).items():
            actual[aliases.get(p, p)] += n
        counted[key] = actual
        for p, n in actual.items():
            fixed[p if p in known else None] += n
        if any(not math.isfinite(v) or v < 0 for v in forecast.values()):
            return {}
        size = sum(forecast.values())-sum(actual.values())
        if size < -.01:
            return {}
        if size <= .01:
            continue
        # A partially counted service has direct information about its own
        # composition. Keep its estimate for outstanding ballots. For an
        # unreported place prefer the pre-count pattern on the same ballot.
        # A replay starting mid-count can still use the reference forecast's
        # relative booth estimates, subject to the very same district totals.
        shape = opening.get(key)
        if sum(actual.values()) or not shape or shape.keys() != forecast.keys() or not sum(shape.values()):
            shape = {p: max(0., v-actual[p]) for p, v in forecast.items()}
        mass = sum(shape.values())
        if not mass:
            return {}
        votes = defaultdict(float)
        for p, n in shape.items():
            votes[p if p in known else None] += n*size/mass
        rows[key] = (size, votes)
        patterns[key] = shape
    turnout = sum(fixed.values()) + sum(size for size, _ in rows.values())
    total_percent = sum(means.values())
    shares = {p: means[p]/total_percent for p in known}
    shares[None] = sum(v for p, v in means.items() if p not in known)/total_percent
    targets = {p: turnout*v-fixed[p] for p, v in shares.items()}
    # A district mean can imply fewer votes than are already counted, for
    # example when published mean shares and projected turnout come from
    # different averages. Do not move recorded ballots to force a fit.
    if any(v < -.01 for v in targets.values()) or not balance_outstanding(rows, {p:max(0., v) for p, v in targets.items()}):
        return {}
    for key, (_, votes) in rows.items():
        shape = patterns[key]
        others = sum(n for p, n in shape.items() if p not in known)
        for p, n in shape.items():
            addition = votes[p] if p in known else (votes[None]*n/others if others else 0.)
            counted[key][p] += addition
    return counted


def local_expectation(booth, shares, fallback):
    """Convert the local fit at the reference time into a whole-place tally.
    Keep every counted FP fixed. The fit names individual parties but can
    combine several minor candidates into one remainder. For those candidates
    preserve the earlier forecast's relative split of the outstanding votes."""
    counted = Counter()
    aliases = booth.get('count_to_projection_ids', {})
    for party, n in booth.get('fp_votes_current', {}).items():
        counted[aliases.get(party,party)] += n
    remaining = sum(booth.get('fp_votes_projected', {}).values()) - sum(counted.values())
    if remaining <= 0 or not shares or any(not math.isfinite(n) or n < 0 for n in shares.values()):
        return None
    remainder = 100-sum(shares.values())
    if remainder < -.001:
        return None
    other = {p:max(0.,n-counted[p]) for p,n in fallback.items() if p not in shares}
    mass = sum(other.values())
    if remainder > .001 and not mass:
        return None
    for p,share in shares.items():
        counted[p] += remaining*share/100
    if mass:
        for p,n in other.items():
            counted[p] += remaining*max(0.,remainder)/100*n/mass
    return counted


def expectations(summary, evidence, wanted):
    """Only fit seats with changed FP counts, using the reference endpoint
    for every number in the fit. All its booths enter, including declarations."""
    seats = defaultdict(dict)
    for key, booth in evidence.get('booths', {}).items():
        seat = booth.get('seat_name')
        if seat in wanted:
            seats[seat][key] = booth
    result = {}
    for seat, booths in seats.items():
        means = summary['seats'].get(seat, {}).get('fp_mean_percent', {})
        fitted = district_expectations(booths, means, evidence.get('opening_fp_geography', {}))
        if not fitted:
            # Even an old report without district means can establish the
            # earlier split for a recheck when every projected vote was
            # already counted. Other places have no reconciled benchmark.
            for key, booth in booths.items():
                actual = Counter()
                aliases = booth.get('count_to_projection_ids', {})
                for p, n in booth.get('fp_votes_current', {}).items():
                    actual[aliases.get(p, p)] += n
                if actual and dict(actual) == booth.get('fp_votes_projected'):
                    fitted[key] = actual
        for key, booth in booths.items():
            shares = evidence.get('fit_remaining_fp_percent', {}).get(key)
            if shares:
                basis = fitted.get(key) or booth.get('fp_votes_projected', {})
                votes = local_expectation(booth, shares, basis)
                if votes is not None:
                    fitted[key] = votes
        result.update(fitted)
    return result
