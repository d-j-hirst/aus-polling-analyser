"""Prepare the immutable vote-count input used by the live C++ forecast.

The input configuration supplies enrolment, previous rates, converted early
and postal counts, category definitions, service identities and the counting
timetable. Maintained parameters supply uncertainty; no fitting or archived
live result snapshots are needed. Later measured counts belong to the C++
received-feed history, never to this pre-election input.
"""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

from lib.paths import REPOSITORY_DIRECTORY
from lib.turnout import allocation, cpp_contract, late_counts, late_schedule, live
from lib.turnout import maintained_parameters, prior


def validate_configuration(config, parameters):
    """Check the election's mapping before generating immutable outcomes.

    Category availability and service closures are explicit input decisions.
    A missing count is not converted to zero here; this command consumes already
    normalized prior inputs, including their treatment of possible zero counts.
    """
    if config['schema_version'] != 1:
        raise ValueError('Unsupported live input configuration version.')
    code, inputs = config['election'], config['inputs']
    if not code or any(c not in '0123456789abcdefghijklmnopqrstuvwxyz' for c in code):
        raise ValueError('Election must be a repository election code.')
    n, g = len(inputs['seat_names']), len(inputs['categories'])
    if not n or not g or len(set(inputs['seat_names'])) != n or len(set(inputs['categories'])) != g:
        raise ValueError('District and category identities must be unique and nonempty.')
    for key in ('enrolment', 'previous_turnout_pct', 'previous_formality_pct', 'local_scale', 'subdivisions'):
        if len(inputs[key]) != n:
            raise ValueError(f'{key} must contain one value per district.')
    if inputs['election_code'] != code:
        raise ValueError('Prior input election differs from the configuration.')
    groups = inputs['controls'] + [inputs['remainder']]
    indices = [i for group in groups for i in group['indices']]
    if sorted(indices) != list(range(g)):
        raise ValueError('Controlled and remaining categories must partition the final total.')
    for group in groups:
        weights = np.asarray(group['weights'], dtype=float)
        if weights.shape != (n, len(group['indices'])) or not np.isfinite(weights).all() or (weights < 0).any() or not np.allclose(weights.sum(axis=1), 1):
            raise ValueError('Category weights must be nonnegative and sum to one per district.')
        for scope in ('common', 'local'):
            matrix = parameters['compositions'][group['name']][scope + '_covariance']
            if np.shape(matrix) != (len(group['indices']), len(group['indices'])):
                raise ValueError('Maintained parameter categories do not match this input mapping.')
    seen = set()
    for unit in config['units']:
        identity = (unit['seat'], unit['name'])
        if identity in seen or not 0 <= unit['seat'] < n or not 0 <= unit['group'] < g:
            raise ValueError('Duplicate or invalid turnout service identity.')
        seen.add(identity)
        if unit['kind'] not in ('ordinary', 'ppvc', 'declaration') or unit.get('counted', 0) != 0:
            raise ValueError('Prior services must have a valid role and no current results.')
        if not np.isfinite(unit['weight']) or not 0 <= unit['weight'] <= 1:
            raise ValueError('Service weights must be finite fractions between zero and one.')
        for flag, reason in (('closed', 'closure_reason'), ('excluded', 'exclusion_reason')):
            if bool(unit[flag]) != bool(unit[reason]):
                raise ValueError('A service exception must record its reason.')
    if not 2 <= config['preparation_samples'] or not 1 <= config['count_draws'] <= 64:
        raise ValueError('Use at least two prior outcomes and between one and 64 count draws.')
    schedule = config['schedule']
    if schedule['ppvc_reporting_decay'] and not schedule['poll_close']:
        raise ValueError('Unreported PPVC decay requires a polling close time.')


def prepare(config, *, provenance):
    """Draw the prior once using installed coefficients and explicit settings.

    Joint outcomes preserve relationships between districts and uncertainty
    sources. C++ shares this account between simulation copies; it does not
    regenerate it or fit coefficients while processing a live feed.
    """
    parameters = maintained_parameters.prior_parameters(config['parameter_preset'])
    validate_configuration(config, parameters)
    inputs = config['inputs']
    draws = prior.draw(inputs, parameters, config['preparation_samples'], config['seed'])
    # Use the common portable schema without the retrospective exporter's
    # archived observations or its election-specific timetable lookup.
    return dict(schema_version=cpp_contract.SCHEMA_VERSION, election=config['election'],
        prior_model=prior.MODEL_VERSION, count_model=live.MODEL_VERSION,
        progress_model=late_counts.MODEL_VERSION, allocation_model=allocation.MODEL_VERSION,
        prior=dict(seats=inputs['seat_names'], groups=inputs['categories'],
            subdivisions=[s or '' for s in inputs['subdivisions']], enrolment=inputs['enrolment'],
            totals=draws['totals'].tolist(), counts=draws['counts'].reshape(config['preparation_samples'], -1).tolist()),
        units=config['units'], provenance=provenance, sensitivities=prior.prepare_responses(inputs, parameters),
        options=dict(count_draws=config['count_draws'], seed=config['seed'],
            declaration_schedule_model=late_schedule.MODEL_VERSION, **config['schedule']))


def main(argv=None):
    """Write the required private prior at the standard election-relative path."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', type=Path, help='Private normalized pre-election configuration JSON')
    parser.add_argument('--output', type=Path, help='Defaults to forecasts/<election>/live-inputs/turnout-prior.json')
    args = parser.parse_args(argv)
    source = args.configuration.read_bytes()
    config = json.loads(source)
    provenance = dict(configuration_sha256=hashlib.sha256(source).hexdigest(),
        maintained_parameters_sha256=hashlib.sha256(maintained_parameters.PARAMETER_PATH.read_bytes()).hexdigest(),
        preparation_code_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        sampler_code_sha256=hashlib.sha256(Path(prior.__file__).read_bytes()).hexdigest(),
        parameter_preset=config['parameter_preset'], sources=config.get('sources', {}))
    result = prepare(config, provenance=provenance)
    path = args.output or REPOSITORY_DIRECTORY/'forecasts'/config['election']/'live-inputs/turnout-prior.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(',', ':')) + '\n', encoding='utf-8')
    temporary.replace(path)
    print(f"Prepared {config['election']}: {len(result['prior']['seats'])} districts, "
          f"{len(result['units'])} services, {len(result['prior']['totals'])} prior outcomes.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
