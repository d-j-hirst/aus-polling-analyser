"""Prepare readable figures and safely render commentary for local inspection.

The model writes ordinary prose with bold numerical statements, plus references
to candidates and seats which the website can decorate. Numerical statements
are prepared from the forecast, rounded only for presentation, and checked
against those statements on return. This checks quantities and their district;
it does not prove a narrative explanation of why the forecast moved.
"""
import html
import math
import re
from collections import Counter
from . import commentary_counts


TOKEN = re.compile(r'\{(?:seat|candidate|party):[^{}]+\}')
BOLD = re.compile(r'\*\*([^*]+)\*\*')


def decimal(value, signed=False):
    value = round(value, 1)
    if value == 0:
        value = 0.0
    return format(value, '+.1f' if signed else '.1f')


def entities(packet):
    """Identify candidates explicitly, including multiple independents in a seat.

    Holder and margin describe the seat entering the election, not its projected
    winner. Older saved packets may lack this metadata; never invent a badge.
    """
    result = {'{party:'+party+'}': {'kind': 'party', 'label': name, 'party': name}
              for party, name in sorted(packet['parties'].items())}
    for seat in sorted(packet['all_seats']):
        holder = packet.get('seat_holders', {}).get(seat, {})
        result['{seat:'+seat+'}'] = {'kind': 'seat', 'seat': seat, 'label': seat,
            'holder_party': packet['parties'].get(holder.get('party_id')),
            'holder_margin': holder.get('margin')}
        candidates = packet.get('all_candidate_identities', packet.get('candidate_identities', {})).get(seat, {})
        for party, identity in sorted(candidates.items()):
            result['{candidate:'+seat+':'+party+'}'] = {'kind': 'candidate', 'seat': seat,
                'party_id': party, 'party': identity.get('party'),
                'label': identity.get('name') or identity.get('party') or party}
    return result


def expectation_difference_fields(value):
    """Describe the same share difference numerically and in natural prose.

    For example, -13.7 points becomes "13.7 pp below expectations". The value
    describes a difference from the reference forecast, not a change in win odds.
    """
    rounded = round(value, 1)
    if rounded > 0:
        direction = 'above expectations'
    elif rounded < 0:
        direction = 'below expectations'
    else:
        direction = 'at expectations'
    return {'value': decimal(value, True)+' pp', 'magnitude': decimal(abs(value))+' pp',
            'direction': direction}


def tcp_comparison_figures(event):
    """Put matched TCP evidence into the same checked vocabulary as FP evidence.

    Expected and observed shares deliberately have their own labels: these are
    not the old and new counted shares. The equivalent vote difference scales
    the previous forecast to the current count's size and is rounded to whole
    votes for prose. It is not an actual batch addition or a causal attribution.
    """
    comparison = event.get('tcp_against_previous_forecast') or {}
    if not comparison.get('available'):
        return []
    seat = event['seat']
    rows = [{'seat': seat, 'booth': event['record'], 'quantity': 'reported cumulative TCP total for this pairing',
             'pair': ['{candidate:'+seat+':'+p+'}' for p in comparison['pair']],
             'value': format(round(comparison['counted_votes']), ',')+' votes'}]
    for party, share in sorted(comparison['shares'].items()):
        rows.append({'seat': seat, 'booth': event['record'],
                     'quantity': 'observed TCP share versus reference forecast for the same pair',
                     'candidate': '{candidate:'+seat+':'+party+'}',
                     'expected_share': decimal(share['expected_percent'])+'%',
                     'observed_share': decimal(share['observed_percent'])+'%',
                     'equivalent_vote_difference': format(round(share['equivalent_vote_difference']), '+,')+' votes',
                     **expectation_difference_fields(share['difference_pp'])})
    return rows


def observed_booth_result(event, ledger, candidates):
    """Describe a changed booth count as a share of all formal votes reported
    in that count so far. Include every ballot in the denominator, even for
    candidates without a display name. A zero for a candidate is meaningful
    when other candidates have votes. A missing or withdrawn count has no share.
    FP and TCP are separate counts and can cover the same ballots."""
    before, after = event.get('before') or {}, event.get('after') or {}
    field = ledger + '_votes_current'
    votes = after.get(field) or {}
    if not votes or votes == before.get(field):
        return []
    if any(isinstance(v, bool) or not isinstance(v, (int, float))
           or not math.isfinite(v) or v < 0 or v != int(v) for v in votes.values()):
        return []
    total = int(sum(votes.values()))
    if total == 0:
        return []
    seat = event['seat']
    aliases = dict(before.get('count_to_projection_ids') or {}) if ledger == 'tcp' else {}
    aliases.update(after.get('count_to_projection_ids') or {})
    identity = {raw: str(aliases.get(raw, raw)) for raw in votes}
    occurrences = Counter(identity.values())
    context = {'seat': seat, 'booth': event['record']}
    if ledger == 'tcp':
        # Matched TCP evidence already has its total and shares. For a new
        # pairing we can report a measured split without a prior expectation.
        if (event.get('tcp_against_previous_forecast') or {}).get('available'):
            return []
        if len(votes) != 2 or len(occurrences) != 2:
            return []
        if not all(p in candidates for p in occurrences):
            return []
        pair = ['{candidate:'+seat+':'+p+'}' for p in sorted(occurrences)]
        total_row = dict(context, quantity='reported cumulative TCP total for this pairing', pair=pair)
    else:
        total_row = dict(context, quantity='reported cumulative FP total')
    total_row['value'] = f'{total:,} votes'
    rows = [total_row]
    for raw, count in sorted(votes.items()):
        party = identity[raw]
        # Several ballot entries can share a forecast party. Their combined
        # votes cannot be attributed to one named candidate.
        if party in candidates and occurrences[party] == 1:
            rows.append(dict(context, quantity='share of reported '+ledger.upper()+' votes',
                             candidate='{candidate:'+seat+':'+party+'}',
                             observed_share=decimal(100*count/total)+'%'))
    return rows


def figures_with_booth_context(packet, *, require_tcp_comparison=False):
    """Extend the checked display figures for the booth-focused prompt. Saved
    reports can lack district progress or a prior forecast for the booth. Only
    publish the context actually available for this comparison window."""
    rows = list(packet.get('display_figures') or figures(packet))
    roster = packet.get('all_candidate_identities', packet.get('candidate_identities', {}))
    for event in packet['evidence']:
        if event['kind'] == 'booths':
            candidates = roster.get(event['seat'], {})
            # Matched TCP totals, shares and gaps already come from figures().
            # Earlier prompts also offered a quotable split without an expectation.
            for ledger in (('fp',) if require_tcp_comparison else ('fp', 'tcp')):
                rows.extend(observed_booth_result(event, ledger, candidates))
    for seat, proportion in packet.get('district_fp_completion', {}).items():
        rows.append({'seat': seat, 'quantity': 'estimated district FP count completion',
                     'value': decimal(100*proportion)+'%'})
    return rows


