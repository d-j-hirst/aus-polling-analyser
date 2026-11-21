"""Check plain-English descriptions of counted leads and changes in vote totals.

The commentary tables store a deficit or removal with a minus sign. In prose
the word 'behind' or 'removed' can carry that sign. Check the stated quantity
against its own electoral account before letting the writer use that wording.
"""
import re
from collections import Counter
from typing import NamedTuple, Optional


NUMBER = r'(?:0|[1-9]\d{0,2}(?:,\d{3})*)'
UNIT = r'(?:(?:(?:additional|absent|early|postal|provisional|pre-poll|primary|first[- ]preference|two-candidate(?:-preferred)?|FP|TCP)\s+)*votes?|first[- ]preferences?)'
# In “a 45-vote deficit” the noun outside the bold figure supplies its sign.
# Leave ordinary descriptions such as “a 45-vote batch” to the count checker.
CLAIM = re.compile(r'\*\*([+\-]?'+NUMBER+r')(?:\s+('+UNIT+r')|[-\u2011]vote(?=\*\*\s+(lead|deficit)\b))?\*\*', re.I)
ENTITY = re.compile(r'\{(?:seat|candidate):([^}:]+)(?::([^}]+))?\}')
CANDIDATE = r'\{candidate:([^}:]+):([^}]+)\}'
SEPARATOR = re.compile(r'[.!?;](?=\s|$)')
DECREASE = r'removed|withdrawn|lost|fell|decreased|declined|reduced|cut|shrank|down|dropped'
INCREASE = r'added|gained|grew|rose|increased|up'
LEDGERS = {'fp': r'\b(?:fp|first[- ]preferences?|primar(?:y|ies))\b',
           'tcp': r'\b(?:tcp|two-candidate(?:-preferred)?|(?<!first-)(?<!first )preference count)\b'}

# In an election-wide progress update the verb often carries the sign, as
# in "the statewide TCP count fell by 200 votes to 800 votes". Keep that
# change and optional ending total attached to the explicitly named count.
VOTE_UNIT = r'(?:\s+votes?\*\*|\*\*\s+votes?\b)'
PROGRESS_CHANGE = re.compile(
    r'(?:^|(?<=[.!?;])\s+)(?:(?:the )?(?:reported )?(?:statewide|national|nationwide)\s+|'
    r'(?:statewide|nationwide),\s+(?:the )?)(?:reported )?'
    r'(?P<ledger>FP|primary|first[- ]preference|TCP|two[- ]candidate(?:[- ]preferred)?)\s+'
    r'(?:vote\s+)?(?:count|tally|total)\s+(?:was\s+)?(?P<verb>'+DECREASE+'|'+INCREASE+r')\s+by\s+'
    r'\*\*(?P<amount>[+-]?'+NUMBER+r')'+VOTE_UNIT+
    r'(?:,?\s+to\s+\*\*(?P<ending>'+NUMBER+r')'+VOTE_UNIT+r')?'
    r'(?=\s*(?:[.;!?]|$)|,?\s+(?:as|after|because|when|with)\b)', re.I)


def checked_progress_changes(text, packet):
    """Check movement in the reported statewide or national total against
    the two forecasts' progress counters. After a TCP withdrawal the model
    can retain the old preferences. Adding changes at the modelled booths
    would then answer a different question from reported counting progress."""
    def check(match):
        ledger = 'fp' if re.fullmatch(LEDGERS['fp'], match['ledger'], re.I) else 'tcp'
        key = ledger + '_votes'
        statewide = packet.get('statewide', {})
        before = (statewide.get('before', {}).get('countProgress') or {}).get(key)
        after = (statewide.get('after', {}).get('countProgress') or {}).get(key)
        change = signed_change(match['amount'], match['verb'], '')
        if before is None or after is None or not change or after - before != change:
            raise ValueError('Unsupported reported '+ledger.upper()+' count change: '+match['amount']+' votes')
        if match['ending'] is not None and int(match['ending'].replace(',', '')) != after:
            raise ValueError('Unsupported reported '+ledger.upper()+' ending count: '+match['ending']+' votes')
        return CLAIM.sub('votes', match[0])
    return PROGRESS_CHANGE.sub(check, text)


