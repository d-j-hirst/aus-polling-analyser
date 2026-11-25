"""Compare new TCP counts with the ranked forecast for the same ballots.

The earlier ranked forecast allocates known first preferences by source party,
final pairing and voting category. Apply its rates to the booths now counted.
Older archives sometimes support only an explicitly approximate comparison.
"""
from collections import defaultdict
import math

from .count_context import mapped_tcp

ARCHIVED_BASIS = 'booth FP with category-average ranked transfers'


def category(booth):
    """Use the groups in LiveV2's preferenceCategory. A PPVC supplies early
    votes even when the feed classifies its ballots as ordinary."""
    vote, kind = booth.get('vote_type'), booth.get('booth_type')
    if kind == 'PPVC' or vote == 'Early':
        return 'Early'
    if vote == 'Ordinary' and kind not in ('Normal', 'Invalid'):
        return kind
    return vote


def allocations(evidence):
    rates = evidence.get('ranked_source_rates', {})
    result = {}
    for key, row in evidence.get('preferences', {}).items():
        if row.get('seat') and row.get('category') and len(row.get('pair', ())) == 2:
            group = row['seat'], tuple(map(str, row['pair'])), row['category']
            result[group] = dict(row, pending_first_rates=rates[key]) if key in rates else row
    return result


def counted(booth, pair):
    raw = booth.get('tcp_votes_current', {})
    if not any(raw.values()):
        return (0, 0)
    counts = mapped_tcp(booth)
    if counts is not None and counts.keys() == set(pair):
        return tuple(counts[p] for p in pair)
    return None


def archived_transfer_rate(pair, row, booths, bounds):
    """Recover the fraction of *other candidates’* preferences the earlier
    forecast allocated to the first finalist across the category. Applying
    that average to a booth preserves its two finalists' known primaries but
    assumes the other voters split like those in the category. Only use it
    in full-preference elections with an identifiable outstanding FP mix."""
    pending = first = second = 0
    for key, old, _ in booths:
        if not old:
            continue
        fp = old['fp_votes_current']
        size = sum(fp.values())
        tcp = counted(old, pair)
        if tcp is None or bounds.get(key):
            return None
        if sum(tcp) == size:
            continue
        if sum(tcp):
            # Partial returns need the FP mix of just the uncounted ballots.
            return None
        pending += size
        aliases = old.get('count_to_projection_ids', {})
        first += sum(v for p, v in fp.items() if aliases.get(p, p) == pair[0])
        second += sum(v for p, v in fp.items() if aliases.get(p, p) == pair[1])
    other = pending - first - second
    expected = row.get('adjusted_first_votes_from_pending_fp')
    if pending != row.get('fp_awaiting_tcp') or other <= 0 or expected is None:
        return None
    rate = (expected - first) / other
    return rate if 0 <= rate <= 1 else None


def new_booth_references(pair, booths, rates, average, bounds):
    """Compare the first TCP return with the very same ballots' earlier FP.
    Party-specific rates are keyed by the original booth candidate IDs. The
    archive approximation uses the two named finalists and a single rate for
    everyone else. Never apply either to a partial or revised FP batch."""
    result = {}
    for key, old, new in booths:
        if not old or not new or key in bounds or counted(old, pair) != (0, 0):
            continue
        fp = old['fp_votes_current']
        tcp = counted(new, pair)
        size = sum(fp.values())
        if not size or tcp is None or sum(tcp) != size or fp != new['fp_votes_current']:
            continue
        if rates is not None:
            if any(not 0 <= rates.get(p, -1) <= 1 for p, v in fp.items() if v):
                continue
            first = sum(v * rates[p] for p, v in fp.items() if v)
            basis = 'ranked TCP'
        elif average is not None:
            aliases = old.get('count_to_projection_ids', {})
            splits = {pair[0]: 1, pair[1]: 0}
            first = sum(v * splits.get(aliases.get(p, p), average) for p, v in fp.items())
            basis = ARCHIVED_BASIS
        else:
            continue
        result[key, frozenset(pair)] = ({pair[0]: first, pair[1]: size-first}, basis)
    return result


