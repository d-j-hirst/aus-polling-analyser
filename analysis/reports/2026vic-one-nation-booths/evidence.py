"""Build booth-level evidence for a South Australian test and Victorian application.

Historical state results provide the answer to test against. The geographic
predictors use only the preceding federal House election. Each state district
has its own record at a shared venue because the voters receive different
ballot papers. Each party share uses all formal first preferences as its base.
"""

import csv
import json
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from io import BytesIO
from pathlib import Path
from statistics import median
from zipfile import ZipFile

HERE = Path(__file__).resolve().parent
ANALYSIS = HERE.parents[1]
ROOT = ANALYSIS.parent
sys.path.insert(0, str(ANALYSIS))

from scripts.elections.federal_state import parse_booth_mapping
from scripts.elections.federal_booth_geography import Sites, federal_booths, position, source
from scripts.turnout.turnout_qld_operational import _shared_strings, read_xlsx_rows
from scripts.turnout.turnout_sa_final import BOOTH_CATEGORIES, SOURCE_URLS, build_dataset

SA_COORDS = 'SA-2026-Pollingplaces.xlsx'
SA_COORDS_URL = ('https://drive.usercontent.google.com/download?'
                 'id=1Za82Rt_8dsKk1lIQvX1wWTGAwYGpJuhB&export=download&confirm=t')
VIC_BOOK = '19326aa361284a49a69b08194421da96.xlsx'
VIC_BOOK_URL = 'https://www.vec.vic.gov.au/-/media/' + VIC_BOOK
METHODS = ('nearest', 'three', 'five', 'venue', 'mapped', 'prepoll')


FIRST_RETURNS = Path('operator/count-history/first-returns.csv')


