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


def _validate_district_configuration(config):
    """Check the active district identities and one starting rate/roll per district."""
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
    return n, g


def _validate_contest_availability(config):
    """Keep postponed contests outside the ordinary active counting population.

    Most districts participate normally. Any exception needs a distinct identity
    and a recorded reason; it cannot silently discard an active district's votes.
    """
    inputs = config['inputs']
    inactive_names = set()
    for contest in config.get('inactive_contests', []):
        name = contest['seat']
        if (not name or name in inactive_names or name in inputs['seat_names']
                or contest['status'] != 'postponed' or not contest['reason']):
            raise ValueError('Inactive contests need a unique name, postponed status and reason, outside the count population.')
        inactive_names.add(name)


def _validate_category_settings(config, parameters, n, g):
    """Require the supported category groups to cover the formal vote total once.

    District weights sum to one within their group. Maintained uncertainty
    matrices must describe exactly those categories, in the same order.
    """
    inputs = config['inputs']
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


def _validate_service_settings(config, n, g):
    """Check service identities, starting weights and explicit exception reasons.

    Normal services contain no current-election counted votes in the prior.
    Closures or calibration exclusions are facts, not inferred behavioural zeros.
    """
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


def _validate_sampling_settings(config):
    """Check sample sizes and the clock needed for optional PPVC delay treatment."""
    if not 2 <= config['preparation_samples'] or not 1 <= config['count_draws'] <= 64:
        raise ValueError('Use at least two prior outcomes and between one and 64 count draws.')
    schedule = config['schedule']
    if schedule['ppvc_reporting_decay'] and not schedule['poll_close']:
        raise ValueError('Unreported PPVC decay requires a polling close time.')


def validate_configuration(config, parameters):
    """Check the election's mapping before generating immutable outcomes.

    Category availability and service closures are explicit input decisions.
    A missing count is not converted to zero here; this command consumes already
    normalized prior inputs, including their treatment of possible zero counts.
    """
    n, g = _validate_district_configuration(config)
    _validate_contest_availability(config)
    _validate_category_settings(config, parameters, n, g)
    _validate_service_settings(config, n, g)
    _validate_sampling_settings(config)


def _format_prior_artifact(config, draws, responses, provenance):
    """Encode the fixed count outcomes and separate sensitivities for the C++ reader.

    This operational file contains no archive history. Current counts are stored
    independently as received observations after the live forecast has started.
    """
    inputs = config['inputs']
    # Use the common portable schema without the retrospective exporter's
    # archived observations or its election-specific timetable lookup.
    return dict(schema_version=cpp_contract.SCHEMA_VERSION, election=config['election'],
        prior_model=prior.MODEL_VERSION, count_model=live.MODEL_VERSION,
        progress_model=late_counts.MODEL_VERSION, allocation_model=allocation.MODEL_VERSION,
        prior=dict(seats=inputs['seat_names'], groups=inputs['categories'],
            subdivisions=[s or '' for s in inputs['subdivisions']], enrolment=inputs['enrolment'],
            totals=draws['totals'].tolist(), counts=draws['counts'].reshape(config['preparation_samples'], -1).tolist()),
        units=config['units'], provenance=provenance, sensitivities=responses,
        inactive_contests=config.get('inactive_contests', []),
        options=dict(count_draws=config['count_draws'], seed=config['seed'],
            declaration_schedule_model=late_schedule.MODEL_VERSION, **config['schedule']))


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
    responses = prior.prepare_responses(inputs, parameters)
    return _format_prior_artifact(config, draws, responses, provenance)


def _preparation_arguments(argv):
    """Read the configuration/output paths supplied by the operator."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('configuration', type=Path, help='Private normalized pre-election configuration JSON')
    parser.add_argument('--output', type=Path, help='Defaults to forecasts/<election>/live-inputs/turnout-prior.json')
    return parser.parse_args(argv)


def _read_preparation_configuration(args):
    """Read normalized inputs and fingerprint their sources and maintained model.

    Provenance describes the exact configuration used for this prior. It does
    not claim that private feed-backed calibration can be reproduced by cloning.
    """
    source = args.configuration.read_bytes()
    config = json.loads(source)
    provenance = dict(configuration_sha256=hashlib.sha256(source).hexdigest(),
        maintained_parameters_sha256=hashlib.sha256(maintained_parameters.PARAMETER_PATH.read_bytes()).hexdigest(),
        preparation_code_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        sampler_code_sha256=hashlib.sha256(Path(prior.__file__).read_bytes()).hexdigest(),
        parameter_preset=config['parameter_preset'], sources=config.get('sources', {}))
    return config, provenance


def _write_prepared_prior(path, result):
    """Replace the prepared prior only after its complete JSON has been written.

    The usual run replaces one election's input file. A temporary file protects
    against an interrupted write leaving a partial prior for the next live run.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(',', ':')) + '\n', encoding='utf-8')
    temporary.replace(path)


def _report_prepared_prior(config, result):
    """Summarise the operational account written by this command."""
    print(f"Prepared {config['election']}: {len(result['prior']['seats'])} districts, "
          f"{len(result['units'])} services, {len(result['prior']['totals'])} prior outcomes.")


def main(argv=None):
    """Read explicit settings, prepare the fixed prior and save its operational file."""
    args = _preparation_arguments(argv)
    config, provenance = _read_preparation_configuration(args)
    result = prepare(config, provenance=provenance)
    path = args.output or REPOSITORY_DIRECTORY/'forecasts'/config['election']/'live-inputs/turnout-prior.json'
    _write_prepared_prior(path, result)
    _report_prepared_prior(config, result)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
