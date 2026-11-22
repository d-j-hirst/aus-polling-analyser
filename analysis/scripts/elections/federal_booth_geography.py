"""Locate federal House first preferences near state polling places.

State election-night preparation and the SA validation use the same AEC
candidate counts, venue grouping and great-circle distances. A site can
serve voters in several districts. Pool its formal ballots before ranking
its distance from the location on the state ballot.
"""

import hashlib
import json
from collections import Counter, defaultdict
from dataclasses import dataclass

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY as ROOT
from scripts.turnout.turnout_federal_prepoll import csv_rows


def source(path, manifest, label=None):
    """Identify the exact bytes used, including inputs shared with other studies."""
    data = path.read_bytes()
    manifest[label or path.relative_to(ROOT).as_posix()] = hashlib.sha256(data).hexdigest()
    return data


def position(lat, lon):
    if not lat or not lon:
        return None
    point = float(lat.strip().removesuffix('°')), float(lon.strip().removesuffix('°'))
    if not (-45 < point[0] < -10 and 110 < point[1] < 156):
        raise ValueError(f'Unexpected Australian polling place position {point}')
    return point


@dataclass(frozen=True)
class FederalBooth:
    division: str
    name: str
    mode: str
    point: tuple
    on: int
    formal: int


def federal_booths(state, manifest):
    """Join AEC candidate counts to venues by division and polling-place IDs.

    OrdinaryVotes is the AEC column for votes cast at both ordinary booths and
    pre-poll centres. A missing One Nation candidate makes comparisons unusable.
    An observed zero for a standing candidate is a genuine result.
    """
    cache = ROOT / 'downloads/turnout/federal-prepoll/2025fed'
    index = json.loads((cache / 'latest.json').read_bytes())

    def rows(label):
        entry = index[label]
        url = entry['url']
        data = source(cache / entry['sha256'] / url.split('/')[-1], manifest, url)
        if manifest[url] != entry['sha256']:
            raise ValueError(f'Federal source digest differs for {label}')
        return csv_rows(data)

    counts = defaultdict(Counter)
    for row in rows(state):
        if row['PartyNm'] == 'Informal':
            continue
        booth = counts[row['DivisionID'], row['PollingPlaceID']]
        votes = int(row['OrdinaryVotes'])
        booth['formal'] += votes
        if row['PartyAb'] == 'ON':
            booth['candidates'] += 1
            booth['on'] += votes
    booths = []
    for row in rows('places'):
        if row['State'] != state or row['PremisesStateAb'] != state:
            continue
        mode = {'1': 'ordinary', '5': 'early'}.get(row['PollingPlaceTypeID'])
        if not mode:
            continue
        count = counts[row['DivisionID'], row['PollingPlaceID']]
        if count['candidates'] != 1:
            raise ValueError(f"One Nation candidate missing or ambiguous at {row['PollingPlaceNm']}")
        point = position(row['Latitude'], row['Longitude'])
        if point and count['formal']:
            booths.append(FederalBooth(row['DivisionNm'], row['PollingPlaceNm'], mode, point,
                                       count['on'], count['formal']))
    return booths


class Sites:
    """Pool ballots at the same federal coordinates before choosing nearby sites.

    A shared building can have two federal counts. Giving both of them one
    place in the distance ranking leaves room for other nearby neighbourhoods.
    Rounding to four coordinate decimals groups points located within about
    ten metres when their rounded coordinates coincide.
    """

    def __init__(self, booths):
        groups = defaultdict(list)
        for b in booths:
            groups[tuple(round(x, 4) for x in b.point)].append(b)
        points = sorted(groups)
        self.points = np.radians(points)
        self.counts = np.array([(sum(b.on for b in groups[p]), sum(b.formal for b in groups[p]))
                                for p in points], dtype=int)
        self.labels = [' / '.join(sorted(f'{b.division}: {b.name}' for b in groups[p]))
                       for p in points]

    def nearby(self, point):
        """Rank federal sites by great-circle distance to a state venue, in km."""
        lat, lon = np.radians(point)
        latitudes, longitudes = self.points.T
        haversine = (np.sin((latitudes - lat) / 2) ** 2 +
                     np.cos(lat) * np.cos(latitudes) * np.sin((longitudes - lon) / 2) ** 2)
        distances = 12742 * np.arcsin(np.sqrt(np.clip(haversine, 0, 1)))
        order = np.argsort(distances, kind='stable')[:5]
        return self.counts[order], distances[order], [self.labels[i] for i in order]