def replacement_measures(match_tcp_coverage):
    """The current table compares the same reporting units. Archived prompts
    through version 30 used a district median in the fourth column."""
    estimate = ('earlier same-pair share for first candidate in these reporting units, weighted by ballots counted'
                if match_tcp_coverage else 'earlier district forecast for first candidate against this opponent')
    return ('reporting units counted for replacement TCP', 'total measured replacement TCP',
            'first candidate share of measured replacement TCP', estimate,
            'matched non-finalist first preferences', 'share of matched non-finalist first preferences to first candidate')


def replacement_figures(seat, count, *, match_tcp_coverage=True):
    """Give the writer checked figures for the newly counted opponents. Pair
    order identifies whose share is being quoted. Distinguish the booth sample
    from the earlier whole-district forecast and its still-uncounted voters."""
    first, second = count['pair']
    pair = ['{candidate:'+seat+':'+p+'}' for p in (first, second)]
    unit_count = count['units']
    def row(quantity, value):
        return {'seat': seat, 'quantity': quantity, 'pair': pair, 'value': value}
    total = count['counted_votes']
    units, votes, share, estimate, pool_size, flow = replacement_measures(match_tcp_coverage)
    unit_label = count.get('unit_label', 'reporting unit') + ('s' if unit_count != 1 else '')
    rows = [row(units, f"{unit_count} {unit_label}"),
            row(votes, f'{total:,} votes'),
            row(share, decimal(100*count['counts'][first]/total)+'%')]
    previous = count.get('previous_matched_booths_first_percent' if match_tcp_coverage else 'previous_district_median_first_percent')
    if previous is not None:
        rows.append(row(estimate, decimal(previous)+'%'))
    transfers = count.get('transfers_from_other_first_preferences')
    if transfers:
        pool = sum(transfers.values())
        rows.extend([row(pool_size, f'{pool:,} votes'), row(flow, decimal(100*transfers[first]/pool)+'%')])
    return rows


def booth_figures(event):
    """Checked booth changes used both to select evidence within the request
    size allowance and to render the same evidence in the final request."""
    rows = []
    for key in ('changed_votes', 'fp_vote_change', 'tcp_vote_change'):
        if key == 'changed_votes' and 'replacement_tcp_votes' in event:
            continue
        if key in event:
            rows.append({'seat': event.get('seat'), 'booth': event['record'], 'quantity': key,
                         'value': format(round(event[key]), ',')+' votes'})
    for party, value in sorted(event.get('fp_share_minus_previous_expectation_pp', {}).items()):
        rows.append({'seat': event.get('seat'), 'booth': event['record'], 'quantity': 'FP share minus previous booth expectation',
                     'candidate': '{candidate:'+event['seat']+':'+party+'}', **expectation_difference_fields(value)})
    rows.extend(tcp_comparison_figures(event))
    if 'replacement_tcp_votes' in event and not (event.get('tcp_against_previous_forecast') or {}).get('available'):
        rows.append({'seat': event['seat'], 'booth': event['record'], 'quantity': 'size of counted replacement pairing',
                     'value': f"{event['replacement_tcp_votes']:,} votes"})
    return rows


