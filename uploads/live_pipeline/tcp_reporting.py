"""Explain changes to the commission's published two-candidate count.

The forecast can still use counts the commission has taken out of its current
TCP total. Comparing modelled booth counts alone can therefore miss even a
district-wide reset. Keep the published candidate identities and counts at the
two forecast dates separate from the booth evidence used in the forecast.
"""
from pathlib import PureWindowsPath
import re
import xml.etree.ElementTree as ET

from .activity import affected_ballots
from .count_context import mapped_tcp
from .storage import read


def district_count(district):
    """Check each named candidate's district total against its reporting units.
    Remember how many units actually had votes: removing the only booth yet
    to report is also consistent with an ordinary booth correction."""
    candidates = district.findall('{*}two_candidate_preferred/{*}preferred_candidate')
    if len(candidates) != 2:
        return None
    counts, positive_units = {}, set()
    for candidate in candidates:
        candidate_id = candidate.findtext('{*}candidate_id', '').strip()
        votes = candidate.findtext('{*}district_total_ballot_papers', '').strip()
        if not candidate_id or not votes.isascii() or not votes.isdigit() or candidate_id in counts:
            return None
        counts[candidate_id] = int(votes)
        unit_total = 0
        for unit in candidate.findall('{*}polling_places/{*}PreferredPollingPlace'):
            value = unit.findtext('{*}ballot_papers', '').strip()
            if not value.isascii() or not value.isdigit():
                return None
            ballots = int(value)
            unit_total += ballots
            if ballots:
                positive_units.add((unit.findtext('{*}polling_place_id'), unit.findtext('{*}polling_place_name')))
        if unit_total != counts[candidate_id]:
            return None
    return {'candidates': counts, 'positive_units': len(positive_units)}


def published_districts(folder, numerical):
    """Read the SA House of Assembly detail file delivered for this forecast.
    The arrival receipt identifies an immutable copy inside the run, including
    when the uploader runs in WSL and the GUI recorded a Windows path. Missing
    archives and other feed formats supply no evidence of a reset."""
    election = numerical['report'].get('termCode', '')
    identity = numerical.get('source_fingerprint', '')
    if (folder is None or not re.fullmatch(r'\d{4}sa', election)
            or not re.fullmatch(r'[0-9a-f]{16}', identity) or numerical.get('origin')):
        return {}
    try:
        arrival = read(folder/'arrivals'/(identity+'.json'))
        content_hash = arrival['content_hash']
        if arrival.get('hash') != identity or not re.fullmatch(r'[0-9a-f]{16}', content_hash):
            return {}
        name = PureWindowsPath(arrival['file']).name
        if not name.lower().endswith('.xml'):
            return {}
        root = ET.parse(folder/'feeds'/content_hash/name).getroot()
    except (FileNotFoundError, ET.ParseError):
        return {}
    if (root.tag != '{http://www.ecsa.sa.gov.au/schema/HofADetail}HouseOfAssemblyDetail'
            or root.findtext('{*}last_updated', '').strip().replace(' ', 'T') != numerical['source_time']):
        return {}

    districts = {}
    for district in root.findall('{*}districts/{*}district'):
        name = district.findtext('{*}district_name')
        count = district_count(district)
        if name and count is not None:
            districts[name] = count
    return districts


def zero_districts(summary):
    """Only an explicit, checked zero for both named candidates proves there
    were no published TCP votes in the district. Missing feed data cannot."""
    return {seat for seat, row in summary.get('published_district_tcp', {}).items()
            if len(row['candidates']) == 2 and all(v == 0 for v in row['candidates'].values())}


def continuing_realigned_pairs(reference, current, earlier):
    """Identify additional counting for opponents who were already the
    replacement pairing at the reference forecast. Use the commission's
    candidate IDs. A blank count by itself cannot establish a realignment."""
    eligible = {}
    for seat, old in reference.get('published_district_tcp', {}).items():
        new = current.get('published_district_tcp', {}).get(seat)
        if new and old['candidates'].keys() == new['candidates'].keys() and old['positive_units']:
            # Use the ordinary ten-vote editorial scale for a report worth
            # putting beside a close district's counted lead.
            if sum(new['candidates'].values()) - sum(old['candidates'].values()) >= 10:
                eligible[seat] = old, new
    confirmed = {}
    # The caller orders forecasts by observation time. Stop at the reference
    # so a later switch or restoration cannot explain earlier counting.
    for forecast in earlier:
        if not eligible or forecast['forecast_id'] == reference['forecast_id']:
            break
        reported = forecast.get('published_district_tcp', {})
        for seat in eligible.keys() & reported.keys():
            old, _ = eligible[seat]
            if reported[seat]['candidates'].keys() != old['candidates'].keys():
                confirmed[seat] = eligible.pop(seat)
    return confirmed


