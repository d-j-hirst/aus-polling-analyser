"""Check plain-English descriptions of counted leads and changes in vote totals.

The commentary tables store a deficit or removal with a minus sign. In prose
the word 'behind' or 'removed' can carry that sign. Check the stated quantity
against its own electoral account before letting the writer use that wording.
"""
import re
from collections import Counter


NUMBER = r'(?:0|[1-9]\d{0,2}(?:,\d{3})*)'
UNIT = r'(?:(?:additional|absent|early|postal|provisional|pre-poll|primary|first[- ]preference|two-candidate(?:-preferred)?|FP|TCP)\s+)*votes?'
CLAIM = re.compile(r'\*\*([+\-]?'+NUMBER+r')(?:\s+('+UNIT+r'))?\*\*', re.I)
ENTITY = re.compile(r'\{(?:seat|candidate):([^}:]+)(?::([^}]+))?\}')
CANDIDATE = r'\{candidate:([^}:]+):([^}]+)\}'
SEPARATOR = re.compile(r'[.!?;](?=\s|$)')
DECREASE = r'removed|withdrawn|lost|fell|decreased|declined|reduced|cut|shrank|down|dropped'
INCREASE = r'added|gained|grew|rose|increased|up'
LEDGERS = {'fp': r'\b(?:fp|first[- ]preferences?|primar(?:y|ies))\b',
           'tcp': r'\b(?:tcp|two-candidate(?:-preferred)?|(?<!first-)(?<!first )preference count)\b'}


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
    for person in re.finditer(CANDIDATE, prefix):
        if person[1] != district:
            continue
        # In 'she took ... against Y', Y is explicitly the opponent.
        if re.search(r'\b(?:against|over|ahead of)\s*$', prefix[:person.start()], re.I):
            continue
        names.append(person[2])
    # In 'ahead of Y' the following name is the opponent.
    pair = account['pair']
    opponent = re.match(r'\s*(?:ahead of|behind|over)\s+'+CANDIDATE, suffix, re.I)
    if opponent:
        if opponent[1] != district or opponent[2] not in pair:
            return False
        subject = next(p for p in pair if p != opponent[2])
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
    earlier = re.search(r'\bfrom\s*$', prefix, re.I) or transition
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


def candidate_changes(event, ledger):
    """Match a candidate across both counts before taking a difference. The
    feed may use a different identifier from the forecast, e.g. for an
    independent. An absent entry or a changed identity is not a known zero."""
    old, new = event.get('before') or {}, event.get('after') or {}
    before = old.get(ledger+'_votes_current') or {}
    after = new.get(ledger+'_votes_current') or {}
    old_ids = {raw: str(old.get('count_to_projection_ids', {}).get(raw, raw)) for raw in before}
    new_ids = {raw: str(new.get('count_to_projection_ids', {}).get(raw, raw)) for raw in after}
    old_matches, new_matches = Counter(old_ids.values()), Counter(new_ids.values())
    changes = {}
    for raw in before.keys() & after.keys():
        # If several ballot entries share a forecast identity, the writer
        # cannot attribute an individual entry's change to a named candidate.
        candidate = new_ids[raw]
        if old_ids[raw] == candidate and old_matches[candidate] == new_matches[candidate] == 1:
            changes[candidate] = after[raw] - before[raw]
    return changes


def change_matches(packet, district, ledgers, amount, candidates, location, district_total, category):
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
            changes = candidate_changes(event, ledger)
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


def checked_vote_changes(text, seats, packet):
    """Replace recognised, verified numerical claims in a validation-only copy.
    The original prose, candidate placeholders and archived evidence are retained.
    Other forms still go through the ordinary exact display-figure checks."""
    text = text.replace('−', '-')

    def check(match):
        prefix, suffix = text[:match.start()], text[match.end():]
        before = SEPARATOR.split(prefix)[-1]
        after = SEPARATOR.split(suffix, maxsplit=1)[0]
        unit = match[2]
        if unit is None:
            external = re.match(r'\s+('+UNIT+r')\b', after, re.I)
            if not external:
                return match[0]
            unit, after = external[1], after[external.end():]
        sentence = before+match[0]+SEPARATOR.split(suffix, maxsplit=1)[0]
        ordered = ordered_district(sentence, len(before))
        district = ordered or claim_seat(prefix, after, seats)
        if not district or (seats and district not in seats):
            return match[0]
        number = match[1]
        # Keep the names for lead validation, but only use nearby words for
        # the numerical claim. The preceding figure often describes something
        # different (e.g. an FP withdrawal followed by a new TCP count).
        prior = before.rsplit('**', 1)[-1]
        later = after.split('**', 1)[0]
        side = re.match(r'\s+(behind|ahead)\b', later, re.I)
        deficit = re.search(r'\bdeficit of\s*$', prior, re.I)
        if side or deficit:
            direction = -1 if deficit or side[1].lower() == 'behind' else 1
            account = packet.get('close_tcp_counts', {}).get(district)
            subject_context = before if re.search(CANDIDATE, before) else prefix
            if account and lead_matches(int(number.replace(',', '')), direction, subject_context, after, account, district):
                return 'votes'
            if account:
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
            also = re.match(r'\s+and\s+'+CANDIDATE, later[named_candidates.end():])
            if also and 'each for' in later[:named_candidates.end()] and also[1] == district:
                candidates.append(also[2])
            if amount is None and re.match(r'\s+(?:were removed from|from)\b', later, re.I):
                amount = -abs(int(number.replace(',', '')))
        elif subject := re.search(CANDIDATE+r'\s+(?:lost|gained)\s*$', prior, re.I):
            if subject[1] != district:
                raise ValueError('Vote change refers to a candidate in a different district')
            candidates = [subject[2]]
        if candidates and amount is None:
            # A single correction can be followed by the affected candidates'
            # shares of it: '561 votes were removed, including 285 for X'.
            breakdown = re.search(r'\b(were removed|were added)\b[^.;]*\bincluding\b', before, re.I)
            if breakdown:
                amount = (-1 if 'removed' in breakdown[1].lower() else 1) * int(number.replace(',', ''))
        if amount is None:
            return match[0]
        ledgers = vote_ledgers(unit, preceding, following)
        # An example naming a booth after the claim might account for only
        # part of the preceding district total.
        location = before+' '+re.split(r',|\b(?:as|even as|while|with)\b', later, maxsplit=1, flags=re.I)[0]
        district_total = bool(ordered or re.search(r'\bdistrict(?:-wide|(?:\s+(?:FP|TCP|first-preference|two-candidate|primary))? count|[’\']s)|\bacross the (?:seat|district)\b',
                                                   location, re.I))
        category = re.search(r'\b(absent|postal|(?:early )?provisional|pre-poll)\b', unit, re.I)
        category = category[1].lower().replace('-', '').replace(' ', '') if category else None
        matched = change_matches(packet, district, ledgers or LEDGERS, amount, candidates, location,
                                 district_total, category)
        if matched and (len(ledgers) < 2 or matched == ledgers):
            return 'votes'
        # Explicit candidate/category assignments must have positive support.
        # If the account instead records the opposite change, a coincidentally
        # matching cumulative count cannot justify reversing the verb.
        if (candidates or category or len(ledgers) == 2 or (district_total and ledgers)
                or change_matches(packet, district, ledgers or LEDGERS, -amount, candidates,
                                  location, district_total, category)):
            raise ValueError('Unsupported numerical statement: '+match[1]+' '+unit)
        return match[0]
    return CLAIM.sub(check, text)
