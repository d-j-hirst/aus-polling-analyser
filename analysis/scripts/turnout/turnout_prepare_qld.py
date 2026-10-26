"""Prepare a Queensland live turnout prior from rosters and earlier vote counts.

The roll and service list come from the current ECQ preload. Previous formal
votes divide each district's starting allowance between ordinary venues,
ordinary early voting, early absents, election-day absents and postal votes.
Election-eve statewide early/postal totals constrain their respective groups;
they are not district measurements. No current result is used in this prior.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
from statistics import median

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY as ROOT
from lib.turnout import maintained_parameters
from scripts.elections.prepare_qld_live import read_state_election
from scripts.turnout import turnout_prepare_live

CATEGORIES = ['absent', 'early_declaration', 'early_ordinary', 'ordinary_other', 'postal']
DECLARATIONS = {'Absent Election Day': ('Absent', 'absent'),
    'Absent Early Voting': ('PrePoll', 'early_declaration'),
    'Postal Declaration Votes': ('Postal', 'postal'),
    'In Person Declaration Votes': ('Provisional', 'ordinary_other'),
    'Telephone Voting': ('Telephone', 'ordinary_other'),
    'Telephone Voting - Early Voting': ('Telephone', 'ordinary_other')}


def group_for(name):
    """Match the C++ reader's physical-venue versus declaration distinction."""
    if name in DECLARATIONS:
        return DECLARATIONS[name][1]
    if 'Early Voting Centre' in name or 'Returning Officer' in name:
        return 'early_ordinary'
    return 'ordinary_other'


def _historical_accounts(previous, totals):
    """Reconcile the historical booth partition before using it as a baseline."""
    accounts = {}
    for name, district in previous.items():
        groups = defaultdict(float)
        for booth, counts in district['booths'].items():
            groups[group_for(booth)] += sum(counts['fp'].values())
        if sum(groups.values()) != totals[name]['formal_votes']:
            raise ValueError('Historical booth totals do not reconcile: ' + name)
        accounts[name] = groups
    return accounts


def _district_inputs(settings, election, previous, totals):
    """Use current enrolment with previous turnout and formality rates."""
    districts = {d.get('districtName'): d for d in election.findall('districts/district')}
    names = sorted(districts)
    if set(names) != set(totals) or set(names) != set(previous):
        raise ValueError('QLD preparation requires a complete matched district population.')
    accounts = _historical_accounts(previous, totals)
    inputs = dict(identity=settings['parameter_preset'], election_code=settings['election'],
        category_mode='qld_detail', seat_names=names, categories=CATEGORIES,
        subdivisions=['qld'] * len(names),
        enrolment=[int(districts[n].get('enrolment')) for n in names],
        previous_turnout_pct=[100 * totals[n]['total_ballots'] / totals[n]['enrolment'] for n in names],
        previous_formality_pct=[100 * totals[n]['formal_votes'] / totals[n]['total_ballots'] for n in names],
        local_scale=[1] * len(names), controls=[])
    return inputs, districts, accounts


def _apply_operational_controls(inputs, settings, parameters, accounts, totals):
    """Allocate statewide evidence by previous resident counts and roll growth.

    Early attendance includes both ordinary early booths and early absents.
    They share a total control but retain separate composition uncertainties and
    separate live records. The single-election early conversion has a broad
    common error, plus uncertainty in the unobserved district allocation.
    """
    names = inputs['seat_names']
    raw = np.array([[accounts[n][c] + .5 for c in CATEGORIES] for n in names])
    used = set()
    for name, members in [('early', ['early_declaration', 'early_ordinary']), ('postal', ['postal'])]:
        indexes = [CATEGORIES.index(c) for c in members]
        coefficient = parameters['controls'][name]
        weights = raw[:, indexes]
        resident = weights.sum(axis=1) * np.array(inputs['enrolment']) / np.array([totals[n]['enrolment'] for n in names])
        amount = resident / resident.sum() * settings['operational_counts'][name] * coefficient['factor']
        inputs['controls'].append(dict(name=name, indices=indexes, amount=amount.tolist(),
            weights=(weights / weights.sum(axis=1, keepdims=True)).tolist(), aggregate=True,
            common_sd=(amount * coefficient['common_sd'] / coefficient['factor']).tolist(),
            local_sd=(amount * coefficient['resident_distribution']['relative_sd']).tolist()))
        used.update(indexes)
    free = [i for i in range(len(CATEGORIES)) if i not in used]
    weights = raw[:, free]
    inputs['remainder'] = dict(name='remainder', indices=free,
        weights=(weights / weights.sum(axis=1, keepdims=True)).tolist())