# "Counts reset in 22 districts. The feed names different candidates in 17,
# while a change of pairing is unconfirmed in the other 5." The unit carries
# through the breakdown even when the writer only says "districts" once.
DISTRICT_FIGURE = re.compile(r'\*\*\d+(?: districts?)?\*\*', re.I)
DISTRICT_NUMBER = r'\*\*(\d+)(?: districts?)?\*\*'
END_OF_CLAUSE = r'(?=\s*[,;:.!?]|$)'
DISTRICT_SUBCOUNT = DISTRICT_NUMBER + END_OF_CLAUSE
# The named-pair group can be the subject ("15 named different candidates")
# or the location ("replacements were named in 15"). Only extend "districts"
# across wording that identifies the group these numbers actually count.
NAMED_REPLACEMENTS = (r'\s*[:.;]\s*(?:(?:(?:the feed|the commission) (?:now )?names (?:different|replacement) candidates|'
                      r'(?:(?:different|replacement) candidates|replacements) (?:are|were) (?:now )?named) in '
                      +DISTRICT_SUBCOUNT+r'|'+DISTRICT_NUMBER+r' (?:now )?named (?:different|replacement) candidates'
                      +END_OF_CLAUSE+r')')
REMAINING_RESETS = (r'\s*[,;.]\s*(?:(?:while |but )?a (?:change of pairing|pairing change) (?:is|was) '
                    r'(?:likely|(?:likely but )?(?:unconfirmed|not (?:yet )?confirmed)) )?in the (?:other|remaining) '
                    +DISTRICT_SUBCOUNT)
RESET_BREAKDOWN = re.compile(r'\b(?:counts?|reporting) (?:was |were )?reset in '
                             r'\*\*(\d+) districts?\*\*(?=\s*[:.;!?]|$)'
                             r'(?:'+NAMED_REPLACEMENTS+r'(?:'+REMAINING_RESETS+r')?)?', re.I)


def checked_reset_districts(text, reporting):
    """Check a TCP reset and its two possible groups in a statewide update.
    A district reporting zero for a newly named pair and one still listing
    the old pair are distinct. Together they give the number of resets.
    Districts retaining earlier counts overlap those groups and are counted
    separately in the evidence. Only extend the district unit into clauses
    that continue this particular breakdown."""
    text = re.sub(r'\*\*(\d+)\*\* (districts?)\b', r'**\1 \2**', text)
    named = reporting.get('reset_different_candidates', {}).get('count', 0)
    unchanged = reporting.get('reset_same_candidates', {}).get('count', 0)
    if not (named or unchanged):
        return text

    def check(match):
        # These district groups describe TCP only. A statement explicitly
        # about a primary count cannot borrow the same numbers.
        prefix = SEPARATOR.split(text[:match.start()])[-1]
        count_types = re.findall(LEDGERS['fp']+'|'+LEDGERS['tcp'], prefix, re.I)
        if count_types and re.fullmatch(LEDGERS['fp'], count_types[-1], re.I):
            raise ValueError('TCP resets cannot verify a district breakdown for a primary count')
        total, named_after_verb, named_before_verb, remainder = match.groups()
        for count, expected in ((total, named + unchanged), (named_after_verb or named_before_verb, named),
                                (remainder, unchanged)):
            if count is not None and int(count) != expected:
                raise ValueError('Unsupported TCP reset district count: '+count)
        return DISTRICT_FIGURE.sub('districts', match[0])

    return RESET_BREAKDOWN.sub(check, text)


def claim_seat(prefix, suffix, seats):
    # Multi-district paragraphs normally name a seat before giving its
    # figures. A lone following candidate can also identify the district.
    earlier = ENTITY.findall(prefix)
    if earlier:
        return earlier[-1][0]
    following = {seat for seat, _ in ENTITY.findall(suffix)}
    if len(following) == 1:
        return following.pop()
    return seats[0] if len(seats) == 1 else None