def compare_group(pair, before, after, booths):
    """Check whether all ballots whose FP was known at the earlier forecast
    now have a TCP. Check the FP mix and TCP at every booth, then reconcile
    the outstanding votes with both simulator exports. A recheck, different
    opponents or new primaries could otherwise create a misleading comparison.
    """
    pending = [0, 0]
    new_first = 0
    contributors, arrivals = [], []
    for key, old, new in booths:
        if old is None or new is None or old['fp_votes_current'] != new['fp_votes_current']:
            return None
        old_counts, new_counts = counted(old, pair), counted(new, pair)
        if old_counts is None or new_counts is None:
            return None
        first, second = (b-a for a, b in zip(old_counts, new_counts))
        if min(first, second) < 0:
            return None
        fp = sum(old['fp_votes_current'].values())
        for i, counts in enumerate((old_counts, new_counts)):
            pending[i] += max(0, fp - sum(counts))
        if fp > sum(old_counts):
            contributors.append((key, old_counts[0], fp))
        if first + second:
            if sum(new_counts) > fp:
                return None
            arrivals.append((first + second, new['name']))
            new_first += first
    if not arrivals or not pending[0] or pending[1]:
        # Only a completed group can supply an observed result for these
        # ballots. Otherwise look up the newly counted booths individually.
        return None

    shares = []
    for i, row in enumerate((before, after)):
        size = row.get('fp_awaiting_tcp')
        estimate = row.get('adjusted_first_votes_from_pending_fp')
        if (size is None or estimate is None or not math.isfinite(estimate)
                or not math.isclose(size, pending[i], abs_tol=.001) or not 0 <= estimate <= size):
            return None
        shares.append(estimate)
    added = sum(n for n, _ in arrivals)
    if added != pending[0] - pending[1]:
        return None
    if len(contributors) == 1:
        # All votes now counted at this booth were already known on FP. Add
        # any TCP already measured there to the ranked allocation for the rest.
        key, counted_first, fp = contributors[0]
        first = counted_first + shares[0]
        return key, {pair[0]: first, pair[1]: fp-first}
    return None, {'pair': list(pair), 'category': before['category'], 'ballots': pending[0],
                  'newly_counted': added, 'units': len(arrivals),
                  'largest_reporting_booths': [name for _, name in sorted(arrivals, reverse=True)[:3]],
                  'first_share_before': 100*shares[0]/pending[0],
                  'first_share_after': 100*new_first/added}


def additional_batch_shares(pair, before, after, booths, old_bounds, new_bounds):
    """Recover the earlier ranked estimate for additional TCP ballots at a
    single reporting place. Only one unit may supply the category estimate,
    and both forecasts must describe the same FP ballots and opponents.
    Compare the increment since the previous report, excluding TCP already
    published then. Keep a comparison only if every possible allocation of
    the ballots still uncounted gives the same earlier share to one decimal."""
    if len(booths) != 1:
        return None
    key, old, new = booths[0]
    if not old or not new or old['fp_votes_current'] != new['fp_votes_current'] or new_bounds.get(key):
        return None
    prior, latest = counted(old, pair), counted(new, pair)
    if not prior or not latest or not sum(prior) or any(b < a for a, b in zip(prior, latest)):
        return None
    added = sum(latest) - sum(prior)
    size = sum(old['fp_votes_current'].values())
    remaining = size - sum(latest)
    if added < 10 or remaining < 0 or remaining != after.get('fp_awaiting_tcp'):
        return None

    # The earlier ranked account can already include newer first preferences
    # for the two finalists beyond their published TCP. These are guaranteed
    # votes for them. Only the other pending ballots use estimated preferences.
    aliases = old.get('count_to_projection_ids', {})
    floors = [{aliases.get(p, p): n for p, n in row.items()} for row in old_bounds.get(key, ())]
    matching = [row for row in floors if set(row) == set(pair) and len(row) == 2]
    if len(matching) > 1:
        return None
    fixed = [matching[0][p] for p in pair] if matching else prior
    pending = size - sum(fixed)
    expected = before.get('adjusted_first_votes_from_pending_fp')
    if (any(not a <= f <= b for a, f, b in zip(prior, fixed, latest))
            or pending != before.get('fp_awaiting_tcp') or expected is None or not 0 <= expected <= pending):
        return None
    result = {}
    for p, counted_before, counted_now, guaranteed, estimate in zip(pair, prior, latest, fixed, (expected, pending-expected)):
        outstanding = guaranteed - counted_before + estimate
        low = 100 * max(0, outstanding-remaining) / added
        high = 100 * min(added, outstanding) / added
        if round(low, 1) != round(high, 1):
            return None
        result[p] = [100*(counted_now-counted_before)/added, round(low, 1)]
    return result