def figures(packet, *, use_median_seats=True, match_tcp_coverage=True, contextualise_counts=False):
    """Supply exact, ready-to-copy display phrases instead of repeated fact IDs.

    Probability/share levels use %, while their differences use pp (percentage
    points). Counted votes and median seats are whole numbers. Archived prompts
    that used mean seats keep their original one-decimal figures.
    """
    rows = []

    def add(seat, quantity, before, after, units='percent', **identity):
        if before is None or after is None:
            return
        if units == 'votes':
            level = lambda x: format(round(x), ',')+' votes'
            change = lambda x: format(round(x), '+,')+' votes'
        elif units == 'median seats':
            level = lambda x: f"{x} {'seat' if x == 1 else 'seats'}"
            change = lambda x: f"{x:+} {'seat' if abs(x) == 1 else 'seats'}"
        else:
            level = lambda x: decimal(x)+('%' if units == 'percent' else ' seats')
            change = lambda x: decimal(x, True)+(' pp' if units == 'percent' else ' seats')
        rows.append({'seat': seat, 'quantity': quantity, **identity,
            'before': level(before), 'after': level(after), 'change': change(after-before),
            'current_with_change': level(after)+' ('+change(after-before)+')'})

    for row in packet['win_changes']:
        candidate = '{candidate:'+row['seat']+':'+row['party_id']+'}'
        add(row['seat'], 'win chance', row['before'], row['after'], candidate=candidate)
        if row['minimum_within_window'] < min(row['before'], row['after']) or row['maximum_within_window'] > max(row['before'], row['after']):
            rows.append({'seat': row['seat'], 'quantity': 'win chance range within this window',
                'candidate': candidate, 'minimum': decimal(row['minimum_within_window'])+'%',
                'maximum': decimal(row['maximum_within_window'])+'%'})
    if contextualise_counts:
        # The movement shortlist can omit a safe seat's favourite. Give the
        # writer a quotable current win chance for counts it might discuss,
        # even when an earlier chance or a measurable change isn't supplied.
        seen = {(r['seat'], r['party_id']) for r in packet['win_changes']}
        count_seats = set(packet.get('close_tcp_counts', {}))
        count_seats.update(e['seat'] for e in packet['evidence'] if e['kind'] == 'booths' and e.get('seat'))
        for seat in sorted(count_seats):
            chances = packet['all_seats'].get(seat, {})
            if chances:
                leader = max(chances, key=chances.get)
                if (seat, leader) not in seen:
                    rows.append({'seat': seat, 'quantity': 'current overall win chance',
                                 'candidate': '{candidate:'+seat+':'+leader+'}', 'value': decimal(chances[leader])+'%'})
    quantities = {'pairing_percent': 'probability of this final pairing',
        'finalist_percent': 'chance of reaching the final two, across all pairings',
        'fp_mean_percent': 'mean projected first preference share',
        'fp_percentiles': 'projected first preference share',
        'tcp_percentiles': 'projected TCP share for FIRST candidate, conditional on this pairing',
        'tpp_percentiles': 'projected Labor TPP share'}
    for district in packet['seat_changes']:
        for row in district['changes']:
            path = row['metric'].strip('/').split('/')
            kind = path[0]
            if kind not in quantities:
                continue
            identity = {}
            if kind in ('pairing_percent', 'tcp_percentiles'):
                identity['pair'] = ['{candidate:'+district['seat']+':'+p+'}' for p in path[1:3]]
            elif kind != 'tpp_percentiles':
                identity['candidate'] = '{candidate:'+district['seat']+':'+path[1]+'}'
            if kind.endswith('percentiles'):
                identity['percentile'] = path[-1]
            add(district['seat'], quantities[kind], row['before'], row['after'], **identity)
    for seat, row in packet.get('retained_pair_revisions', {}).items():
        pair = ['{candidate:'+seat+':'+p+'}' for p in row['pair']]
        add(seat, 'earlier TCP votes still usable for this pair, published TCP zero at both endpoints',
            *row['usable_pair_votes_before_after'], units='votes', pair=pair)
        if 'estimated_preference_share_to_first_before_after' in row:
            add(seat, 'estimated share of other candidates’ preferences going to FIRST candidate from usable local counts',
                *row['estimated_preference_share_to_first_before_after'], pair=pair)
    for seat, groups in packet.get('ranked_tcp_groups', {}).items():
        for group in groups:
            opponents = ['{candidate:'+seat+':'+p+'}' for p in group['pair']]
            scope = group['category']+' ballots in this district awaiting TCP at the earlier forecast'
            add(seat, 'estimated final share for FIRST candidate on '+scope,
                group['first_share_before'], group['first_share_after'], pair=opponents)
            for label, field in [('size of that group', 'ballots'), ('newly counted TCP within it', 'newly_counted')]:
                rows.append({'seat': seat, 'quantity': scope+' — '+label, 'pair': opponents,
                             'value': format(group[field], ',')+' votes'})
    for seat, context in packet.get('district_preference_evidence', {}).items():
        opponents = ['{candidate:'+seat+':'+p+'}' for p in context['pair']]
        for label, number in zip(('earlier', 'current'), context['matched_ordinary_units_before_after']):
            rows.append({'seat': seat, 'quantity': label+' matched ordinary booths in '+context['strongest_supporting_district'],
                         'pair': opponents, 'value': f"{number} {'booth' if number == 1 else 'booths'}"})
        add(seat, 'win chance for FIRST candidate if this pairing occurs',
            *context['first_candidate_conditional_win_percent'], pair=opponents)
        add(seat, 'extra estimated votes for FIRST candidate from preference learning on unresolved FP',
            *context['estimated_extra_first_candidate_votes_from_learning_on_pending_fp'], units='votes', pair=opponents)
    old, new = packet['statewide']['before'], packet['statewide']['after']
    for kind, group in packet.get('tcp_reporting_changes', {}).items():
        count = group['count']
        rows.append({'seat': 'Statewide', 'quantity': kind,
                     'value': f"{count} {'district' if count == 1 else 'districts'}"})
    for row in packet.get('statewide_preference_changes', []):
        donor, destination, other = (packet['parties'][p] for p in row['parties'])
        add('Statewide', 'estimated statewide preference flow', row['before'], row['after'],
            party=f'{donor} first preferences to {destination} over {other}', percentile='50')
    for field, quantity in [('tpp_percentiles', 'projected Labor TPP share')]:
        for q in ('5', '50', '95'):
            add('Statewide', quantity, (old.get(field) or {}).get(q), (new.get(field) or {}).get(q), percentile=q)
    seat_quantity = 'medianSeats' if use_median_seats else 'expectedSeats'
    for field in (seat_quantity, 'majorityWinPc', 'minorityWinPc', 'mostSeatsWinPc', 'overallWinPc'):
        for party in sorted((old.get(field) or {}).keys() & (new.get(field) or {}).keys()):
            if field == 'medianSeats':
                if party in packet['parties']:
                    add('Statewide', 'median seats', old[field][party], new[field][party],
                        units='median seats', party='{party:'+party+'}')
            else:
                add('Statewide', field, old[field][party], new[field][party],
                    units='seats' if field == 'expectedSeats' else 'percent', party=packet['parties'].get(party, party))
    add('Statewide', 'hung parliament chance', old.get('hungParliamentPc'), new.get('hungParliamentPc'))
    for party in sorted((old.get('fp_percentiles_by_party') or {}).keys() & (new.get('fp_percentiles_by_party') or {}).keys()):
        for q in ('5', '50', '95'):
            add('Statewide', 'projected first preference share', old['fp_percentiles_by_party'][party].get(q),
                new['fp_percentiles_by_party'][party].get(q), party=packet['parties'].get(party, party), percentile=q)
    for key in ('fp_votes', 'tcp_votes'):
        add('Statewide', key, (old.get('countProgress') or {}).get(key), (new.get('countProgress') or {}).get(key), units='votes')
    transfer_seats = {t['seat'] for t in packet.get('category_transfers', [])}
    for seat, counts in sorted(packet.get('observed_count_changes_by_seat', {}).items()):
        for key, value in sorted(counts.items()):
            # The activity filter counts both changed records in a transfer.
            # Its gross size would be misleading as a count of distinct votes.
            if key == 'affected_votes' and seat in transfer_seats:
                continue
            rows.append({'seat': seat, 'quantity': key, 'value': format(round(value), ',')+' votes'})
    # Both ledgers may describe the same transferred ballots. The district
    # net changes above include any unrelated additions in the same update.
    for transfer in packet.get('category_transfers', []):
        for ledger in ('fp', 'tcp'):
            votes = transfer[ledger+'_votes']
            if votes:
                rows.append({'seat': transfer['seat'], 'booth': transfer['from']+' → '+transfer['to'],
                             'quantity': 'apparent category transfer ('+ledger.upper()+')',
                             'value': f'{votes:,} votes'})
    for seat, counts in sorted(packet.get('replacement_tcp_counts', {}).items()):
        rows.extend(replacement_figures(seat, counts, match_tcp_coverage=match_tcp_coverage))
    for seat, counts in sorted(packet.get('close_tcp_counts', {}).items()):
        pair = ['{candidate:'+seat+':'+p+'}' for p in counts['pair']]
        remaining_share = counts.get('approx_remaining_tcp_percent_for_first')
        if contextualise_counts and remaining_share is not None:
            rows.append({'seat': seat, 'quantity': 'approximate share of net remaining TCP votes for FIRST candidate, including revisions',
                         'pair': pair, 'value': decimal(remaining_share)+'%'})
        add(seat, 'counted TCP lead for FIRST candidate (negative means behind)',
            counts['lead_before'], counts['lead_after'], units='votes', pair=pair)
        for party, votes in counts['net_by_candidate'].items():
            rows.append({'seat': seat, 'quantity': 'net change in counted TCP votes for candidate',
                         'candidate': '{candidate:'+seat+':'+party+'}', 'value': f'{votes:,} votes'})
        old_remaining, new_remaining = counts['remaining_before'], counts['remaining_after']
        quantity = 'estimated net TCP votes still to come for this pair, including revisions'
        if old_remaining is not None and new_remaining is not None:
            add(seat, quantity, old_remaining, new_remaining, units='votes', pair=pair)
        elif new_remaining is not None:
            rows.append({'seat': seat, 'quantity': quantity, 'pair': pair, 'value': f'{new_remaining:,} votes'})
    # Booth observations can support prose about a large correction even when
    # win probabilities did not move. Keep these in the same checked vocabulary.
    for event in packet['evidence']:
        if event['kind'] != 'booths':
            continue
        rows.extend(booth_figures(event))
    # Deterministic ordering is necessary for exact prompt-prefix reuse and
    # reproducible benchmark identities across Python processes.
    return rows


