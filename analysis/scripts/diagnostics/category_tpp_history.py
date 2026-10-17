"""Normalize historical category preference counts without changing forecasts.

Run from analysis: python -B -m scripts.diagnostics.category_tpp_history
The source collector must have populated the private download cache first.
Only Labor-versus-Coalition preference counts are accepted. A non-classic TCP
pair is missing TPP evidence, never a zero for either major party.
"""

import csv
import io
import json
import re
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path
from urllib.parse import urljoin

from bs4 import BeautifulSoup

from scripts.diagnostics.category_sources import CACHE, ROOT, fetch
from scripts.diagnostics.collect_category_sources import nsw_tool_urls, nsw_table_url
from scripts.turnout.turnout_aec import ELECTIONS as AEC
from scripts.turnout.turnout_vic import ELECTIONS as VEC, discover_district_urls
from scripts.turnout.turnout_nsw import ELECTIONS as NSW
from scripts.turnout.turnout_qld import ELECTIONS as QLD, read_archive, classify_booth

OUTPUT = CACHE / 'normalized.json'
LABOR = {'ALP', 'LAB', 'AUSTRALIAN LABOR PARTY', 'AUSTRALIAN LABOR PARTY - VICTORIAN BRANCH',
         'AUSTRALIAN LABOR PARTY (STATE OF QUEENSLAND)'}
COALITION = {'LIB', 'LP', 'LNP', 'LNQ', 'NP', 'NAT', 'NATS', 'CLP', 'LIBERAL',
             'THE NATIONALS', 'NATIONAL', 'NATIONALS', 'NATIONAL PARTY', 'NATIONAL PARTY OF AUSTRALIA - VICTORIA',
             'LIBERAL NATIONAL PARTY OF QUEENSLAND'}


def side(party):
    party = party.strip().upper()
    return 'alp' if party in LABOR else 'coalition' if party in COALITION else None


def integer(value):
    value = str(value).replace(',', '').strip()
    if not re.fullmatch(r'\d+', value):
        raise ValueError(f'Not a nonnegative integer count: {value!r}')
    return int(value)


def record(code, seat, category, alp, coalition, source, state=None):
    return dict(election=code, seat=seat, category=category, alp=integer(alp),
                coalition=integer(coalition), source=source, state=state or code[4:])


def csv_rows(data):
    lines = data.decode('utf-8-sig').splitlines()
    return list(csv.DictReader(lines[1:]))


def aec_url(election, stem):
    return (f'https://results.aec.gov.au/{election.event_id}/{election.download_path}/'
            f'{stem}Download-{election.event_id}.csv')


def federal_records(code, election):
    # The booth TPP table provides the day/PPVC split. The separate category
    # table supplies declarations; older editions have only classic-seat TCP.
    place_rows = csv_rows(fetch(aec_url(election, 'GeneralPollingPlaces')))
    places = {(r['DivisionID'], r['PollingPlaceID']): r['PollingPlaceTypeID'] for r in place_rows}
    grouped = defaultdict(list)
    totals_url = aec_url(election, 'HouseTcpByCandidateByVoteType')
    for r in csv_rows(fetch(totals_url)):
        grouped[r['DivisionNm']].append(r)
    accounts = {}
    for seat, candidates in grouped.items():
        by_side = {side(r['PartyAb']): r for r in candidates}
        if len(candidates) == 2 and set(by_side) == {'alp', 'coalition'}:
            accounts[seat] = {
                name: [integer(by_side[p][name + 'Votes']) for p in ('alp', 'coalition')]
                for name in ('Ordinary', 'Absent', 'Provisional', 'PrePoll', 'Postal')}
    if int(code[:4]) >= 2016:
        totals_url = aec_url(election, 'HouseTppByDivisionByVoteType')
        accounts = {}
        for r in csv_rows(fetch(totals_url)):
            accounts[r['DivisionNm']] = {
                name: [integer(r[party + ' ' + field + 'Votes']) for party in
                       ('Australian Labor Party', 'Liberal/National Coalition')]
                for name, field in [('Ordinary', 'Ordinary'), ('Absent', 'Absent'),
                    ('Provisional', 'Provisional'), ('PrePoll', 'DeclarationPrePoll'), ('Postal', 'Postal')]}
    booth_url = aec_url(election, 'HouseTppByPollingPlace')
    booths = defaultdict(lambda: defaultdict(lambda: [0, 0]))
    states = {}
    for r in csv_rows(fetch(booth_url)):
        seat = r['DivisionNm']
        if seat not in accounts:
            continue
        place_type = places.get((r['DivisionID'], r['PollingPlaceID']))
        if place_type is None:
            raise ValueError(f'{code} missing polling-place identity: {r}')
        category = 'early' if place_type == '5' else 'ordinary' if place_type == '1' else 'mobile'
        values = [integer(r[p + ' Votes']) for p in ('Australian Labor Party', 'Liberal/National Coalition')]
        for i in range(2):
            booths[seat][category][i] += values[i]
        states[seat] = r['StateAb']
    rows, exclusions = [], []
    for seat, account in accounts.items():
        booth_sum = [sum(v[i] for v in booths[seat].values()) for i in range(2)]
        if booth_sum != account['Ordinary']:
            exclusions.append((seat, 'Booth TPP does not reconcile to ordinary account', booth_sum, account['Ordinary']))
            continue
        for category, values in booths[seat].items():
            rows.append(record(code, seat, category, *values, booth_url, states[seat]))
        for source_cat, category in [('Absent', 'absent'), ('Provisional', 'provisional'),
                                     ('PrePoll', 'early_declaration'), ('Postal', 'postal')]:
            rows.append(record(code, seat, category, *account[source_cat], totals_url, states[seat]))
    return rows, exclusions


