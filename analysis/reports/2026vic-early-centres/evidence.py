"""Record the electoral evidence used to compare early voting centres.

One observation belongs to one district at one centre. Its outcome is the
formal candidate count at that centre. Its predictors are earlier ordinary
votes near the venue, restricted to booths serving the district. Keeping
these counts separate lets the recorded experiment run without a live feed.
"""

import csv
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import astuple, dataclass
from pathlib import Path
from tempfile import NamedTemporaryFile

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'analysis'))

from scripts.turnout.turnout_federal_prepoll import csv_rows
from scripts.diagnostics.category_tpp_history import side
from lib.turnout.ppvc_sizes import LocationScreen


# One booth tests the closest venue alone. Larger groups trade local detail
# for more ballots and a wider catchment. Five is the original study choice.
WINDOWS = (1, 3, 5, 8, 12)
# Five hundred formal ballots defines a substantial comparison centre. This
# research cutoff limits comparisons dominated by small counts. The full
# election-night roster keeps smaller venues when estimating outstanding votes.
MIN_VOTES = 500
CHOSEN_WINDOW = 5
# A rounded four-fifths is the common FP strength tested in Victoria. TPP
# uses the separately fitted federal strength when allocating the 2026 vote.
STRENGTH = 0.8
PRIMARY = ('labor', 'coalition', 'greens', 'other')
YEARS = (2010, 2013, 2016, 2019, 2022, 2025)
COUNT_FIELDS = ('formal', 'labor', 'coalition', 'greens', 'tpp_labor', 'tpp_formal')


@dataclass(frozen=True)
class Counts:
    """FP groups share a formal denominator. TPP has its own count and can be missing."""

    formal: int
    labor: int
    coalition: int
    greens: int
    tpp_labor: int | None = None
    tpp_formal: int | None = None

    def __post_init__(self):
        primary = (self.labor, self.coalition, self.greens)
        if min(primary) < 0 or sum(primary) > self.formal:
            raise ValueError(f'Invalid first-preference count {self}')
        if (self.tpp_labor is None) != (self.tpp_formal is None):
            raise ValueError(f'Incomplete two-party count {self}')
        if self.tpp_formal is not None and not 0 <= self.tpp_labor <= self.tpp_formal:
            raise ValueError(f'Invalid two-party count {self}')


@dataclass(frozen=True)
class Booth:
    seat: str
    name: str
    key: str
    position: tuple[float, float] | None
    votes: Counts | None


def distance_km(a, b):
    """Great-circle distance from the official latitude and longitude in degrees."""
    lat1, lon1, lat2, lon2 = map(math.radians, (*a, *b))
    chord = (math.sin((lat2 - lat1) / 2) ** 2
             + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2) ** 2)
    return 12742.0176 * math.asin(min(1, math.sqrt(chord)))


def position(lat, lon):
    """Blank or out-of-range coordinates cannot identify a Victorian neighbourhood."""
    try:
        lat, lon = float(lat), float(lon)
    except (TypeError, ValueError):
        return None
    return (lat, lon) if -40 < lat < -33 and 139 < lon < 151 else None


def grouped_votes(parties, tpp_labor=None, tpp_formal=None):
    """Combine formal party counts, keeping all remaining candidates in the denominator."""
    totals = Counter()
    for party, votes in parties.items():
        group = side(party)
        if party.upper() in ('AUSTRALIAN GREENS', 'THE GREENS', 'GRN'):
            group = 'greens'
        totals[group] += votes
    return Counts(sum(totals.values()), totals['alp'], totals['coalition'], totals['greens'],
                  tpp_labor, tpp_formal)


def pooled(booths):
    """Pool ballots across booths. A missing preference count leaves local TPP unknown."""
    columns = zip(*(astuple(b.votes) for b in booths))
    return Counts(*(None if None in column else sum(column) for column in columns))


def observation(election, centre, ordinary, size=None, size_source=''):
    """Record prior ordinary counts at each distance window separately from the outcome.

    Historical voters contribute one ballot each to the local share. Distance
    selects which booths enter. Tied distances use the official stable key.
    All windows are recorded on the same population, using every available
    booth when the district has fewer than the requested number.
    """
    nearby = sorted(ordinary, key=lambda b: (distance_km(centre.position, b.position), b.key))
    row = dict(election=election, district=centre.seat, centre=centre.name, key=centre.key,
               size_weight=size, size_source=size_source, available=len(nearby))
    outcome = astuple(centre.votes) if centre.votes is not None else [None] * len(COUNT_FIELDS)
    row.update(zip(COUNT_FIELDS, outcome))
    for count in WINDOWS:
        values = astuple(pooled(nearby[:count])) if nearby else [None] * len(COUNT_FIELDS)
        row.update((f'n{count}_{field}', val) for field, val in zip(COUNT_FIELDS, values))
    return row, nearby[:5]


FIRST_REPORTS = Path('operator/count-history/first-reports.csv')


