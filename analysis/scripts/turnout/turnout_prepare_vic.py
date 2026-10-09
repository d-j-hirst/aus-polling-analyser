"""Prepare Victorian live turnout inputs from pre-election evidence.

Current candidate and booth preloads supply the roll and reporting identities.
Previous public final results supply rates and relative booth/category sizes;
only pre-election operational observations are read from the target election's
turnout dataset. The generated configuration and its booth audit are private.
This command neither fits coefficients nor reads live result snapshots.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
from statistics import median
from types import SimpleNamespace
from xml.etree import ElementTree as ET

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import category_policy, maintained_parameters
from scripts.turnout import turnout_prepare_live


CATEGORIES = ['absent', 'early', 'ordinary', 'other', 'postal']
SOURCE_GROUPS = dict(absent='absent', early_combined='early',
    election_day_ordinary='ordinary', provisional='other',
    marked_as_voted='other', postal='postal')
DECLARATIONS = [('Early', 'early'), ('Postal', 'postal'), ('Absent', 'absent'),
    ('Provisional', 'other'), ('Marked as voted', 'other')]


def district_name(name):
    return name.removesuffix(' District')


def seat_metadata(path):
    """Read predecessor names and existing forecast regions, ignoring FP proxies.

    A share-estimation proxy is not evidence that its turnout population is a
    predecessor. Only the explicit previous-name field can supply that link.
    """
    seats = {}
    current = None
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if line.startswith('#'):
            current = seats.setdefault(line[1:].strip(), {})
        elif current is not None and '=' in line:
            key, value = line.split('=', 1)
            current[key] = value
    return seats


def read_preloads(candidate_path, booth_path):
    """Retain the pre-election roll and all ordinary reporting services.

    Combined Early votes are declaration-style records in this feed format;
    the polling-place preload provides the ordinary booths only.
    """
    districts = {}
    for contest in ET.parse(candidate_path).findall('.//{*}Contest'):
        full_name = contest.findtext('./{*}ContestIdentifier/{*}ContestName')
        if not full_name.endswith(' District'):
            continue  # Upper-house regions are outside this turnout account.
        name = district_name(full_name)
        districts[name] = dict(enrolment=int(contest.findtext('./{*}Enrolment')), booths=[])
    for district in ET.parse(booth_path).findall('.//{*}PollingDistrict'):
        name = district_name(district.findtext('./{*}PollingDistrictIdentifier/{*}Name'))
        for booth in district.findall('./{*}PollingPlaces/{*}PollingPlace/{*}PollingPlaceIdentifier'):
            districts[name]['booths'].append(dict(id=int(booth.attrib['Id']), name=booth.attrib['Name']))
    for name, row in districts.items():
        if row['enrolment'] <= 0 or len({b['name'] for b in row['booths']}) != len(row['booths']):
            raise ValueError('Invalid roll or duplicate within-district booth name: ' + name)
    return districts


def historical_baselines(dataset):
    """Use complete historical partitions; a missing split gets a pooled baseline.

    The district total remains useful when a category split was not published.
    Pooled category proportions include only complete, reconciled partitions,
    rather than treating missing components as measured zeros.
    """
    totals = {district_name(r['seat_name']): r for r in dataset['seat_totals']}
    rows = defaultdict(dict)
    for row in dataset['vote_types']:
        rows[district_name(row['seat_name'])][row['canonical_category']] = row
    partitions = {}
    for name, categories in rows.items():
        if (set(categories) == set(SOURCE_GROUPS)
                and all(r['coverage'] == 'complete' and r['formal_votes'] is not None for r in categories.values())
                and sum(r['formal_votes'] for r in categories.values()) == totals[name]['formal_votes']):
            partitions[name] = {key: r['formal_votes'] for key, r in categories.items()}
    if not partitions:
        raise ValueError('No complete historical Victorian category partitions.')
    pooled = {key: sum(r[key] for r in partitions.values()) for key in SOURCE_GROUPS}
    enrolment = sum(r['enrolment'] for r in totals.values())
    ballots = sum(r['total_ballots'] for r in totals.values())
    formal = sum(r['formal_votes'] for r in totals.values())
    rates = dict(turnout=100 * ballots / enrolment, formality=100 * formal / ballots)
    return totals, partitions, pooled, rates


def match_booths(districts, previous_booths):
    """Mirror the loader's name/district matching without guessing ambiguous links.

    A globally unique current name can match a historical booth in another
    district. Repeated names need the same district name. If several historical
    records reach one current booth, none supplies a reliable size estimate.
    """
    frequency = Counter(b['name'] for r in districts.values() for b in r['booths'])
    unique = {b['name']: b['id'] for r in districts.values() for b in r['booths'] if frequency[b['name']] == 1}
    contextual = {(seat, b['name']): b['id'] for seat, r in districts.items() for b in r['booths']}
    candidates = defaultdict(list)
    for seat, record in previous_booths.items():
        for name, booth in record['booths'].items():
            if 'Votes' in name:
                continue
            target = unique.get(name) if frequency[name] == 1 else contextual.get((seat, name))
            if target is not None:
                candidates[target].append(dict(seat=seat, name=name, count=sum(booth['fp'].values())))
    return {key: rows[0] for key, rows in candidates.items() if len(rows) == 1}, {
        key for key, rows in candidates.items() if len(rows) > 1}


def operational_controls(observations, names, poll_close):
    """Select the latest usable district counts known before polling closes.

    These are vote/application counts, including the accepted one-decimal roll
    percentages already converted by the evidence adapter. Later revisions and
    target-election final result totals cannot enter a starting expectation.
    """
    measures = dict(early='prepoll_votes_cast_cumulative', postal='postal_applications_cumulative')
    result = {}
    for category, measure in measures.items():
        values = []
        for name in names:
            rows = [r for r in observations if district_name(r['seat_name']) == name
                and r['geography_basis'] == 'elector_division' and r['measure'] == measure
                and r['observation_status'] == 'contemporaneous' and r['observed_at'] < poll_close
                and r['count'] is not None
                and r.get('count_relation', 'equal') == 'equal'
                and r.get('count_basis', 'reported') == 'reported'
                and (r.get('count_precision', 'exact') == 'exact'
                    or (r.get('count_precision') == 'approximate'
                        and r.get('derivation') == 'rounded_rate_times_enrolment'))]
            if not rows:
                raise ValueError(f'Missing pre-election {category} count: {name}')
            row = max(rows, key=lambda r: r['observed_at'])
            if row['count'] <= 0:
                raise ValueError(f'Pre-election {category} count needs review: {name}')
            values.append(row)
        result[category] = values
    return result


def _configuration_evidence(settings, districts, metadata, historical, previous_booths, observations):
    """Resolve the district population and its historical/count evidence.

    Active districts normally all participate. A configured postponed contest
    stays in the forecast while being excluded from this period's count account.
    """
    inactive = {r['seat'] for r in settings.get('inactive_contests', [])}
    if set(districts) != set(metadata) or not inactive <= set(districts):
        raise ValueError('Preload districts, forecast seats and inactive contests do not agree.')
    names = sorted(set(districts) - inactive)
    totals, partitions, pooled, rates = historical_baselines(historical)
    matches, ambiguous = match_booths(districts, previous_booths)
    shared_size = median(r['count'] for r in matches.values() if r['count'] > 0)
    parameters = maintained_parameters.prior_parameters(settings['parameter_preset'])
    controls = operational_controls(observations, names, settings['schedule']['poll_close'])
    inputs = dict(identity=settings['election'] + '/operational', election_code=settings['election'],
        seat_names=names, categories=CATEGORIES, subdivisions=[], enrolment=[],
        previous_turnout_pct=[], previous_formality_pct=[], local_scale=[], controls=[],
        remainder=dict(name='remainder', indices=[0, 2, 3], weights=[]))
    units, audit = [], []
    return SimpleNamespace(names=names, totals=totals, partitions=partitions, pooled=pooled, rates=rates, matches=matches, ambiguous=ambiguous, shared_size=shared_size, parameters=parameters, controls=controls, inputs=inputs, units=units, audit=audit)


def _append_district_configuration(work, index, name, districts, metadata):
    """Build one district's prior rates, category proportions and booth sizes.

    Matched booths normally use previous final counts as relative size weights.
    New or ambiguous booths use a typical local size. We retain transferred
    matches as size information without treating them as live turnout drift.
    """
    totals = work.totals
    partitions = work.partitions
    pooled = work.pooled
    rates = work.rates
    matches = work.matches
    ambiguous = work.ambiguous
    shared_size = work.shared_size
    inputs = work.inputs
    units = work.units
    audit = work.audit
    predecessor = name if name in totals else metadata[name].get('sPreviousName')
    baseline = totals.get(predecessor)
    partition = partitions.get(predecessor, pooled)
    inputs['enrolment'].append(districts[name]['enrolment'])
    inputs['subdivisions'].append(metadata[name]['sRegion'])
    inputs['previous_turnout_pct'].append(100 * baseline['total_ballots'] / baseline['enrolment'] if baseline else rates['turnout'])
    inputs['previous_formality_pct'].append(100 * baseline['formal_votes'] / baseline['total_ballots'] if baseline else rates['formality'])
    inputs['local_scale'].append(1 if predecessor == name and name in partitions else 1.5)
    inputs['remainder']['weights'].append(category_policy.possible_proportions([
        partition['absent'], partition['election_day_ordinary'], partition['provisional'] + partition['marked_as_voted']]))
    ordinary = districts[name]['booths']
    if not ordinary:
        raise ValueError('Active district has no ordinary services: ' + name)
    sizes = [matches[b['id']]['count'] for b in ordinary if b['id'] in matches and matches[b['id']]['count'] > 0]
    fallback = median(sizes) if sizes else shared_size
    weights = [max(.5, matches[b['id']]['count']) if b['id'] in matches else fallback for b in ordinary]
    seat_audit = dict(seat=name, previous_seat=predecessor,
        rate_baseline='district' if baseline else 'statewide',
        category_baseline='district' if predecessor in partitions else 'complete-statewide-partitions',
        local_scale=inputs['local_scale'][-1], ordinary_booths=[])
    for booth, size in zip(ordinary, weights):
        match = matches.get(booth['id'])
        # Retain the same-predecessor relationship as match metadata. All
        # historical matches can inform starting size weights, including
        # transferred booths; ordinary matches do not train a live shared
        # size adjustment in the active C++ calculation.
        reliable = bool(match and match['seat'] == predecessor)
        units.append(unit(index, booth['name'], 'ordinary', 'ordinary', size / sum(weights), reliable))
        seat_audit['ordinary_booths'].append(dict(name=booth['name'], historical_match=match,
            same_predecessor_match=reliable, starting_size_weight=size,
            status='matched' if match else 'ambiguous' if booth['id'] in ambiguous else 'unmatched-local-median'))
    other = category_policy.possible_proportions([partition['provisional'], partition['marked_as_voted']])
    if other is None:
        other = category_policy.possible_proportions([pooled['provisional'], pooled['marked_as_voted']])
    for declaration, group in DECLARATIONS:
        weight = other[0 if declaration == 'Provisional' else 1] if group == 'other' else 1
        units.append(unit(index, declaration, group, 'declaration', weight, False))
    audit.append(seat_audit)


def _populate_district_accounts(work, districts, metadata):
    """Prepare the same count account for each active lower-house district."""
    for index, name in enumerate(work.names):
        _append_district_configuration(work, index, name, districts, metadata)


def _populate_early_postal_controls(work):
    """Convert known pre-election counts to expected formal early/postal votes.

    The shared multiplier comes from maintained calibration. Its common and
    district error scales remain separate; no target-election final count enters.
    """
    controls, parameters, inputs, names = work.controls, work.parameters, work.inputs, work.names
    for category, rows in controls.items():
        coefficients = parameters['controls'][category]
        inputs['controls'].append(dict(name=category, indices=[CATEGORIES.index(category)],
            weights=[[1] for _ in names], aggregate=False,
            amount=[r['count'] * coefficients['factor'] for r in rows],
            common_sd=[r['count'] * coefficients['common_sd'] for r in rows],
            local_sd=[r['count'] * coefficients['local_sd'] for r in rows]))


def _assemble_victorian_configuration(settings, work):
    """Return the operational configuration and the evidence audit separately.

    The audit explains matches and source choices. It does not supply additional
    prediction inputs or change the validated category accounting.
    """
    inputs, units, parameters, audit, controls = work.inputs, work.units, work.parameters, work.audit, work.controls
    config = dict(schema_version=1, **settings, inputs=inputs, units=units)
    turnout_prepare_live.validate_configuration(config, parameters)
    return config, dict(districts=audit, controls=controls, inactive_contests=settings.get('inactive_contests', []))


def configuration(settings, districts, metadata, historical, previous_booths, observations):
    """Combine the evidence into the existing operational sampler's input format.

    Booth weights divide each district's ordinary allowance; they do not create
    an extra vote pool. Unmatched services use a local median, and transferred
    matches retain starting size information. The active C++ model does not
    estimate a shared ordinary-booth size change from their current results.
    """
    work = _configuration_evidence(settings, districts, metadata, historical, previous_booths, observations)
    _populate_district_accounts(work, districts, metadata)
    _populate_early_postal_controls(work)
    return _assemble_victorian_configuration(settings, work)


def unit(seat, name, group, kind, weight, matched):
    return dict(seat=seat, name=name, category=name if kind == 'declaration' else group,
        group=CATEGORIES.index(group), kind=kind,
        weight=weight, counted=0, matched=matched, eav=False, closed=False, excluded=False,
        closure_reason='', exclusion_reason='')


def _adapter_arguments(argv):
    """Read the election and any operator-supplied preload paths."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('election')
    parser.add_argument('--candidates', type=Path)
    parser.add_argument('--booths', type=Path)
    return parser.parse_args(argv)