def reporting_views(previous, current, old_zero, new_zero):
    """Separate changes to earlier usable TCPs while a district remains on
    published zero. Keep the original inputs for the forecast and the genuine
    district reset / replacement comparison. A first new published count or
    an actual reset has a different meaning and follows that existing path."""
    empty = old_zero & new_zero
    if not empty:
        return previous, current, {}
    changes = {}
    def published_booth_view(evidence):
        booths = evidence.get('booths', {})
        return dict(evidence, booths={key: dict(row, tcp_votes_current={})
                    if row.get('seat_name') in empty and row.get('tcp_votes_current') else row
                    for key, row in booths.items()})
    old, new = previous.get('booths', {}), current.get('booths', {})
    for key in sorted(old.keys() | new.keys()):
        before, after = old.get(key, {}), new.get(key, {})
        unit = after or before
        if unit.get('seat_name') not in empty:
            continue
        old_tcp = before.get('tcp_votes_current', {})
        new_tcp = after.get('tcp_votes_current', {})
        if old_tcp != new_tcp and (sum(old_tcp.values()) or sum(new_tcp.values())):
            change = {'unit': key, 'name': unit.get('name'), 'before': old_tcp, 'after': new_tcp}
            for side, booth in (('before', before), ('after', after)):
                # An independent may have a commission ID in the count and a
                # different ID in the forecast. Preserve the raw count and use
                # only an unambiguous correspondence when comparing opponents.
                mapped = mapped_tcp(booth) or {}
                if mapped != change[side]:
                    change[side+'_forecast_ids'] = mapped
            changes.setdefault(unit['seat_name'], []).append(change)
    return published_booth_view(previous), published_booth_view(current), changes


def close_retained_contests(changes, summary, evidence):
    """Eligible earlier counts usually need no commentary. Flag the exception
    when the seat is nearly counted, the old opponents remain likely and their
    projected lead is small enough that the changed ballots could matter. The
    projected lead is a scale for selecting a story, not the effect of excluding
    those ballots. The 95% FP completion and 50% pairing cutoffs are editorial."""
    result = {}
    for seat, units in changes.items():
        progress = evidence.get('seats', {}).get(seat, {}).get('count_progress') or {}
        if (progress.get('fp_completion') or 0) < .95:
            continue
        ballots_by_pair = {}
        unit_changes = {}
        for unit in units:
            before = unit.get('before_forecast_ids', unit['before'])
            after = unit.get('after_forecast_ids', unit['after'])
            pairs = {tuple(sorted(tally)) for tally in (before, after) if len(tally) == 2 and sum(tally.values())}
            for pair in pairs:
                # A change of opponents is assessed separately for both pairs.
                old = before if tuple(sorted(before)) == pair else {}
                new = after if tuple(sorted(after)) == pair else {}
                amount = affected_ballots(old, new)
                ballots_by_pair[pair] = ballots_by_pair.get(pair, 0)+amount
                net = sum(new.values())-sum(old.values())
                if amount:
                    change = 'allocation revised'
                    if net < 0:
                        change = 'fewer earlier votes usable'
                    elif net > 0:
                        change = 'more earlier votes usable'
                    unit_changes.setdefault(pair, []).append((amount, unit['name'], change))
        district = summary['seats'].get(seat, {})
        for opponents, likelihood in district.get('pairing_percent', {}).items():
            pair = tuple(sorted(opponents.split('/')))
            votes = ballots_by_pair.get(pair, 0)
            median = (district.get('tcp_percentiles', {}).get(opponents) or {}).get('50')
            if not votes or likelihood < 50 or median is None or abs(median-50) > 1:
                continue
            turnout = sum(sum(b.get('fp_votes_projected', {}).values())
                          for b in evidence.get('booths', {}).values() if b.get('seat_name') == seat)
            if turnout > 0 and votes >= turnout*abs(2*median-100)/100:
                # Keep up to two named examples for prose. The mechanical
                # comparison retains every unit and its exact candidate totals.
                examples = sorted(unit_changes[pair], key=lambda row: -row[0])[:2]
                result[seat] = {'pair': opponents.split('/'), 'units': {name: change for _, name, change in examples}}
    return result


