"""Stan compilation, sampling and diagnostics for fp_model."""

import datetime
import os
import pystan
from dataclasses import dataclass
from datetime import timedelta
from time import perf_counter

from stan_cache import stan_cache

from fp_model_data import ElectionData, ModelParams

_STAN_MODEL_CACHE = {}

class StanDiagnosticsRecorder:
    """Accumulate non-fatal HMC diagnostic failures for one batch."""

    EXPECTED_CHECKS = {
        'n_eff',
        'Rhat',
        'divergence',
        'treedepth',
        'energy',
    }

    def __init__(self, path='./Outputs/fp_model_diagnostics.log'):
        self.path = path
        self.model_count = 0
        self.issue_counts = {}
        self.issue_count = 0
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'a') as output:
            output.write(
                '\nStan diagnostic failures for fp_model batch starting '
                '{} UTC\n'.format(
                    datetime.datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')
                )
            )

    def record(
        self,
        election,
        party,
        excluded_pollster,
        random_seed,
        checks,
        mode=None,
    ):
        self.model_count += 1
        failed = {
            check
            for check, passed in checks.items()
            if not passed
        }
        failed.update(
            'unavailable:{}'.format(check)
            for check in self.EXPECTED_CHECKS - set(checks)
        )
        if not failed:
            return

        self.issue_count += 1
        for check in failed:
            self.issue_counts[check] = (
                self.issue_counts.get(check, 0) + 1
            )
        pollster = excluded_pollster or '<none>'
        with open(self.path, 'a') as output:
            output.write(
                '{} | {} | mode {} | excluded pollster {} | seed {} | '
                'failed: {}\n'
                .format(
                    election,
                    party,
                    mode or 'unknown',
                    pollster,
                    random_seed,
                    ', '.join(sorted(failed)),
                )
            )

    def report(self, completed=True):
        if self.issue_counts:
            counts = ', '.join(
                '{}={}'.format(check, count)
                for check, count in sorted(self.issue_counts.items())
            )
            summary = (
                '{} of {} Stan models had diagnostic problems ({}). '
                'See {}.'
                .format(
                    self.issue_count,
                    self.model_count,
                    counts,
                    self.path,
                )
            )
        else:
            summary = (
                'All {} completed Stan models passed the available HMC '
                'diagnostic checks.'.format(self.model_count)
            )
        if not completed:
            summary = 'Batch terminated before completion. ' + summary
        with open(self.path, 'a') as output:
            output.write(summary + '\n')
        print(summary)


@dataclass
class ModelInputs:
    chains: int
    diagnostics_recorder: 'StanDiagnosticsRecorder'
    e_data: ElectionData
    excluded_pollster: str
    iterations: int
    model_params: ModelParams
    mode: str
    party: str
    random_seed: int
    stan_data: dict


def load_stan_model(path='./Models/fp_model.stan'):
    """Load one compiled model per source text for the current process."""

    with open(path, 'r') as source:
        model_code = source.read()
    if model_code not in _STAN_MODEL_CACHE:
        _STAN_MODEL_CACHE[model_code] = stan_cache(model_code=model_code)
    return _STAN_MODEL_CACHE[model_code]


def run_stan_model(model_inputs: ModelInputs):
    e_data = model_inputs.e_data
    model_params = model_inputs.model_params

    # Encode the Stan model in C++ or retrieve it from the process/disk cache.
    sm = load_stan_model()

    # Report dates for model, this means we can easily check if new
    # data has actually been saved without waiting for model to run
    print(f'*** Beginning sampling for {model_inputs.party} ***')
    end = e_data.start + timedelta(days=int(e_data.n_days))
    print(f'Start date of model: {e_data.start:%Y-%m-%d}')
    print(f'End date of model: {end:%Y-%m-%d}')
    print()

    # Do model sampling. Time for diagnostic purposes
    start_time = perf_counter()
    fit = sm.sampling(data=model_inputs.stan_data,
                        iter=model_inputs.iterations,
                        chains=model_inputs.chains,
                        seed=model_inputs.random_seed,
                        control={'max_treedepth': model_params.stan_max_treedepth,
                                'adapt_delta': model_params.stan_adapt_delta})
    finish_time = perf_counter()
    print('Time elapsed: ' + format(finish_time - start_time, '.2f')
            + ' seconds')
    print(f'*** Finished sampling for {model_inputs.party} ***')

    # Check technical model diagnostics
    diagnostic_results = pystan.diagnostics.check_hmc_diagnostics(fit)
    print(diagnostic_results)
    model_inputs.diagnostics_recorder.record(
        election=''.join(e_data.e_tuple),
        party=model_inputs.party,
        excluded_pollster=model_inputs.excluded_pollster,
        random_seed=model_inputs.random_seed,
        checks=diagnostic_results,
        mode=model_inputs.mode,
    )

    return fit