def lead_matches(count, side, prefix, suffix, account, district):
    """Read the lead from the named candidate's perspective. A move from
    behind to ahead compares two distinct observations for the same pair."""
    if not account:
        return False
    names = []
    opponent_before = None
    for person in re.finditer(CANDIDATE, prefix):
        # “The counted lead over B changed from ...” identifies B as the
        # opponent even if the subject was named in the previous sentence.
        if re.search(r'\b(?:lead|deficit)\s+(?:over|against|to)\s*$', prefix[:person.start()], re.I):
            opponent_before = person.groups()
            continue
        if person[1] != district:
            continue
        # In 'she took ... against Y', Y is explicitly the opponent.
        if re.search(r'\b(?:against|over|ahead of)\s*$', prefix[:person.start()], re.I):
            continue
        names.append(person[2])
    # In 'ahead of Y' the following name is the opponent.
    pair = account['pair']
    following = re.match(r'\s*(?:(?:lead|deficit)\s+)?(?:ahead of|behind|over|against|to)\s+'+CANDIDATE, suffix, re.I)
    opponent = following.groups() if following else opponent_before
    if opponent:
        if opponent[0] != district or opponent[1] not in pair or (opponent_before and opponent_before != opponent):
            return False
        subject = next(p for p in pair if p != opponent[1])
        if names and names[-1] != subject:
            return False
    elif names:
        subject = names[-1]
    else:
        return False
    if subject not in pair:
        return False
    # A deficit before 'into a lead' also describes the earlier observation.
    # A destination or infinitive later in the sentence ("ahead ... to win")
    # does not turn a current count into a previous count.
    transition = re.match(r'\s*(?:behind|ahead)?\s*(?:to\s+\*\*|into (?:a|the) lead\b)', suffix, re.I)
    earlier = re.search(r'\bfrom(?:\s+an?)?\s*$', prefix, re.I) or transition
    lead = account['lead_before' if earlier else 'lead_after']
    return lead * (1 if pair[0] == subject else -1) == side * count


def changed_lead(before, amount, account):
    """A lead can shrink even as both candidates gain votes. Use their
    difference at each endpoint for 'X's lead ... fell to N, down by M'."""
    subject = re.search(r'\blead (?:for|of) '+CANDIDATE+r'.*?\b(?:fell|rose|grew|narrowed|widened) to '
                        r'\*\*[^*]+\*\*,\s*(?:down|up)(?: by)?\s*$', before, re.I)
    if not subject:
        return False
    if not account or subject[2] not in account['pair']:
        raise ValueError('Unsupported counted lead change')
    direction = 1 if subject[2] == account['pair'][0] else -1
    if amount != direction * (account['lead_after'] - account['lead_before']):
        raise ValueError('Unsupported counted lead change')
    return True


def signed_change(number, before, after):
    """Recognise the direction adjacent to this figure, independently of FP
    or TCP. For 'fell by 23', the figure is the magnitude of a negative change."""
    verb = re.match(r'\s+(?:(?:were|was|have been|had been|previously reported[^.,;*]{0,65} were)\s+)?'
                    r'('+DECREASE+'|'+INCREASE+r')\b', after, re.I)
    if not verb:
        verb = re.search(r'\b('+DECREASE+'|'+INCREASE+r')\b(?:\s+(?:by|of|a|net|the|district[’\']s|'
                         r'first-preference|two-candidate|primary|fp|tcp|count|a net))*\s*$', before, re.I)
    if verb:
        direction = -1 if re.fullmatch(DECREASE, verb[1], re.I) else 1
        if number.startswith(('+', '-')) and int(number.replace(',', '')) * direction < 0:
            raise ValueError('Vote change sign conflicts with its direction: '+number)
        return direction * abs(int(number.replace(',', '')))
    if number.startswith(('+', '-')):
        return int(number.replace(',', ''))
    return None


def ordered_district(text, position):
    """'In A and B ... X and Y respectively' associates quantities by order.
    Only use that correspondence if the two lists have equal lengths."""
    if 'respectively' not in text:
        return None
    names = re.findall(r'\{seat:([^}]+)\}', text)
    amounts = list(CLAIM.finditer(text))
    if len(names) == len(amounts):
        for name, claim in zip(names, amounts):
            if claim.start() == position:
                return name
    return None


def candidate_count_pairs(event, ledger):
    """Return the earlier and current observed counts for each matched candidate. The
    feed may use a different identifier from the forecast, e.g. for an
    independent. An absent entry or a changed identity is not a known zero."""
    old, new = event.get('before') or {}, event.get('after') or {}
    before = old.get(ledger+'_votes_current') or {}
    after = new.get(ledger+'_votes_current') or {}
    old_ids = {raw: str(old.get('count_to_projection_ids', {}).get(raw, raw)) for raw in before}
    new_ids = {raw: str(new.get('count_to_projection_ids', {}).get(raw, raw)) for raw in after}
    old_matches, new_matches = Counter(old_ids.values()), Counter(new_ids.values())
    pairs = {}
    for raw in before.keys() & after.keys():
        # If several ballot entries share a forecast identity, the writer
        # cannot attribute an individual entry's change to a named candidate.
        candidate = new_ids[raw]
        if old_ids[raw] == candidate and old_matches[candidate] == new_matches[candidate] == 1:
            pairs[candidate] = (before[raw], after[raw])
    return pairs


