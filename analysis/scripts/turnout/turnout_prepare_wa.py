"""Prepare WA's changed counting categories without using current results.

The 2021 resident totals and current enrolment supply starting turnout. Published
election-eve early and postal totals constrain the starting account. The unknown
division of early voters between local places and absents has broad, shared and
local uncertainty. All categories still partition the same formal-vote total.
"""

import argparse
import hashlib
import json
from statistics import median

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY as ROOT
from lib.turnout import maintained_parameters, prior
from scripts.elections.prepare_wa_baseline import xml_districts, NS
from scripts.turnout import turnout_prepare_live

CATEGORIES = ['ordinary', 'early', 'absent', 'postal', 'mobile', 'provisional']
DECLARATIONS = {'Absent': 'absent', 'Postal': 'postal', 'SIR': 'mobile', 'Provisional': 'provisional'}


def configuration(settings, baseline, audit, current, historical_totals, reference_provisionals, parameters):
    """Keep observed population facts separate from the uncertain reporting split.

    Postal applications use the installed conversion learned from older WA
    elections. The early issued/formal conversion uses WA 2021; its new local
    versus absent split is an explicit assumption. NSW 2023 enrolment votes
    supply a broad provisional size reference because WA 2021 eligibility was
    materially narrower. No WA 2025 count, share or final total is read here.
    """
    names = sorted(current)
    old = {s['seat_name']: s for s in historical_totals['seat_totals']}
    previous = []
    for name in names:
        rows = [old[n] for n in audit[name]['references']]
        previous.append({k: sum(r[k] for r in rows) for k in ('enrolment', 'formal_votes', 'total_ballots')})
    inputs = dict(identity=settings['parameter_preset'], election_code=settings['election'],
        category_mode='wa_synthetic_reporting', seat_names=names, categories=CATEGORIES,
        subdivisions=['wa']*len(names), enrolment=[int(current[n].get('Enrolment')) for n in names],
        previous_turnout_pct=[100*r['total_ballots']/r['enrolment'] for r in previous],
        previous_formality_pct=[100*r['formal_votes']/r['total_ballots'] for r in previous],
        local_scale=[1.5 if n in ('Oakford', 'Jandakot', 'Mid-West') else 1 for n in names], controls=[])
    turnout, formality = prior.starting_rates(inputs, parameters)
    totals = np.asarray(inputs['enrolment'])*turnout*formality/10000
    growth = np.asarray(inputs['enrolment'])/np.array([r['enrolment'] for r in previous])
    old_categories = {}
    for name in names:
        groups = dict.fromkeys(CATEGORIES, 0.)
        for row in baseline['districts'][name]['booths']:
            groups['early' if row['kind']=='ppvc' else 'ordinary'] += sum(row['fp'].values())
        for category, row in baseline['districts'][name]['declarations'].items():
            groups[DECLARATIONS[category]] += sum(row['fp'].values())
        old_categories[name] = groups
    raw = np.array([[old_categories[n][c] for c in CATEGORIES] for n in names])
    postal = parameters['controls']['postal']
    resident_postal = (raw[:, 3]+.5)*growth
    amount = resident_postal/resident_postal.sum()*settings['operational_counts']['postal']*postal['factor']
    inputs['controls'].append(dict(name='postal', indices=[3], amount=amount.tolist(),
        weights=[[1.]]*len(names), aggregate=True,
        common_sd=(amount*postal['common_sd']/postal['factor']).tolist(),
        local_sd=(amount*.30).tolist()))
    early_formal = sum(r['formal_votes'] for r in historical_totals['vote_types'] if r['canonical_category']=='early_in_person')
    early_issued = next(r['count'] for r in historical_totals['operational_observations']
                        if r['measure']=='prepoll_votes_cast_cumulative' and not r['seat_name'])
    resident_early = np.array([audit[n]['historical_early'] for n in names])*growth
    early = resident_early/resident_early.sum()*settings['operational_counts']['early']*early_formal/early_issued
    fraction = np.array([audit[n]['own_early_fraction'] for n in names])
    # Expanded provisional eligibility has no comparable WA 2021 denominator.
    # Use a comparable election's formal-vote fraction, retaining much wider
    # split uncertainty than an observed historical category would warrant.
    prov_rate = (sum(r['formal_votes'] for r in reference_provisionals['vote_types']
                     if r['canonical_category']=='enrolment_or_provisional') /
                 sum(r['formal_votes'] for r in reference_provisionals['seat_totals']))
    provisional = totals*prov_rate
    mobile = np.maximum(.5, raw[:, 4])*growth
    # Undo only the synthetic early transfer to recover the old day-absent amount.
    historical_absent = np.array([max(0., raw[i, 2] - audit[n]['historical_early']*(1-fraction[i]))
                                  for i, n in enumerate(names)])
    day_absent = (historical_absent+.5)*growth
    day = totals-amount-early-day_absent-mobile-provisional
    if (day <= 0).any(): raise ValueError('WA starting categories exceed expected formal votes')
    day_early = day*np.array([audit[n]['day_at_early_fraction'] for n in names])
    counts = np.column_stack((day-day_early, early*fraction+day_early,
        early*(1-fraction)+day_absent, amount, mobile, provisional))
    free = [0, 1, 2, 4, 5]
    weights = counts[:, free]/counts[:, free].sum(axis=1, keepdims=True)
    inputs['remainder'] = dict(name='remainder', indices=free, weights=weights.tolist())
    units = []
    for index, name in enumerate(names):
        district = baseline['districts'][name]
        by_number = {b['number']: b for b in district['booths'] if b['number'] is not None}
        ordinary_sizes = [sum(b['fp'].values()) for b in district['booths'] if b['kind']=='ordinary' and sum(b['fp'].values())]
        fallback = median(ordinary_sizes) if ordinary_sizes else 800
        local = []
        for booth in current[name].findall('w:OrdinaryPollingPlace', NS):
            row = by_number.get(booth.get('Number'))
            early_place = 'Early Polling Place' in booth.get('Name')
            size = sum(row['fp'].values()) if row else fallback
            local.append(dict(seat=index, name=booth.get('Name'), category='early' if early_place else 'ordinary',
                group=1 if early_place else 0, kind='ppvc' if early_place else 'ordinary',
                weight=max(.5, size), counted=0, matched=bool(row and not row['synthetic']),
                incremental=early_place, eav=False, closed=False, excluded=False, closure_reason='', exclusion_reason=''))
        for declaration, category in DECLARATIONS.items():
            local.append(dict(seat=index, name=declaration, category=declaration,
                group=CATEGORIES.index(category), kind='declaration', weight=1., counted=0,
                matched=declaration in ('Postal', 'SIR'), eav=False, closed=False,
                excluded=False, closure_reason='', exclusion_reason='',
                transfer_exclusion_reason='Expanded eligibility has no comparable historical size.' if declaration=='Provisional' else ''))
        for group in range(len(CATEGORIES)):
            members = [u for u in local if u['group']==group]
            if not members:
                if counts[index, group] != 0: raise ValueError('Positive WA category has no service')
                continue
            size = sum(u['weight'] for u in members)
            for unit in members: unit['weight'] /= size
        units.extend(local)
    config = dict(schema_version=1, **settings, inputs=inputs, units=units)
    turnout_prepare_live.validate_configuration(config, parameters)
    return config, dict(central_category_totals=dict(zip(CATEGORIES, counts.sum(axis=0).tolist())),
        early_formal_conversion=early_formal/early_issued, provisional_reference='2023nsw',
        provisional_formal_fraction=prov_rate, early_is_starting_allocation=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__); parser.parse_args(argv)
    folder = ROOT/'forecasts/2025wa'; output = folder/'live-inputs'
    paths = dict(settings=folder/'live-turnout-settings.json', baseline=output/'historical-baseline.json',
        audit=output/'baseline-audit.json', current=ROOT/'downloads/2025wa_booths_current.xml',
        historical=ROOT/'analysis/Data/Turnout/2021wa.json', provisional=ROOT/'analysis/Data/Turnout/2023nsw.json')
    data = {k: json.loads(p.read_text(encoding='utf8')) for k,p in paths.items() if k!='current'}
    parameters = maintained_parameters.prior_parameters(data['settings']['parameter_preset'])
    config, audit = configuration(data['settings'], data['baseline'], data['audit'], xml_districts(paths['current']),
        data['historical'], data['provisional'], parameters)
    config['sources'] = {k: dict(name=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for k,p in paths.items()}
    target = output/'turnout-config.json'; target.write_text(json.dumps(config, separators=(',', ':'))+'\n')
    (output/'turnout-input-audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    return turnout_prepare_live.main([str(target)])


if __name__ == '__main__':
    raise SystemExit(main())