CONTEXT_FIGURE_FIELDS = ('minimum', 'maximum', 'value', 'magnitude', 'direction',
                         'expected_share', 'observed_share', 'equivalent_vote_difference')
FIGURE_FIELDS = ('before', 'after', 'change', 'current_with_change',
                *(field for field in CONTEXT_FIGURE_FIELDS if field != 'direction'))


def observed_vote_phrases(packet, seats):
    """Permit actual candidate counts already supplied in selected booth evidence.

    The prompt carries raw counts, even when its compact display table has only
    booth totals. Checking only that table rejected a correct "83 votes" quote.
    Accept whole non-negative FP/TCP counts from those same before/after records,
    restricted to the paragraph's districts. Do not admit projected totals,
    arbitrary numbers elsewhere in the data, or counts omitted from the prompt.
    This avoids repeating every candidate count in a second prompt table.
    """
    allowed = set()
    for event in packet['evidence']:
        if event['kind'] != 'booths' or (seats and event.get('seat') not in seats):
            continue
        for observation in ('before', 'after'):
            record = event.get(observation) or {}
            for field in ('fp_votes_current', 'tcp_votes_current'):
                for value in (record.get(field) or {}).values():
                    if isinstance(value, bool) or not isinstance(value, (int, float)):
                        continue
                    if math.isfinite(value) and value >= 0 and value == int(value):
                        allowed.add(format(int(value), ',')+' votes')
    return allowed


def interval_phrases(rows):
    """Pair endpoints only for the same district, race, measure and forecast.

    Before/after endpoints are individually present in the prompt. Combining
    the lower endpoint from one forecast with the upper from another would
    describe a range that neither forecast made.
    """
    intervals = {}
    for row in rows:
        percentile = row.get('percentile')
        if percentile not in ('5', '95'):
            continue
        key = (row['seat'], row['quantity'], row.get('candidate'), row.get('party'), tuple(row.get('pair', [])))
        intervals.setdefault(key, {})[percentile] = row
    ranges = set()
    for endpoints in intervals.values():
        if '5' in endpoints and '95' in endpoints:
            for period in ('before', 'after'):
                ranges.add((endpoints['5'][period], endpoints['95'][period]))
    return bool(intervals), ranges


COUNT_NUMBER = commentary_counts.NUMBER
COUNT_WORDS = commentary_counts.UNIT


def typed_vote_phrases(packet, seats):
    """Keep a named category or count type attached to its own booth. The
    published change and current count can differ when counting is partial."""
    phrases = set()
    for transfer in packet.get('category_transfers', []):
        if seats and transfer['seat'] not in seats:
            continue
        for ledger in ('fp', 'tcp'):
            count = transfer[ledger+'_votes']
            if count:
                phrases.add(f'{count:,} {ledger} votes')
    for seat, counts in packet.get('close_tcp_counts', {}).items():
        if not seats or seat in seats:
            for count in counts['net_by_candidate'].values():
                if count >= 0:
                    phrases.add(f'{count:,} tcp votes')
    for seat, count in packet.get('replacement_tcp_counts', {}).items():
        if not seats or seat in seats:
            for label in ('tcp', 'two-candidate', 'two-candidate-preferred'):
                phrases.add(f"{count['counted_votes']:,} {label} votes")
    for event in packet['evidence']:
        if event['kind'] != 'booths' or (seats and event.get('seat') not in seats):
            continue
        record = event.get('after') or {}
        vote_type = str(record.get('vote_type', '')).lower()
        category = {'prepoll': 'pre-poll'}.get(vote_type, vote_type)
        for kind, labels in [('fp', ['fp', 'first-preference', 'primary']),
                             ('tcp', ['tcp', 'two-candidate', 'two-candidate-preferred'])]:
            observed = record.get(kind+'_votes_current') or {}
            values = [event.get(kind+'_vote_change')]
            if observed:
                values.append(sum(observed.values()))
            for count in values:
                if not isinstance(count, (int, float)) or count < 0 or count != int(count):
                    continue
                for name in [*labels, category, *(category+' '+label for label in labels)]:
                    if name.strip():
                        phrases.add(f'{int(count):,} {name} votes')
    return phrases


def equivalent_vote_count(match, following, allowed, typed):
    """A count can modify a noun ('429-vote count') or take its unit from the
    following words ('429 TCP votes'). A bare number alone has no unit."""
    phrase = match.group(1)
    count = re.fullmatch('('+COUNT_NUMBER+r')[-\u2011]vote', phrase)
    if count:
        return count[1]+' votes' in allowed
    if re.fullmatch(COUNT_NUMBER, phrase):
        unit = re.match(r'\s+(first preferences|formal votes|'+COUNT_WORDS+r')\b', following, re.I)
        if not unit:
            return False
        phrase += ' '+unit[1]
    phrase = phrase.lower().replace(' additional ', ' ')
    return phrase in typed or phrase in allowed


class MissingBoothTcpComparison(ValueError):
    """An optional booth point cannot give a checked same-pair comparison."""