def _load_victorian_sources(args):
    """Read settings and locate the pre-election/historical source material.

    The target-election turnout file supplies only observations known before
    polling closes. Its later final results are not used to prepare this prior.
    """
    root = REPOSITORY_DIRECTORY
    settings_path = root / 'forecasts' / args.election / 'live-turnout-settings.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8'))
    if settings['election'] != args.election:
        raise ValueError('Settings election differs from the requested election.')
    previous = settings['previous_election']
    spec = json.loads((settings_path.parent / 'forecast.json').read_text(encoding='utf-8'))
    paths = dict(settings=settings_path, seats=root / spec['data_sources']['seats'],
        candidates=args.candidates or root / 'downloads' / (args.election + '_candidates.xml'),
        booths=args.booths or root / 'downloads' / (args.election + '_booths.xml'),
        previous_totals=root / 'analysis/Data/Turnout' / (previous + '.json'),
        previous_booths=root / 'analysis/Booth Results' / (previous + '.json'))
    observations = json.loads((root / 'analysis/Data/Turnout' / (args.election + '.json')).read_text(encoding='utf-8'))['operational_observations']
    return SimpleNamespace(settings=settings, settings_path=settings_path, paths=paths,
        observations=observations, election=args.election)


def _build_adapter_configuration(context):
    """Interpret the authority's preloads and public historical results together."""
    settings, paths, observations = context.settings, context.paths, context.observations
    config, audit = configuration(settings, read_preloads(paths['candidates'], paths['booths']),
        seat_metadata(paths['seats']), json.loads(paths['previous_totals'].read_text(encoding='utf-8')),
        json.loads(paths['previous_booths'].read_text(encoding='utf-8')), observations)
    return config, audit


