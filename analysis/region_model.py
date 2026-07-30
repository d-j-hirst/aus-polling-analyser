"""Generate election-specific regional swing adjustments.

Each input combines an overall poll estimate with one or more regional
breakdowns. The model expresses each region as a deviation from the overall
swing, smooths those deviations through time in Stan, and exports the latest
posterior mean for the C++ seat simulation.

Regional models deliberately remain election-specific because available
breakdowns and region definitions vary substantially between jurisdictions.
The shared preparation and validation below keeps their common assumptions
consistent while the individual Stan-data mappings remain explicit.
"""

import argparse
import math
import secrets
import sys
from time import perf_counter

import pandas as pd

from election_code import ElectionCode
from poll_transform import transform_vote_share
import region_model_provenance


fed_regions = ['NSW', 'VIC', 'QLD', 'WA', 'SA', 'WSTAN']
qld_regions_2024 = [
  'Inner Suburbs',
  'Outer Suburbs',
  'Coasts',
  'Regional',
  'C+R',
  'SE',
  'Central',
  'Far North',
  'Regional ex-rural',
  'Rural',
  'Pure Regional',
]
vic_regions = [
  'InnerMetro',
  'OuterMetro',
  'Regional',
  'Metro',
  'Provincial',
  'Rural',
]
nsw_regions = ['Metro', 'Regional']
qld_regions = ['Metro', 'SEQ', 'Regional']

MISSING_OBSERVATION = -10000
DAYS_PER_MODEL_STEP = 5
BASELINE_WEIGHT_TOLERANCE = 2.0


class ConfigError(ValueError):
  pass


def model_contract(election):
  """Describe the columns and baseline assumptions for one regional model."""

  year = election.year()
  region = election.region()
  if region == 'fed' and year >= 2025:
    return {
      'overall': 'National',
      'regions': fed_regions,
      'required_poll_regions': ['NSW', 'VIC', 'QLD'],
      'requires_baseline': True,
      'baseline_weights': None,
    }
  if region == 'qld' and year == 2024:
    return {
      'overall': 'State',
      'regions': qld_regions_2024,
      'required_poll_regions': [],
      'requires_baseline': True,
      'baseline_weights': None,
    }
  if region == 'vic' and year == 2026:
    return {
      'overall': 'State',
      'regions': vic_regions,
      'required_poll_regions': [],
      'requires_baseline': True,
      'baseline_weights': {
        'InnerMetro': 0.2847,
        'OuterMetro': 0.3896,
        'Provincial': 0.1269,
        'Rural': 0.1988,
      },
    }
  if region == 'nsw' and year == 2027:
    return {
      'overall': 'State',
      'regions': nsw_regions,
      'required_poll_regions': nsw_regions,
      'requires_baseline': True,
      'baseline_weights': {
        'Metro': 0.5796,
        'Regional': 0.4204,
      },
    }
  if region == 'qld' and year >= 2028:
    return {
      'overall': 'State',
      'regions': qld_regions,
      'required_poll_regions': qld_regions,
      'requires_baseline': True,
      'baseline_weights': {
        'Metro': 0.4534,
        'SEQ': 0.2841,
        'Regional': 0.2625,
      },
    }
  raise ConfigError(
    'No regional model implementation exists for {}.'.format(
      election.short()
    )
  )


def _validated_vote_share(value, label, required=True):
  if pd.isna(value):
    if required:
      raise ConfigError('{} is missing.'.format(label))
    return None
  try:
    numeric_value = float(value)
  except (TypeError, ValueError) as error:
    raise ConfigError('{} is not numeric.'.format(label)) from error
  if not math.isfinite(numeric_value) or not 0 < numeric_value < 100:
    raise ConfigError(
      '{} must be finite and strictly between 0 and 100.'.format(label)
    )
  return numeric_value


def validate_election_baseline(previous_results, contract, input_path):
  """Validate baseline shares and their population-weighted aggregate."""

  if previous_results is None:
    if contract['requires_baseline']:
      raise ConfigError(
        '{} requires an Election baseline row.'.format(input_path)
      )
    return

  baseline_values = {
    field: _validated_vote_share(
      previous_results[field],
      '{} Election baseline {}'.format(input_path, field),
    )
    for field in [contract['overall']] + contract['regions']
  }
  weights = contract['baseline_weights']
  if not weights:
    return

  weighted_baseline = sum(
    baseline_values[field] * weight
    for field, weight in weights.items()
  )
  overall_baseline = baseline_values[contract['overall']]
  if abs(weighted_baseline - overall_baseline) > (
    BASELINE_WEIGHT_TOLERANCE
  ):
    raise ConfigError(
      '{} Election regional baselines imply {:.4f}, but {} is '
      '{:.4f}; check that all vote shares use the 0-100 scale.'.format(
        input_path,
        weighted_baseline,
        contract['overall'],
        overall_baseline,
      )
    )


