"""Score the prepared SA ordinary-booth geography against outstanding election-night votes.

This read-only check uses the new replay's federal input and its turnout
prior. One observation is a district at its first update with one or three
ordinary booths reporting at least 100 formal ballots. A capture can contain
several new booths. Final outcomes in the district being evaluated supply
the score for the combined votes at booths still to report.
"""
import argparse
import hashlib
import json
from dataclasses import replace
from pathlib import Path

import numpy as np

from evidence import HERE, ROOT
from study import LOCAL_LIMIT_KM, district_data, federal_log_odds, read_first_returns, read_rows, replay_scores


def score(input_path):
    """Compare carrying the raw counted share forward with the federal prediction.

    The same pre-count formal sizes weight both methods. In each district
    they use the same actual ballots at the milestone to set the local level.
    The published measure is the average absolute error, giving each eligible
    district equal weight, in percentage points of the outstanding ordinary FP.
    """
    geography = json.loads(input_path.read_bytes())
    if geography['election'] != '2026sa':
        raise ValueError('Select the geography from an SA replay.')
    turnout_path = 'forecasts/2026sa/live-inputs/turnout-prior.json'
    data = (ROOT / turnout_path).read_bytes()
    if hashlib.sha256(data).hexdigest() != geography['sources'][turnout_path]:
        raise ValueError('The prepared geography records a different SA turnout prior.')
    turnout = json.loads(data)
    prior = turnout['prior']
    formal = np.mean(prior['counts'], axis=0).reshape(len(prior['seats']), len(prior['groups']))
    sizes = {(prior['seats'][u['seat']], u['name']):
             0. if u['closed'] else u['weight'] * formal[u['seat'], u['group']]
             for u in turnout['units'] if u['kind'] == 'ordinary'}
    districts, methods = {}, {}
    for identity, d in district_data(read_rows(HERE / 'sa-booths.csv')).items():
        if d.mode != 'ordinary':
            continue
        prepared = geography['districts'][d.seat]
        offsets = []
        for name in d.names:
            place = prepared['places'][name]
            local = place.get('nearest_km', np.inf) <= LOCAL_LIMIT_KM
            offsets.append(federal_log_odds(f"{place['one_nation']}/{place['formal']}") if local else np.nan)
        # A zero offset at every location gives every outstanding voter
        # the pooled reported share. Both rules retain the same turnout weights.
        contrasts = {'federal': np.array(offsets), 'uniform': np.zeros(len(offsets))}
        districts[identity] = replace(d, x=contrasts, sizes=np.array([sizes[d.seat,n] for n in d.names]))
        methods[identity] = {'federal': ('federal', prepared['strength']), 'uniform': ('uniform', 1.)}
    returns = [r for r in read_first_returns(HERE) if r['mode'] == 'ordinary']
    results = replay_scores(districts, methods, returns)
    for stage in sorted({r['stage'] for r in results}):
        prior_results = [r for r in results if r['stage'] == stage and r['weights'] == 'prior']
        errors = {rule: [abs(r['error_pp']) for r in prior_results if r['rule'] == rule]
                  for rule in ('federal', 'uniform')}
        print(f"At the {stage}-booth milestone across {len(errors['federal'])} districts, "
              f"federal {np.mean(errors['federal']):.2f} pp, uniform {np.mean(errors['uniform']):.2f} pp")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=Path, help='Prepared one-nation-booths.json from a private SA replay')
    score(parser.parse_args().input)
