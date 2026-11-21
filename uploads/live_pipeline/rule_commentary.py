"""Write a second election update from the same saved comparison as the AI.

The comparison already identifies candidates, changed counts and observations
that can be matched to an earlier estimate. The rules below choose what to tell
a reader about the winner, then give supporting measurements where available.
They make editorial choices only. They neither recalculate a forecast nor use
the other writer's selections, prose or subsequent counting.
"""
from typing import NamedTuple, Optional
from . import activity, commentary_format as display, pairing_commentary
from .comparisons import headline_candidate_priority


# An unassigned development with this exact first line separates the editions
# inside the existing website payload. Ordinary seat sections have a seat ID.
MARKER = '[[AEF_RULE_BASED_COMMENTARY_V1]]'


def candidate(seat, party):
    return '{candidate:' + seat + ':' + party + '}'


def movement(after, delta):
    verb = 'rose' if delta > 0 else 'fell'
    return f'{verb} to **{display.decimal(after)}% ({display.decimal(delta, True)} pp)**'


def seat_total_headline(old, new, parties, endpoints):
    """Describe up to two changes in the statewide seat medians. When a
    median moves by one, check the chance of reaching the higher total in
    both forecasts. A rise from 49.9% to 50.1% can change the median just
    as surely as a rise from 40% to 60%, but says much less to readers."""
    moves = []
    for party in old.keys() & new.keys() & parties.keys():
        delta = new[party] - old[party]
        if not delta:
            continue
        boundary = str(max(old[party], new[party]))
        support = 0
        if endpoints:
            before, after = [side.get('chance_at_least_seats', {}).get(party, {}).get(boundary)
                             for side in endpoints]
            if before is not None and after is not None:
                support = after - before if delta > 0 else before - after
        # Five percentage points is an editorial minimum for a one-seat
        # headline. A shift of two or more seats already moves the middle
        # outcome past a whole intervening total and earns a mention on its own.
        if abs(delta) >= 2 or support >= 5:
            moves.append((abs(delta), support, party))
    moves.sort(key=lambda move: (-move[0], -move[1], move[2]))
    phrases = []
    for _, _, party in moves[:2]:
        verb = 'rose' if new[party] > old[party] else 'fell'
        phrases.append(f'for {{party:{party}}} {verb} from **{old[party]:g}** to **{new[party]:g}**')
    if phrases:
        return 'The median seat total '+', and the median '.join(phrases)+'.'
    return None


def election_headlines(packet, endpoints):
    """Describe broad movements separately from individual districts. A reset
    concerns reported progress. Only a verified match to the missing ballots
    attributes the whole drop to those resets."""
    old, new = packet['statewide']['before'], packet['statewide']['after']
    headlines = []
    reporting = packet.get('tcp_reporting_changes', {})
    same = reporting.get('reset_same_candidates', {}).get('count', 0)
    changed = reporting.get('reset_different_candidates', {}).get('count', 0)
    if same or changed:
        districts = 'one district' if same + changed == 1 else f'**{same + changed} districts**'
        text = f'The reported TCP count reset in {districts}.'
        if packet.get('tcp_reporting_reconciliation'):
            earlier, current = [side['countProgress']['tcp_votes'] for side in (old, new)]
            text += f' This accounts for the drop of **{earlier-current:,.0f} votes** to **{current:,.0f}**.'
        if changed:
            text += ' The feed also names a new pairing.' if not same else ' The feed also names new pairings in some districts.'
        if same and not changed:
            text += ' A change of opponents is likely, although the feed still names the original candidates.'
        if reporting.get('reset_districts_with_earlier_booth_counts_in_forecast', {}).get('count'):
            text += ' Earlier booth counts still inform the forecast for their original pairings where available.'
        headlines.append(text)

    # Half a percentage point is an editorial threshold for the median vote
    # share. Name at most two parties so the headline remains readable.
    old_fp = old.get('fp_percentiles_by_party') or {}
    new_fp = new.get('fp_percentiles_by_party') or {}
    changes = []
    for party in old_fp.keys() & new_fp.keys() & packet['parties'].keys():
        before, after = old_fp[party].get('50'), new_fp[party].get('50')
        if before is not None and after is not None and abs(after-before) >= .5:
            changes.append((abs(after-before), party, movement(after, after-before)))
    changes.sort(key=lambda item: (-item[0], item[1]))
    if changes:
        parties = ['for {party:'+p+'} '+phrase for _, p, phrase in changes[:2]]
        headlines.append('Across the election, the projected first-preference share '+' and '.join(parties)+'.')
    old_tpp, new_tpp = [(side.get('tpp_percentiles') or {}).get('50') for side in (old, new)]
    if old_tpp is not None and new_tpp is not None and abs(new_tpp-old_tpp) >= .5:
        headlines.append('The projected Labor two-party-preferred share '+movement(new_tpp, new_tpp-old_tpp)+'.')
    old_seats, new_seats = [side.get('medianSeats') or {} for side in (old, new)]
    seats = seat_total_headline(old_seats, new_seats, packet['parties'], endpoints)
    if seats:
        headlines.append(seats)
    # The comparison supplies only matched, material changes in estimated
    # preferences. These refer to projected first-preference voters, including
    # those whose eventual distribution between the named opponents is unknown.
    flows = packet.get('statewide_preference_changes', [])
    if flows:
        flow = flows[0]
        source, destination, other = flow['parties']
        if all(p in packet['parties'] for p in flow['parties']):
            headlines.append(f"The estimated share of {{party:{source}}} voters preferring "
                             f"{{party:{destination}}} over {{party:{other}}} "+movement(flow['after'], flow['change_pp'])+'.')
    return headlines