def html_rows(data):
    soup = BeautifulSoup(data, 'html.parser')
    return [[x.get_text(' ', strip=True) for x in row.find_all(['td', 'th'], recursive=False)]
            for row in soup.find_all('tr')]


def reconciled_html_account(records, rows, alp_column, total_label):
    totals = [r for r in rows if r and r[0].strip().lower() == total_label]
    if len(totals) != 1:
        return False
    expected = [integer(totals[0][alp_column]), integer(totals[0][3-alp_column])]
    actual = [sum(r[p] for r in records) for p in ('alp', 'coalition')]
    return actual == expected


def victorian_records(code, election):
    urls = discover_district_urls(election, fetch(election.index_url))
    output, exclusions = [], []
    category_names = {'ordinary votes total': 'ordinary', 'early votes': 'early',
        'postal votes': 'postal', 'absent votes': 'absent', 'provisional votes': 'provisional',
        'marked as voted votes': 'marked_as_voted', 'declaration votes': 'declaration_combined'}
    for fp_url in urls:
        replacement = 'tcpbyvc' if code[:4] in ('2006', '2010') else 'tcpbyvotingcentre'
        url = (fp_url.replace('fpvbyvotingcentre', replacement) if election.page_style == 'historical'
               else fp_url.replace('-district-results-by-voting-centre', '-2cp-results-by-voting-centre'))
        data = fetch(url)
        soup = BeautifulSoup(data, 'html.parser')
        headings = ' '.join(h.get_text(' ', strip=True) for h in soup.find_all(['h1', 'h2']))
        match = re.search(r'([\w -]+?) (?:District )?Two Candidate Preferred', headings, re.I)
        if election.page_style != 'historical':
            seat = url.split('/')[-1].replace('-2cp-results-by-voting-centre', '').replace('-', ' ').title()
        elif match:
            seat = match[1].strip()
        else:
            exclusions.append((url, 'No district heading'))
            continue
        if seat + ' District' in election.districts_without_vote_types:
            exclusions.append((seat, 'Known unavailable or inconsistent final category partition'))
            continue
        if 'Narracan' in seat and code == '2022vic':
            exclusions.append((seat, 'Supplementary election'))
            continue
        rows = html_rows(data)
        header = next((r for r in rows if len(r) >= 3 and side(r[1]) and side(r[2])), None)
        if header is None or {side(header[1]), side(header[2])} != {'alp', 'coalition'}:
            exclusions.append((seat, 'No Labor-Coalition category pair'))
            continue
        alp_column = 1 if side(header[1]) == 'alp' else 2
        category_rows = [r for r in rows if r and r[0].lower() in category_names]
        if not any(r[0].lower() == 'ordinary votes total' for r in category_rows):
            exclusions.append((seat, 'Missing category partition'))
            continue
        # Keep the published count, including tiny mis-sort differences. These
        # are preference-count denominators, not FP formal-vote denominators.
        account = [record(code, seat, category_names[r[0].lower()],
                          r[alp_column], r[3-alp_column], url) for r in category_rows]
        if reconciled_html_account(account, rows, alp_column, 'total'):
            output.extend(account)
        else:
            exclusions.append((seat, 'Category counts do not reconcile to published total'))
    return output, exclusions