def validate_regional_input(
    poll_data, previous_results, contract, input_path
):
  """Reject malformed data before compiling or sampling a Stan model."""

  required_columns = {
    'StartDate',
    'EndDate',
    'Firm',
    'Size',
    contract['overall'],
    *contract['regions'],
  }
  missing_columns = sorted(required_columns - set(poll_data.columns))
  if missing_columns:
    raise ConfigError(
      '{} lacks required column(s): {}'.format(
        input_path, ', '.join(missing_columns)
      )
    )
  if poll_data.empty:
    raise ConfigError('{} contains no regional polls.'.format(input_path))

  validate_election_baseline(
    previous_results, contract, input_path
  )

  required_poll_regions = set(contract['required_poll_regions'])
  for row_index, row in poll_data.iterrows():
    row_number = int(row_index) + 2
    if pd.isna(row['Firm']) or not str(row['Firm']).strip():
      raise ConfigError(
        '{} row {} Firm is missing.'.format(input_path, row_number)
      )
    try:
      poll_size = float(row['Size'])
    except (TypeError, ValueError) as error:
      raise ConfigError(
        '{} row {} Size is not numeric.'.format(input_path, row_number)
      ) from error
    if not math.isfinite(poll_size) or poll_size <= 0:
      raise ConfigError(
        '{} row {} Size must be finite and positive.'.format(
          input_path, row_number
        )
      )
    _validated_vote_share(
      row[contract['overall']],
      '{} row {} {}'.format(
        input_path, row_number, contract['overall']
      ),
    )
    for region in contract['regions']:
      _validated_vote_share(
        row[region],
        '{} row {} {}'.format(input_path, row_number, region),
        required=region in required_poll_regions,
      )


class Config:
  def __init__(self):
    parser = argparse.ArgumentParser(
      description='Generate election-specific regional swing deviations.')
    parser.add_argument(
      '--election',
      action='store',
      type=str,
      help='Generate regional trends for this election.'
           'Enter as 1234-xxx format, e.g. 2013-fed. Write "all" '
           'to do it for all elections.')
    parser.add_argument(
      '--party',
      action='store',
      type=str,
      help='Party to generate regional trends for. Currently only supports ON. '
           'If not specified, will do 2PP.',
      default='')
    parser.add_argument(
      '--seed',
      action='store',
      type=int,
      help='Base random seed used to derive reproducible per-election seeds.')
    args = parser.parse_args()
    if args.election is None:
      raise ConfigError('The --election argument is required.')
    self.election_instructions = args.election.lower()
    self.party_instructions = args.party.lower()
    if self.party_instructions not in ('', 'on'):
      raise ConfigError('The --party argument currently supports only "ON".')
    if args.seed is not None and not 1 <= args.seed < 2 ** 31:
      raise ConfigError('The --seed value must be between 1 and 2^31-1.')
    self.seed = args.seed
    self.prepare_election_list()

  def prepare_election_list(self):
    with open('./Data/polled-elections.csv', 'r') as f:
      elections = ElectionCode.load_elections_from_file(f)
    with open('./Data/future-elections.csv', 'r') as f:
      elections += ElectionCode.load_elections_from_file(f)
    if self.election_instructions == 'all':
      self.elections = elections
    else:
      parts = self.election_instructions.split('-')
      if len(parts) < 2:
        raise ConfigError(
          'Error in "elections" argument: given value did not have two parts '
          'separated by a hyphen (e.g. 2013-fed)'
        )
      try:
        code = ElectionCode(parts[0], parts[1])
      except ValueError:
        raise ConfigError(
          'Error in "elections" argument: first part of election name could'
          ' not be converted into an integer'
        )
      if code not in elections:
        raise ConfigError(
          'Error in "elections" argument: value given did not match any '
          'configured polled or future election'
        )
      if len(parts) == 2:
        self.elections = [code]
      elif parts[2] == 'onwards':
        try:
          self.elections = (elections[elections.index(code):])
        except ValueError:
          raise ConfigError(
            'Error in "elections" argument: value given did not match any '
            'election given in Data/polled-elections.csv'
          )
      else:
        raise ConfigError('Invalid instruction in "elections" argument.')


