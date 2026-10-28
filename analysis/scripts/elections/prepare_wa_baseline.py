"""Recover WA historical comparisons and prepare a conservative reporting bridge.

The 2021 published district tables supply final primary and preference counts.
Current rosters identify physical venues, never election results. A separately
recorded synthetic split bridges the changed early/absent reporting definitions;
it is an assumption about old voters, not an observation of 2025 behaviour.
"""

import argparse
from collections import defaultdict
import hashlib
import json
import logging
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from scripts.elections.prepare_wa_live import NS

ROOT = Path(__file__).resolve().parents[3]
REPORT_URL = ('https://www.elections.wa.gov.au/sites/default/files/content/SGE%202021/'
    '2020-2021%20content/Reports/4_21_Res_Stats_Dist_Prof_Elect_Results.pdf')
DECLARATIONS = {'Absent Votes': 'Absent', 'Early Votes (by Post)': 'Postal',
    'Early Votes (In Person)': 'PrePoll', 'Provisional Votes': 'Provisional',
    'Special Institutions, Hospitals & Remotes': 'SIR'}


def xml_districts(path):
    return {d.get('Name'): d for d in ET.parse(path).getroot().findall('w:ElectionRegion/w:ElectionDistrict', NS)}


def roster(path):
    """Retain ballot order and candidate identity, including distinct independents."""
    return {name: [dict(name=c.get('BallotPaperName'), party=c.get('RegisteredPartyAbbreviation') or 'OTH')
                  for c in sorted(d.findall('w:LA/w:Candidate', NS), key=lambda c: int(c.get('BallotPaperOrder')))]
            for name, d in xml_districts(path).items()}


def read_report(pdf, candidates):
    """Read final counts, rejecting ambiguous rows rather than guessing columns.

    These printed tables are rotated within portrait pages. Rotate the content
    before extracting rows, then check every candidate sum, district sum and
    final TCP pair. The report can have slightly fewer TCP than FP ballots
    because a small number exhausted during the distribution of preferences.
    """
    from pypdf import PdfReader
    # Only the upright table is needed; rotated page furniture is deliberately omitted.
    logging.getLogger('pypdf').setLevel(logging.ERROR)
    reader = PdfReader(pdf)
    result = {}
    for page_index in range(2, len(reader.pages), 2):
        page = reader.pages[page_index]
        page.rotate(90); page.transfer_rotation_to_content()
        text = page.extract_text(extraction_mode='layout', layout_mode_space_vertically=False)
        lines = [re.sub(r' +', ' ', line).strip() for line in text.splitlines()]
        name = next(line.removesuffix(' Electoral District') for line in lines if line.endswith(' Electoral District'))
        name = next(n for n in candidates if n.replace(' ', '') == name.replace(' ', ''))
        ballot = candidates[name]
        header = next(line for line in lines if line.startswith('No. Polling Place'))
        # PDF character spacing sometimes separates letters inside party codes.
        tail = header.split('Votes Votes Votes')[-1].replace('AL P', 'ALP').replace('LI B', 'LIB').replace('N AT', 'NAT')
        parties = tail.split()
        if len(parties) != 2:
            raise ValueError(f'Unrecognized TCP header on page {page_index+1}: {tail}')
        pair = []
        for party in parties:
            ids = [str(i) for i, c in enumerate(ballot) if c['party'].replace('NATS', 'NAT') == party]
            if len(ids) != 1:
                raise ValueError(f'Ambiguous final candidate in {name}: {party}')
            pair.append(ids[0])
        accounts = {}
        totals = None
        for line in lines:
            if line.startswith('Distribution of Preferences'): break
            line = re.sub(r'(Votes)+$', '', line)
            numbered = re.match(r'^\d+ (.+?) (\d[\d,]*(?: \d[\d,]*)*) ([\d.]+)% (\d[\d,]*) ([\d.]+)%$', line)
            declaration = next((n for n in DECLARATIONS if line.startswith(n+' ')), None)
            is_total = line.startswith('Total First Preferences Votes ')
            if numbered:
                account_name, numeric = numbered[1], numbered[2]
                last = numbered[4]
            elif declaration or is_total:
                account_name = declaration or 'TOTAL'
                prefix = declaration or 'Total First Preferences Votes'
                if declaration and set(line[len(prefix)+1:].split()) == {'0'}:
                    accounts[account_name] = dict(fp={str(i): 0 for i in range(len(ballot))},
                        tcp={c: 0 for c in pair}, formal=0, informal=0)
                    continue
                match = re.fullmatch(r'(\d[\d,]*(?: \d[\d,]*)*) ([\d.]+)% (\d[\d,]*) ([\d.]+)%', line[len(prefix)+1:])
                if not match: raise ValueError(f'Unrecognized historical row: {name}: {line}')
                numeric, last = match[1], match[3]
            else: continue
            values = [int(v.replace(',', '')) for v in numeric.split()] + [int(last.replace(',', ''))]
            if len(values) != len(ballot)+5: raise ValueError(f'Wrong column count: {name}: {line}')
            fp = {str(i): v for i, v in enumerate(values[:len(ballot)])}
            formal, informal, total, a, b = values[-5:]
            if sum(fp.values()) != formal or formal+informal != total or a+b > formal:
                raise ValueError(f'Historical accounting mismatch: {name}: {line}')
            account = dict(fp=fp, tcp=dict(zip(pair, (a, b))), formal=formal, informal=informal)
            if is_total: totals = account
            else: accounts[account_name] = account
        if totals is None or not accounts: raise ValueError('Incomplete historical district: '+name)
        for field in ('fp', 'tcp'):
            summed = {c: sum(a[field].get(c, 0) for a in accounts.values()) for c in totals[field]}
            if summed != totals[field]: raise ValueError(f'{name}: {field} district total does not reconcile')
        result[name] = dict(candidates=ballot, booths=accounts, totals=totals, report_page=page_index+1)
    if set(result) != set(candidates): raise ValueError('Incomplete district report')
    return result