def district_order(packet, entities):
    """Choose up to three districts. Give priority to a win chance movement
    alongside actual local counting (one percentage point). A five-point
    movement elsewhere can also merit coverage. A large reversal within the
    period or a counted lead changing hands in a competitive seat can earn a
    place even when the endpoint movements are small. The thresholds select
    stories; coincidence alone gives no cause for the movement."""
    selected, subjects = {}, {}
    for row in packet['win_changes']:
        seat, party = row['seat'], row['party_id']
        if row.get('identity_changed') or candidate(seat, party) not in entities:
            continue
        local = packet.get('observed_count_changes_by_seat', {}).get(seat, {}).get('affected_votes', 0) > 0
        change = abs(row['change_pp'])
        reversal = row['maximum_within_window'] - row['minimum_within_window'] - change
        score = max(change, reversal if reversal >= 10 else 0)
        count = packet.get('close_tcp_counts', {}).get(seat, {})
        favourite = max(packet['all_seats'].get(seat, {}).values(), default=0)
        if (party in count.get('pair', []) and row['after'] >= 10 and favourite <= 90
                and count['lead_before'] < 0 < count['lead_after']):
            score = max(3, score)
        if score < (1 if local else 5):
            continue
        # With no local counts, a movement twice as large can take the place of
        # a local story. This is an editorial preference for results a reader
        # can inspect, independent of how strongly the forecast weighs either.
        priority = (score if local else score * .5, change, row['after'])
        if seat not in selected or priority > selected[seat][0]:
            selected[seat] = priority, row
        subject_rank = headline_candidate_priority(row, packet['all_seats'][seat])
        if seat not in subjects or subject_rank > subjects[seat][0]:
            subjects[seat] = subject_rank, row
    ordered = sorted(sorted(selected), key=lambda seat: selected[seat][0], reverse=True)
    # A district may have earned its place through a reversal inside the
    # window rather than an endpoint movement. Keep that story if the
    # alternative subject has little net movement to explain.
    return [subjects[seat][1] if abs(subjects[seat][1]['change_pp']) >= 1 else selected[seat][1]
            for seat in ordered[:3]]


class LocalCount(NamedTuple):
    """A checked district bullet and, for one identifiable reporting unit,
    its location and the number of changed ballots in the count described."""
    location: Optional[str]
    changed_ballots: int
    point: str


