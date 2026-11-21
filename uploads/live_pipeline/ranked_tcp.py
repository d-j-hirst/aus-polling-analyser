"""Compare new TCP counts with the ranked forecast for the same ballots.

The live simulator exports its central allocation of first preferences still
awaiting a TCP for each district, voting category and pair. Several booths can
contribute to one allocation. Only when one booth accounts for the entire
unresolved group can we recover that booth's earlier estimate from it.
"""
from collections import defaultdict
import math

from .count_context import mapped_tcp


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
    return {(r['seat'], tuple(map(str, r['pair'])), r['category']): r
            for r in evidence.get('preferences', {}).values()
            if r.get('seat') and r.get('category') and len(r.get('pair', ())) == 2}


def counted(booth, pair):
    raw = booth.get('tcp_votes_current', {})
    if not any(raw.values()):
        return (0, 0)
    counts = mapped_tcp(booth)
    if counts is not None and counts.keys() == set(pair):
        return tuple(counts[p] for p in pair)
    return None


def compare_group(pair, before, after, booths):
    """Follow the ballots whose FP was known at the earlier forecast. New TCP
    replaces an estimate on some of those ballots. The rest keeps the updated
    ranked allocation. Check the FP mix and TCP at every booth, then reconcile
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
    if not arrivals or not pending[0]:
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
    if len(contributors) == 1 and not pending[1]:
        # All votes now counted at this booth were already known on FP. Add
        # any TCP already measured there to the ranked allocation for the rest.
        key, counted_first, fp = contributors[0]
        first = counted_first + shares[0]
        return key, {pair[0]: first, pair[1]: fp-first}
    return None, {'pair': list(pair), 'category': before['category'], 'ballots': pending[0],
                  'newly_counted': added, 'units': len(arrivals),
                  'largest_reporting_booths': [name for _, name in sorted(arrivals, reverse=True)[:3]],
                  'first_share_before': 100*shares[0]/pending[0],
                  'first_share_after': 100*(new_first+shares[1])/pending[0]}


def references(previous, current, old_evidence, new_evidence):
    """Identify pairs using the ranked live account. When the ranked evidence
    first appears during this window, the earlier forecast supplies no ranked
    benchmark either. A later fit cannot tell us what was expected then.
    """
    before, after = allocations(old_evidence), allocations(new_evidence)
    ranked = {(seat, frozenset(pair)) for seat, pair, _ in before.keys() | after.keys()}
    exact, groups = {}, defaultdict(list)
    if not ranked or previous['input_revision'] != current['input_revision']:
        return ranked, exact, groups
    old, new = old_evidence.get('booths', {}), new_evidence.get('booths', {})
    units = defaultdict(list)
    changed = set()
    for key in old.keys() | new.keys():
        booth = new.get(key) or old[key]
        group = booth['seat_name'], category(booth)
        units[group].append((key, old.get(key), new.get(key)))
        if (old.get(key) or {}).get('tcp_votes_current') != (new.get(key) or {}).get('tcp_votes_current'):
            changed.add(group)
    for key in sorted(before.keys() & after.keys()):
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
        result = compare_group(pair, before[key], after[key], units[seat, kind])
        if result:
            booth_key, reference = result
            if booth_key is not None:
                exact[booth_key, frozenset(pair)] = reference
            else:
                groups[seat].append(reference)
    return ranked, exact, groups
