"""Set research starting weights for new or changed federal pre-poll centres.

Past local centres provide a size assumption, while centres serving voters
away from their electorate retain their existing smaller fallback or count.
Coordinates and historical counts are fixed before live reporting begins.
The weights divide the existing district category expectation; they do not
create a new district or category vote target.
"""

from collections import Counter, defaultdict
import csv

import numpy as np
from scipy.spatial import cKDTree

from lib.turnout import aec_live


def spherical_positions(positions):
    """Use three-dimensional unit vectors to compare distances on the globe."""
    latitude, longitude = np.radians(np.asarray(positions, dtype=float)).T
    return np.column_stack((np.cos(latitude)*np.cos(longitude),
                            np.cos(latitude)*np.sin(longitude), np.sin(latitude)))


class LocationScreen:
    """Screen PPVCs using nearby ordinary booths, without claiming boundaries.

    The nearest ordinary booth's electorate is a simple indication of the
    local catchment. If none of the nearest three belongs to the home electorate,
    label the PPVC as likely outside. A mixed neighbourhood is uncertain and
    receives no new-size assumption. This screen avoids treating distant city
    services as typical local centres; it cannot resolve redistribution effects.
    """

    def __init__(self, ordinary):
        # Some city ordinary venues are returned under many electorates, just
        # like remote PPVC services. They cannot locate an electorate boundary:
        # choosing an arbitrary tied record would label a remote centre local.
        # Exclude shared sites before constructing the geographic reference.
        located = [b for b in ordinary if b.get('position') is not None]
        owners = defaultdict(set)
        for booth in located:
            owners[tuple(round(v, 6) for v in booth['position'])].add(booth['seat_name'])
        self.ordinary = list({tuple(round(v, 6) for v in b['position']): b for b in located
                              if len(owners[tuple(round(v, 6) for v in b['position'])]) == 1}.values())
        self.tree = cKDTree(spherical_positions([b['position'] for b in self.ordinary])) if self.ordinary else None

    def classify(self, seat, position):
        """Return a location role using only the supplied election's geography."""
        if position is None or self.tree is None:
            return 'unknown'
        _, indices = self.tree.query(spherical_positions([position])[0], k=min(3, len(self.ordinary)))
        neighbours = [self.ordinary[int(i)]['seat_name'] for i in np.atleast_1d(indices)]
        if neighbours[0] == seat:
            return 'likely_local'
        return 'likely_outside' if seat not in neighbours else 'uncertain'


SEAT_TYPE_NAMES = {'0': 'Inner metropolitan', '1': 'Outer metropolitan', '2': 'Provincial', '3': 'Rural'}

# Research stop-gap derived from retained 2019/2022 reporting histories.
# In 2022, substantial centres were still unreported on Monday evening, so
# retain full size through 48 hours. Later knots deliberately exceed the
# observed aggregate later-feed/initial-size ratios at comparable times.
# A positive final plateau avoids pretending that delay proves closure.
REPORTING_DECAY_KNOTS = ((48., 1.), (54., .25), (78., .10), (102., .02))


def reporting_decay_factor(hours_after_poll_close):
    """Interpolate a conservative delayed-return assumption on the log scale.

    This is a marginal expectation for future votes under an unreported feed
    identity, not a claim about votes cast at its venue. The live updater adds
    its log factor to the booth share's log odds. The rule is experimental;
    it does not model a separate probability of a large delayed return.
    """
    hours, factors = np.asarray(REPORTING_DECAY_KNOTS).T
    return float(np.exp(np.interp(hours_after_poll_close, hours, np.log(factors))))


def read_seat_types(path):
    """Use the project's existing federal classifications, without guessing.

    The file has no header. Historical seats absent from it remain unclassified
    and use the national reference rather than inheriting another seat's type.
    """
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return {name: kind for name, region, kind in csv.reader(stream) if region == 'fed'}