def retained_sample(evidence, seat):
    """Identify the usable local booths and the opponents their totals cover.
    When available, attach the simulator's estimate of how non-finalist ballots
    split, oriented to the first candidate in the pair. Missing or mixed pairs
    cannot establish one district-wide comparison."""
    counts = {}
    pair = None
    flow = evidence.get('local_tcp_preference_flows', {}).get(seat)
    flow_shares = set()
    for key, booth in evidence.get('booths', {}).items():
        raw = booth.get('tcp_votes_current', {})
        if booth.get('seat_name') != seat or not sum(raw.values()):
            continue
        mapped = mapped_tcp(booth)
        if not mapped or (pair and set(mapped) != set(pair)):
            return None
        pair = sorted(mapped)
        counts[key] = mapped
        if flow and sorted(raw) == flow['raw_pair']:
            focus = booth.get('count_to_projection_ids', {}).get(flow['focus'], flow['focus'])
            flow_shares.add(flow['share'] if focus == pair[0] else 100-flow['share'])
        else:
            flow = None
    if not pair:
        return None
    return pair, counts, flow_shares.pop() if flow and len(flow_shares) == 1 else None


def retained_pair_revisions(changes, previous, current, previous_evidence, current_evidence):
    """Identify when losing earlier booth counts helps explain a forecast move
    even before the end of counting. Require the same likely opponents, a
    material change in their projected result, and evidence that the changed
    booths themselves were re-estimated in that direction. The comparison
    cannot assign the entire district movement to those booths."""
    result = {}
    for seat in changes:
        earlier = retained_sample(previous_evidence, seat)
        later = retained_sample(current_evidence, seat)
        if not earlier or not later or earlier[0] != later[0]:
            continue
        pair, before, old_flow = earlier
        _, after, new_flow = later
        lost = before.keys() - after.keys()
        # Isolate a change of coverage. New or revised counts elsewhere for
        # this pair could move its estimate independently during the window.
        if not lost or any(before.get(k) != votes for k, votes in after.items()):
            continue
        old_seat, new_seat = previous['seats'][seat], current['seats'][seat]
        if any(not old_seat['candidates'].get(p) or old_seat['candidates'][p] != new_seat['candidates'].get(p)
               for p in pair):
            continue
        pair_key = '/'.join(pair)
        if pair_key not in old_seat['pairing_percent']:
            pair_key = '/'.join(reversed(pair))
        old_median, new_median = [(s['tcp_percentiles'].get(pair_key) or {}).get('50') for s in (old_seat, new_seat)]
        if old_median is None or new_median is None:
            continue
        shift = (new_median-old_median) * (1 if pair_key.split('/')[0] == pair[0] else -1)
        lost_total = sum(sum(before[k].values()) for k in lost)
        before_total = sum(sum(count.values()) for count in before.values())
        # Editorial cutoffs: the contest was likely at both endpoints, its
        # median moved at least one point, and at least a quarter of the usable
        # ballots were lost. A small change to an unlikely pair stays quiet.
        if (any(s['pairing_percent'].get(pair_key, 0) < 50 for s in (old_seat, new_seat))
                or abs(shift) < 1 or lost_total < .25*before_total):
            continue
        # Compare the same booths at fixed weights (their earlier usable
        # ballots). Changing the estimated size of a booth alone should not
        # establish that its missing preferences were allocated differently.
        projections = []
        for evidence in (previous_evidence, current_evidence):
            projected = {k: evidence['booths'].get(k, {}).get('tcp_votes_projected', {}) for k in lost}
            if all(set(v) == set(pair) and sum(v.values()) > 0 for v in projected.values()):
                projections.append(sum(sum(before[k].values())*v[pair[0]]/sum(v.values())
                                       for k, v in projected.items())/lost_total)
        if len(projections) != 2:
            continue
        booth_shift = projections[1]-projections[0]
        turnout = sum(sum(b.get('fp_votes_projected', {}).values()) for b in current_evidence['booths'].values()
                      if b.get('seat_name') == seat)
        # A tiny booth can constitute most of a sparse early TCP. Require
        # the reallocation on the lost sample alone to be on the scale of
        # at least a tenth of a point of the district's estimated formal vote.
        if turnout <= 0 or booth_shift*shift <= 0 or abs(booth_shift)*lost_total < .001*turnout:
            continue
        units = sorted(lost, key=lambda k: (-sum(before[k].values()), k))
        examples = [previous_evidence['booths'][k]['name'] for k in units[:2]]
        revised_fp = True
        for k in lost:
            old_fp = previous_evidence['booths'][k].get('fp_votes_current')
            new_fp = current_evidence['booths'].get(k, {}).get('fp_votes_current')
            if old_fp is None or new_fp is None or old_fp == new_fp:
                revised_fp = False
                break
        row = {'pair': pair, 'booths_no_longer_usable_examples': examples, 'fp_revised_at_all_lost_booths': revised_fp,
               'reestimated_booths_favour_more_than_before': pair[0 if shift > 0 else 1],
               'usable_pair_votes_before_after': [before_total, before_total-lost_total]}
        if len(after) == 1:
            row['only_remaining_booth'] = current_evidence['booths'][next(iter(after))]['name']
        if old_flow is not None and new_flow is not None:
            row['estimated_preference_share_to_first_before_after'] = [old_flow, new_flow]
        result[seat] = row
    return result


