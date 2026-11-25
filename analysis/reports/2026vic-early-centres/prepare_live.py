"""Prepare centre sizes and previous-election neighbourhood votes for the live forecast.

The venue workbook supplies relative sizes within each reporting district.
The matched ordinary booths supply political geography only. The live model
provides the district vote totals and keeps individual candidates on the ballot.
"""
import argparse
import json
import math
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from tempfile import NamedTemporaryFile

from victoria import ROOT, current_cases, historical_votes, locations, venue_weights, workbooks
from evidence import distance_km
from scripts.elections.federal_booth_geography import Sites, federal_booths, position, source
from scripts.elections.federal_state import parse_booth_mapping


def narracan_on_contrasts(centres, sources):
    """Use earlier ON ballots to distinguish centres while preserving Narracan's early vote."""
    roster = ET.fromstring(source(ROOT / 'downloads/2026vic_booths.xml', sources))
    district = next(d for d in roster.findall('{*}PollingDistrict')
                    if d.findtext('{*}PollingDistrictIdentifier/{*}Name') == 'Narracan District')
    positions = {}
    for booth in district.findall('.//{*}PollingPlace'):
        ident = booth.find('{*}PollingPlaceIdentifier')
        if ident.get('Classification') in ('VotingCentre', 'EarlyVotingCentre'):
            point = position(booth.findtext('.//{*}AddressLatitude'), booth.findtext('.//{*}AddressLongitude'))
            if point:
                positions[ident.get('Name')] = point

    mapping = ROOT / 'analysis/Federal-State/booths-2026vic.txt'
    source(mapping, sources)
    members = parse_booth_mapping(mapping)['Narracan']
    federal = Sites(b for b in federal_booths('VIC', sources)
                    if b.mode == 'ordinary' and (b.division, b.name) in members)
    history = json.loads(source(ROOT / 'analysis/Booth Results/2022vic.json', sources))['Narracan']
    candidate, = (key for key, c in history['candidates'].items() if c['party'] == "PAULINE HANSON'S ONE NATION")
    # The January supplementary poll supplies ON votes but had no Labor candidate.
    # Compare same-named ordinary booths at their current locations. Restrict both
    # elections to the reporting district, especially at Moe, which serves Morwell too.
    ordinary = (positions.keys() & history['booths'].keys()) - centres.keys()
    scores, reliabilities = {}, {}
    for name in centres:
        point = positions[name]
        ballots, distances, _ = federal.nearby(point)
        on, formal = ballots.sum(axis=0)
        nearest = sorted(ordinary, key=lambda b: (distance_km(point, positions[b]), b))[:5]
        piles = [history['booths'][b]['fp'] for b in nearest]
        state_on = sum(fp.get(candidate, 0) for fp in piles)
        state_formal = sum(sum(fp.values()) for fp in piles)
        if len(nearest) != 5 or len(ballots) != 5 or not (0 < on < formal and 0 < state_on < state_formal):
            raise ValueError(f'Incomplete Narracan neighbourhood evidence for {name}')
        scores[name] = (math.log(on / (formal-on)) + math.log(state_on / (state_formal-state_on))) / 2
        # A five-kilometre distance scale discounts a guest centre's weaker
        # connection to the electorate. Both this and half-strength transfer
        # are cautious assumptions about how widely early voters travel.
        reliabilities[name] = min(1., 5 / max(5., distances[0], distance_km(point, positions[nearest[0]])))
    weights = {k: row['weight'] * reliabilities[k] for k, row in centres.items()}
    mean = sum(weights[k] * scores[k] for k in centres) / sum(weights.values())
    contrasts = {k: 0.5 * reliabilities[k] * (scores[k]-mean) for k in centres}
    if any(abs(value) > 1 for value in contrasts.values()):
        raise ValueError('Narracan contrast exceeds the bounded share adjustment')
    return contrasts


def prepare(fetch=False):
    sources = {}
    books = workbooks(fetch, sources, {'locations2022', 'venues2026'})
    cases = current_cases(locations(books['locations2022'], 2022), historical_votes(2022, sources),
                          [], venue_weights(books['venues2026']), sources, source_ids=True)
    locality_counts = Counter((row['district'], row['suburb']) for row in cases)
    districts = {}
    for case in cases:
        # Readers see the locality. In districts with two venues in the same
        # suburb the VEC's own label distinguishes them. The source key and ID
        # remain stable for count history regardless of the presentation name.
        label = case['suburb'] + ' EVC'
        if locality_counts[case['district'], case['suburb']] > 1:
            label += ' (' + case['centre'] + ')'
        row = {'id': int(case['key']), 'label': label, 'weight': int(case['size_weight'])}
        if case['available'] >= 5 and case['n5_formal']:
            row['fp'] = [case['n5_' + party] for party in ('labor', 'coalition', 'greens')]
            if case['n5_tpp_formal']:
                labor = case['n5_tpp_labor']
                row['tpp'] = [labor, case['n5_tpp_formal'] - labor]
        districts.setdefault(case['district'], {})[case['centre']] = row
    narracan = districts['Narracan']
    for name, contrast in narracan_on_contrasts(narracan, sources).items():
        narracan[name]['one_nation_contrast'] = contrast
    path = ROOT / 'forecasts/2026vic/live-inputs/evc-centres.json'
    data = {'schema_version': 1, 'election': '2026vic', 'sources': sources, 'districts': districts}
    with NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent, delete=False) as stream:
        json.dump(data, stream, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
        temporary = stream.name
    Path(temporary).replace(path)
    print(f'Prepared {len(cases)} centre entries in {len(districts)} districts at {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fetch', action='store_true', help='Obtain missing public VEC workbooks')
    prepare(parser.parse_args().fetch)