class ElectionData:
  def __init__(self, input_path, contract):
    """Load and validate one election's baseline and regional polls."""

    self.base_df = pd.read_csv(input_path)
    if 'Firm' not in self.base_df.columns:
      raise ConfigError('{} lacks a Firm column.'.format(input_path))

    normalised_firms = self.base_df['Firm'].apply(
      lambda value: (
        '' if pd.isna(value) else str(value).strip().casefold()
      )
    )
    baseline_mask = normalised_firms == 'election'
    previous_results = self.base_df[baseline_mask].to_dict('records')
    if len(previous_results) > 1:
      raise ConfigError(
        '{} contains more than one Election baseline row.'.format(input_path)
      )
    self.previous_results = (
      previous_results[0] if previous_results else None
    )

    self.base_df = self.base_df[~baseline_mask]
    validate_regional_input(
      self.base_df, self.previous_results, contract, input_path
    )

    # convert dates to days from start
    try:
      self.base_df['StartDate'] = [
        pd.Timestamp(date) for date in self.base_df['StartDate']
      ]
      self.base_df['EndDate'] = [
        pd.Timestamp(date) for date in self.base_df['EndDate']
      ]
    except (TypeError, ValueError) as error:
      raise ConfigError(
        '{} contains an invalid poll date.'.format(input_path)
      ) from error
    invalid_periods = (
      self.base_df['EndDate'] < self.base_df['StartDate']
    )
    if invalid_periods.any():
      row_number = int(invalid_periods[invalid_periods].index[0]) + 2
      raise ConfigError(
        '{} row {} ends before it starts.'.format(
          input_path, row_number
        )
      )
    self.base_df['MidDate'] = (
      self.base_df['StartDate'] + (
        self.base_df['EndDate'] - self.base_df['StartDate']
      ) / 2
    )
    # day number for each poll period
    self.start = self.base_df['StartDate'].min()  # day zero
    self.base_df['StartDay'] = (self.base_df['StartDate'] - self.start).dt.days
    self.base_df['MidDay'] = (self.base_df['MidDate'] - self.start).dt.days
    self.base_df['EndDay'] = (self.base_df['EndDate'] - self.start).dt.days
    self.n_days = self.base_df['EndDay'].max() + 1
    self.end = self.base_df['EndDate'].max()

    self.create_day_series()

  def create_day_series(self):
    # Convert "days" objects into raw numerical data
    # that Stan can accept
    for i in self.base_df.index:
      self.base_df.loc[i, 'StartDayNum'] = int(self.base_df.loc[i, 'StartDay'] + 1)
      self.base_df.loc[i, 'MidDayNum'] = int(self.base_df.loc[i, 'MidDay'] + 1)
      self.base_df.loc[i, 'EndDayNum'] = int(self.base_df.loc[i, 'EndDay'] + 1)


def prepare_poll_timing(e_data, df):
  """Represent each poll across its fieldwork period on a five-day grid."""

  # The established model enters each poll at its start, midpoint and end.
  # This both spreads and triples its likelihood contribution; preserve that
  # behaviour unless the weighting method is reviewed explicitly.
  poll_days = (
    [int(value) for value in df['StartDayNum'].values]
    + [int(value) for value in df['MidDayNum'].values]
    + [int(value) for value in df['EndDayNum'].values]
  )
  modified_day_count = max(
    math.floor(e_data.n_days / DAYS_PER_MODEL_STEP), 1
  )
  modified_poll_days = [
    min(
      modified_day_count,
      max(1, math.floor(day / DAYS_PER_MODEL_STEP)),
    )
    for day in poll_days
  ]
  return modified_day_count, modified_poll_days


def add_transformed_swing_deviations(
    df, previous_results, overall_column, regions
):
  """Express regional polling as transformed swing minus overall swing."""

  previous_overall = previous_results[overall_column]
  df['OverallSwing'] = df[overall_column].apply(
    lambda value: (
      transform_vote_share(value)
      - transform_vote_share(previous_overall)
    )
  )
  for region in regions:
    previous_region = previous_results[region]

    def swing_deviation(row):
      if pd.isna(row[region]):
        return MISSING_OBSERVATION
      return (
        transform_vote_share(row[region])
        - transform_vote_share(previous_region)
        - row['OverallSwing']
      )

    df[f'{region}_SwingDev'] = df.apply(swing_deviation, axis=1)