def _record_adapter_provenance(config, context):
    """Fingerprint evidence used for the configuration without recording private paths.

    Actual source choices are normally reproducible from supplied files. Hash
    only the selected operational observations from the target-election dataset,
    so later result updates do not pretend to have been known before polling.
    """
    paths, observations = context.paths, context.observations
    config['sources'] = {key: dict(name=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for key, path in paths.items()}
    config['sources']['operational_observations'] = dict(
        name=context.election + '.json: operational_observations only',
        sha256=hashlib.sha256(json.dumps(observations, sort_keys=True).encode()).hexdigest())
    config['sources']['adapter_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def _write_victorian_inputs(settings_path, config, audit):
    """Save the private configuration and explanatory audit beside the election.

    The fixed count prior is generated separately from this normalized input;
    neither file contains a progressive live-result history.
    """
    output = settings_path.parent / 'live-inputs'
    output.mkdir(parents=True, exist_ok=True)
    for filename, value in [('turnout-config.json', config), ('turnout-input-audit.json', audit)]:
        (output / filename).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return output / 'turnout-config.json'


def main(argv=None):
    """Load evidence, configure district accounts and prepare the fixed live prior."""
    args = _adapter_arguments(argv)
    context = _load_victorian_sources(args)
    config, audit = _build_adapter_configuration(context)
    _record_adapter_provenance(config, context)
    configuration_path = _write_victorian_inputs(context.settings_path, config, audit)
    return turnout_prepare_live.main([str(configuration_path)])


if __name__ == '__main__':
    raise SystemExit(main())
