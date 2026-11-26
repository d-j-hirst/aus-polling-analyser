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


def mapped_tcp(booth):
    """Keep the commission's two distinct finalists when converting a booth
    count to the candidate IDs used by the forecast. A collision is ambiguous."""
    counted = booth.get('tcp_votes_current', {})
    aliases = booth.get('count_to_projection_ids', {})
    mapped = {aliases.get(p, p): v for p, v in counted.items()}
    if (len(counted) == 2 and len(mapped) == 2 and
            all(math.isfinite(v) and v >= 0 and v == int(v) for v in mapped.values())):
        return {p: int(v) for p, v in mapped.items()}
    return None


def matched_transfers(booth, pair):
    """Reconstruct transfers from other candidates when FP and TCP cover
    equal totals and each finalist has exactly one identifiable FP tally.
    Equal totals support the comparison but cannot verify individual ballots."""
    tcp = mapped_tcp(booth)
    fp = booth.get('fp_votes_current', {})
    if not tcp or tcp.keys() != set(pair) or sum(fp.values()) != sum(tcp.values()) or any(v < 0 for v in fp.values()):
        return None
    aliases = booth.get('count_to_projection_ids', {})
    transfers = {}
    for party in pair:
        entries = [v for p, v in fp.items() if aliases.get(p, p) == party]
        if len(entries) != 1 or tcp[party] < entries[0]:
            return None
        transfers[party] = tcp[party] - entries[0]
    return transfers


def _tcp_account(booths):
    """Sum measured votes for one identifiable pair across an entire district.

    The observed lead needs only counted booths. Estimating votes still to
    come also needs a projection for every booth with counted or expected
    votes. Omit that estimate when coverage is incomplete or opponents differ.
    """
    totals = defaultdict(int)
    for booth in booths:
        counted = booth.get('tcp_votes_current', {})
        if sum(counted.values()) > 0:
            mapped = mapped_tcp(booth)
            if not mapped:
                return None
            for party, votes in mapped.items():
                totals[party] += votes
    if len(totals) != 2:
        return None

    # In a Labor–Coalition contest the simulator commonly exports its booth
    # forecast as TPP. First establish who is actually in the district count,
    # then use TPP only where a booth has no separate TCP and its projected
    # candidates match both counted candidates. This also covers a booth
    # awaiting its first TCP. A different Coalition partner is a different pair.
    projections = defaultdict(float)
    complete_projection = True
    for booth in booths:
        projected = booth.get('tcp_votes_projected', {})
        if not projected:
            tpp = booth.get('tpp_votes_projected', {})
            if tpp.keys() == totals.keys():
                projected = tpp
        if sum(projected.values()) > 0:
            if len(projected) != 2 or not all(math.isfinite(v) and v >= 0 for v in projected.values()):
                complete_projection = False
            for party, votes in projected.items():
                projections[party] += votes
        elif 'fp_votes_projected' not in booth or any(sum(booth.get(k, {}).values()) > 0 for k in
                 ('fp_votes_current', 'fp_votes_projected', 'tcp_votes_current', 'tpp_votes_projected')):
            complete_projection = False
    remaining = None
    if complete_projection and projections.keys() == totals.keys():
        # Rounding is for public prose. A negative net remainder is possible
        # when the forecast anticipates a downward revision in counted votes.
        remaining = round(sum(projections.values()) - sum(totals.values()))
    return totals, remaining


def district_tcp_median(endpoint, first, second):
    """Read the forecast district share for the first named candidate against
    this opponent. The saved percentiles refer to the first name in the
    stored pair, which can be either of the candidates in the counted lead."""
    for pair, bands in endpoint.get('tcp_percentiles', {}).items():
        members = pair.split('/')
        if set(members) == {first, second} and bands.get('50') is not None:
            return bands['50'] if members[0] == first else 100 - bands['50']
    return None