def apportion(count, weights):
    """Split an integer account without creating or dropping ballots by rounding."""
    total = sum(weights)
    values = [count*w/total for w in weights]
    whole = [int(v) for v in values]
    for i in sorted(range(len(values)), key=lambda i: values[i]-whole[i], reverse=True)[:count-sum(whole)]:
        whole[i] += 1
    return whole


def split_account(account, weights):
    """Partition candidate counts while keeping each TCP inside its FP account.

    Independent integer rounding can place, for example, two TCP ballots in a
    synthetic part receiving only one FP ballot. Move those rounding remainders
    to another part with spare FP capacity; each candidate's total stays fixed.
    This is allocation bookkeeping, not a change to historical preference flows.
    """
    result = [dict(fp={}, tcp={}) for _ in weights]
    for field in ('fp', 'tcp'):
        for candidate, count in account[field].items():
            for row, amount in zip(result, apportion(count, weights)):
                row[field][candidate] = amount
    capacity = [sum(row['fp'].values())-sum(row['tcp'].values()) for row in result]
    if sum(capacity) < 0:
        raise ValueError('Historical TCP exceeds its FP account')
    for i, row in enumerate(result):
        while capacity[i] < 0:
            destination = max(range(len(result)), key=lambda j: capacity[j])
            candidate = max(row['tcp'], key=row['tcp'].get)
            moved = min(-capacity[i], capacity[destination], row['tcp'][candidate])
            row['tcp'][candidate] -= moved
            result[destination]['tcp'][candidate] += moved
            capacity[i] += moved; capacity[destination] -= moved
    return result


def add_account(destination, source):
    for field in ('fp', 'tcp'):
        for candidate, count in source[field].items():
            destination.setdefault(field, {})[candidate] = destination.setdefault(field, {}).get(candidate, 0)+count


def key(name):
    # Ignore only punctuation/spacing, not words that distinguish two venues.
    return re.sub(r'[^a-z0-9]', '', name.lower())


def match_venues(current, previous, references):
    """Link physical places using independent evidence, never booth numbers.

    Prefer a unique venue GUID, then an exact normalized name. A shared building
    requires a unique resident-district match; competing claims are rejected.
    Numbering remains useful only inside one election's feed.
    """
    old = [(seat, b) for seat, d in previous.items() for b in d.findall('w:OrdinaryPollingPlace', NS)]
    proposals = {}
    for seat, d in current.items():
        for b in d.findall('w:OrdinaryPollingPlace', NS):
            if 'Early Polling Place' in b.get('Name'): continue
            matches = []
            for attribute in ('VenueId', 'Name'):
                value = b.get(attribute)
                if not value: continue
                found = [(s, p) for s, p in old if p.get(attribute) and
                         (key(p.get(attribute)) == key(value))]
                local = [(s, p) for s, p in found if s in references[seat]]
                if len(local) == 1: matches = local; break
                if len(found) == 1: matches = found; break
            if len(matches) == 1:
                s, p = matches[0]
                proposals[(seat, b.get('Number'))] = (s, p.get('Name'))
    claims = defaultdict(list)
    for new, old_key in proposals.items(): claims[old_key].append(new)
    return {new: old for new, old in proposals.items() if len(claims[old]) == 1}