def _district_units(index, district, previous, mobile_counts):
    """Divide category totals among venues, preserving absent service identities.

    Ordinary matches use their previous size. New venues use the median of the
    district's comparable venues. Mobile polling resumed after cancellation in
    2020, so its old zero must not freeze its 2024 allowance: use the last active
    2017 district count, falling back to the statewide active-district mean.
    """
    old = previous['booths']
    medians = {g: median([sum(v['fp'].values()) for n, v in old.items()
                         if group_for(n) == g and n not in DECLARATIONS
                         and n != 'Mobile Polling' and sum(v['fp'].values()) > 0])
               for g in ('ordinary_other', 'early_ordinary')}
    rows = {}
    for booth in district.find('countRound').findall('booths/booth'):
        name = booth.get('name'); group = group_for(name)
        recorded = sum(old.get(name, {}).get('fp', {}).values())
        matched = recorded > 0
        if name in DECLARATIONS:
            identity = DECLARATIONS[name][0]; kind = 'declaration'; size = max(.5, recorded)
        else:
            identity = name; kind = 'ppvc' if group == 'early_ordinary' else 'ordinary'
            size = recorded if matched else medians[group]
            if name == 'Mobile Polling':
                size = mobile_counts.get(district.get('districtName'), 0) or mobile_counts['fallback']
                matched = False
        if identity in rows:
            rows[identity]['weight'] += size
        else:
            rows[identity] = dict(seat=index, name=identity, category=identity if kind=='declaration' else group,
                group=CATEGORIES.index(group), kind=kind, weight=size, counted=0, matched=matched,
                eav=False, closed=False, excluded=False, closure_reason='', exclusion_reason='')
    for g in range(len(CATEGORIES)):
        members = [u for u in rows.values() if u['group'] == g]
        total = sum(u['weight'] for u in members)
        if total <= 0: raise ValueError('Missing QLD category service population.')
        for u in members: u['weight'] /= total
    return list(rows.values())


def configuration(settings, election, previous, historical, mobile_history, parameters):
    """Outline preparation independently of file handling and sampling."""
    totals = {r['seat_name']: r for r in historical['seat_totals']}
    inputs, districts, accounts = _district_inputs(settings, election, previous, totals)
    _apply_operational_controls(inputs, settings, parameters, accounts, totals)
    mobile = defaultdict(float)
    for row in mobile_history['vote_types']:
        if row['canonical_category'] == 'mobile_or_institution' and row['seat_name'] in districts:
            mobile[row['seat_name']] += row['formal_votes'] or 0
    mobile['fallback'] = sum(mobile.values()) / len(districts)
    units = [u for i, n in enumerate(inputs['seat_names'])
             for u in _district_units(i, districts[n], previous[n], mobile)]
    result = dict(schema_version=1, **settings, inputs=inputs, units=units)
    turnout_prepare_live.validate_configuration(result, parameters)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('election')
    args = parser.parse_args(argv)
    folder = ROOT / 'forecasts' / args.election
    settings = json.loads((folder / 'live-turnout-settings.json').read_text())
    previous = settings['previous_election']
    paths = dict(preload=ROOT/'downloads'/f'{args.election}_zeros.xml',
        booths=ROOT/'analysis/Booth Results'/f'{previous}.json',
        totals=ROOT/'analysis/Data/Turnout'/f'{previous}.json',
        mobile=ROOT/'analysis/Data/Turnout/2017qld.json')
    _, election = read_state_election(paths['preload'], int(args.election[:4]))
    parameters = maintained_parameters.prior_parameters(settings['parameter_preset'])
    config = configuration(settings, election,
        *[json.loads(paths[k].read_text()) for k in ('booths', 'totals', 'mobile')], parameters)
    config['sources'] = {k: dict(name=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for k,p in paths.items()}
    output = folder/'live-inputs';output.mkdir(exist_ok=True)
    target = output/'turnout-config.json'
    target.write_text(json.dumps(config, separators=(',', ':'))+'\n')
    return turnout_prepare_live.main([str(target)])


if __name__ == '__main__':
    raise SystemExit(main())
