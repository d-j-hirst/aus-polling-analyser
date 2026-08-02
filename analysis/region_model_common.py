"""Shared contracts and poll preparation for regional Stan models.

Parent: region_model.py is the command-line entry point. It selects work,
dispatches the election-specific mappings, and records generated provenance.
This module keeps validation and data preparation consistent across those
mappings without imposing a common Stan-data layout on them.

Main functions:
* ``model_contract`` defines the required input/output shape for each
  supported election-specific mapping.
* ``validate_*`` functions reject malformed baseline and regional-poll data
  before any Stan work begins.
* ``ElectionData`` loads one validated election's inputs for a mapping.
* ``prepare_poll_timing`` and ``add_transformed_swing_deviations`` perform
  shared data preparation used by the core mappings.
"""

import argparse
import math

import pandas as pd

from election_code import ElectionCode
from poll_transform import transform_vote_share


fed_regions = ['NSW', 'VIC', 'QLD', 'WA', 'SA', 'WSTAN']
qld_regions_2024 = [
  'Inner Suburbs', 'Outer Suburbs', 'Coasts', 'Regional', 'C+R', 'SE',
  'Central', 'Far North', 'Regional ex-rural', 'Rural', 'Pure Regional',
]
vic_regions = [
  'InnerMetro', 'OuterMetro', 'Regional', 'Metro', 'Provincial', 'Rural',
]
nsw_regions = ['Metro', 'Regional']
qld_regions = ['Metro', 'SEQ', 'Regional']

MISSING_OBSERVATION = -10000
DAYS_PER_MODEL_STEP = 5
BASELINE_WEIGHT_TOLERANCE = 2.0


# Contract definitions and validation

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
    'StartDate', 'EndDate', 'Firm', 'Size', contract['overall'],
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

  validate_election_baseline(previous_results, contract, input_path)

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
      '{} row {} {}'.format(input_path, row_number, contract['overall']),
    )
    for region in contract['regions']:
      _validated_vote_share(
        row[region],
        '{} row {} {}'.format(input_path, row_number, region),
        required=region in required_poll_regions,
      )


# Command-line configuration and data loading

class Config:
  """Parse the regional-model command line and select configured elections."""

  def __init__(self):
    parser = argparse.ArgumentParser(
      description='Generate election-specific regional swing deviations.')
    parser.add_argument(
      '--election', action='store', type=str,
      help='Generate regional trends for this election. '
           'Enter as 1234-xxx format, e.g. 2013-fed. Write "all" '
           'to do it for all elections.')
    parser.add_argument(
      '--party', action='store', type=str,
      help='Party to generate regional trends for. Currently only supports ON. '
           'If not specified, will do 2PP.', default='')
    parser.add_argument(
      '--seed', action='store', type=int,
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
    with open('./Data/polled-elections.csv', 'r') as file:
      elections = ElectionCode.load_elections_from_file(file)
    with open('./Data/future-elections.csv', 'r') as file:
      elections += ElectionCode.load_elections_from_file(file)
    if self.election_instructions == 'all':
      self.elections = elections
      return
    parts = self.election_instructions.split('-')
    if len(parts) < 2:
      raise ConfigError(
        'Error in "elections" argument: given value did not have two parts '
        'separated by a hyphen (e.g. 2013-fed)'
      )
    try:
      code = ElectionCode(parts[0], parts[1])
    except ValueError as error:
      raise ConfigError(
        'Error in "elections" argument: first part of election name could '
        'not be converted into an integer'
      ) from error
    if code not in elections:
      raise ConfigError(
        'Error in "elections" argument: value given did not match any '
        'configured polled or future election'
      )
    if len(parts) == 2:
      self.elections = [code]
    elif parts[2] == 'onwards':
      self.elections = elections[elections.index(code):]
    else:
      raise ConfigError('Invalid instruction in "elections" argument.')


class ElectionData:
  """Load, validate, and date-index one election's regional poll data."""

  def __init__(self, input_path, contract):
    self.base_df = pd.read_csv(input_path)
    if 'Firm' not in self.base_df.columns:
      raise ConfigError('{} lacks a Firm column.'.format(input_path))

    normalised_firms = self.base_df['Firm'].apply(
      lambda value: '' if pd.isna(value) else str(value).strip().casefold()
    )
    baseline_mask = normalised_firms == 'election'
    previous_results = self.base_df[baseline_mask].to_dict('records')
    if len(previous_results) > 1:
      raise ConfigError(
        '{} contains more than one Election baseline row.'.format(input_path)
      )
    self.previous_results = previous_results[0] if previous_results else None
    self.base_df = self.base_df[~baseline_mask]
    validate_regional_input(
      self.base_df, self.previous_results, contract, input_path
    )

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
    invalid_periods = self.base_df['EndDate'] < self.base_df['StartDate']
    if invalid_periods.any():
      row_number = int(invalid_periods[invalid_periods].index[0]) + 2
      raise ConfigError(
        '{} row {} ends before it starts.'.format(input_path, row_number)
      )
    self.base_df['MidDate'] = self.base_df['StartDate'] + (
      self.base_df['EndDate'] - self.base_df['StartDate']
    ) / 2
    self.start = self.base_df['StartDate'].min()
    self.base_df['StartDay'] = (
      self.base_df['StartDate'] - self.start
    ).dt.days
    self.base_df['MidDay'] = (
      self.base_df['MidDate'] - self.start
    ).dt.days
    self.base_df['EndDay'] = (
      self.base_df['EndDate'] - self.start
    ).dt.days
    self.n_days = self.base_df['EndDay'].max() + 1
    self.end = self.base_df['EndDate'].max()
    self.create_day_series()

  def create_day_series(self):
    for index in self.base_df.index:
      self.base_df.loc[index, 'StartDayNum'] = int(
        self.base_df.loc[index, 'StartDay'] + 1
      )
      self.base_df.loc[index, 'MidDayNum'] = int(
        self.base_df.loc[index, 'MidDay'] + 1
      )
      self.base_df.loc[index, 'EndDayNum'] = int(
        self.base_df.loc[index, 'EndDay'] + 1
      )


# Shared numerical input preparation

def prepare_poll_timing(e_data, df):
  """Represent each poll across its fieldwork period on a five-day grid."""

  # This established convention both spreads and triples each likelihood.
  poll_days = (
    [int(value) for value in df['StartDayNum'].values]
    + [int(value) for value in df['MidDayNum'].values]
    + [int(value) for value in df['EndDayNum'].values]
  )
  modified_day_count = max(
    math.floor(e_data.n_days / DAYS_PER_MODEL_STEP), 1
  )
  modified_poll_days = [
    min(modified_day_count, max(1, math.floor(day / DAYS_PER_MODEL_STEP)))
    for day in poll_days
  ]
  return modified_day_count, modified_poll_days


def add_transformed_swing_deviations(
    df, previous_results, overall_column, regions
):
  """Express regional polling as transformed swing minus overall swing."""

  previous_overall = previous_results[overall_column]
  df['OverallSwing'] = df[overall_column].apply(
    lambda value: transform_vote_share(value) - transform_vote_share(
      previous_overall
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