# A declaration category can also be the name of a reporting unit. In
# “the estimated share among provisional ballots awaiting TCP fell from … to …”,
# both percentages describe the same group of ballots. A separate statement
# that a candidate received a share at Provisional describes a measured result.
TCP_LABEL = r'(?:TCP|2CP|two[- ]candidate(?:[- ]preferred)?)'
PENDING_BALLOTS = (
    r'(?:among|for|across|on) (?:the )?(?P<category>[\w-]+(?: [\w-]+){0,2}) '
    r'(?:ballots|votes) (?:still )?awaiting (?:a |the |their )?'+TCP_LABEL+
    r'(?: count)?(?: at the (?:earlier|previous) (?:forecast|update))?')
PROJECTED_SHARE = (
    r'(?:the )?(?:estimated|projected) (?:final )?(?:'+TCP_LABEL+r' )?share (?:for|of) '
    r'\{candidate:(?P<seat>[^}:]+):(?P<party>[^}:]+)\}'
    r'(?: against (?P<opponent>\{candidate:[^}:]+:[^}:]+\}))?')
PROJECTED_MOVEMENT = (
    r' (?P<verb>fell|rose|decreased|increased|went|moved|changed) from '
    r'(?P<levels>\*\*(?P<before>\d+(?:\.\d+)?)%\*\* to \*\*(?P<after>\d+(?:\.\d+)?)%'
    r'(?: \((?P<change>[+-]\d+(?:\.\d+)?) pp\))?\*\*)')
PENDING_SHARE_CHANGES = tuple(re.compile(r'\b'+description+PROJECTED_MOVEMENT, re.I) for description in (
    PROJECTED_SHARE+r' '+PENDING_BALLOTS, PENDING_BALLOTS+r',? '+PROJECTED_SHARE))


def checked_pending_tcp_estimates(text, seats, groups):
    """Check estimates for the ballots awaiting TCP at the earlier forecast.
    The later estimate combines newly counted preferences and the forecast for
    the remainder. Return a checking copy with only the verified percentages
    removed. Any measured booth share alongside them still needs its own
    comparison. The reader receives the original wording."""
    if 'awaiting' not in text.casefold():
        return text

    def check(claim):
        seat, party = claim['seat'], claim['party']
        matching_groups = []
        for group in groups.get(seat, []):
            pair = group['pair']
            if group['category'].casefold() != claim['category'].casefold() or party not in pair:
                continue
            opponent = next(p for p in pair if p != party)
            if claim['opponent'] and claim['opponent'] != '{candidate:'+seat+':'+opponent+'}':
                continue
            matching_groups.append(group)
        # If the writer leaves out the opponent, the available evidence must
        # identify one pairing without using the quoted percentages to guess.
        if (seats and seat not in seats) or len(matching_groups) != 1:
            raise ValueError('No unique projected TCP comparison for these ballots and candidates')
        group = matching_groups[0]
        before, after = group['first_share_before'], group['first_share_after']
        if party != group['pair'][0]:
            before, after = 100-before, 100-after
        # Round the change independently of the endpoints, as on the website.
        delta = after-before
        verb = claim['verb'].lower()
        if (claim['before'] != decimal(before) or claim['after'] != decimal(after)
                or (claim['change'] is not None and claim['change'] != decimal(delta, signed=True))
                or (verb in ('fell', 'decreased') and delta >= 0)
                or (verb in ('rose', 'increased') and delta <= 0)):
            raise ValueError('Projected TCP change does not match the estimates for these ballots and candidates')
        return claim.string[claim.start():claim.start('levels')]+'one estimate to the next'

    for wording in PENDING_SHARE_CHANGES:
        text = wording.sub(check, text)
    return text


def matches_booth_tcp_quote(event, subject, opponent, share, sentence, estimate):
    comparison = event.get('tcp_against_previous_forecast') or {}
    seat, party = subject
    if (not comparison.get('available') or seat != event['seat'] or opponent[0] != seat
            or party == opponent[1] or sorted([party, opponent[1]]) != comparison['pair']):
        return False
    observed = comparison['shares'][party]
    if share != decimal(observed['observed_percent'])+'%':
        return False
    gap = expectation_difference_fields(observed['difference_pp'])
    if gap['direction'] == 'at expectations':
        phrase = r'\bmatching the '+estimate
    else:
        amount, direction = re.escape(gap['magnitude'].split()[0]), gap['direction'].split()[0]
        phrase = r'\*\*'+amount+r' (?:pp|percentage points?)\*\*\s+'+direction+' the '+estimate
    return bool(re.search(phrase+r'\b', sentence, re.I))


def check_booth_tcp_context(text, seats, packet, booth_points):
    """A measured booth split needs a comparison for the same voters and
    opponents. A coincidentally identical figure elsewhere in the district
    cannot establish either the result or the size and direction of its gap.
    The caller has already checked any projected vote-category comparisons."""
    progress = packet.get('statewide', {}).get('before', {}).get('countProgress') or {}
    pre_count = (packet.get('reference_forecast_id') == 'origin'
                 or (progress.get('fp_votes') == 0 and progress.get('tcp_votes') == 0))
    estimate = r'(?:earlier estimate|estimate before counting began)' if pre_count else 'earlier estimate'
    ledger = TCP_LABEL
    for sentence in commentary_counts.SEPARATOR.split(text):
        if not re.search(r'\b'+ledger+r'\b|\bagainst\s+'+commentary_counts.CANDIDATE, sentence, re.I):
            continue
        # Isolate a quoted share. A win chance with a signed change, or a
        # booth revision expressed only in ballots, is a different claim.
        shares = list(re.finditer(r'\*\*(\d+(?:\.\d+)?%)\*\*', sentence))
        if not shares:
            continue
        events = []
        for event in packet.get('evidence', []):
            if event['kind'] != 'booths' or (seats and event['seat'] not in seats):
                continue
            name = (event.get('after') or event.get('before') or {}).get('name') or event['record'].rsplit('/', 1)[-1]
            # Candidate and district placeholders contain the district name,
            # which can also be a booth name (e.g. Mount Gambier).
            if name and re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)', TOKEN.sub('', sentence), re.I):
                events.append(event)
        if not events and not booth_points:
            continue  # A district's projected TCP has its own figures.
        # Primary results can legitimately be followed by a note that TCP is
        # still outstanding. Only check percentages presented as a TCP split.
        for share in shares:
            before, after = sentence[:share.start()], sentence[share.end():]
            follows = re.match(r'\s+(?:of|on)\s+(?:the\s+)?(?:\*\*[\d,]+(?:\*\*\s+|\s+))?'+ledger+r'\b', after, re.I)
            if not follows and re.search(r'\b(?:FP|primary|primaries|first[- ]preferences?|win chance|pairing chance)\b',
                                         before+' '+after, re.I):
                continue
            subjects = re.findall(commentary_counts.CANDIDATE, before)
            opponents = re.findall(r'\bagainst\s+'+commentary_counts.CANDIDATE, after, re.I)
            if not subjects or not opponents or not any(matches_booth_tcp_quote(
                    event, subjects[-1], opponents[0], share[1], sentence, estimate) for event in events):
                raise MissingBoothTcpComparison('Booth TCP share needs its same-pair difference from the earlier estimate')