def nsw_records(code, election):
    output, exclusions = [], []
    for tool_url in nsw_tool_urls(election):
        seat = tool_url.lower().split('/la/')[1].split('/')[0].replace('-', ' ').title()
        url = nsw_table_url(tool_url)
        if url is None:
            exclusions.append((seat, 'No unique Labor-Coalition pairing'))
            continue
        rows = html_rows(fetch(url))
        header = next((r for r in rows if len(r) > 2 and
                       any(p in ' '.join(r[1:3]) for p in ('(ALP)', '(LAB)', '(CLP)'))), None)
        if header is None:
            exclusions.append((seat, 'Missing preference table'))
            continue
        alp_column = 1 if any(p in header[1] for p in ('(ALP)', '(LAB)', '(CLP)')) else 2
        account = []
        for r in rows:
            if len(r) < 3:
                continue
            category = nsw_category(r[0])
            if category:
                account.append(record(code, seat, category, r[alp_column], r[3-alp_column], url))
        if reconciled_html_account(account, rows, alp_column, 'total votes'):
            output.extend(account)
        else:
            exclusions.append((seat, 'Category counts do not reconcile to published total'))
    return output, exclusions


def nsw_category(label):
    label = re.sub(r'\s+', ' ', label).lower()
    # Test the more specific early-voting label first: it contains the ordinary
    # label too, but it is a disjoint category rather than an extra total.
    if label.startswith('total ') and any(x in label for x in ('pre-poll ordinary', 'early voting centre ordinary')):
        return 'early'
    if label.startswith('total ') and any(x in label for x in ('polling place ordinary', 'voting centre ordinary')):
        return 'ordinary'
    if label in ('postal', 'postal votes'):
        return 'postal'
    if label in ('absent', 'absent votes'):
        return 'absent'
    if any(x in label for x in ('provisional', 'enrolment')) and not label.startswith('total'):
        return 'provisional'
    if 'ivote' in label and not label.startswith('total'):
        return 'ivote'
    return None


def qld_records(code, election):
    root = ET.fromstring(read_archive(fetch(election.archive_url), code))
    output, exclusions = [], []
    for district in root.findall('./election/districts/district'):
        seat = district.get('districtName')
        parties = {c.get('ballotOrderNumber'): side(c.get('party')) for c in district.findall('./candidates/candidate')}
        count = district.find('./countRound[@id="4"]')
        if count is None:
            exclusions.append((seat, 'No final preference distribution'))
            continue
        candidates = count.findall('./twoCandidateVotes/candidate')
        if len(candidates) != 2 or {parties[c.get('ballotOrderNumber')] for c in candidates} != {'alp', 'coalition'}:
            exclusions.append((seat, 'Non-classic preference distribution'))
            continue
        account = []
        for booth in count.findall('./booths/booth'):
            values = {parties[c.get('ballotOrderNumber')]: integer(c.findtext('count'))
                      for c in booth.findall('./twoCandidateVotes/candidate')}
            if set(values) != {'alp', 'coalition'}:
                raise ValueError(f'{code} {seat}: incomplete booth preference count')
            category = classify_booth(booth, 'current')[1]
            category = {'election_day_ordinary': 'ordinary', 'early_in_person': 'early'}.get(category, category)
            account.append(record(code, seat, category, values['alp'], values['coalition'], election.archive_url))
        expected = {parties[c.get('ballotOrderNumber')]: integer(c.findtext('count')) for c in candidates}
        if any(sum(r[p] for r in account) != expected[p] for p in ('alp', 'coalition')):
            raise ValueError(f'{code} {seat}: booth preference counts do not reconcile')
        output.extend(account)
    return output, exclusions


def wa_source(year, label):
    base = f'http://media.waec.wa.gov.au/Archive/{year}%20SGE%20Final/'
    soup = BeautifulSoup(fetch(base), 'html.parser')
    return next(urljoin(base, a['href']) for a in soup.find_all('a', href=True) if label in a.text)


def xml_without_namespaces(data):
    root = ET.fromstring(data)
    for node in root.iter():
        node.tag = node.tag.split('}')[-1]
    return root