def write_csv(path, rows):
    """Replace one complete recorded table after writing and closing its temporary copy."""
    if not rows:
        raise ValueError(f'Empty evidence table {path.name}')
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile('w', encoding='utf-8', newline='', dir=path.parent, delete=False) as temp:
        writer = csv.DictWriter(temp, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    Path(temp.name).replace(path)


def read_csv(path):
    with path.open(encoding='utf-8', newline='') as source:
        return list(csv.DictReader(source))


def counts(row, prefix=''):
    """Recover the recorded numerators and denominators for TPP and the four FP groups."""
    def value(key):
        text = row[prefix + key]
        return float(text) if text != '' else np.nan

    total = value('formal')
    labor, coalition, greens = (value(k) for k in PRIMARY[:3])
    other = total - labor - coalition - greens
    return (np.array([value('tpp_labor'), labor, coalition, greens, other]),
            np.array([value('tpp_formal'), total, total, total, total]))


def district_rows(rows):
    districts = defaultdict(list)
    for row in rows:
        districts[row['district']].append(row)
    return districts


def digest(data):
    return hashlib.sha256(data).hexdigest()


def aec_election(year, sources):
    """Read the final Victorian AEC House polling places, formal primaries and Labor TPP.

    A federal pre-poll centre is type 5. Ordinary booths are type 1. Both can
    appear in the column called OrdinaryVotes. The separate AEC two-party
    count also covers divisions with a different final candidate pairing.
    """
    directory = ROOT / 'downloads/turnout/federal-prepoll' / f'{year}fed'
    manifest = json.loads((directory / 'latest.json').read_bytes())

    def retained(key):
        entry = manifest[key]
        data = (directory / entry['sha256'] / entry['url'].split('/')[-1]).read_bytes()
        if digest(data) != entry['sha256']:
            raise ValueError(f'Changed AEC input {entry["url"]}')
        sources[entry['url']] = digest(data)
        return csv_rows(data)

    places, candidates = retained('places'), retained('VIC')
    url = manifest['places']['url'].replace('GeneralPollingPlaces', 'HouseTppByPollingPlace')
    tpp_file = ROOT / 'downloads/category-tpp-research' / digest(url.encode())
    data = tpp_file.read_bytes()
    sources[url] = digest(data)
    tpp = {(r['DivisionID'], r['PollingPlaceID']):
           (int(r['Australian Labor Party Votes']), int(r['Liberal/National Coalition Votes']))
           for r in csv_rows(data) if r['StateAb'] == 'VIC'}
    parties = defaultdict(Counter)
    candidate_party = {}
    for r in candidates:
        if r['PartyNm'] != 'Informal':
            parties[r['DivisionID'], r['PollingPlaceID']][r['PartyAb']] += int(r['OrdinaryVotes'])
            candidate_party[r['CandidateID']] = r['PartyAb']

    kinds = {'1': {}, '5': {}}
    for r in places:
        kind, key = r['PollingPlaceTypeID'], r['PollingPlaceID']
        if r['State'] != 'VIC' or kind not in kinds:
            continue
        coordinates = position(r['Latitude'], r['Longitude'])
        if coordinates is None:
            continue
        identity = r['DivisionID'], key
        # Some listed sites finish with no votes attributed to their ID. Zero
        # describes their contribution to the published total. Their physical
        # voting activity is unknown, and the full-roster test retains them.
        labor, coalition = tpp.get(identity, (0, 0))
        votes = grouped_votes(parties[identity], labor, labor + coalition)
        if votes.formal != votes.tpp_formal or key in kinds[kind]:
            raise ValueError(f'AEC booth counts or identity disagree at {year}/{identity}')
        kinds[kind][key] = Booth(r['DivisionNm'], r['PollingPlaceNm'], key, coordinates, votes)
    return kinds['1'], kinds['5'], candidate_party


def federal_cases(year, previous, current):
    """Construct pre-election neighbourhoods and a usable pre-election size comparison.

    An ordinary site's persistent ID can cross a boundary. Assign its previous
    votes to the current reporting division if the site stayed within 1 km.
    For early centre size, a continuing centre must also retain its division.
    New or moved centres take the median substantial previous centre in that
    division, or the Victorian median when the division name itself is new.
    """
    from statistics import median

    ordinary = defaultdict(list)
    for key, site in current[0].items():
        old = previous[0].get(key)
        if old and old.votes.formal and distance_km(old.position, site.position) <= 1:
            ordinary[site.seat].append(old)
    by_seat = defaultdict(list)
    for site in previous[1].values():
        if site.votes.formal >= MIN_VOTES:
            by_seat[site.seat].append(site.votes.formal)
    statewide = median(n for sizes in by_seat.values() for n in sizes)
    # The same geographic screen used in the project's federal turnout work
    # identifies centres far from the division's ordinary booths. This only
    # labels a transferability test. It does not pick votes or predict size.
    screen = LocationScreen([dict(position=b.position, seat_name=b.seat) for b in current[0].values()])
    rows = []
    for key, site in current[1].items():
        if len(ordinary[site.seat]) < 3:
            continue
        old = previous[1].get(key)
        if (old and old.seat == site.seat and old.votes.formal > 0
                and distance_km(old.position, site.position) <= 1):
            size, source = old.votes.formal, 'previous centre'
        elif by_seat[site.seat]:
            size, source = median(by_seat[site.seat]), 'previous division median'
        else:
            size, source = statewide, 'previous Victoria median'
        row, _ = observation(f'{year}fed', site, ordinary[site.seat], size, source)
        row['catchment'] = screen.classify(site.seat, site.position)
        rows.append(row)
    return rows