def _new_pair_count(booths, before, after, seat):
    """Describe the measured district count for a newly reported pair. All
    reporting units for the new pair contribute, even if only a few will be
    individually described. A previous district forecast for these opponents
    gives context, although the first booths need not represent the district.

    In each unit with matching FP and TCP totals and identifiable FP votes
    for both finalists, subtraction gives the split from all other candidates
    combined. Keep a partial sample separate from a complete district account.
    Equal totals are evidence of matching coverage, not a ballot-level link.
    """
    first, second = sorted(after, key=lambda p: (-after[p], p))
    units = [b for b in booths if sum(b.get('tcp_votes_current', {}).values()) > 0]
    total = sum(after.values())
    result = {'previous_pair': sorted(before), 'pair': [first, second],
              'units': len(units), 'counted_votes': total, 'counts': dict(after)}
    result['unit_label'] = ('booth' if all(b.get('vote_type') == 'Ordinary' and b.get('booth_type') in ('Normal', 'PPVC')
                                           for b in units) else 'reporting unit')
    median = district_tcp_median(seat['before'], first, second)
    if median is not None:
        result['previous_district_median_first_percent'] = median
    transfers = {first: 0, second: 0}
    matched_units = 0
    matched_name = None
    for booth in units:
        unit_transfers = matched_transfers(booth, transfers)
        if unit_transfers is not None:
            matched_units += 1
            matched_name = booth.get('name') if booth.get('vote_type') == 'Ordinary' else None
            for party, amount in unit_transfers.items():
                transfers[party] += amount
    if matched_units:
        if matched_units == len(units) and sum(transfers.values()):
            result['transfers_from_other_first_preferences'] = transfers
        else:
            result['matched_transfer_sample'] = {'units': matched_units, 'transfers': transfers}
            if matched_units == 1 and matched_name:
                result['matched_transfer_sample']['unit_name'] = matched_name
    return result


def named_party_pair(pair, parties):
    """Only pool district counts when each finalist represents the same
    identifiable party everywhere. An IND badge can name a different person
    in every seat."""
    labels = [(parties.get(p) or '').upper() for p in pair]
    return len(set(labels)) == 2 and all(s and not s.startswith(('IND', 'OTH', '???')) for s in labels)


def early_party_pair_counts(previous, current, seats, parties):
    """Identify the first substantial observed sample for a non-classic party
    matchup in this window, even when it arrives in a single safe district.
    The 500-ballot threshold is an editorial choice. Compare cumulative totals
    on both dates so several small deliveries can cross it together."""
    if not all(view.get('observed_counts_available') for view in (previous, current)):
        return []
    previous_totals = defaultdict(int)
    current_units = defaultdict(lambda: defaultdict(list))
    for is_reference, view in ((True, previous), (False, current)):
        for booth in view.get('booths', {}).values():
            counts = mapped_tcp(booth)
            if not counts or not sum(counts.values()):
                continue
            pair = tuple(sorted(counts))
            if is_reference:
                previous_totals[pair] += sum(counts.values())
            elif previous_totals[pair] < 500:
                current_units[pair][booth['seat_name']].append(booth)
    seat_rows = {s['seat']: s for s in seats}
    samples = []
    for pair, districts in current_units.items():
        # Routine Labor–Coalition counts already have statewide TPP coverage.
        # Candidates sharing the independent or Others label in different
        # districts do not represent a common party preference flow.
        if not named_party_pair(pair, parties):
            continue
        labels = {parties[p].upper() for p in pair}
        if 'ALP' in labels and labels & {'LIB', 'LNP', 'NAT', 'CLP'}:
            continue
        rows = {}
        for district, units in districts.items():
            seat = seat_rows.get(district)
            if not seat or any(p not in seat['after']['candidates'] or
                    seat['after']['candidates'][p] != seat['before']['candidates'].get(p) for p in pair):
                continue
            totals = {p: sum(mapped_tcp(b)[p] for b in units) for p in pair}
            rows[district] = _new_pair_count(units, {}, totals, seat)
        if sum(row['counted_votes'] for row in rows.values()) >= 500:
            samples.append(rows)
    return samples