def reset_explains_drop(previous, current, reset, previous_evidence, current_evidence):
    """Use accepted booth counts to reconcile a statewide progress drop.
    Commission summaries can include votes set aside during validation. Check
    that the earlier usable booths account for the whole earlier tally, that
    removing the affected districts gives the new tally, and that counts in
    the other districts stayed the same at both ends of the comparison."""
    old, new = previous['published_district_tcp'], current['published_district_tcp']
    if not reset or old.keys() != new.keys() or new.keys() != current['seats'].keys():
        return False
    if any(old[s] != new[s] for s in old.keys() - reset):
        return False

    def booth_counts(evidence, districts):
        counts = {}
        for key, booth in evidence.get('booths', {}).items():
            votes = booth.get('tcp_votes_current', {})
            if booth.get('seat_name') in districts and sum(votes.values()) > 0:
                counts[key] = votes
        return counts

    other = old.keys() - reset
    outside = booth_counts(previous_evidence, other)
    if outside != booth_counts(current_evidence, other):
        return False
    continuing = sum(sum(v.values()) for v in outside.values())
    withdrawn = sum(sum(v.values()) for v in booth_counts(previous_evidence, reset).values())
    before = (previous['statewide'].get('countProgress') or {}).get('tcp_votes')
    after = (current['statewide'].get('countProgress') or {}).get('tcp_votes')
    return withdrawn > 0 and before == continuing + withdrawn and after == continuing


def changes(previous, current, previous_evidence, current_evidence):
    """Compare named district pairs at the endpoints. Positive counts falling
    to explicit zero across a district are usually preparation for a different
    pairing. That is an interpretation until different candidates are named.
    An individual booth disappearing or a smaller positive tally does not
    establish a district-wide reset."""
    old = previous.get('published_district_tcp', {})
    new = current.get('published_district_tcp', {})
    groups = {}
    affected_votes = 0
    reset = set()
    for district in sorted(old.keys() & new.keys()):
        before, after = old[district]['candidates'], new[district]['candidates']
        old_votes, new_votes = sum(before.values()), sum(after.values())
        same_pair = before.keys() == after.keys()
        if old_votes and not new_votes:
            if same_pair and old[district]['positive_units'] < 2:
                continue
            reset.add(district)
            kind = 'reset_same_candidates' if same_pair else 'reset_different_candidates'
        elif not same_pair:
            kind = 'different_candidates_with_votes' if new_votes else 'different_candidates_still_on_zero'
        else:
            continue
        groups.setdefault(kind, []).append(district)
        # The earlier and later TCP can describe the same ballots. Only the
        # larger count is needed to decide whether this warrants commentary.
        affected_votes += max(old_votes, new_votes)
    if not groups:
        return None

    # Check retention against the actual forecast. An identical, positive
    # candidate count at a booth after its district has reset is direct
    # evidence that earlier ballots are still available for that pairing.
    earlier_booths = previous_evidence.get('booths', {})
    retained = set()
    for key, booth in current_evidence.get('booths', {}).items():
        district = booth.get('seat_name')
        votes = booth.get('tcp_votes_current', {})
        if (district in reset and sum(votes.values()) > 0
                and earlier_booths.get(key, {}).get('tcp_votes_current') == votes):
            retained.add(district)
    if retained:
        groups['reset_districts_with_earlier_booth_counts_in_forecast'] = sorted(retained)
    result = {'districts': groups, 'votes_for_activity_threshold': affected_votes}
    if reset_explains_drop(previous, current, reset, previous_evidence, current_evidence):
        result['earlier_booth_counts_match_statewide_drop'] = True
    return result


def prompt_context(change):
    """Give the writer the extent of each change. A short list can identify
    individual seats. A statewide reset only needs the number of districts."""
    return {kind: {'count': len(seats), **({'districts': seats} if len(seats) <= 3 else {})}
            for kind, seats in change['districts'].items()}
