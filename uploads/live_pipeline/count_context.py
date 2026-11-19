"""Describe changes in recorded vote categories and close TCP counts for commentary.

These accounts use all booths at the two comparison times. Selecting a few
newsworthy booths first could omit the receiving category of a transfer, or
make one postal batch look like the whole change in a district's counted lead.
"""
from collections import defaultdict
import math


def _transfer_signature(event):
    """Read the candidate-by-candidate FP and TCP changes at a declaration unit."""
    before, after = event.get('before') or {}, event.get('after') or {}
    unit = after or before
    category = unit.get('vote_type')
    if not category or category == 'Ordinary':
        return None
    differences = []
    for field in ('fp_votes_current', 'tcp_votes_current'):
        old, new = before.get(field, {}), after.get(field, {})
        changes = [(party, new.get(party, 0)-old.get(party, 0)) for party in sorted(old.keys() | new.keys())]
        differences.append(tuple((party, delta) for party, delta in changes if delta))
    # A single candidate's change or a matching total alone is weak evidence
    # of relabelling. Require multiple matching allocations in at least one
    # ledger and a consistent direction across both. Keep the raw candidate IDs.
    if max(map(len, differences)) < 2:
        return None
    directions = {delta > 0 for ledger in differences for _, delta in ledger}
    if len(directions) != 1:
        return None
    signature = tuple(tuple((party, abs(delta)) for party, delta in ledger) for ledger in differences)
    return category, directions.pop(), signature


def category_transfers(events):
    """Find unambiguous equal and opposite changes within a district.

    This is an apparent transfer between the two observations. The feed does
    not tell us why the commission changed the category. An unmatched removal,
    a partial match, or several possible partners stay ordinary count changes.
    """
    matches = defaultdict(lambda: ([], []))
    for event in events:
        if event['kind'] != 'booths' or not event.get('changed_votes'):
            continue
        signature = _transfer_signature(event)
        if signature:
            category, increase, amounts = signature
            matches[event['seat'], amounts][increase].append((category, event['id']))
    transfers = []
    for (seat, amounts), (sources, destinations) in matches.items():
        if len(sources) != 1 or len(destinations) != 1 or sources[0][0] == destinations[0][0]:
            continue
        (source, removed), (destination, added) = sources[0], destinations[0]
        transfers.append({'seat': seat, 'from': source, 'to': destination,
                          'fp_votes': sum(v for _, v in amounts[0]), 'tcp_votes': sum(v for _, v in amounts[1]),
                          'evidence_ids': [removed, added]})
    return transfers


def _tcp_account(booths):
    """Sum measured votes for one identifiable pair across an entire district.

    The observed lead needs only counted booths. Estimating votes still to
    come also needs a projection for every booth with counted or expected
    votes. Omit that estimate when coverage is incomplete or opponents differ.
    """
    totals = defaultdict(int)
    projections = defaultdict(float)
    complete_projection = True
    for booth in booths:
        counted = booth.get('tcp_votes_current', {})
        if sum(counted.values()) > 0:
            aliases = booth.get('count_to_projection_ids', {})
            mapped = {aliases.get(p, p): v for p, v in counted.items()}
            if len(counted) != 2 or len(mapped) != 2 or min(mapped.values()) < 0:
                return None
            for party, votes in mapped.items():
                totals[party] += votes
        projected = booth.get('tcp_votes_projected', {})
        if sum(projected.values()) > 0:
            if len(projected) != 2 or not all(math.isfinite(v) and v >= 0 for v in projected.values()):
                complete_projection = False
            for party, votes in projected.items():
                projections[party] += votes
        elif 'fp_votes_projected' not in booth or any(sum(booth.get(k, {}).values()) > 0 for k in
                 ('fp_votes_current', 'fp_votes_projected', 'tcp_votes_current')):
            complete_projection = False
    if len(totals) != 2:
        return None
    remaining = None
    if complete_projection and projections.keys() == totals.keys():
        # Rounding is for public prose. A negative net remainder is possible
        # when the forecast anticipates a downward revision in counted votes.
        remaining = round(sum(projections.values()) - sum(totals.values()))
    return totals, remaining


def close_tcp_changes(previous, current, seats):
    """Give the lead, each finalist's net votes and estimated remainder for close races.

    Five points either side of 50% is an editorial threshold for extra detail.
    Test both endpoints and the forecast median as well as the raw count:
    early booths can be unrepresentative. A withdrawal or switch to another
    pair cannot be interpreted as a change in the lead against the old opponent.
    """
    if not previous.get('observed_counts_available') or not current.get('observed_counts_available'):
        return {}
    by_district = []
    for evidence in (previous, current):
        groups = defaultdict(list)
        for booth in evidence.get('booths', {}).values():
            groups[booth['seat_name']].append(booth)
        by_district.append(groups)
    changes = {}
    for seat in seats:
        district = seat['seat']
        old, new = [_tcp_account(groups[district]) for groups in by_district]
        if not old or not new or old == new:
            continue
        before, remaining_before = old
        after, remaining_after = new
        if before.keys() != after.keys() or not after.keys() <= seat['after']['candidates'].keys():
            continue
        if any(seat['before']['candidates'].get(p) != seat['after']['candidates'][p] for p in after):
            continue
        first, second = sorted(after, key=lambda p: (-after[p], p))
        shares = [100*t[first]/sum(t.values()) for t in (before, after)]
        for endpoint in ('before', 'after'):
            for pair, percentiles in seat[endpoint]['tcp_percentiles'].items():
                if set(pair.split('/')) == set(after) and percentiles and '50' in percentiles:
                    shares.append(percentiles['50'])
        if min(abs(share-50) for share in shares) > 5:
            continue
        changes[district] = {'pair': [first, second],
            'lead_before': before[first]-before[second], 'lead_after': after[first]-after[second],
            'net_by_candidate': {p: after[p]-before[p] for p in (first, second)},
            'remaining_before': remaining_before, 'remaining_after': remaining_after}
    return changes