def other_local_counts_changed(packet, seat):
    """Both editions can single out one booth to illustrate a district's
    movement. Qualify the headline when the comparison also records a change
    at another booth or declaration category in this district."""
    booths = packet.get('booth_changes', {})
    if booths.get('available') is False:
        return False
    if 'seats_with_multiple_changed_units' in booths:
        return seat in booths['seats_with_multiple_changed_units']
    # Older saved comparisons have lists of new and revised booths. Their
    # totals include entries dropped from the shortlist. A smaller recheck
    # can also be visible in the detailed evidence alongside a listed booth.
    district = booths.get('by_seat', {}).get(seat, {})
    groups = [district.get(kind, {}) for kind in ('new', 'updated')]
    if sum(group.get('total', 0) for group in groups) > 1:
        return True
    records = {row['record'] for group in groups for row in group.get('shown', []) if 'record' in row}
    records.update(event['record'] for event in packet.get('evidence', [])
                   if event['kind'] == 'booths' and event.get('seat') == seat and event.get('changed_votes'))
    return len(records) > 1


def booth_location(event):
    """Give a reporting place or declaration count its reader-facing name."""
    after = event.get('after') or {}
    name = after.get('name') or event['record'].rsplit('/', 1)[-1]
    category = after.get('vote_type')
    if category and name.casefold() == category.casefold():
        location = 'the ' + name + ' count'
        return location, 'In ' + location
    return 'the result at ' + name, 'At ' + name


def headline_booth_comparison(packet, seat, record, candidate, count_type):
    """Build the district bullet supporting a local headline from the same
    rounded figures available to the writer. Every value must belong to this
    reporting unit, candidate, count and comparison window. A missing share
    or expectation leaves the forecast movement publishable on its own.
    """
    event = next((e for e in packet['evidence'] if e['kind'] == 'booths'
                  and e.get('seat') == seat and e.get('record') == record), None)
    if not event or count_type not in ('FP', 'TCP'):
        return None
    ledger = count_type.lower() + '_votes_current'
    before = event.get('before') or {}
    after = event.get('after') or {}
    if not after.get(ledger) or after[ledger] == before.get(ledger):
        return None
    rows = {r['quantity']: r for r in packet['display_figures']
            if r['seat'] == seat and r.get('booth') == record and r.get('candidate') in (None, candidate)}
    if count_type == 'FP':
        share = rows.get('share of reported FP votes')
        gap = rows.get('FP share minus previous booth expectation')
        total = rows.get('reported cumulative FP total')
        count = 'first preferences'
    else:
        share = gap = rows.get('observed TCP share versus reference forecast for the same pair')
        total = rows.get('reported cumulative TCP total for this pairing')
        if not total or candidate not in total['pair']:
            return None
        opponent = next(person for person in total['pair'] if person != candidate)
        count = 'two-candidate-preferred votes against ' + opponent
    if not (share and total and gap):
        return None
    number = total['value'].split()[0]
    comparison = 'matching the earlier estimate'
    if gap['direction'] != 'at expectations':
        direction = gap['direction'].split()[0]
        comparison = f"**{gap['magnitude']}** {direction} the earlier estimate"
    location, opening = booth_location(event)
    point = f"{opening}, {candidate} received **{share['observed_share']}** of **{number}** {count}, {comparison}."
    return location, point


# A straightforward booth comparison can be written by the model or supplied by
# the exporter to support a headline. Recognise the two common ways to express
# its denominator. A correction, a TCP split or a sentence with further context
# remains a separate point with its original wording.
SIMPLE_FP_POINT = re.compile(
    r'(?P<opening>(?:At |In ).+?), (?P<candidate>\{candidate:[^{}]+\}) received '
    r'(?P<share>\*\*[\d.]+%\*\*) of \*\*(?P<total>[\d,]+)(?: votes)?\*\* (?:in )?first preferences, '
    r'(?P<gap>\*\*[\d.]+ pp\*\* (?:above|below) (?:the earlier estimate|expectations)|matching the earlier estimate)\.')


def district_booth_openings(events, seat):
    """Match a writer's opening to the reporting unit and its public wording.
    Postal and other declaration totals can arrive under the category name in
    the booth-name field. The writer can therefore call a postal batch “At
    Postal”, while a headline about those same ballots says “In the Postal
    count”. Keep all matching records so an ambiguous name cannot join two
    different counts."""
    records = {}
    for event in events:
        if event['kind'] != 'booths' or event['seat'] != seat:
            continue
        _, opening = booth_location(event)
        name = (event.get('after') or {}).get('name') or event['record'].rsplit('/', 1)[-1]
        for label in {opening, 'At ' + name}:
            records.setdefault(label.casefold(), {})[event['record']] = opening
    return records


def group_booth_primaries(points, seat, packet):
    """Report a booth's first-preference count once, with each selected
    candidate's share and its difference from the earlier estimate. Only join
    comparisons we can reproduce for the same reporting unit and window."""
    if not points:
        return points
    records = district_booth_openings(packet['evidence'], seat)

    output, groups = [], {}
    for point in points:
        match = SIMPLE_FP_POINT.fullmatch(point)
        written_opening = match['opening'] if match else point.partition(', ')[0]
        matches = records.get(written_opening.casefold(), {})
        if len(matches) != 1:
            output.append(point)
            continue
        (record, opening), = matches.items()
        point = opening + point[len(written_opening):]
        if not match:
            # A correction or a TCP count can use the same place name but
            # carries its own information and keeps its own bullet.
            output.append(point)
            continue
        candidate = match['candidate']
        comparison = headline_booth_comparison(packet, seat, record, candidate, 'FP')
        gap = match['gap'].replace(' expectations', ' the earlier estimate')
        claim = f"{candidate} received {match['share']}"
        canonical = f"{opening}, {claim} of **{match['total']}** first preferences, {gap}."
        if not comparison or canonical != comparison[1]:
            output.append(point)
            continue
        if record not in groups:
            groups[record] = (len(output), {candidate})
            output.append(canonical)
        else:
            index, candidates = groups[record]
            if candidate not in candidates:
                output[index] += f" {claim}, {gap}."
                candidates.add(candidate)
    return output