def referenced_booth(events, booth_list, district, location, previous_clause):
    """Resolve 'the booth' within one commentary bullet. Prefer a name in the
    current clause, then try the immediately preceding clause. Include names
    from the mechanical list so that two named booths remain ambiguous even
    when only one's detailed count was supplied to the writer."""
    names = {e['record']: e['record'].rsplit('/', 1)[-1] for e in events if e.get('record')}
    for group in booth_list.values():
        for row in group.get('shown', []):
            if row.get('record'):
                names[row['record']] = row['name']
    for clause in (location, previous_clause):
        if any(seat != district for seat, _ in ENTITY.findall(clause)):
            return None
        prose = ENTITY.sub('', clause)
        records = [record for record, name in names.items()
                   if name and re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)', prose, re.I)]
        if records:
            return records[0] if len(records) == 1 else None
    return None


def change_matches(packet, district, ledgers, amount, candidates, location, district_total, category,
                   previous_clause=None):
    """District net changes, same-pair candidate additions, and individual
    polling-place corrections are different accounts. Preserve the level at
    which the writer identifies the change, as well as its sign and ledger."""
    totals = packet.get('observed_count_changes_by_seat', {}).get(district, {})
    close = packet.get('close_tcp_counts', {}).get(district, {})
    events = [e for e in packet['evidence'] if e.get('seat') == district and e['kind'] == 'booths']
    def mentions(name):
        return name and re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)', location, re.I)
    named = [e for e in events if mentions((e.get('record') or '').rsplit('/', 1)[-1])]
    # The mechanical list can name a booth whose candidate records were
    # omitted to bound prompt size. Its name alone cannot establish a
    # candidate correction from another booth in the same district.
    booth_list = packet.get('booth_changes', {}).get('by_seat', {}).get(district, {})
    if previous_clause is not None:
        record = referenced_booth(events, booth_list, district, location, previous_clause)
        named = [e for e in events if record and e.get('record') == record]
        if len(named) != 1 or district_total:
            return set()
        events = named
    named_without_details = any(mentions(row['name']) for group in booth_list.values()
                                for row in group.get('shown', [])) and not named
    if named_without_details and candidates and not district_total:
        return set()
    if category:
        district_total = False
        events = [e for e in events if str(((e.get('after') or e.get('before') or {}).get('vote_type') or
                  e['record'].rsplit('/', 1)[-1])).lower().replace('-', '').replace(' ', '') == category]
        named = [e for e in named if e in events]
    quantities = set()
    for ledger in ledgers:
        if not category and (not named or district_total):
            if candidates:
                changes = close.get('net_by_candidate', {}) if ledger == 'tcp' else {}
                if all(changes.get(c) == amount for c in candidates):
                    quantities.add(ledger)
            elif totals.get(ledger+'_net_change') == amount:
                quantities.add(ledger)
        for event in ([] if district_total else named or events):
            if not candidates:
                if event.get(ledger+'_vote_change') == amount and (named or category or not totals):
                    quantities.add(ledger)
                continue
            changes = {party: new - old for party, (old, new) in candidate_count_pairs(event, ledger).items()}
            if candidates == ['independent']:
                identities = packet.get('all_candidate_identities', {}).get(district, {})
                if named and any(change == amount and identities.get(p, {}).get('party') == 'IND'
                                 for p, change in changes.items()):
                    quantities.add(ledger)
            elif all(changes.get(c) == amount for c in candidates):
                quantities.add(ledger)
        if len(named) > 1 and not candidates and not district_total:
            changes = [event.get(ledger+'_vote_change') for event in named]
            if all(isinstance(v, (float, int)) for v in changes) and sum(changes) == amount:
                quantities.add(ledger)
    return quantities


def vote_ledgers(unit, preceding, following):
    """Find whether the writer described FP, TCP or both. Prefer the unit
    attached to the number. An adjoining clause can describe a different
    count, as in 'removed from FP and 300 added to the TCP count'."""
    both = re.search(r'\bboth (?:the )?(?:district )?(?:(?:FP|first-preference) and (?:TCP|two-candidate) )?'
                     r'(?:counts|tallies)\b', preceding+' '+following, re.I)
    if both:
        return set(LEDGERS)
    if re.search(r'\band\s*$', preceding, re.I):
        preceding = ''
    following = re.split(r'\b(?:and|but|while)\b', following, maxsplit=1, flags=re.I)[0]
    for fragment in (unit, preceding, following):
        ledgers = {k for k, pattern in LEDGERS.items() if re.search(pattern, fragment, re.I)}
        if ledgers:
            return ledgers
    return set()