def sample_region_model(
    e_data,
    stan_data,
    model_path,
    random_seed,
    chains,
    iterations,
):
  """Compile/cache and sample one regional Stan model."""

  # Defer importing PyStan through stan_cache until validated input actually
  # needs sampling. Configuration and data errors should not require a working
  # Stan toolchain merely to be reported.
  from stan_cache import stan_cache

  with open(model_path, 'r') as model_file:
    model = model_file.read()
  stan_model = stan_cache(model_code=model)

  print('Beginning sampling ...')
  print('Start date of model: {}'.format(
    e_data.start.strftime('%Y-%m-%d')
  ))
  print('End date of model: {}'.format(
    e_data.end.strftime('%Y-%m-%d')
  ))

  start_time = perf_counter()
  fit = stan_model.sampling(
    data=stan_data,
    iter=iterations,
    chains=chains,
    seed=random_seed,
    control={
      'max_treedepth': 18,
      'adapt_delta': 0.8,
    },
  )
  print(
    'Time elapsed: {:.2f} seconds'.format(
      perf_counter() - start_time
    )
  )
  print('Stan finished.')

  import pystan.diagnostics as stan_diagnostics
  diagnostics = stan_diagnostics.check_hmc_diagnostics(fit)
  failed_diagnostics = sorted(
    name for name, passed in diagnostics.items() if not passed
  )
  if failed_diagnostics:
    print(
      'Warning: Stan diagnostics failed: {}'.format(
        ', '.join(failed_diagnostics)
      )
    )
  else:
    print('Stan diagnostics passed.')
  return fit


def latest_parameter_means(fit, parameter_names, day_count):
  """Read final-day posterior means by name rather than summary row order."""

  summary = fit.summary(probs=(0.5,))
  rows = dict(zip(
    summary['summary_rownames'],
    summary['summary'].tolist(),
  ))
  values = []
  for parameter in parameter_names:
    row_name = '{}[{}]'.format(parameter, day_count)
    if row_name not in rows:
      raise ConfigError(
        "Stan summary does not contain '{}'.".format(row_name)
      )
    value = float(rows[row_name][0])
    if not math.isfinite(value):
      raise ConfigError(
        "Stan summary contains a non-finite mean for '{}'.".format(
          row_name
        )
      )
    values.append(value)
  return values


def write_latest_parameter_means(
    fit,
    parameter_names,
    output_headers,
    day_count,
    output_path,
):
  values = latest_parameter_means(fit, parameter_names, day_count)
  print('Latest regional deviations: {}'.format(values))
  with open(output_path, 'w') as output_file:
    output_file.write(','.join(output_headers) + '\n')
    output_file.write(','.join(str(value) for value in values))