def historical_sizes(previous, older, places, seat_types=None,
                     election='2022fed', comparison_election='2019fed'):
    """Measure earlier local centres new or changed since their preceding election.

    An exact same-seat, same-name/type, positive-count match identifies an
    established centre. All other public PPVCs are candidates for the historical
    new/changed sample. EAV, Divisional Office and BLV services are excluded.
    Only the likely-local sample sets the average/median used for predictions.
    Positive formal counts are exact measurements; empty records are excluded
    as unknown service/reporting status rather than invented small centres.
    """
    locations = {(r['DivisionNm'], r['PollingPlaceID']): r for r in places}
    seat_types = seat_types or {}
    ordinary = [dict(seat_name=r['DivisionNm'], position=csv_position(r))
                for r in places if r['PollingPlaceTypeID'] == '1']
    screen = LocationScreen(ordinary)
    samples, exclusions = [], []
    for booth in previous['booths'].values():
        if aec_live.live_booth_type(booth) != 'ppvc' or booth['name'].startswith('EAV '):
            continue
        old = older['booths'].get(booth['id'])
        established = bool(old and old['seat_name'] == booth['seat_name'] and old['name'] == booth['name']
                           and old['classification'] == booth['classification'] and old['counted'] > 0)
        if established:
            continue
        place = locations.get((booth['seat_name'], booth['id']))
        role = screen.classify(booth['seat_name'], csv_position(place) if place else None)
        row = dict(seat=booth['seat_name'], name=booth['name'], id=booth['id'], count=booth['counted'], location=role,
                   seat_type=seat_types.get(booth['seat_name']))
        if role == 'likely_local' and booth['counted'] > 0:
            samples.append(row)
        else:
            exclusions.append(row)
    if not samples:
        raise ValueError('No positive local new/changed PPVC counts in the historical reference.')
    counts = [r['count'] for r in samples]
    # Rural local centres can serve much smaller communities than metropolitan
    # or provincial centres. Keep the four existing types separate; retain the
    # national statistic as the explicit fallback for an unclassified seat.
    by_type = {}
    for kind, name in SEAT_TYPE_NAMES.items():
        rows = [r for r in samples if r['seat_type'] == kind]
        if rows:
            by_type[kind] = dict(name=name, centres=len(rows), districts=len({r['seat'] for r in rows}),
                                 mean=float(np.mean([r['count'] for r in rows])),
                                 median=float(np.median([r['count'] for r in rows])))
    return dict(election=election, comparison_election=comparison_election, samples=samples, excluded=exclusions,
                centres=len(samples), mean=float(np.mean(counts)), median=float(np.median(counts)),
                seat_types=seat_types, by_seat_type=by_type,
                excluded_locations=dict(Counter(r['location'] for r in exclusions)))


def csv_position(row):
    """Read a historical coordinate, retaining absent/placeholder locations."""
    if not row or not row.get('Latitude') or not row.get('Longitude'):
        return None
    value = [float(row['Latitude']), float(row['Longitude'])]
    return value if value != [0., 0.] else None


def starting_weights(units, raw_counts, metadata, calibration, statistic='median'):
    """Divide each fixed category expectation with corrected centre weights.

    Replace only unreliable public PPVC sizes screened as local. Reliable
    centres retain their previous counts, and outside/uncertain services retain
    the existing count or fallback. EAV keeps its separate override in the live
    updater. Re-normalising within each category changes allocation alone.
    The estimate's source and geographic role are exported for inspection.
    """
    if statistic not in ('legacy', 'mean', 'median', 'mean_by_type', 'median_by_type'):
        raise ValueError('Choose legacy, mean/median or a mean/median by seat type.')
    screen = LocationScreen([b for b in metadata.values() if b['classification'] == 'Normal'])
    sizes = np.asarray(raw_counts, dtype=float).copy()
    details = {}
    for j, unit in enumerate(units):
        source = 'existing_count_or_fallback'
        role = None
        if unit['kind'] == 'ppvc' and not unit.get('is_eav'):
            role = screen.classify(unit['seat_name'], metadata[unit['id']].get('position'))
            if statistic != 'legacy' and not unit['matched'] and role == 'likely_local':
                kind = calibration.get('seat_types', {}).get(unit['seat_name'])
                reference = calibration.get('by_seat_type', {}).get(kind, calibration) if statistic.endswith('_by_type') else calibration
                sizes[j] = reference[statistic.removesuffix('_by_type')]
                source = '2022_local_new_or_changed_' + statistic
        details[unit['id']] = dict(ppvc_location=role, starting_size_source=source,
                                  seat_type=calibration.get('seat_types', {}).get(unit['seat_name']))
    totals = Counter()
    for unit, value in zip(units, sizes):
        totals[(unit['seat_index'], unit['group_index'])] += value
    weights = {u['id']: float(value/totals[(u['seat_index'], u['group_index'])]) for u, value in zip(units, sizes)}
    return weights, details
