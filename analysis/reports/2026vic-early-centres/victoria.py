"""Join earlier state booth votes to the locations serving each Victorian district.

The November 2022 final recheck distinguishes own-district early centres from
other early votes. The 2026 setup supplies district–centre identities, including
guest districts at a shared address. All 2026 records are pre-count inputs.
"""

import json
import re
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from io import BytesIO
from urllib.request import Request, urlopen
from zipfile import ZipFile

from evidence import (HERE, ROOT, Booth, Counts, digest, distance_km, grouped_votes, observation,
                      position, side)
from scripts.turnout.turnout_qld_operational import _shared_strings, read_xlsx_rows


MEDIA = 'https://www.vec.vic.gov.au/-/media/'
BOOKS = {
    'locations2018': 'd537f12314234c3da9d9a55ffde16b16.xlsx',
    'locations2022': 'd6faeb7027634194bf56150194ecd7f6.xlsx',
    'results2022': 'c4665365795d431e811d0ce818e373d4.xlsx',
    'venues2026': '19326aa361284a49a69b08194421da96.xlsx',
}


def workbooks(fetch, sources):
    """Retain the official spreadsheets and identify the exact source bytes used."""
    books = {}
    for label, name in BOOKS.items():
        url, path = MEDIA + name, HERE / 'raw' / name
        if not path.exists():
            if not fetch:
                raise FileNotFoundError(f'Missing {label}. Run record.py --fetch to obtain {url}')
            with urlopen(Request(url, headers={'User-Agent': 'Australian Election Forecasts'}), timeout=45) as response:
                data = response.read()
            # Check the response is a workbook before retaining a new source.
            with ZipFile(BytesIO(data)) as wb:
                wb.getinfo('xl/workbook.xml')
            path.parent.mkdir(exist_ok=True)
            tmp = path.with_suffix('.partial')
            tmp.write_bytes(data)
            tmp.replace(path)
        books[label] = path.read_bytes()
        sources[url] = digest(books[label])
    return books


def normalized(name):
    return ' '.join(name.casefold().replace('’', "'").split())


def locations(book, year):
    """Expand a shared venue into each district listed by the VEC.

    Historic tables sometimes list both electorates in one field, sometimes
    in two. A repeated coordinate at the same named site is the same location.
    Competing coordinates for the same district/name cannot identify a booth.
    """
    rows = read_xlsx_rows(book, f'{year} VEC locations')
    positions = defaultdict(set)
    for r in rows.values():
        kind = r.get('D')
        if kind not in ('Early Voting Centre', 'Voting Centre'):
            continue
        if year == 2018:
            seats = [r.get('E', ''), r.get('F', '')]
            point = position(r.get('O'), r.get('N'))
        else:
            seats = r.get('E', '').split(',')
            point = position(r.get('P'), r.get('Q'))
        if point:
            for district in filter(None, seats):
                seat = district.split(' District')[0].strip()
                positions[kind, seat, normalized(r['C'])].add(point)
    return {key: next(iter(points)) for key, points in positions.items() if len(points) == 1}


def historical_votes(year, sources):
    """Read the project's VEC booth histories, retaining actual party pairings.

    A direct Labor–Coalition count supplies TPP only when both parties appear
    as the final pair. Primary votes remain usable in non-classic districts.
    """
    name = f'analysis/Booth Results/{year}vic.json'
    data = (ROOT / name).read_bytes()
    sources[name] = digest(data)
    records = {}
    for seat, district in json.loads(data).items():
        parties = {key: c['party'] for key, c in district['candidates'].items()}
        for booth_name, booth in district['booths'].items():
            fp = Counter()
            for candidate, count in booth.get('fp', {}).items():
                fp[parties[candidate]] += count
            tcp = booth.get('tcp', {})
            final = {side(parties[c]): n for c, n in tcp.items()}
            classic = len(tcp) == 2 and final.keys() == {'alp', 'coalition'} and sum(tcp.values()) > 0
            records[seat, normalized(booth_name)] = grouped_votes(
                fp, final['alp'] if classic else None, sum(tcp.values()) if classic else None)
    return records


def early_2022(book, geography, history):
    """Reconcile named centres plus the residual to all published district early votes.

    Informal ballots and the worksheet's total rows are checks. Only candidates
    contribute to formal shares. Other Early Votes can check the district sum
    but has no single location and supplies no centre-level observation.
    """
    ballots = defaultdict(Counter)
    for row in read_xlsx_rows(book, 'VEC 2022 final recheck').values():
        if row.get('H') == 'Early Votes' and row.get('B') == 'Recheck' and row.get('C') == 'First Preference':
            ballots[row['A'], row['D']][row['G']] += int(row['I'])
    centres, sums = [], defaultdict(Counter)
    for (seat, name), votes in ballots.items():
        total = votes.pop('Total Votes')
        informal = votes.pop('Informal Votes')
        if sum(votes.values()) + informal != total:
            raise ValueError(f'Unreconciled VEC candidate votes at {seat}/{name}')
        sums[seat].update(votes)
        if name != 'Other Early Votes':
            coords = geography['Early Voting Centre', seat, normalized(name)]
            centres.append(Booth(seat, name, name, coords, grouped_votes(votes)))
    for seat, votes in sums.items():
        expected = history[seat, 'early votes']
        expected_fp = Counts(expected.formal, expected.labor, expected.coalition, expected.greens)
        if grouped_votes(votes) != expected_fp:
            raise ValueError(f'Named and residual votes disagree with {seat} early total')
    return centres