def shared_unit_candidate_count(match, text, seats, packet):
    """In 'fell from 419 to 246 votes', the final word describes both counts.
    Check the endpoints in order for one named candidate at one named booth.
    The booth and count type can be introduced before a semicolon, as when a
    writer describes a booth correction and then gives a candidate's figures.
    """
    following = text[match.end():]
    endpoint = re.match(r'\s+to\s+\*\*('+NUMBER+r')(?:\s+votes?\*\*|\*\*\s+votes?\b)', following, re.I)
    if not endpoint:
        return False
    preceding = re.split(r'[.!?](?=\s|$)', text[:match.start()])[-1]
    subject = re.search(CANDIDATE + r"(?:['’]s)?\s+(?:(?:first[- ]preference|primary|FP|TCP)\s+)?"
                        r'(?:(?:count|tally)\s+)?(fell|dropped|rose|grew|went|changed|was revised|was corrected)\s+from\s*$',
                        preceding, re.I)
    if subject:
        district, candidate, verb = subject.groups()
    else:
        # A second candidate can share the first clause's neutral verb and
        # count type: "was revised from ... to ... votes, and that for B
        # from ... to ... votes". The booth, ledger and both of B's endpoints
        # still have to match. A directional verb cannot be carried across
        # a revision in the opposite direction.
        continued = re.search(r'\bwas (?:revised|corrected) from \*\*'+NUMBER+r'(?: votes?)?\*\* to '
                              r'\*\*'+NUMBER+VOTE_UNIT+r',? and that for '+CANDIDATE+r' from\s*$', preceding, re.I)
        if not continued:
            return False
        district, candidate = continued.groups()
        verb = 'revised'
    if seats and district not in seats:
        return False
    old, new = (int(n.replace(',', '')) for n in (match[1], endpoint[1]))
    if (verb.lower() in ('fell', 'dropped') and new >= old) or (verb.lower() in ('rose', 'grew') and new <= old):
        return False
    sentence = ENTITY.sub('', preceding + SEPARATOR.split(following, maxsplit=1)[0])
    booths = []
    for event in packet['evidence']:
        if event['kind'] == 'booths' and event.get('seat') == district:
            name = (event.get('record') or '').rsplit('/', 1)[-1]
            if name and re.search(r'(?<!\w)'+re.escape(name)+r'(?!\w)', sentence, re.I):
                booths.append(event)
    if len(booths) != 1:
        return False
    ledgers = [k for k, pattern in LEDGERS.items() if re.search(pattern, sentence, re.I)]
    if len(ledgers) > 1:
        return False
    return any(candidate_count_pairs(booths[0], ledger).get(candidate) == (old, new)
               for ledger in ledgers or LEDGERS)


class IncludedVoteChange(NamedTuple):
    """The verified total that the next candidate figure is said to be part of."""
    position: int
    district: str
    total_change: int
    ledgers: set
    category: Optional[str]
    named_booth: bool
    candidates_seen: frozenset


# The unit may be printed inside or outside the bold figure. The second
# category can inherit the unit ("and 80 appeared as absent") or both the
# unit and the amount ("matched those added to absent").
CATEGORY_LEDGER = r'(?:first[- ]preferences?|(?:first[- ]preference|primary|FP|TCP|two-candidate(?:-preferred)?) votes?)'
CATEGORY_NAME = r'(?:early )?[a-z][a-z-]*'
CATEGORY_COMPARISON = re.compile(
    r'\*\*(?P<removed>'+NUMBER+r')(?: (?P<inside>'+CATEGORY_LEDGER+r')\*\*|\*\* (?P<outside>'+CATEGORY_LEDGER+r'))'
    r'\s+(?:(?:was|were) )?(?:removed|withdrawn) from (?:the )?(?P<source>'+CATEGORY_NAME+r')(?: count| category)?'
    r'(?:,? and \*\*(?P<added>'+NUMBER+r')\*\* (?:(?:was|were) )?(?:appeared (?:as|in)|added to)'
    r'| matched (?:those|the number) added to) (?:the )?(?P<destination>'+CATEGORY_NAME+r')(?: count| category)?'
    r'(?=\s*[,.;!?]|$)', re.I)


