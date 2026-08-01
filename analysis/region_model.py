"""Generate election-specific regional swing adjustments.

This entry point selects elections, coordinates provenance, and dispatches the
explicit Stan-data mappings. Shared contracts and data preparation live in
region_model_common.py; sampling/output helpers live in region_model_stan.py;
and election-specific Stan interfaces live in region_model_mappings.py.
"""

import secrets
import sys
from pathlib import Path

from region_model_common import (
  BASELINE_WEIGHT_TOLERANCE,
  DAYS_PER_MODEL_STEP,
  MISSING_OBSERVATION,
  Config,
  ConfigError,
  ElectionData,
  add_transformed_swing_deviations,
  fed_regions,
  model_contract,
  nsw_regions,
  prepare_poll_timing,
  qld_regions,
  qld_regions_2024,
  validate_election_baseline,
  validate_regional_input,
  vic_regions,
)
from region_model_mappings import (
  run_model_fed2025,
  run_model_nsw2027,
  run_model_qld2024,
  run_model_qld2028,
  run_model_vic2026,
  runner_for,
)
from region_model_stan import (
  latest_parameter_means,
  sample_region_model,
  write_latest_parameter_means,
)
import region_model_provenance


def run_models():
  """Generate each requested regional model that has actual poll data."""

  config = Config()
  party = region_model_provenance.canonical_party(
    config.party_instructions
  )
  work_items = []
  for desired_election in config.elections:
    election = desired_election.short()
    input_path = region_model_provenance.input_path(election, party)
    if input_path is None:
      print(
        'No regional polling file for {} and {}, skipping.'.format(
          election, party
        )
      )
      continue
    if not region_model_provenance.has_actual_poll_data(input_path):
      print(
        '{} contains no actual regional polls, skipping.'.format(input_path)
      )
      continue
    work_items.append((desired_election, input_path))

  if not work_items:
    print('No regional model work was required.')
    return 0

  base_seed = (
    config.seed
    if config.seed is not None
    else secrets.randbelow(2 ** 31 - 1) + 1
  )
  print('Base random seed: {}'.format(base_seed))
  recorded_command = [sys.executable] + sys.argv
  if config.seed is None:
    # Preserve the generated base seed in the manifest's replay command.
    recorded_command.extend(['--seed', str(base_seed)])
  recorder = region_model_provenance.RegionalModelRecorder(recorded_command)

  for desired_election, input_path in work_items:
    election = desired_election.short()
    random_seed = region_model_provenance.derive_stan_seed(
      base_seed, election, party
    )
    output_path = region_model_provenance.output_path(election, party)
    contract = model_contract(desired_election)
    e_data = ElectionData(input_path=input_path, contract=contract)
    runner_for(desired_election)(
      e_data=e_data,
      random_seed=random_seed,
      output_path=output_path,
    )
    recorder.record(
      election=election,
      party=party,
      output=output_path,
      random_seed=random_seed,
    )
  return 0


def main():
  try:
    return run_models()
  except (
    ConfigError,
    region_model_provenance.RegionalProvenanceError,
  ) as error:
    print(
      'Could not generate regional model: {}'.format(error),
      file=sys.stderr,
    )
    return 2


if __name__ == '__main__':
  sys.exit(main())