def comparison_booths(geo18, geo22, votes18):
    """Assign stable 2018 ordinary results to districts on the 2022 boundaries.

    Prefer a continuing district at a shared site. A booth transferred by the
    redistribution is usable when exactly one previous district matches its
    name and position within 1 km. Ambiguous transfers do not supply votes.
    """
    by_name = defaultdict(list)
    for (kind, seat, name), point in geo18.items():
        count = votes18.get((seat, name))
        if kind == 'Voting Centre' and count and count.formal:
            by_name[name].append(Booth(seat, name, name, point, count))
    ordinary = defaultdict(list)
    for (kind, seat, name), point in geo22.items():
        if kind != 'Voting Centre':
            continue
        nearby = [b for b in by_name[name] if distance_km(b.position, point) <= 1]
        options = [b for b in nearby if b.seat == seat] or nearby
        if len(options) == 1:
            ordinary[seat].append(options[0])
    return ordinary


def venue_weights(book):
    """Read the VEC's EVCs worksheet, using the separate host and guest district rows."""
    with ZipFile(BytesIO(book)) as z:
        sheets = ET.fromstring(z.read('xl/workbook.xml')).findall('.//{*}sheet')
        sheet = next(s for s in sheets if s.get('name') == 'EVCs')
        key = sheet.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id')
        links = ET.fromstring(z.read('xl/_rels/workbook.xml.rels'))
        target = next(link.get('Target') for link in links if link.get('Id') == key)
        target = target.lstrip('/') if target.startswith('/') else 'xl/' + target
        tab = ET.fromstring(z.read(target))
        strings = _shared_strings(z, 'VEC venue estimates')
    result = {}
    for line in tab.findall('.//{*}sheetData/{*}row'):
        r = {}
        for cell in line.findall('{*}c'):
            value = cell.findtext('{*}v')
            if value is not None:
                column = re.match('[A-Z]+', cell.get('r'))[0]
                r[column] = strings[int(value)] if cell.get('t') == 's' else value
        if line.get('r') == '1':
            if r.get('I') != 'Est. Early Votes':
                raise ValueError('Venue estimates column has changed')
        elif r.get('B'):
            name = re.sub(r'\s*\(H?JEVC\)\s*$', '', r['B'].strip())
            identity = r['A'].removesuffix(' District').strip(), name
            if identity in result or int(r['I']) <= 0:
                raise ValueError(f'Invalid or duplicate venue weight for {identity}')
            role = 'single'
            if '(JEVC)' in r['B']:
                role = 'guest'
            elif '(HJEVC)' in r['B']:
                role = 'host'
            result[identity] = (int(r['I']), r['G'], role)
    return result


def current_cases(geo22, history, centres22, weights, private_sources):
    """Join prepared 2026 locations to the public VEC estimates and prior booth votes.

    The turnout audit identifies ordinary booths matched to the predecessor
    district. Confirm each on the published 2022 map within 1 km. An EVC
    models only its reporting district, including at a joint city venue.
    Missing local evidence stays identifiable on the output roster.
    """
    def local_data(name):
        data = (ROOT / name).read_bytes()
        private_sources[name] = digest(data)
        return data

    roster = ET.fromstring(local_data('downloads/2026vic_booths.xml'))
    audit = json.loads(local_data('forecasts/2026vic/live-inputs/turnout-input-audit.json'))
    audited = {d['seat']: {b['name']: b for b in d['ordinary_booths']} for d in audit['districts']}
    rows, used = [], set()
    for district in roster.findall('{*}PollingDistrict'):
        seat = district.findtext('{*}PollingDistrictIdentifier/{*}Name').removesuffix(' District')
        early, ordinary = [], []
        for site in district.findall('.//{*}PollingPlace'):
            ident = site.find('{*}PollingPlaceIdentifier')
            name, kind = ident.get('Name'), ident.get('Classification')
            point = position(site.findtext('{*}Address/{*}AddressLatitude'),
                             site.findtext('{*}Address/{*}AddressLongitude'))
            if kind == 'EarlyVotingCentre':
                early.append(Booth(seat, name, name, point, None))
            elif kind == 'VotingCentre' and point:
                match = audited[seat][name]
                if not match['same_predecessor_match'] or not match['historical_match']:
                    continue
                old = match['historical_match']
                key = old['seat'], normalized(old['name'])
                previous = geo22.get(('Voting Centre', *key))
                if previous and key in history and history[key].formal and distance_km(point, previous) <= 1:
                    ordinary.append(Booth(seat, old['name'], old['name'], previous, history[key]))
        past = [c for c in centres22 if c.seat == seat]
        for centre in early:
            identity = seat, centre.name
            if identity in used:
                raise ValueError(f'Duplicate EVC in VEC roster {identity}')
            used.add(identity)
            weight, suburb, role = weights[identity]
            row, near = observation('2026vic', centre, ordinary if centre.position else [], weight, 'VEC venue')
            old_distances = [distance_km(centre.position, b.position) for b in past if centre.position]
            row.update(suburb=suburb, role=role, nearest_five=' | '.join(b.name for b in near),
                       fifth_km=round(distance_km(centre.position, near[-1].position), 3) if len(near) == 5 else '',
                       nearest_2022_evc_km=round(min(old_distances), 3) if old_distances else '')
            rows.append(row)
    if used != set(weights):
        raise ValueError(f'Venue and feed district/centre rosters differ: {used ^ set(weights)}')
    return rows