def save_rows(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('w', encoding='utf-8', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0], lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


def key(name):
    return re.sub('[^a-z0-9]', '', name.casefold())


def federal_neighbourhoods(state, manifest):
    """Build statewide comparison pools and ordinary pools for each state district.

    The existing booth map allocates federal returns to state districts. This
    gives a district-specific alternative at joint or out-of-district centres.
    The federal division contributing the most mapped formal votes defines a
    larger geographic group used to check how well the result travels.
    """
    booths = federal_booths(state, manifest)
    mapping_file = ANALYSIS / 'Federal-State' / f'booths-2026{state.lower()}.txt'
    source(mapping_file, manifest)
    mapping = parse_booth_mapping(mapping_file)
    ordinary = [b for b in booths if b.mode == 'ordinary']
    pools, divisions, aggregates = {}, {}, {}
    for seat, members in mapping.items():
        district = [b for b in ordinary if (b.division, b.name) in members]
        if not district:
            raise ValueError(f'No mapped ordinary booths for {seat}')
        weights = Counter()
        for b in district:
            weights[b.division] += b.formal
        pools[seat] = Sites(district)
        divisions[seat] = weights.most_common(1)[0][0]
        aggregates[seat] = dict(one_nation_votes=sum(b.on for b in district),
                                formal_votes=sum(b.formal for b in district))
    if state == 'VIC':
        existing = ANALYSIS / 'Federal-State/2026vic-one-nation-fp.json'
        if aggregates != json.loads(existing.read_bytes())['districts']:
            raise ValueError('Federal booth totals disagree with the Victorian district trial')
    return Sites(ordinary), Sites(b for b in booths if b.mode == 'early'), pools, divisions


def geographic_inputs(seat, point, pools):
    """Compute six candidate predictors before looking at any state party votes.

    Each pair is One Nation ballots / all formal ballots. The hybrid methods
    use a near-coincident site on its own (within 250 metres), otherwise the
    three closest sites available. This allows both a common building and a
    wider neighbourhood to be tested. The threshold is a study assumption.
    """
    ordinary, early, mapped, divisions = pools
    row = dict(federal_area=divisions[seat], nearest_site='', nearest_km='', three_sites='', third_km='', mapped_km='',
               mapped_site='', mapped_sites_used='', **dict.fromkeys(METHODS, ''))
    if point is None:
        return row
    nearby, distances, labels = ordinary.nearby(point)
    local, within_distances, local_labels = mapped[seat].nearby(point)
    pre, _, _ = early.nearby(point)
    distance, within_distance = distances[0], within_distances[0]
    local_count = 1 if within_distance <= .25 else min(3, len(local))
    row.update(nearest_site=labels[0], nearest_km=round(distance, 4),
               three_sites=' | '.join(labels[:3]), third_km=round(distances[2], 4), mapped_site=local_labels[0],
               mapped_km=round(within_distance, 4), mapped_sites_used=local_count)
    selections = (nearby[:1], nearby[:3], nearby[:5], nearby[:1 if distance <= .25 else 3],
                  local[:local_count], pre[:1])
    for method, selection in zip(METHODS, selections):
        on, total = selection.sum(axis=0)
        row[method] = f'{on}/{total}'
    return row


def south_australia(geo_path, manifest):
    """Match reviewed ECSA candidate votes to independent venue coordinates.

    A join must identify one district, name and voting mode in both sources.
    The existing ECSA adapter first reconciles the ballots for all 47 districts.
    The 2022 count at the same district/name is a prospective size proxy for
    replay. A new ordinary booth takes its district's median matched size.
    Early centres have equal prospective weights because the 2022 booth
    history does not split out these named early counts.
    """
    folder = ROOT / 'downloads/turnout/2026sa/version-912-b1bb7181a5f8'
    raw = {k: source(folder / f'{k}.json', manifest, url) for k, url in SOURCE_URLS.items()}
    build_dataset(raw)
    static = {d['districtName']: d for d in json.loads(raw['static'])['districts']}
    history = json.loads(source(ANALYSIS / 'Booth Results/2022sa.json', manifest))
    coords = {}
    for r in read_xlsx_rows(source(geo_path, manifest, SA_COORDS_URL), 'SA polling places').values():
        mode = {'1': 'ordinary', '2': 'early'}.get(r.get('E'))
        if mode:
            identity = r['C'], key(r['D']), mode
            if identity in coords:
                raise ValueError(f'Ambiguous SA venue {identity}')
            point = position(r.get('J'), r.get('K'))
            # The workbook places this Adelaide recreation centre over the
            # Victorian border. The operator identifies the Kings/Martins Road
            # venue. Use the midpoint of its mapped building bounds, from
            # OpenStreetMap contributors, way 475189747 v3 (see the report).
            # Apply the correction only to the anomalous coordinates.
            if identity == ('Playford', key('Parafield Gardens Early Voting Centre'), 'early'):
                if point == (-36.329886, 141.007015):
                    point = (-34.77433115, 138.6168322)
            coords[identity] = point
    pools = federal_neighbourhoods('SA', manifest)
    rows = []
    for district in json.loads(raw['results'])['districts']:
        seat = district['districtId']
        candidate, = (c['candidateId'] for c in static[seat]['candidates'] if c['partyId'] == 'ONP')
        # ECSA's 2024 redistribution renamed Frome to Ngadjuri. A booth
        # at the same name in that predecessor can still supply a size prior.
        prior_seat = 'Frome' if seat == 'Ngadjuri' else seat
        prior_district = history[prior_seat]
        previously_stood = any(c['party'] == 'ONP' for c in prior_district.get('candidates', {}).values())
        previous = {key(n): sum(b['fp'].values()) for n, b in prior_district.get('booths', {}).items()}
        # ECSA's ordinary candidate summary includes mobile teams as well as
        # fixed sites. Check One Nation's count against it before excluding
        # mobile voting from the geographic comparison.
        reported = sum(c['formalVotes'] for b in district['pollingPlaces'] for c in b['pollingCandidates']
                       if c['candidateId'] == candidate)
        summary, = (c['ordinaryVotes'] for c in district['candidates'] if c['candidateId'] == candidate)
        if reported != summary:
            raise ValueError(f'One Nation booth votes do not reconcile in {seat}')
        district_rows = []
        for booth in district['pollingPlaces']:
            kind = BOOTH_CATEGORIES[booth['pollingPlaceTypeIdName']]
            mode = {'election_day_ordinary': 'ordinary', 'early_in_person': 'early'}.get(kind)
            if not mode:
                continue
            name = booth['pollingPlaceName']
            point = coords.pop((seat, key(name), mode))
            if point is None:
                raise ValueError(f'Missing SA location for {seat} / {name}')
            votes = {c['candidateId']: c['formalVotes'] for c in booth['pollingCandidates']}
            old = previous.get(key(name)) if mode == 'ordinary' else None
            district_rows.append(dict(seat=seat, mode=mode, booth=name, prior_candidate=previously_stood,
                                      on=votes[candidate],
                                      formal=sum(votes.values()), prior_size=old,
                                      size_source='2022 booth' if old else '',
                                      **geographic_inputs(seat, point, pools)))
        sizes = [r['prior_size'] for r in district_rows if r['prior_size']]
        for r in district_rows:
            if not r['prior_size']:
                r['prior_size'] = median(sizes) if r['mode'] == 'ordinary' and sizes else 1
                r['size_source'] = 'district median' if r['mode'] == 'ordinary' and sizes else 'equal'
        rows.extend(district_rows)
    if coords:
        raise ValueError(f'Unmatched SA geography: {list(coords)[:5]}')
    return sorted(rows, key=lambda r: (r['seat'], r['mode'], r['booth']))


def victorian_weights(book):
    """Read expected ordinary and early votes for the *reporting district*.

    The VEC publishes separate rows for the host and guest at a joint site.
    The ordinary column excludes the adjoining estimate of absent votes.
    These estimates serve as relative sizes within each district and mode.
    """
    weights = {}
    with ZipFile(BytesIO(book)) as z:
        strings = _shared_strings(z, 'VEC venue estimates')
        links = {r.get('Id'): r.get('Target') for r in ET.fromstring(z.read('xl/_rels/workbook.xml.rels'))}
        for sheet in ET.fromstring(z.read('xl/workbook.xml')).findall('.//{*}sheet'):
            tab = sheet.get('name')
            if tab not in ('EVCs', 'VCs'):
                continue
            rid = sheet.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id')
            target = links[rid]
            path = target.lstrip('/') if target.startswith('/') else 'xl/' + target
            mode = 'early' if tab == 'EVCs' else 'ordinary'
            for row in ET.fromstring(z.read(path)).findall('.//{*}sheetData/{*}row'):
                values = {}
                for cell in row.findall('{*}c'):
                    value = cell.findtext('{*}v')
                    if value is not None:
                        col = re.match('[A-Z]+', cell.get('r'))[0]
                        values[col] = strings[int(value)] if cell.get('t') == 's' else value
                if row.get('r') == '1':
                    expected = 'Est. Early Votes' if mode == 'early' else 'Est. Ord Votes'
                    if values['I'] != expected:
                        raise ValueError(f'Venue estimate column changed in {tab}')
                elif values.get('A') and values.get('B'):
                    name = values['B'].strip()
                    joint = re.search(r'\s*\((H?J(?:E)?VC)\)\s*$', name)
                    role = ('host' if joint[1].startswith('H') else 'guest') if joint else 'single'
                    identity = (values['A'].removesuffix(' District').strip(), mode,
                                key(name[:joint.start()] if joint else name))
                    if identity in weights:
                        raise ValueError(f'Duplicate Victorian venue estimate {identity}')
                    weights[identity] = int(values['I']), role
    return weights


def victoria(book_path, manifest):
    """Construct a pre-count research roster using the VEC's district and venue IDs."""
    weights = victorian_weights(source(book_path, manifest, VIC_BOOK_URL))
    candidates = ET.fromstring(source(ROOT / 'downloads/2026vic_candidates.xml', manifest))
    previous = json.loads(source(ANALYSIS / 'Booth Results/2022vic.json', manifest))
    standing = {}
    for election in candidates.findall('.//{*}Election'):
        if election.find('{*}ElectionIdentifier').get('Id') == 'L':
            for contest in election.findall('{*}Contest'):
                seat = contest.findtext('{*}ContestIdentifier/{*}ContestName').removesuffix(' District')
                standing[seat] = any(p.text == 'One Nation Victoria' for p in contest.findall('.//{*}RegisteredName'))
    roster = ET.fromstring(source(ROOT / 'downloads/2026vic_booths.xml', manifest))
    pools = federal_neighbourhoods('VIC', manifest)
    rows = []
    for district in roster.findall('{*}PollingDistrict'):
        seat = district.findtext('{*}PollingDistrictIdentifier/{*}Name').removesuffix(' District')
        previously_stood = any('ONE NATION' in c['party'] for c in previous[seat]['candidates'].values())
        for place in district.findall('.//{*}PollingPlace'):
            ident = place.find('{*}PollingPlaceIdentifier')
            mode = {'VotingCentre': 'ordinary', 'EarlyVotingCentre': 'early'}.get(ident.get('Classification'))
            if not mode:
                continue
            name = ident.get('Name')
            point = position(place.findtext('.//{*}AddressLatitude'), place.findtext('.//{*}AddressLongitude'))
            weight, role = weights.pop((seat, mode, key(name)))
            rows.append(dict(seat=seat, mode=mode, booth=name, booth_id=ident.get('Id'),
                             role=role, standing=standing[seat], prior_candidate=previously_stood,
                             prior_size=weight,
                             **geographic_inputs(seat, point, pools)))
    if weights:
        raise ValueError(f'VEC venues missing from the setup: {list(weights)[:5]}')
    return sorted(rows, key=lambda r: (r['seat'], r['mode'], r['booth']))