def checked_category_comparisons(text, seats, packet):
    """Verify the removal and addition separately. Two categories can move
    by slightly different numbers during a recheck. This establishes the two
    observed changes without assuming they were exactly the same ballots."""
    def check(match):
        district = claim_seat(text[:match.start()], '', seats)
        if not district or (seats and district not in seats):
            raise ValueError('Cannot identify district for vote category comparison')
        unit = match['inside'] or match['outside']
        ledger = 'fp' if re.search(LEDGERS['fp'], unit, re.I) else 'tcp'
        source, destination = (match[key].casefold() for key in ('source', 'destination'))
        removed = int(match['removed'].replace(',', ''))
        added = int((match['added'] or match['removed']).replace(',', ''))
        # An exact transfer may already have been verified before its two
        # booth records were omitted from the shorter commentary request.
        for transfer in packet.get('category_transfers', []):
            if (transfer['seat'] == district and transfer['from'].casefold() == source
                    and transfer['to'].casefold() == destination and transfer[ledger+'_votes'] == removed == added):
                return 'the compared vote counts'
        # Use signed changes at the named declaration units. District totals
        # and votes at ordinary booths cannot establish either side. The
        # mechanical list can retain a unit omitted from the detailed evidence.
        changes = {}
        groups = packet.get('booth_changes', {}).get('by_seat', {}).get(district, {})
        for group in groups.values():
            for row in group.get('shown', []):
                name = (row.get('name') or '').casefold()
                if name in (source, destination) and name == (row.get('vote_type') or '').casefold() and row.get('record'):
                    changes.setdefault(name, {})[row['record']] = row.get(ledger+'_net_change')
        for event in packet['evidence']:
            if event['kind'] == 'booths' and event.get('seat') == district:
                booth = event.get('after') or event.get('before') or {}
                name = (booth.get('name') or '').casefold()
                if name in (source, destination) and name == (booth.get('vote_type') or '').casefold() and ledger+'_vote_change' in event:
                    changes.setdefault(name, {})[event['record']] = event[ledger+'_vote_change']
        if (source == destination or list(changes.get(source, {}).values()) != [-removed]
                or list(changes.get(destination, {}).values()) != [added]):
            raise ValueError('Unsupported change between vote categories: '+match[0])
        return 'the compared vote counts'
    return CATEGORY_COMPARISON.sub(check, text)