RAW_COUNT = re.compile(r'\b(?:counted|counts?|tally|ballots?|booths?|votes?|first preferences)\b|'
                       r'\b(?:received|polled)\s+\*\*[\d.]+%', re.I)
SEAT_OUTLOOK = re.compile(r'\b(?:win chance|chance of winning(?! (?:that|this|the) (?:matchup|pairing))|'
                          r'favou?rite(?! (?:to (?:reach|face|finish)|for second))|favou?red to (?:win|hold|retain)|'
                          r'moved ahead in the forecast|(?:likely|projected|expected) to (?:hold|retain)|'
                          r'(?:very|highly|overwhelmingly) likely to win|'
                          r'too close to call|(?:forecast|race|contest) (?:remains|is) (?:close|competitive))\b', re.I)


DISTRICT_TALLY = re.compile(r'\b(?:two-candidate(?:-preferred)? (?:count|tally)|TCP (?:count|tally)|'
                            r'counted (?:TCP |two-candidate )?(?:lead|vote)|votes?\*{0,2} (?:ahead|behind))\b', re.I)


def projected_remaining_votes(remaining):
    """Describe the forecast's net outstanding TCP ballots beside a measured
    lead. Even late in counting the turnout estimate can include revisions."""
    if remaining == 0:
        return 'The forecast expects no further net additions to this TCP count.'
    unit = 'vote' if remaining == 1 else 'votes'
    return f'The forecast projects about **{remaining:,}** TCP {unit} still to come.'


def continued_pair_point(seat, packet):
    """Put a further count for already revised opponents in context. Unit
    coverage is compared at the two dates. A growing postal batch can add
    ballots without increasing the number of units reporting."""
    account = packet.get('close_tcp_counts', {}).get(seat, {})
    counts = account.get('continued_realigned_count')
    if not counts:
        return None
    a, b = ['{candidate:'+seat+':'+p+'}' for p in account['pair']]
    if any(token not in packet['entities'] for token in (a, b)):
        return None
    before, after = counts['units_before'], counts['units_after']
    units = 'reporting unit' if after == 1 else 'reporting units'
    text = (f"The revised TCP count between {a} and {b} grew by **{counts['net_added']:,} votes** "
            f"and now covers **{after} {units}**")
    if after != before:
        direction = 'up' if after > before else 'down'
        text += f', {direction} from **{before}**'
    return text+'.'


def add_remaining_to_lead(seat, points, packet):
    """Place expected outstanding ballots beside the observed district lead.
    Only extend a point that gives this pair's actual current lead. A booth
    result by itself gives no reason to add a district turnout figure."""
    account = packet.get('close_tcp_counts', {}).get(seat, {})
    remaining = account.get('remaining_after')
    if remaining is None or remaining < 0:
        return points
    pair = {'{candidate:'+seat+':'+party+'}' for party in account['pair']}
    lead = re.compile(r'(?<![\d,])'+re.escape(format(abs(account['lead_after']), ','))+r'(?![\d,%])')
    # The writer may already have given the quantity elsewhere in the section.
    # Keep its wording, including a remaining share alongside the vote count.
    for sentence in commentary_counts.SEPARATOR.split(' '.join(points)):
        if (re.search(r'(?<![\d,])'+format(remaining, ',')+r'(?![\d,%])', sentence)
                and re.search(r'\b(remain(?:s|ing)?|outstanding|uncounted|to (?:come|count|be counted)|still expected|left)\b', sentence, re.I)):
            return points
    for index, point in enumerate(points):
        names = set(TOKEN.findall(point))
        candidates = {name for name in names if name.startswith('{candidate:')}
        if (candidates and candidates <= pair and DISTRICT_TALLY.search(point) and lead.search(point)
                and not re.search(r'\b(?:primary|first[- ]preferences?|FP)\b', point, re.I)):
            return [*points[:index], point+' '+projected_remaining_votes(remaining), *points[index+1:]]
    return points


def count_outlook(seat, seat_points, booth_points, packet, *, explain_remaining=False):
    """Supply a missing district outlook when the writer reports only what has
    been counted. One explanation covers all the booths in a district section.
    The overview includes seats whose win chances have barely moved, so absence
    from the win-change shortlist does not make a tight raw tally a toss-up."""
    prose = ' '.join([*seat_points, *booth_points])
    if not (booth_points or RAW_COUNT.search(prose)):
        return ''
    chances = packet['all_seats'].get(seat, {})
    if not chances or not max(chances.values()):
        return ''
    party = max(chances, key=chances.get)
    chance = chances[party]
    candidate = '{candidate:'+seat+':'+party+'}'
    if candidate not in packet['entities']:
        return ''
    if SEAT_OUTLOOK.search(prose):
        outlook = ''
    elif decimal(chance) == '100.0':
        # A finite simulation, and then display rounding, can give 100%.
        # Describe the strength of the forecast without declaring a winner.
        outlook = f'The forecast overwhelmingly favours {candidate} to win the seat.'
    else:
        outlook = f'The forecast puts the win chance for {candidate} at **{decimal(chance)}%**.'
    # A booth result alone cannot establish how the rest will vote. Only
    # explain a district tally this way with a comparable remaining-vote
    # estimate, and keep the explanation conditional on its two opponents.
    account = packet.get('close_tcp_counts', {}).get(seat, {})
    share = account.get('approx_remaining_tcp_percent_for_first')
    # A decimal point inside a quoted percentage does not end its sentence.
    prose_without_figures = BOLD.sub('figure', prose)
    describes_remaining = re.search(r'\b(?:remaining|outstanding|uncounted) (?:two-candidate |TCP )?(?:votes?|ballots?)\b'
                                    r'[^.!?]*(?:favou?r|lean|expected to (?:go|break))|'
                                    r'\b(?:projected|expected) to (?:win|receive|take|favour)[^.!?]*\b(?:remaining|outstanding|uncounted)\b',
                                    prose_without_figures, re.I)
    if explain_remaining and share is not None and account.get('count_and_projection_diverge') and not describes_remaining:
        first, other = ('{candidate:'+seat+':'+p+'}' for p in account['pair'])
        for point in seat_points:
            names = set(re.findall(r'\{candidate:[^}]+\}', point))
            same_pair_tally = DISTRICT_TALLY.search(point)
            if (names == {first, other} and same_pair_tally
                    and not re.search(r'\b(?:primary|first[- ]preferences?|FP)\b', point, re.I)):
                outlook = (outlook + f' Against {other}, {first} is projected to receive about **{decimal(share)}%** '
                           'of the remaining two-candidate vote, including revisions.').strip()
                break
    return outlook