def supporting_booth(packet, row, minimum_ballots=10):
    """Use the largest change in ballots for which this candidate has a
    comparison against the earlier estimate at the same booth. A revision
    to a handful of ballots in a large booth receives only its actual size.
    The sentence describes the cumulative share, without assigning the
    district's entire forecast movement to this one observation."""
    seat = row['seat']
    subject = candidate(seat, row['party_id'])
    choices = []
    for event in packet['evidence']:
        if event['kind'] != 'booths' or event.get('seat') != seat:
            continue
        before, after = event.get('before') or {}, event.get('after') or {}
        for ledger in ('TCP', 'FP'):
            if ledger == 'TCP' and seat in packet.get('retained_tcp_only_published_zero_at_both_ends', ()):
                continue
            field = ledger.lower() + '_votes_current'
            old, new = before.get(field, {}), after.get(field, {})
            size = activity.affected_ballots(old, new)
            if ledger == 'TCP':
                size = activity.first_tcp_size(before, after) or size
            if size >= minimum_ballots and sum(new.values()):
                choices.append((-size, ledger != 'TCP', event['record'], ledger))
    for negative_size, _, record, ledger in sorted(choices):
        detail = display.headline_booth_comparison(packet, seat, record, subject, ledger)
        if detail:
            location, point = detail
            return LocalCount(location, -negative_size, point)
    return None


def quiet_count_headline(packet):
    """When the whole report would otherwise be quiet, give at most one
    smaller movement with a local count readers can check. Half the usual
    one-point cutoff and 100 changed ballots in one reporting unit are
    editorial choices. They keep tiny rechecks and simulation noise from
    supplying a story just because nothing larger happened."""
    eligible = [row for row in packet['win_changes']
                if .5 <= abs(row['change_pp']) < 1 and not row.get('identity_changed')
                and candidate(row['seat'], row['party_id']) in packet['entities']]
    # Two finalists can have equal and opposite movements to the published
    # precision. In that case describe the candidate now more likely to win.
    eligible.sort(key=lambda row: (-round(abs(row['change_pp']), 1), -row['after'], row['seat'], row['party_id']))
    # Give a district's leading contender first consideration even when small
    # movements for other candidates make the two largest changes unequal.
    by_seat = {}
    for row in eligible:
        by_seat.setdefault(row['seat'], []).append(row)
    eligible = []
    for seat, rows in by_seat.items():
        eligible.extend(sorted(rows, key=lambda r: headline_candidate_priority(r, packet['all_seats'][seat]), reverse=True))
    for row in eligible:
        booth = supporting_booth(packet, row, minimum_ballots=100)
        if not booth or any(token not in packet['entities'] for token in display.TOKEN.findall(booth.point)):
            continue
        subject = candidate(row['seat'], row['party_id'])
        if display.decimal(row['after']) == '100.0':
            outlook = f'the forecast now overwhelmingly favours {subject} to win'
        else:
            outlook = 'the win chance for '+subject+' '+movement(row['after'], row['change_pp'])
        headline = 'In {seat:'+row['seat']+'}, '+outlook+'. '+booth.point
        if display.other_local_counts_changed(packet, row['seat']):
            headline += ' Other local counts also changed.'
        return headline
    return None


def count_detail(packet, seat):
    """Put a continuing district TCP in perspective. The remaining share
    compares the same opponents and comes from the district forecast and
    estimated final turnout, including possible count revisions."""
    count = packet.get('close_tcp_counts', {}).get(seat)
    if not count:
        return None
    first, other = [candidate(seat, p) for p in count['pair']]
    before, after = count['lead_before'], count['lead_after']
    if before == after:
        return None
    if before < 0 < after:
        unit = 'vote' if before == -1 else 'votes'
        text = f'In the TCP count, {first} went from **{-before:,} {unit}** behind {other} to **{after:,}** ahead.'
    elif after == 0:
        text = f'{first} and {other} are now level in the TCP count.'
    elif before > 0:
        direction = 'grew' if after > before else 'narrowed'
        text = (f'The counted TCP lead for {first} over {other} {direction} '
                f'from **{before:,}** to **{after:,}** votes.')
    else:
        unit = 'vote' if after == 1 else 'votes'
        text = f'{first} now leads {other} by **{after:,} {unit}** in the TCP count.'
    remaining = count.get('remaining_after')
    if remaining is not None and remaining > 0:
        share = count.get('approx_remaining_tcp_percent_for_first')
        if share is not None and count.get('count_and_projection_diverge'):
            text += (f' The forecast puts {first} on about **{display.decimal(share)}%** of the '
                     f'**{remaining:,}** TCP votes projected to remain, allowing for revisions.')
        else:
            text += ' '+display.projected_remaining_votes(remaining)
    elif remaining == 0:
        text += ' '+display.projected_remaining_votes(remaining)
    median = count.get('projected_median_percent_for_first')
    if median is not None and abs(median - 50) <= 5:
        # A hundred-vote lead has different implications at the beginning and
        # end of counting. Give the median final share for the same opponents
        # when the forecast itself puts them within five points of 50%.
        text += f' The projected final share for {first} against {other} is **{display.decimal(median)}%**.'
    return text