def checked_vote_changes(text, seats, packet):
    """Replace recognised, verified numerical claims in a validation-only copy.
    The original prose, candidate placeholders and archived evidence are retained.
    Other forms still go through the ordinary exact display-figure checks."""
    text = checked_progress_changes(text.replace('−', '-'), packet)
    text = checked_category_comparisons(text, seats, packet)
    # A verified total can introduce candidate changes with “including” or
    # a colon, e.g. “added 90 votes to the TCP count: 50 for A and 40 for B”.
    # Remember where the next component begins so that an unrelated number
    # later in this bullet cannot borrow the total's sign or count type.
    included_change = None

    def check(match):
        nonlocal included_change
        prefix, suffix = text[:match.start()], text[match.end():]
        before = SEPARATOR.split(prefix)[-1]
        after = SEPARATOR.split(suffix, maxsplit=1)[0]
        included = included_change if included_change and included_change.position == match.start() else None
        unit = match[2]
        compound_lead = match[3]
        if unit is None and not compound_lead:
            external = re.match(r'\s+('+UNIT+r')\b', after, re.I)
            if external:
                unit, after = external[1], after[external.end():]
            elif included and re.match(r'\s+(?:each\s+)?for\s+'+CANDIDATE, after, re.I):
                # The writer only needs to say “votes” once. The parent count
                # was checked, but each candidate still has to match their own
                # change at the same booth and in the same FP or TCP count.
                unit = 'votes'
            else:
                if match[1][0].isdigit() and shared_unit_candidate_count(match, text, seats, packet):
                    return 'votes'
                return match[0]
        sentence = before+match[0]+SEPARATOR.split(suffix, maxsplit=1)[0]
        ordered = ordered_district(sentence, len(before))
        district = ordered or claim_seat(prefix, after, seats)
        if not district or (seats and district not in seats):
            if compound_lead:
                raise ValueError('Cannot identify district for counted lead: '+match[0])
            return match[0]
        number = match[1]
        # Keep the names for lead validation, but only use nearby words for
        # the numerical claim. The preceding figure often describes something
        # different (e.g. an FP withdrawal followed by a new TCP count).
        prior = before.rsplit('**', 1)[-1]
        later = after.split('**', 1)[0]
        side = re.match(r'\s+(behind|ahead)\b', later, re.I)
        deficit = re.search(r'\bdeficit of\s*$', prior, re.I)
        if compound_lead or side or deficit:
            if compound_lead:
                # The noun gives the direction. The ballot difference in
                # “a 45-vote deficit” is an unsigned magnitude.
                if number.startswith(('+', '-')):
                    raise ValueError('Expected an unsigned counted lead: '+match[0])
                direction = -1 if compound_lead.lower() == 'deficit' else 1
            else:
                direction = -1 if deficit or side[1].lower() == 'behind' else 1
            account = packet.get('close_tcp_counts', {}).get(district)
            subject_context = before if re.search(CANDIDATE, before) else prefix
            if account and lead_matches(int(number.replace(',', '')), direction, subject_context, after, account, district):
                return 'votes'
            if account or compound_lead:
                raise ValueError('Unsupported counted lead: '+match[0])
            return match[0]
        # Remove identity labels from the words used to identify the ledger:
        # an electorate or candidate name never establishes that this is FP/TCP.
        preceding = ENTITY.sub('', prior.rsplit(',', 1)[-1])
        following = ENTITY.sub('', later.split(',', 1)[0])
        amount = signed_change(number, preceding, following)
        if amount is not None and changed_lead(before, amount, packet.get('close_tcp_counts', {}).get(district)):
            return 'votes'
        if ordered and amount is None:
            verb = re.search(r'\b(fell|grew|rose|dropped)\s+by\s+\*\*', sentence, re.I)
            if verb:
                amount = (-1 if verb[1].lower() in ('fell', 'dropped') else 1) * int(number.replace(',', ''))
        # Prose may carry the same direction over several districts.
        if amount is None and re.search(r'\bremovals\b', before, re.I) and re.match(r'\s+net\)', later):
            amount = -int(number.replace(',', ''))
        if amount is None and re.fullmatch(r'\s*(?:and\s+)?in \{seat:[^}]+\}, by\s*', before, re.I):
            clauses = re.split(r'[.!?](?=\s|$)', prefix)[-1].split(';')
            if len(clauses) > 1:
                governing = re.search(r'\b(fell|rose|grew) by \*\*[^*]+\*\*\s*$', clauses[-2], re.I)
                if governing:
                    amount = (-1 if governing[1].lower() == 'fell' else 1) * int(number.replace(',', ''))
        named_candidates = re.match(r'\s+(?:(?:were|was)\s+)?(?:removed\s+from|added\s+to|from|to|(?:each\s+)?for)'
                                    r'\s+(?:(?:the|primary|first-preference|two-candidate|tcp|fp|tally|count|for)\s+)*'
                                    +CANDIDATE, later, re.I)
        candidates = []
        if re.search(r'\bmoved\s*$', preceding, re.I) and re.match(r'\s+to (?:the|an) independent\b', later, re.I):
            amount = int(number.replace(',', ''))
            candidates = ['independent']
        if named_candidates:
            if named_candidates[1] != district:
                raise ValueError('Vote change refers to a candidate in a different district')
            candidates = [named_candidates[2]]
            remainder = later[named_candidates.end():]
            if re.search(r'\beach\s+for\b', named_candidates[0], re.I):
                # “Each” makes a claim about every listed candidate. All must
                # have the stated change in the same booth or district count.
                while also := re.match(r'(?:\s*,\s*(?:and\s+)?|\s+and\s+)'+CANDIDATE, remainder, re.I):
                    if also[1] != district or also[2] in candidates:
                        raise ValueError('Vote change needs distinct candidates in the same district')
                    candidates.append(also[2])
                    remainder = remainder[also.end():]
            if amount is None and re.match(r'\s+(?:were removed from|from)\b', later, re.I):
                amount = -abs(int(number.replace(',', '')))
        elif subject := re.search(CANDIDATE+r'\s+(?:lost|gained)\s*$', prior, re.I):
            if subject[1] != district:
                raise ValueError('Vote change refers to a candidate in a different district')
            candidates = [subject[2]]
        ledgers = vote_ledgers(unit, preceding, following)
        if candidates and included:
            if district != included.district or (ledgers and not ledgers <= included.ledgers):
                raise ValueError('Candidate breakdown refers to a different district or count type')
            if named_candidates and re.match(r'\s+and\s+'+CANDIDATE, remainder, re.I):
                raise ValueError('Breakdown does not specify a separate change for each candidate')
            if included.candidates_seen.intersection(candidates):
                raise ValueError('Candidate is repeated in the same vote breakdown')
            ledgers = ledgers or included.ledgers
            if amount is None:
                component = int(number.replace(',', ''))
                amount = -abs(component) if included.total_change < 0 else abs(component)
            if amount * included.total_change < 0:
                raise ValueError('Candidate breakdown conflicts with the direction of the total change')
        elif candidates and amount is None:
            # Older reports can also describe ballots being removed or added
            # without first quoting a total that supplies the direction.
            breakdown = re.search(r'\b(were removed|were added)\b[^.;]*\bincluding\b', before, re.I)
            if breakdown:
                amount = (-1 if 'removed' in breakdown[1].lower() else 1) * int(number.replace(',', ''))
        if amount is None:
            return match[0]
        # An example naming a booth after the claim might account for only
        # part of the preceding district total.
        location = before+' '+re.split(r',|\b(?:as|even as|while|with)\b', later, maxsplit=1, flags=re.I)[0]
        # A follow-up can use the booth name from the preceding sentence but
        # describe a different count there (e.g. an FP correction following
        # a TCP result). Carry over only the location.
        previous_clause = None
        if re.search(r'\b(?:the|this|that) (?:booth|polling place)\b', location, re.I):
            clauses = SEPARATOR.split(prefix)
            previous_clause = clauses[-2] if len(clauses) > 1 else ''
        district_total = bool(ordered or re.search(r'\bdistrict(?:-wide|(?:\s+(?:FP|TCP|first-preference|two-candidate|primary))? count|[’\']s)|\bacross the (?:seat|district)\b',
                                                   location, re.I))
        category = re.search(r'\b(absent|postal|(?:early )?provisional|pre-poll)\b', unit, re.I)
        category = category[1].lower().replace('-', '').replace(' ', '') if category else None
        if candidates and included:
            if category and included.category and category != included.category:
                raise ValueError('Candidate breakdown refers to a different vote category')
            category = category or included.category
            if included.named_booth and previous_clause is None:
                # The location still contains the parent's booth name. Require
                # one unambiguous booth even if the child names a second place.
                previous_clause = ''
        matched = change_matches(packet, district, ledgers or LEDGERS, amount, candidates, location,
                                 district_total, category, previous_clause)
        if matched and (len(ledgers) < 2 or matched == ledgers):
            # Reuse the checked direction whether the writer said “fell by”,
            # “was revised down by” or “were removed”. For a list of separate
            # candidate amounts, carry it on only across a list separator.
            next_figure = SEPARATOR.split(suffix, maxsplit=1)[0].find('**')
            count_colon = re.fullmatch(r'\s*(?:(?:to|from|in) (?:the )?(?:'+LEDGERS['tcp']+'|'+LEDGERS['fp']+
                                       r') (?:count|tally)\s*)?:\s*', later, re.I)
            starts_list = not candidates and (count_colon or re.search(r'\bincluding\s*$', later, re.I))
            continues_list = included and named_candidates and re.fullmatch(r'\s*(?:,\s*(?:and\s+)?|and\s+)', remainder, re.I)
            if amount and next_figure >= 0 and (starts_list or continues_list):
                position = match.end() + next_figure
                if starts_list:
                    events = [e for e in packet['evidence'] if e['kind'] == 'booths' and e.get('seat') == district]
                    booth_list = packet.get('booth_changes', {}).get('by_seat', {}).get(district, {})
                    booth = referenced_booth(events, booth_list, district, location, previous_clause or '')
                    if not count_colon or booth or district_total:
                        included_change = IncludedVoteChange(position, district, amount, matched, category,
                                                             bool(booth and not district_total), frozenset())
                else:
                    included_change = included._replace(position=position, candidates_seen=included.candidates_seen.union(candidates))
            return 'votes'
        # Explicit candidate/category assignments must have positive support.
        # If the account instead records the opposite change, a coincidentally
        # matching cumulative count cannot justify reversing the verb.
        if (previous_clause is not None or candidates or category or len(ledgers) == 2 or (district_total and ledgers)
                or change_matches(packet, district, ledgers or LEDGERS, -amount, candidates,
                                  location, district_total, category)):
            raise ValueError('Unsupported numerical statement: '+match[1]+' '+unit)
        return match[0]
    return CLAIM.sub(check, text)
