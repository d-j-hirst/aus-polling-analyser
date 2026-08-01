"""Explicit election-to-Stan data mappings for the regional model.

Parent: region_model.py dispatches these functions after shared validation.
Each function intentionally keeps its Stan parameter names and regional
aggregation choices visible: regional definitions differ by election and are
not interchangeable configuration.
"""

from region_model_common import (
  MISSING_OBSERVATION,
  add_transformed_swing_deviations,
  fed_regions,
  nsw_regions,
  prepare_poll_timing,
  qld_regions,
  qld_regions_2024,
  vic_regions,
)
from region_model_stan import (
  sample_region_model,
  write_latest_parameter_means,
)


def run_model_fed2025(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'National', fed_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(e_data, df)
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
    e_data, stan_data, './Models/region_model_2025fed.stan', random_seed,
    chains=15, iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    ['nswSwingDev', 'vicSwingDev', 'qldSwingDev', 'waSwingDev',
     'saSwingDev', 'tanSwingDev'],
    ['nsw', 'vic', 'qld', 'wa', 'sa', 'tan'],
    modified_day_count, output_path,
  )


def run_model_qld2024(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', qld_regions_2024
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(e_data, df)
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
    e_data, stan_data, './Models/region_model_2024qld.stan', random_seed,
    chains=6, iterations=300,
  )
  write_latest_parameter_means(
    fit,
    ['isSwingDev', 'osSwingDev', 'coreSwingDev', 'seruSwingDev',
     'cereSwingDev', 'ceruSwingDev', 'fnreSwingDev', 'fnruSwingDev'],
    ['is', 'os', 'core', 'coru', 'cere', 'ceru', 'fnre', 'fnru'],
    modified_day_count, output_path,
  )


def run_model_vic2026(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', vic_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(e_data, df)
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
    e_data, stan_data, './Models/region_model_2026vic.stan', random_seed,
    chains=15, iterations=1000,
  )
  write_latest_parameter_means(
    fit,
    ['innerMetroSwingDev', 'outerMetroSwingDev', 'provincialSwingDev',
     'ruralSwingDev'],
    ['innerMetro', 'outerMetro', 'provincial', 'rural'],
    modified_day_count, output_path,
  )


def run_model_nsw2027(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', nsw_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(e_data, df)
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
    e_data, stan_data, './Models/region_model_2027nsw.stan', random_seed,
    chains=15, iterations=1000,
  )
  write_latest_parameter_means(
    fit, ['metroSwingDev', 'regionalSwingDev'], ['metro', 'regional'],
    modified_day_count, output_path,
  )


def run_model_qld2028(e_data, random_seed, output_path):
  df = e_data.base_df.copy()
  add_transformed_swing_deviations(
    df, e_data.previous_results, 'State', qld_regions
  )
  modified_day_count, modified_poll_days = prepare_poll_timing(e_data, df)
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
    e_data, stan_data, './Models/region_model_2028qld.stan', random_seed,
    chains=15, iterations=1000,
  )
  write_latest_parameter_means(
    fit, ['metroSwingDev', 'seqSwingDev', 'regionalSwingDev'],
    ['metro', 'seq', 'regional'], modified_day_count, output_path,
  )


MODEL_RUNNERS = {
  ('fed', 2025): run_model_fed2025,
  ('qld', 2024): run_model_qld2024,
  ('vic', 2026): run_model_vic2026,
  ('nsw', 2027): run_model_nsw2027,
  ('qld', 2028): run_model_qld2028,
}


def runner_for(election):
  """Return the explicit mapping selected by ``model_contract``."""

  region = election.region()
  year = election.year()
  if region == 'fed' and year >= 2025:
    return run_model_fed2025
  if region == 'qld' and year >= 2028:
    return run_model_qld2028
  try:
    return MODEL_RUNNERS[(region, year)]
  except KeyError as error:
    raise AssertionError(
      'Unhandled regional model implementation for {}.'.format(
        election.short()
      )
    ) from error