def _new_pair_booths(booths, old_pair):
    """If some booths still show the old opponents, isolate the positive
    counts for the new opponents. Several competing new pairs or unidentified
    candidates leave no single combined result to describe."""
    groups = defaultdict(list)
    for booth in booths:
        if not sum(booth.get('tcp_votes_current', {}).values()):
            continue
        counts = mapped_tcp(booth)
        if counts is None:
            return []
        if counts.keys() != old_pair:
            groups[frozenset(counts)].append(booth)
    return next(iter(groups.values())) if len(groups) == 1 else []


def add_matched_booth_expectations(replacements, events):
    """Compare a new pair's combined count with the earlier forecast for that
    same set of reporting units. Use the checked same-pair comparisons from
    every booth before the editorial shortlist is drawn. Weight each earlier
    share by the TCP ballots actually counted there: a booth supplying 100 of
    today's 1,000 ballots supplies a tenth of both sides of the comparison.
    If even one booth lacks a matching forecast, the combined count is still
    useful on its own, but has no comparable earlier booth estimate."""
    matched = defaultdict(list)
    for event in events:
        district = event.get('seat')
        if district not in replacements or event['kind'] != 'booths':
            continue
        check = event.get('tcp_against_previous_forecast') or {}
        pair = replacements[district]['pair']
        if check.get('available') and set(check['pair']) == set(pair):
            matched[district].append((check['counted_votes'], check['shares'][pair[0]]['expected_percent']))
    for district, observations in matched.items():
        count = replacements[district]
        ballots = sum(size for size, _ in observations)
        if len(observations) == count['units'] and ballots == count['counted_votes']:
            count['previous_matched_booths_first_percent'] = sum(size * share for size, share in observations) / ballots


def _lead_source_text(old_units, new_units, pair, lead_change, transferred_units):
    """Locate a change in the published lead for the same two candidates.

    A batch of 230 votes for one finalist and 142 for the other moves their
    lead by 88 votes. Rank locations by this difference, not batch size.
    Exclude recognised transfers between categories: they cancel in the
    district account. Only describe sources that reconcile to its full change.
    This arithmetic explains the counted lead, not the simulated win chance.
    """
    if not lead_change:
        return None
    by_location = defaultdict(int)
    first, second = pair
    for key in old_units.keys() | new_units.keys():
        if key in transferred_units:
            continue
        before, after = old_units.get(key, {}), new_units.get(key, {})
        old = mapped_tcp(before) or {}
        new = mapped_tcp(after) or {}
        delta = new.get(first, 0) - new.get(second, 0) - old.get(first, 0) + old.get(second, 0)
        if delta:
            unit = after or before
            name = unit.get('name') or key.rsplit('/', 1)[-1]
            location = 'the '+name+' count' if name == unit.get('vote_type') else name
            by_location[location] += delta
    sources = sorted((name for name, delta in by_location.items() if delta),
                     key=lambda name: (-abs(by_location[name]), name))
    if not sources or sum(by_location.values()) != lead_change:
        return None
    first_source = sources[0]
    if len(sources) == 1:
        return 'That change came entirely from '+first_source+'.'
    # Call one location dominant only when it accounts for more movement
    # than all the other locations combined, including movements in reverse.
    strongest = by_location[first_source]
    if 2 * abs(strongest) > sum(abs(v) for v in by_location.values()):
        if (lead_change - strongest) * strongest < 0:
            return 'The change came mainly from '+first_source+', partly offset by other counting.'
        return 'The change came mainly from '+first_source+', with other counting also contributing.'
    named = sources[:3]
    if len(named) < len(sources):
        named.append('other counting')
    places = ', '.join(named[:-1])+' and '+named[-1]
    return 'That change reflects updates from '+places+'.'