def early_centres(pdf):
    """Use published election-eve location totals only as capacity evidence.

    These include visitors from other districts. They are not resident counts
    and are never substituted directly for the local early-vote allowance.
    """
    from pypdf import PdfReader
    text = '\n'.join(page.extract_text() for page in PdfReader(pdf).pages)
    return {key(name.strip()): int(count.replace(',', '')) for name, count in
            re.findall(r'([^\n]*Early Polling Place[^\n]*)\n([\d,]+)\n', text)}


def district_references(current, historical):
    """Use existing authored district references, combining the merged Mid-West.

    Oakford and Jandakot share the old Jandakot reference; their allocation is
    divided by current enrolment so it cannot duplicate old votes. This is a
    broad district comparison, not a reconstruction of redistributed electors.
    """
    aliases = {'Bibra Lake': 'Willagee', 'Girrawheen': 'Mirrabooka',
               'Mindarie': 'Burns Beach', 'Secret Harbour': 'Warnbro', 'Oakford': 'Jandakot'}
    references = {n: [aliases.get(n, n)] for n in current}
    references['Mid-West'] = ['Moore', 'North West Central']
    if set(sum(references.values(), [])) != set(historical):
        raise ValueError('Historical district references do not cover the previous election')
    return references


def prepare_baseline(historical, previous, current, capacities, settings):
    """Conserve historical ballots while bridging the changed reporting accounts.

    Observed postal/mobile comparisons remain intact. Early in-person history
    divides between local early places and the enlarged absent account. Early
    places also receive a modest district day-vote mixture: some day voters use
    those venues. Provisionals borrow a day-voter composition, with explicit
    synthetic status. These splits describe uncertain comparison populations;
    the separate turnout prior determines expected 2025 amounts.
    """
    references = district_references(current, historical)
    matches = match_venues(current, previous, references)
    reverse = {(old[0], key(old[1])): new for new, old in matches.items()}
    result = {n: dict(enrolment=int(d.get('Enrolment')), candidates={}, booths=[], declarations={})
              for n, d in current.items()}
    candidate_info = {f'{n}/{i}': c for n, s in historical.items() for i, c in enumerate(s['candidates'])}
    pools = {n: defaultdict(lambda: dict(fp={}, tcp={})) for n in current}
    for old in historical:
        destinations = [n for n in current if old in references[n]]
        weights = [result[n]['enrolment'] for n in destinations]
        for booth_name, account in historical[old]['booths'].items():
            remapped = {f: {f'{old}/{c}': v for c, v in account[f].items()} for f in ('fp', 'tcp')}
            if booth_name not in DECLARATIONS and (old, key(booth_name)) in reverse:
                new, number = reverse[old, key(booth_name)]
                current_name = next(b.get('Name') for b in current[new].findall('w:OrdinaryPollingPlace', NS) if b.get('Number') == number)
                result[new]['booths'].append(dict(name=current_name, number=number,
                    comparison_seat=old, synthetic=False, kind='ordinary', **remapped))
            else:
                category = DECLARATIONS.get(booth_name, 'unmatched_day')
                for new, part in zip(destinations, split_account(remapped, weights)):
                    add_account(pools[new][category], part)
    audit = {}
    for name, district in current.items():
        early = [b for b in district.findall('w:OrdinaryPollingPlace', NS) if 'Early Polling Place' in b.get('Name')]
        issued = [capacities.get(key(b.get('Name'))) for b in early]
        if any(v is None for v in issued): raise ValueError('Missing election-eve early centre capacity: '+name)
        # Match capacity against the resident early allowance, then shrink the
        # implied share towards the agreed 60% reference. A larger centre does
        # not prove all its voters live here. No centre means no local account.
        old_early = sum(pools[name]['PrePoll']['fp'].values())
        own_fraction = (settings['own_early_fraction'] * min(1., sum(issued)/max(1., old_early))) if early else 0.
        own, away = split_account(pools[name]['PrePoll'], [own_fraction, 1-own_fraction])
        add_account(pools[name]['Absent'], away)
        day_records = result[name]['booths'] + [dict(name='Unmatched historical day venues',
            number=None, comparison_seat=name, synthetic=False, kind='ordinary', **pools[name]['unmatched_day'])]
        day_for_early = dict(fp={}, tcp={}); provisional = pools[name]['Provisional']
        # This working mixture uses one typical day venue per new early venue,
        # capped at 10% of district day votes. It is not fitted to 2025 results.
        day_fraction = min(.10, len(early)/max(1, len(day_records))) if early else 0.
        provisional_fraction = settings['synthetic_provisional_day_fraction']
        for row in day_records:
            keep, transfer, prov = split_account(row, [1-day_fraction-provisional_fraction, day_fraction, provisional_fraction])
            row.update(keep); add_account(day_for_early, transfer); add_account(provisional, prov)
        result[name]['booths'] = day_records
        add_account(own, day_for_early)
        if early:
            for booth, part in zip(early, split_account(own, issued)):
                result[name]['booths'].append(dict(name=booth.get('Name'), number=booth.get('Number'),
                    comparison_seat=name, synthetic=True, kind='ppvc', **part))
        for category in ('Absent', 'Postal', 'SIR', 'Provisional'):
            result[name]['declarations'][category] = dict(synthetic=category in ('Absent', 'Provisional'), **pools[name][category])
        result[name]['declarations']['Provisional'].update(provisional)
        used = {c for row in result[name]['booths']+list(result[name]['declarations'].values()) for f in ('fp', 'tcp') for c in row[f]}
        result[name]['candidates'] = {c: candidate_info[c] for c in sorted(used)}
        audit[name] = dict(references=references[name], own_early_fraction=own_fraction,
            early_capacity=sum(issued), historical_early=old_early, day_at_early_fraction=day_fraction,
            matched_day=sum(1 for n, _ in matches if n == name),
            unmatched_current_day=[b.get('Name') for b in district.findall('w:OrdinaryPollingPlace', NS)
                if 'Early Polling Place' not in b.get('Name') and (name, b.get('Number')) not in matches])
    # A reconstructed account is a partition of the old ballot population.
    # Verify candidate-by-candidate, not only statewide totals: an accidentally
    # duplicated Greens/Independent pool could otherwise look well reconciled.
    for field in ('fp', 'tcp'):
        expected = {f'{n}/{c}': v for n, s in historical.items() for c, v in s['totals'][field].items()}
        actual = defaultdict(int)
        for district in result.values():
            for row in district['booths']+list(district['declarations'].values()):
                for c, count in row[field].items(): actual[c] += count
        if any(actual[c] != v for c, v in expected.items()) or any(c not in expected and v for c, v in actual.items()):
            raise ValueError('Synthetic baseline failed candidate conservation: '+field)
    return dict(schema_version=1, election='2025wa', previous_election='2021wa',
        election_date='2025-03-08', districts=result), audit


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--early-centres', required=True, type=Path)
    args = parser.parse_args(argv)
    historical = read_report(args.report, roster(ROOT/'downloads/2025wa_candidates_prev.xml'))
    output = ROOT/'forecasts/2025wa/live-inputs'; output.mkdir(parents=True, exist_ok=True)
    (output/'historical-2021.json').write_text(json.dumps(historical, indent=2)+'\n', encoding='utf8')
    settings = json.loads((ROOT/'forecasts/2025wa/live-turnout-settings.json').read_text())
    previous = xml_districts(ROOT/'downloads/2025wa_booths_prev.xml')
    current = xml_districts(ROOT/'downloads/2025wa_booths_current.xml')
    baseline, audit = prepare_baseline(historical, previous, current, early_centres(args.early_centres), settings)
    paths = [args.report, args.early_centres, ROOT/'downloads/2025wa_candidates_prev.xml',
             ROOT/'downloads/2025wa_booths_prev.xml', ROOT/'downloads/2025wa_booths_current.xml',
             ROOT/'forecasts/2025wa/live-turnout-settings.json']
    baseline['sources'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    baseline['report_url'] = REPORT_URL
    (output/'historical-baseline.json').write_text(json.dumps(baseline, separators=(',', ':'))+'\n', encoding='utf8')
    (output/'baseline-audit.json').write_text(json.dumps(audit, indent=2)+'\n', encoding='utf8')
    print(f'Recovered {len(historical)} districts and {sum(len(s["booths"]) for s in historical.values())} historical accounts.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