def check_text(text, seats, packet, *, booth_points=None):
    """Check inline entities and figures on a copy, preserving the reader's text.
    Prompts requiring a matched benchmark also identify whether the point is a
    booth detail or a broader district statement. Older formats omit this check."""
    if not isinstance(text, str):
        raise ValueError('Commentary text must be a string')
    allowed_entities = packet.get('entities') or entities(packet)
    stripped = checked_pending_tcp_estimates(text, seats, packet.get('ranked_tcp_groups', {}))
    if booth_points is not None:
        check_booth_tcp_context(stripped, seats, packet, booth_points)
    stripped = commentary_counts.checked_vote_changes(stripped, seats, packet)
    if packet.get('replacement_tcp_counts') or packet.get('district_preference_evidence'):
        stripped = re.sub(r'\*\*(\d+)\*\* (reporting units?|booths?)\b', r'**\1 \2**', stripped)
    if packet.get('tcp_reporting_changes'):
        stripped = commentary_counts.checked_reset_districts(stripped, packet['tcp_reporting_changes'])
    # A measured external preference effect can be explained in the receiving
    # district's section. Allow the source district's name there, while its
    # candidates and its own numerical results retain their district scope.
    source_seats = {context[field] for seat in (seats or ())
                    for context in [packet.get('district_preference_evidence', {}).get(seat, {})]
                    for field in ('strongest_supporting_district', 'strongest_offsetting_district') if field in context}
    for token in TOKEN.findall(text):
        if token not in allowed_entities:
            raise ValueError('Unknown commentary entity: '+token)
        source_name = allowed_entities[token].get('kind') == 'seat' and allowed_entities[token]['seat'] in source_seats
        if seats and allowed_entities[token].get('seat') and allowed_entities[token]['seat'] not in seats and not source_name:
            raise ValueError('Commentary entity belongs to a different seat: '+token)
        stripped = stripped.replace(token, '')
    if '{' in stripped or '}' in stripped:
        raise ValueError('Unrecognised commentary placeholder')
    supplied_figures = packet.get('display_figures') or figures(packet)
    scoped = [row for row in supplied_figures if not seats or row['seat'] in seats or row['seat'] == 'Statewide']
    allowed = {row[key] for row in scoped
               for key in FIGURE_FIELDS if key in row}
    allowed.update(observed_vote_phrases(packet, seats))
    typed = typed_vote_phrases(packet, seats)
    # In booth prose "318 first preferences" and "318 first-preference votes"
    # name the same denominator. Only an actual cumulative FP total supplies it.
    for row in scoped:
        if row['quantity'] == 'reported cumulative FP total':
            total = row['value'].split()[0]
            typed.update((total+' first preferences', total+' formal votes'))
    # Positive additions can be quoted with an explicit plus. Only change
    # fields carry a direction; a cumulative vote count supplies no addition.
    for row in scoped:
        if row['quantity'] in ('fp_net_change', 'tcp_net_change', 'fp_vote_change', 'tcp_vote_change'):
            if row['value'][0].isdigit():
                allowed.add('+'+row['value'])
    has_interval, ranges = interval_phrases([r for r in scoped if not seats or r['seat'] in seats])
    if has_interval:
        # The prompt defines this fixed label. It is a numerical forecast
        # claim when used elsewhere, e.g. 'a 90% chance', and still needs a figure.
        stripped = re.sub(r'(?<![\w.+-])(?:\*\*90%\*\*|90%)(?=\s+(?:(?:forecast|projected|conditional|TCP|TPP|credible|prediction|uncertainty)\s+){0,4}(?:interval|range)\b)',
                          'central', stripped, flags=re.I)
    for match in BOLD.finditer(stripped):
        phrase = match.group(1)
        # Readers can see the unit written out while the checked figures and
        # compact prompt tables continue to use "pp" for the same difference.
        phrase = re.sub(r'(?<=\d) percentage points?$', ' pp', phrase)
        if phrase in allowed or equivalent_vote_count(match, stripped[match.end():], allowed, typed):
            continue
        endpoints = re.fullmatch(r'(\d+(?:\.\d+)?%)\s*(?:[–—-]|to)\s*(\d+(?:\.\d+)?%)', phrase)
        if endpoints and endpoints.groups() in ranges:
            continue
        comparison = re.fullmatch(r'(\d+(?:\.\d+)?%)\s+—\s+(\d+(?:\.\d+)? pp)', phrase)
        if comparison and any(row.get('expected_share') == comparison[1] and row.get('magnitude') == comparison[2]
                              and stripped[match.end():].lstrip().startswith(row['direction'])
                              for row in scoped if 'direction' in row):
            continue
        raise ValueError('Unsupported numerical statement: '+phrase)
    stripped = BOLD.sub('', stripped)
    # Booth names can contain digits. Those labels are not numerical claims.
    for event in packet['evidence']:
        for record in (event.get('before'), event.get('after')):
            if isinstance(record, dict) and isinstance(record.get('name'), str):
                stripped = stripped.replace(record['name'], '')
    if re.search(r'\d', stripped):
        raise ValueError('Numerical statements must be bold and match a supplied figure')
    if '**' in stripped or '<' in text or '>' in text:
        raise ValueError('Use only entity placeholders and bold numerical statements, not HTML')


def render(text, entity_map):
    """Escape all prose and labels; support only our entity and bold conventions."""
    pattern = re.compile(r'\{(?:seat|candidate|party):[^{}]+\}|\*\*[^*]+\*\*')
    output, cursor = [], 0
    for match in pattern.finditer(text):
        output.append(html.escape(text[cursor:match.start()]))
        token = match.group()
        entity = entity_map.get(token)
        if entity and entity['kind'] == 'party':
            output.append('<small class="badge">'+html.escape(entity['label'])+'</small>')
        elif entity:
            label = '<b>'+html.escape(entity['label'])+'</b>'
            badge = entity.get('party') if entity['kind'] == 'candidate' else entity.get('holder_party')
            if badge:
                label += ' <small class="badge">'+html.escape(badge)+'</small>'
            margin = entity.get('holder_margin')
            if entity['kind'] == 'seat' and margin is not None:
                label += ' <small>'+html.escape(decimal(margin)+'%')+'</small>'
            output.append(label)
        elif token.startswith('**'):
            output.append('<strong>'+html.escape(token[2:-2])+'</strong>')
        else:
            output.append(html.escape(token))
        cursor = match.end()
    output.append(html.escape(text[cursor:]))
    return ''.join(output)
