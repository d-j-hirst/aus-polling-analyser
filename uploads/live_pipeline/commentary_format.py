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


def figures(packet):
    """Supply exact, ready-to-copy display phrases instead of repeated fact IDs.

    Probability/share levels use %, while their differences use pp (percentage
    points). Vote totals and expected seats have their own units. Public report
    values remain unrounded; the one-decimal convention is only for commentary.
    """
    rows = []

    def add(seat, quantity, before, after, units='percent', **identity):
        if before is None or after is None:
            return
        if units == 'votes':
            level = lambda x: format(round(x), ',')+' votes'
            change = lambda x: format(round(x), '+,')+' votes'
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
    old, new = packet['statewide']['before'], packet['statewide']['after']
    for row in packet.get('statewide_preference_changes', []):
        donor, destination, other = (packet['parties'][p] for p in row['parties'])
        add('Statewide', 'estimated statewide preference flow', row['before'], row['after'],
            party=f'{donor} first preferences to {destination} over {other}', percentile='50')
    for field, quantity in [('tpp_percentiles', 'projected Labor TPP share')]:
        for q in ('5', '50', '95'):
            add('Statewide', quantity, (old.get(field) or {}).get(q), (new.get(field) or {}).get(q), percentile=q)
    for field in ('expectedSeats', 'majorityWinPc', 'minorityWinPc', 'mostSeatsWinPc', 'overallWinPc'):
        for party in sorted((old.get(field) or {}).keys() & (new.get(field) or {}).keys()):
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
    for seat, counts in sorted(packet.get('close_tcp_counts', {}).items()):
        pair = ['{candidate:'+seat+':'+p+'}' for p in counts['pair']]
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
        for key in ('changed_votes', 'fp_vote_change', 'tcp_vote_change'):
            if key in event:
                rows.append({'seat': event.get('seat'), 'booth': event['record'], 'quantity': key,
                             'value': format(round(event[key]), ',')+' votes'})
        for party, value in sorted(event.get('fp_share_minus_previous_expectation_pp', {}).items()):
            rows.append({'seat': event.get('seat'), 'booth': event['record'], 'quantity': 'FP share minus previous booth expectation',
                         'candidate': '{candidate:'+event['seat']+':'+party+'}', **expectation_difference_fields(value)})
        rows.extend(tcp_comparison_figures(event))
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
    cannot establish either the result or the size and direction of its gap."""
    progress = packet.get('statewide', {}).get('before', {}).get('countProgress') or {}
    pre_count = (packet.get('reference_forecast_id') == 'origin'
                 or (progress.get('fp_votes') == 0 and progress.get('tcp_votes') == 0))
    estimate = r'(?:earlier estimate|estimate before counting began)' if pre_count else 'earlier estimate'
    ledger = r'(?:TCP|2CP|two[- ]candidate(?:[- ]preferred)?)'
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
            if name and re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)', sentence, re.I):
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


def check_text(text, seats, packet):
    """Check inline entities and display figures, leaving readable prose intact."""
    if not isinstance(text, str):
        raise ValueError('Commentary text must be a string')
    allowed_entities = packet.get('entities') or entities(packet)
    stripped = commentary_counts.checked_vote_changes(text, seats, packet)
    for token in TOKEN.findall(text):
        if token not in allowed_entities:
            raise ValueError('Unknown commentary entity: '+token)
        if seats and allowed_entities[token].get('seat') and allowed_entities[token]['seat'] not in seats:
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
