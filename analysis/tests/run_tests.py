"""Run analysis tests with stable imports and production-relative fixture paths.

The default selection preserves the existing Linux/Windows CI suite. --all
explicitly discovers additional tests, including ones requiring the full Stan
environment. The production Stan smoke fit remains opt-in through its existing
FP_MODEL_STAN_SMOKE environment variable.

Main functions:
* test_name accepts module, method or moved-file selectors for focused checks.
* main sets up the analysis working directory, loads the selected unittest suite
  and returns a failing exit status when any test fails or cannot be imported.
"""

import argparse
import os
from pathlib import Path
import sys
import unittest


TEST_DIRECTORY = Path(__file__).resolve().parent
ANALYSIS_DIRECTORY = TEST_DIRECTORY.parent

# Keep the established CI order and dependency boundary in one place instead
# of duplicating the long module list in Linux and Windows workflow steps.
CI_TEST_MODULES = (
    'test_election_data',
    'test_election_store',
    'test_election_analysis',
    'test_region_model',
    'test_region_model_provenance',
    'test_pipeline_registry',
    'test_pipeline',
    'test_source_provenance',
    'test_generated_provenance',
    'test_provenance_maintenance',
    'test_calibration_provenance',
    'test_calibration_summary',
    'test_calibration_summary_provenance',
    'test_pollster_analysis',
    'test_pollster_analysis_evidence',
    'test_pollster_analysis_equivalence',
    'test_pollster_analysis_provenance',
    'test_approvals',
    'test_approvals_provenance',
    'test_fp_model_provenance',
    'test_fp_model_cutoffs',
    'test_trend_adjust',
    'test_trend_adjust_cutoffs',
    'test_trend_adjust_provenance',
    'test_analysis_provenance',
    'test_run_fp_model',
    'test_sample_kurtosis',
    'test_stan_cache',
    'test_by_elections',
    'test_federal_state',
    'test_election_catalogue',
    'test_required_work',
    'test_generated_data_archive',
    'test_internal_module_layout',
    'test_turnout_data',
    'test_turnout_aec',
    'test_turnout_aec_operational',
    'test_turnout_nsw',
    'test_turnout_nsw_operational',
    'test_turnout_vic',
    'test_turnout_qld',
    'test_turnout_qld_operational',
    'test_turnout_wa',
    'test_turnout_sa',
    'test_turnout_sa_operational',
    'test_turnout_published_operational',
)


def test_name(selector):
    """Normalize focused selections to this package, retaining class/method names."""
    name = selector.replace('\\', '/').rsplit('/', 1)[-1]
    if name.endswith('.py'):
        name = name[:-3]
    return name if name.startswith('tests.') else 'tests.' + name


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tests', nargs='*', help='test modules, file paths or dotted test methods')
    parser.add_argument('--all', action='store_true', help='discover every test; requires full analysis dependencies')
    parser.add_argument('-v', '--verbose', action='store_true', help='show individual test names')
    args = parser.parse_args(argv)
    if args.all and args.tests:
        parser.error('--all cannot be combined with individual test selections')

    # Production modules still use their existing top-level import names and
    # some tests read Data/Regional fixtures relative to the analysis directory.
    # This is a command-line process setup, not a change to production loaders.
    sys.path.insert(0, str(ANALYSIS_DIRECTORY))
    os.chdir(ANALYSIS_DIRECTORY)
    print('Loading analysis test modules{}...'.format(' (full numerical suite)' if args.all else ''), flush=True)
    loader = unittest.TestLoader()
    if args.all:
        suite = loader.discover(str(TEST_DIRECTORY), top_level_dir=str(ANALYSIS_DIRECTORY))
        selection = 'all discovered'
    else:
        names = args.tests or CI_TEST_MODULES
        # The small runner regression suite is additional infrastructure coverage;
        # the original CI test selection and its numerical requirements are intact.
        if not args.tests:
            names = CI_TEST_MODULES + ('test_test_runner',)
        suite = loader.loadTestsFromNames([test_name(name) for name in names])
        selection = 'selected' if args.tests else 'CI'
    print('Analysis {} suite: {} tests'.format(selection, suite.countTestCases()), flush=True)
    result = unittest.TextTestRunner(verbosity=2 if args.verbose else 1).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