def wa_records(code):
    year = code[:4]
    candidates = xml_without_namespaces(fetch(wa_source(year, 'CANDIDATE SETUP')))
    parties = {(d.get('Code'), c.get('BallotPaperOrder')): side(c.get('RegisteredPartyAbbreviation', ''))
               for d in candidates.findall('.//ElectionDistrict') for c in d.findall('./LA/Candidate')}
    url = wa_source(year, 'LA VERBOSE')
    root = xml_without_namespaces(fetch(url))
    output, exclusions = [], []
    for district in root.findall('.//ElectionDistrict'):
        seat, district_code = district.get('Name'), district.get('Code')
        count = district.find('./LA/DistrictVotes[@CountDefinitionCode="LAND"]')
        if count is None:
            exclusions.append((seat, 'Missing preference count'))
            continue
        def pair(node):
            return {parties.get((district_code, c.get('CandidateBallotPaperOrder'))): integer(c.get('Votes'))
                    for c in node.findall('CandidateVotes')}
        if set(pair(count)) != {'alp', 'coalition'}:
            exclusions.append((seat, 'Non-classic preference count'))
            continue
        primary = district.find('./LA/DistrictVotes[@CountDefinitionCode="LAPC"]')
        primary_total = integer(primary.get('FormalVotes'))
        pair_total = sum(pair(count).values())
        # WA's final archive sometimes retains an unfinished indicative count,
        # especially in 2021. Small mis-sort differences are normal; missing
        # thousands of preferences are not a final category measurement.
        if abs(pair_total - primary_total) > max(20, .02 * primary_total):
            exclusions.append((seat, 'Incomplete indicative count', pair_total, primary_total))
            continue
        for booth in count.findall('OrdinaryPollingPlaceVotes'):
            values = pair(booth)
            category = 'early' if 'early' in booth.get('OrdinaryPollingPlaceName', '').lower() else 'ordinary'
            output.append(record(code, seat, category, values['alp'], values['coalition'], url))
        for cat in count.findall('CategoryVotes'):
            # APP repeats the ordinary booths. Older WA early votes are a
            # category total; 2025 instead lists individual early centres.
            category = {'AV': 'absent', 'POV': 'postal', 'PPV': 'early', 'PRV': 'provisional',
                        'SIR': 'mobile'}.get(cat.get('CategoryCode'))
            if category:
                values = pair(cat)
                output.append(record(code, seat, category, values['alp'], values['coalition'], url))
    return output, exclusions


def combine_records(rows):
    # Several booths or NSW provisional subtypes may map to one category.
    grouped = {}
    for row in rows:
        key = row['election'], row['seat'], row['category']
        if key not in grouped:
            grouped[key] = dict(row)
        else:
            grouped[key]['alp'] += row['alp']
            grouped[key]['coalition'] += row['coalition']
    return list(grouped.values())


def validate_victorian_count_coverage(code, rows, exclusions):
    # Published VEC 2CP pages can retain small pre-distribution mis-sorts.
    # Require a near-complete account, rather than confuse missing late votes
    # with a category swing. This research-quality tolerance is not a change
    # to live-feed recovery. OPV elections are not subjected to this FP check.
    def key(name):
        return re.sub('[^a-z0-9]', '', name.lower().replace(' district', ''))
    turnout = json.loads((ROOT / 'analysis' / 'Data' / 'Turnout' / f'{code}.json').read_text())
    formal = {key(r['seat_name']): r['formal_votes'] for r in turnout['seat_totals']}
    totals = defaultdict(int)
    for row in rows:
        totals[row['seat']] += row['alp'] + row['coalition']
    rejected = set()
    for seat, total in totals.items():
        expected = formal[key(seat)]
        if abs(total-expected) > max(20, .02*expected):
            rejected.add(seat)
            exclusions.append((seat, 'Published 2CP differs from final FP by more than 2%', total, expected))
    return [r for r in rows if r['seat'] not in rejected]


def main():
    records, coverage = [], {}
    jobs = [(code, federal_records, e) for code, e in AEC.items()]
    jobs += [(code, victorian_records, e) for code, e in VEC.items()]
    jobs += [(code, nsw_records, e) for code, e in NSW.items()]
    jobs += [(code, qld_records, e) for code, e in QLD.items() if e.feed_version == 'current']
    jobs += [(f'{y}wa', lambda code, unused: wa_records(code), None) for y in (2017, 2021, 2025)]
    for code, function, election in jobs:
        rows, excluded = function(code, election)
        if code.endswith('vic'):
            rows = validate_victorian_count_coverage(code, rows, excluded)
        records.extend(rows)
        coverage[code] = dict(seats=len({r['seat'] for r in rows}), exclusions=excluded,
                              categories=sorted({r['category'] for r in rows}))
        print(code, coverage[code]['seats'], coverage[code]['categories'], 'excluded', len(excluded))
    OUTPUT.write_text(json.dumps(dict(records=combine_records(records), coverage=coverage), indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