def preference_detail(packet, seat):
    """Name a supporting district only when the live preference diagnostics
    have already checked its contribution for these opponents."""
    context = packet.get('district_preference_evidence', {}).get(seat)
    if context:
        favoured = context['ordinary_preference_shift_favours']
        other = next(p for p in context['pair'] if p != favoured)
        donor = '{seat:'+context['strongest_supporting_district']+'}'
        return (f'Ordinary booth counts in {donor} strengthened the estimated preference '
                f'flow to {candidate(seat, favoured)} against {candidate(seat, other)}.')
    return None


def retained_count_detail(packet, seat):
    """A recheck can change how much earlier TCP evidence the forecast can
    still use after a pairing switch. Describe that loss of coverage only
    when the comparison traces it to a material change for the same pair."""
    revision = packet.get('retained_pair_revisions', {}).get(seat)
    if not revision:
        return None
    favoured = revision['reestimated_booths_favour_more_than_before']
    opponent = next(p for p in revision['pair'] if p != favoured)
    before, after = revision['usable_pair_votes_before_after']
    examples = ' and '.join(revision['booths_no_longer_usable_examples'])
    return (f"The forecast could still use **{after:,}** of the **{before:,}** earlier TCP votes "
            f"for {candidate(seat, favoured)} against {candidate(seat, opponent)}. "
            f"It had to estimate the split at {examples} again, shifting those booths towards "
            f"{candidate(seat, favoured)}. The published district TCP stayed at zero.")


def ranked_count_detail(packet, seat, party):
    """Use the earlier ranked forecast for the same ballots where a booth-level
    comparison cannot be made. Known TCP replaces an estimate for some of these
    ballots. Both percentages still refer to the entire original group."""
    groups = [group for group in packet.get('ranked_tcp_groups', {}).get(seat, [])
              if party in group['pair'] and group['newly_counted'] >= 10
              and abs(group['first_share_after'] - group['first_share_before']) >= .5]
    if not groups:
        return None
    group = max(groups, key=lambda item: item['newly_counted'])
    first, second = group['pair']
    before, after = group['first_share_before'], group['first_share_after']
    opponent = second
    if party == second:
        before, after = 100-before, 100-after
        opponent = first
    opening, location = 'New TCP counts', None
    booths = group['largest_reporting_booths']
    if group['units'] == 1 and booths:
        if booths[0].casefold() == group['category'].casefold():
            location = 'the ' + booths[0] + ' count'
            opening = 'The TCP update in ' + location
        else:
            location = 'the TCP count at ' + booths[0]
            opening = 'The TCP count at ' + booths[0]
    point = (f"{opening} accounted for **{group['newly_counted']:,}** of the **{group['ballots']:,}** "
            f"{group['category'].lower()} votes awaiting preferences at the earlier update. "
            f"Across that whole group, the estimated share for {candidate(seat, party)} "
            f"against {candidate(seat, opponent)} went from **{display.decimal(before)}%** to **{display.decimal(after)}%**.")
    return LocalCount(location, group['newly_counted'], point)