def references(previous, current, old_evidence, new_evidence):
    """Identify pairs using the ranked live account. When the ranked evidence
    first appears during this window, the earlier forecast supplies no ranked
    benchmark either. A later fit cannot tell us what was expected then.
    """
    before, after = allocations(old_evidence), allocations(new_evidence)
    ranked = {(seat, frozenset(pair)) for seat, pair, _ in before.keys() | after.keys()}
    estimates, groups, increments = {}, defaultdict(list), {}
    if not ranked or previous['input_revision'] != current['input_revision']:
        return ranked, estimates, groups, increments
    old, new = old_evidence.get('booths', {}), new_evidence.get('booths', {})
    units = defaultdict(list)
    changed = set()
    for key in old.keys() | new.keys():
        booth = new.get(key) or old[key]
        group = booth['seat_name'], category(booth)
        units[group].append((key, old.get(key), new.get(key)))
        if (old.get(key) or {}).get('tcp_votes_current') != (new.get(key) or {}).get('tcp_votes_current'):
            changed.add(group)
    bounds = old_evidence.get('booth_tcp_bounds', {})
    revised = bounds.keys() | new_evidence.get('booth_tcp_bounds', {}).keys()
    for key in sorted(before):
        seat, pair, kind = key
        if (seat, kind) not in changed:
            continue
        # SA can restore an earlier booth count to the model after a recheck
        # even while the commission still reports the same district totals.
        # With no sign of new published TCP, do not describe those ballots as
        # newly counted. Older archives may lack the separate published ledger.
        old_report = previous.get('published_district_tcp', {}).get(seat)
        new_report = current.get('published_district_tcp', {}).get(seat)
        if new_report and (new_report == old_report or not any(new_report['candidates'].values())):
            continue
        earlier = previous['seats'].get(seat, {}).get('candidates', {})
        later = current['seats'].get(seat, {}).get('candidates', {})
        if any(p not in earlier or earlier[p] != later.get(p) for p in pair):
            continue
        booths = units[seat, kind]
        result = compare_group(pair, before[key], after[key], booths) if key in after else None
        if result:
            booth_key, reference = result
            if booth_key is not None:
                estimates[booth_key, frozenset(pair)] = (reference, 'ranked TCP')
                continue
        rates = before[key].get('pending_first_rates')
        average = None
        if rates is None and not result and old_evidence.get('optional_preferential') is False:
            average = archived_transfer_rate(pair, before[key], booths, bounds)
        new_refs = new_booth_references(pair, booths, rates, average, revised)
        estimates.update(new_refs)
        if result and len(new_refs) < reference['units']:
            groups[seat].append(reference)
        if not result and not new_refs and key in after and old_evidence.get('optional_preferential') is False:
            shares = additional_batch_shares(pair, before[key], after[key], booths, bounds,
                                            new_evidence.get('booth_tcp_bounds', {}))
            if shares:
                increments[booths[0][0], frozenset(pair)] = shares
    return ranked, estimates, groups, increments