def run_model_fed2025(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'National', fed_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(
    e_data, df
  )
  df.fillna(MISSING_OBSERVATION, inplace=True)

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count,
    'pollDay': modified_poll_days,
    'nswSwingDevPoll': df['NSW_SwingDev'].tolist() * 3,
    'vicSwingDevPoll': df['VIC_SwingDev'].tolist() * 3,
    'qldSwingDevPoll': df['QLD_SwingDev'].tolist() * 3,
    'waSwingDevPoll': df['WA_SwingDev'].tolist() * 3,
    'saSwingDevPoll': df['SA_SwingDev'].tolist() * 3,
    'wstanSwingDevPoll': df['WSTAN_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  fit = sample_region_model(
    e_data,
    stan_data,
    './Models/region_model_2025fed.stan',
    random_seed,
    chains=15,
    iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    [
      'nswSwingDev',
      'vicSwingDev',
      'qldSwingDev',
      'waSwingDev',
      'saSwingDev',
      'tanSwingDev',
    ],
    ['nsw', 'vic', 'qld', 'wa', 'sa', 'tan'],
    modified_day_count,
    output_path,
  )


def run_model_qld2024(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', qld_regions_2024
  )

  modified_day_count, modified_poll_days = prepare_poll_timing(
    e_data, df
  )
  df.fillna(MISSING_OBSERVATION, inplace=True)

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count,
    'pollDay': modified_poll_days,
    'isSwingDevPoll': df['Inner Suburbs_SwingDev'].tolist() * 3,
    'osSwingDevPoll': df['Outer Suburbs_SwingDev'].tolist() * 3,
    'coSwingDevPoll': df['Coasts_SwingDev'].tolist() * 3,
    'reSwingDevPoll': df['Regional_SwingDev'].tolist() * 3,
    'crSwingDevPoll': df['C+R_SwingDev'].tolist() * 3,
    'seSwingDevPoll': df['SE_SwingDev'].tolist() * 3,
    'ceSwingDevPoll': df['Central_SwingDev'].tolist() * 3,
    'fnSwingDevPoll': df['Far North_SwingDev'].tolist() * 3,
    'rexSwingDevPoll': df['Regional ex-rural_SwingDev'].tolist() * 3,
    'ruSwingDevPoll': df['Rural_SwingDev'].tolist() * 3,
    'prSwingDevPoll': df['Pure Regional_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  fit = sample_region_model(
    e_data,
    stan_data,
    './Models/region_model_2024qld.stan',
    random_seed,
    chains=6,
    iterations=300,
  )
  write_latest_parameter_means(
    fit,
    [
      'isSwingDev',
      'osSwingDev',
      'coreSwingDev',
      'seruSwingDev',
      'cereSwingDev',
      'ceruSwingDev',
      'fnreSwingDev',
      'fnruSwingDev',
    ],
    ['is', 'os', 'core', 'coru', 'cere', 'ceru', 'fnre', 'fnru'],
    modified_day_count,
    output_path,
  )


def run_model_vic2026(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', vic_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(
    e_data, df
  )
  df.fillna(MISSING_OBSERVATION, inplace=True)

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count,
    'pollDay': modified_poll_days,
    'innerMetroDevPoll': df['InnerMetro_SwingDev'].tolist() * 3,
    'outerMetroDevPoll': df['OuterMetro_SwingDev'].tolist() * 3,
    'regionalDevPoll': df['Regional_SwingDev'].tolist() * 3,
    'metroDevPoll': df['Metro_SwingDev'].tolist() * 3,
    'provincialDevPoll': df['Provincial_SwingDev'].tolist() * 3,
    'ruralDevPoll': df['Rural_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  fit = sample_region_model(
    e_data,
    stan_data,
    './Models/region_model_2026vic.stan',
    random_seed,
    chains=15,
    iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    [
      'innerMetroSwingDev',
      'outerMetroSwingDev',
      'provincialSwingDev',
      'ruralSwingDev',
    ],
    ['innerMetro', 'outerMetro', 'provincial', 'rural'],
    modified_day_count,
    output_path,
  )


def run_model_nsw2027(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', nsw_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(
    e_data, df
  )
  df.fillna(MISSING_OBSERVATION, inplace=True)

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count,
    'pollDay': modified_poll_days,
    'metroDevPoll': df['Metro_SwingDev'].tolist() * 3,
    'regionalDevPoll': df['Regional_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  fit = sample_region_model(
    e_data,
    stan_data,
    './Models/region_model_2027nsw.stan',
    random_seed,
    chains=15,
    iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    ['metroSwingDev', 'regionalSwingDev'],
    ['metro', 'regional'],
    modified_day_count,
    output_path,
  )


def run_model_qld2028(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', qld_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(
    e_data, df
  )
  df.fillna(MISSING_OBSERVATION, inplace=True)

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count,
    'pollDay': modified_poll_days,
    'metroDevPoll': df['Metro_SwingDev'].tolist() * 3,
    'seqDevPoll': df['SEQ_SwingDev'].tolist() * 3,
    'regionalDevPoll': df['Regional_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  fit = sample_region_model(
    e_data,
    stan_data,
    './Models/region_model_2028qld.stan',
    random_seed,
    chains=15,
    iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    ['metroSwingDev', 'seqSwingDev', 'regionalSwingDev'],
    ['metro', 'seq', 'regional'],
    modified_day_count,
    output_path,
  )


def run_models():
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
  recorder = region_model_provenance.RegionalModelRecorder(
    recorded_command
  )

  for desired_election, input_path in work_items:
    election = desired_election.short()
    random_seed = region_model_provenance.derive_stan_seed(
      base_seed, election, party
    )
    output_path = region_model_provenance.output_path(election, party)
    contract = model_contract(desired_election)
    e_data = ElectionData(
      input_path=input_path,
      contract=contract,
    )

    if desired_election.year() >= 2025 and desired_election.region() == 'fed':
      run_model_fed2025(
        e_data=e_data,
        random_seed=random_seed,
        output_path=output_path,
      )
    elif desired_election.year() >= 2028 and desired_election.region() == 'qld':
      run_model_qld2028(
        e_data=e_data,
        random_seed=random_seed,
        output_path=output_path,
      )
    elif desired_election.year() == 2024 and desired_election.region() == 'qld':
      run_model_qld2024(
        e_data=e_data,
        random_seed=random_seed,
        output_path=output_path,
      )
    elif desired_election.year() == 2026 and desired_election.region() == 'vic':
      run_model_vic2026(
        e_data=e_data,
        random_seed=random_seed,
        output_path=output_path,
      )
    elif desired_election.year() == 2027 and desired_election.region() == 'nsw':
      run_model_nsw2027(
        e_data=e_data,
        random_seed=random_seed,
        output_path=output_path,
      )
    else:
      # model_contract() has already checked this dispatch.
      raise AssertionError(
        'Unhandled regional model implementation for {}.'.format(election)
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