def count_revision_headline(packet):
    """A substantial administrative correction can matter even in a safe seat.
    Report at most one. A matched transfer names both categories, counting each
    ballot once. Otherwise look for a fall in a booth's FP total or a change
    in candidate allocations at the same total. A disappearance or a TCP reset
    alone cannot establish a booth recheck."""
    stories = []
    for row in packet.get('category_transfers', []):
        size = max(row['fp_votes'], row['tcp_votes'])
        ledger = 'first-preference' if size == row['fp_votes'] else 'two-candidate-preferred'
        text = (f"In {{seat:{row['seat']}}}, **{size:,}** {ledger} votes appear to have been "
                f"moved from the {row['from']} count to the {row['to']} count.")
        stories.append((size, text))
    for event in packet['evidence']:
        if event['kind'] != 'booths' or not event.get('seat'):
            continue
        before = (event.get('before') or {}).get('fp_votes_current') or {}
        after = (event.get('after') or {}).get('fp_votes_current') or {}
        new_total = sum(after.values())
        if new_total == 0:
            continue
        name = event['after'].get('name') or event['record'].rsplit('/', 1)[-1]
        removed = sum(before.values()) - new_total
        if removed >= 100:
            text = (f"In {{seat:{event['seat']}}}, the reported first-preference total for {name} "
                    f"fell by **{removed:,} votes** to **{new_total:,}**.")
            stories.append((removed, text))
        elif (removed == 0 and before.keys() == after.keys() and event['before'].get('count_to_projection_ids')
              == event['after'].get('count_to_projection_ids')):
            moved = activity.affected_ballots(before, after)
            text = (f"In {{seat:{event['seat']}}}, the allocation of **{moved:,}** first preferences between "
                    f"candidates changed at {name}. The total stayed at **{new_total:,} votes**.")
            stories.append((moved, text))
    # This threshold is for a headline about the administration of the count.
    # Smaller rechecks can still feature through their effect on a close race.
    material = [(size, text) for size, text in stories if size >= 100]
    return max(material)[1] if material else None


def district_story(packet, row, endpoints):
    seat = row['seat']
    subject = candidate(seat, row['party_id'])
    after, delta = row['after'], row['change_pp']
    scope = '{seat:'+seat+'}'
    if display.decimal(after) == '100.0':
        outcome = f'The forecast overwhelmingly favours {subject} to win.'
    elif abs(delta) >= .05:
        outcome = f'The win chance for {subject} '+movement(after, delta)+'.'
    else:
        outcome = f'The forecast gives {subject} a **{display.decimal(after)}%** win chance.'
    # If much of a candidate's gain came alongside another candidate losing
    # ground, the second win chance tells readers who else is in contention.
    other_moves = [r for r in packet['win_changes'] if r['seat'] == seat and r['party_id'] != row['party_id']
                   and not r.get('identity_changed') and r['change_pp']*delta < 0
                   and abs(r['change_pp']) >= max(3, abs(delta)*.5)
                   and candidate(seat, r['party_id']) in packet['entities']]
    if other_moves:
        other = max(other_moves, key=lambda r: abs(r['change_pp']))
        other_subject = candidate(seat, other['party_id'])
        outcome += f" The win chance for {other_subject} "+movement(other['after'], other['change_pp'])+'.'
    points = [outcome]
    low, high = row['minimum_within_window'], row['maximum_within_window']
    if high - low - abs(delta) >= 10:
        points.append(f'The win chance for {subject} ranged from **{display.decimal(low)}%** to '
                      f'**{display.decimal(high)}%** during the period.')
    count = follow_up = None
    if row['party_id'] in packet.get('close_tcp_counts', {}).get(seat, {}).get('pair', ()):
        count = count_detail(packet, seat)
        follow_up = display.continued_pair_point(seat, packet)
    prefs = preference_detail(packet, seat)
    retained = retained_count_detail(packet, seat)
    booth = ranked_count_detail(packet, seat, row['party_id']) or supporting_booth(packet, row)
    matchup = pairing_commentary.explain(row, endpoints)
    if booth and any(token not in packet['entities'] for token in display.TOKEN.findall(booth.point)):
        booth = None
    if abs(delta) >= 1:
        verb = 'rose' if delta > 0 else 'fell'
        headline = f'In {scope}, the win chance for {subject} {verb} **{display.decimal(abs(delta))} pp**'
        # A comparable booth result helps readers locate the counting.
        # Give it headline space only if its changed ballots account for at
        # least a quarter of the local activity in this window. The timing
        # alone cannot tell us how much of the win chance movement it caused.
        local_ballots = packet.get('observed_count_changes_by_seat', {}).get(seat, {}).get('affected_votes', 0)
        if (booth and booth.location and local_ballots and booth.changed_ballots >= .25 * local_ballots
                and not packet.get('input_revision_changed')):
            if display.other_local_counts_changed(packet, seat):
                headline += f' with updates to {booth.location} and other local counts'
            else:
                headline += f' as {booth.location} was updated'
        elif follow_up and not packet.get('input_revision_changed'):
            headline += ' as more votes were counted for the revised TCP pairing'
        headline += '.'
    else:
        headline = f'In {scope}, the forecast puts {subject} on a **{display.decimal(after)}%** win chance.'
    for point in (matchup, follow_up, count, retained, prefs, booth.point if booth else None):
        if point and all(token in packet['entities'] for token in display.TOKEN.findall(point)):
            points.append(point)
    if ((packet.get('count_activity') or {}).get('known') and not prefs and not retained
            and not packet.get('input_revision_changed') and 'observed_count_changes_by_seat' in packet
            and seat not in packet['observed_count_changes_by_seat']):
        points.append('The two forecasts show the same local booth counts.')
    return headline, {'seats': [seat], 'text': '\n- '.join([f'In {scope}:', *points])}