def district_tcp_changes(previous, current, seats, continuing_pairs=None, transferred_units=()):
    """Give the first aggregate on a replacement pair, or the lead, each
    finalist's net votes and estimated remainder for a continuing close race.

    Five points either side of 50% is an editorial threshold for extra detail.
    Test both endpoints and the forecast median as well as the raw count:
    early booths can be unrepresentative. A withdrawal or switch to another
    pair cannot be interpreted as a change in the lead against the old opponent.
    """
    if not previous.get('observed_counts_available') or not current.get('observed_counts_available'):
        return {}, {}
    by_district = []
    for evidence in (previous, current):
        groups = defaultdict(dict)
        for key, booth in evidence.get('booths', {}).items():
            groups[booth['seat_name']][key] = booth
        by_district.append(groups)
    changes, replacements = {}, {}
    for seat in seats:
        district = seat['seat']
        old_units, new_units = [groups[district] for groups in by_district]
        old, new = _tcp_account(old_units.values()), _tcp_account(new_units.values())
        if not old or old == new:
            continue
        before, remaining_before = old
        new_booths = new_units.values()
        if not new:
            new_booths = _new_pair_booths(new_booths, before.keys())
            new = _tcp_account(new_booths)
            if not new:
                continue
        after, remaining_after = new
        if not after.keys() <= seat['after']['candidates'].keys():
            continue
        if any(seat['before']['candidates'].get(p) != seat['after']['candidates'][p] for p in after):
            continue
        if before.keys() != after.keys():
            # Blank announcements and withdrawals give no measured split.
            replacements[district] = _new_pair_count(new_booths, before, after, seat)
            continue
        first, second = sorted(after, key=lambda p: (-after[p], p))
        counted_share = 100 * after[first] / sum(after.values())
        median = district_tcp_median(seat['after'], first, second)
        shares = [counted_share, 100 * before[first] / sum(before.values()),
                  median, district_tcp_median(seat['before'], first, second)]
        if all(abs(share - 50) > 5 for share in shares if share is not None):
            continue
        changes[district] = {'pair': [first, second],
            'lead_before': before[first]-before[second], 'lead_after': after[first]-after[second],
            'net_by_candidate': {p: after[p]-before[p] for p in (first, second)},
            'counted_after': sum(after.values()), 'remaining_before': remaining_before, 'remaining_after': remaining_after}
        account = changes[district]
        source = _lead_source_text(old_units, new_units, account['pair'],
                                   account['lead_after'] - account['lead_before'], transferred_units)
        if source:
            account['lead_source'] = source
        if district in (continuing_pairs or {}):
            # An earlier announced change identifies the pairing. Check that
            # the published tallies also match both candidates' booth totals
            # at both dates before describing this lead as a further count.
            # Feed candidate IDs can differ from the forecast's party IDs.
            old_report, new_report = continuing_pairs[district]
            old_votes, new_votes = old_report['candidates'], new_report['candidates']
            if sorted((old_votes[p], new_votes[p]) for p in old_votes) == sorted((before[p], after[p]) for p in before):
                changes[district]['continued_realigned_count'] = {
                    'net_added': sum(after.values()) - sum(before.values()),
                    'units_before': old_report['positive_units'], 'units_after': new_report['positive_units']}
        if median is not None:
            changes[district]['projected_median_percent_for_first'] = median
        # For readers following a narrow counted lead, estimate how the votes
        # still to come would divide in the central forecast for these same
        # finalists. Apply the public district median to estimated final turnout,
        # then subtract the first candidate's actual count. For example, 55% of
        # an expected 10,000 votes, less 4,000 already counted for the candidate,
        # leaves about 1,500 of the outstanding votes for them. This includes net
        # revisions. It combines two estimates and is not a separately simulated
        # median for the uncounted ballots. Incomplete turnout coverage or an
        # infeasible remainder cannot support a claim about how those votes split.
        if remaining_after is not None and remaining_after > 0 and median is not None:
            final_votes = (sum(after.values()) + remaining_after) * median / 100
            remaining_share = 100 * (final_votes - after[first]) / remaining_after
            if 0 <= remaining_share <= 100:
                changes[district]['approx_remaining_tcp_percent_for_first'] = remaining_share
                # Use the same five-point editorial scale as above to flag
                # a count that gives a different impression of the district.
                # Crossing 50% also matters even if the difference is smaller.
                changes[district]['count_and_projection_diverge'] = (
                    abs(counted_share - median) >= 5 or (counted_share - 50) * (median - 50) < 0)
    return changes, replacements
