"""Stan execution and posterior-output helpers for regional models.

Parent: region_model.py orchestrates election work. The explicit Stan-data
mappings remain in region_model_mappings.py; this module owns their shared
compile, sampling, diagnostics, and final-day extraction behaviour.
"""

import math
from time import perf_counter

from region_model_common import ConfigError


def sample_region_model(
    e_data, stan_data, model_path, random_seed, chains, iterations
):
  """Compile/cache and sample one regional Stan model."""

  # Defer PyStan loading until input validation has completed.
  from stan_cache import stan_cache

  with open(model_path, 'r') as model_file:
    model = model_file.read()
  stan_model = stan_cache(model_code=model)
  print('Beginning sampling ...')
  print('Start date of model: {}'.format(e_data.start.strftime('%Y-%m-%d')))
  print('End date of model: {}'.format(e_data.end.strftime('%Y-%m-%d')))
  start_time = perf_counter()
  fit = stan_model.sampling(
    data=stan_data,
    iter=iterations,
    chains=chains,
    seed=random_seed,
    control={'max_treedepth': 18, 'adapt_delta': 0.8},
  )
  print('Time elapsed: {:.2f} seconds'.format(perf_counter() - start_time))
  print('Stan finished.')

  import pystan.diagnostics as stan_diagnostics
  diagnostics = stan_diagnostics.check_hmc_diagnostics(fit)
  failed = sorted(name for name, passed in diagnostics.items() if not passed)
  if failed:
    print('Warning: Stan diagnostics failed: {}'.format(', '.join(failed)))
  else:
    print('Stan diagnostics passed.')
  return fit


def latest_parameter_means(fit, parameter_names, day_count):
  """Read final-day posterior means by name rather than summary row order."""

  summary = fit.summary(probs=(0.5,))
  rows = dict(zip(summary['summary_rownames'], summary['summary'].tolist()))
  values = []
  for parameter in parameter_names:
    row_name = '{}[{}]'.format(parameter, day_count)
    if row_name not in rows:
      raise ConfigError("Stan summary does not contain '{}'.".format(row_name))
    value = float(rows[row_name][0])
    if not math.isfinite(value):
      raise ConfigError(
        "Stan summary contains a non-finite mean for '{}'.".format(row_name)
      )
    values.append(value)
  return values


def write_latest_parameter_means(
    fit, parameter_names, output_headers, day_count, output_path
):
  values = latest_parameter_means(fit, parameter_names, day_count)
  print('Latest regional deviations: {}'.format(values))
  with open(output_path, 'w') as output_file:
    output_file.write(','.join(output_headers) + '\n')
    output_file.write(','.join(str(value) for value in values))