def write(packet, endpoints=None):
    """Return a summary and district bullets from the frozen window, together
    with the display identities needed by this edition. A change of model
    inputs can affect every movement, even in a seat with new counts."""
    entities = display.entities(packet)
    packet = dict(packet, entities=entities,
                  display_figures=display.figures_with_booth_context(packet, require_tcp_comparison=True))
    headlines = election_headlines(packet, endpoints)
    sections = []
    for row in district_order(packet, entities):
        headline, section = district_story(packet, row, endpoints)
        headlines.append(headline)
        sections.append(section)
    revision = count_revision_headline(packet)
    if revision and all(t in entities for t in display.TOKEN.findall(revision)):
        headlines.append(revision)
    if packet.get('input_revision_changed'):
        headlines.insert(0, 'Forecast inputs also changed during this period.')
    if not headlines and (packet.get('count_activity') or {}).get('known'):
        local = quiet_count_headline(packet)
        if local:
            headlines.append(local)
    if headlines:
        summary = '\n- '.join(['Key changes:', *headlines])
    elif (packet.get('count_activity') or {}).get('known'):
        summary = 'Counting was updated with little change in the projected winners.'
    else:
        summary = 'There is insufficient comparable count evidence to explain this update.'
    used = set(display.TOKEN.findall(summary))
    for section in sections:
        used.update(display.TOKEN.findall(section['text']))
    return summary, sections, {token: entities[token] for token in sorted(used)}


def append_editions(results, requests, forecast_id, load_summary=None):
    """At publication, append the independent report after the original
    developments. Preserve the AI status and receipt, even on timeout. Several
    windows can share a pair of forecasts and therefore both editions. A local
    no-count notice already serves both readers without a second edition."""
    editions = {}
    combined = dict(results)
    for window, request in requests.items():
        key = request['key']
        if key not in editions:
            try:
                packet = request['packet']
                # Use the completed forecasts named by this window. The cached
                # summaries hold pairing results and statewide seat totals locally, so this
                # explanation requires no extra model request or prompt data.
                endpoints = None
                if load_summary:
                    try:
                        endpoints = load_summary(packet['reference_forecast_id']), load_summary(forecast_id)
                    except (OSError, ValueError) as error:
                        # A saved comparison can still supply the ordinary
                        # count stories if an older diagnostic is unavailable.
                        print(f'Simulation detail unavailable for {forecast_id}/{window}: {error}', flush=True)
                editions[key] = write(packet, endpoints)
            except Exception as error:
                # A trial writer failure must leave an otherwise complete
                # numerical publication and the primary commentary available.
                print(f'Rule-based commentary unavailable for {forecast_id}/{window}: {error}', flush=True)
                editions[key] = None
        if editions[key] is None:
            continue
        summary, sections, entities = editions[key]
        primary = results[window]
        text = primary.get('text') or {}
        divider = {'seats': [], 'text': MARKER+'\n'+summary}
        combined[window] = dict(primary, entities={**entities, **primary.get('entities', {})},
            text=dict(text, developments=[*text.get('developments', []), divider, *sections]))
    return combined
