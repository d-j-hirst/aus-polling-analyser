"""Check the relocated runner without invoking generators or real Stan sampling."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tests import ANALYSIS_DIRECTORY
from tests import run_tests


class AnalysisTestRunnerTests(unittest.TestCase):
    def test_selectors_accept_modules_methods_and_moved_paths(self):
        cases = {
            'test_pipeline': 'tests.test_pipeline',
            'tests.test_pipeline': 'tests.test_pipeline',
            'test_pipeline.SomeTests.test_example': 'tests.test_pipeline.SomeTests.test_example',
            'analysis/tests/test_pipeline.py': 'tests.test_pipeline',
            r'tests\test_pipeline.py': 'tests.test_pipeline',
        }
        for selector, expected in cases.items():
            with self.subTest(selector=selector):
                self.assertEqual(run_tests.test_name(selector), expected)

    def test_ci_selection_is_unique_and_does_not_import_production_stan_tests(self):
        self.assertEqual(len(run_tests.CI_TEST_MODULES), len(set(run_tests.CI_TEST_MODULES)))
        self.assertNotIn('test_fp_model_behaviour', run_tests.CI_TEST_MODULES)
        self.assertNotIn('test_fp_model_stan_smoke', run_tests.CI_TEST_MODULES)
        for name in run_tests.CI_TEST_MODULES:
            self.assertTrue((run_tests.TEST_DIRECTORY / (name + '.py')).is_file(), name)

    def test_test_package_resolves_the_actual_analysis_directory(self):
        self.assertEqual(ANALYSIS_DIRECTORY, run_tests.ANALYSIS_DIRECTORY)
        self.assertTrue((ANALYSIS_DIRECTORY / 'pipeline_registry.json').is_file())

    def test_focused_runner_works_outside_the_repository(self):
        # A real subprocess tests path setup independently of this parent's
        # imported modules. The selected method uses only standard-library data;
        # its count remains stable when more catalogue tests are added later.
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, '-B', str(Path(run_tests.__file__)),
                 'test_election_catalogue.ElectionCatalogueTests.test_future_catalogue_requires_status'],
                cwd=directory, text=True, capture_output=True,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Ran 1 test', result.stderr)

    def test_unknown_test_returns_failure(self):
        result = subprocess.run(
            [sys.executable, '-B', str(Path(run_tests.__file__)), 'test_missing_analysis_module'],
            cwd=ANALYSIS_DIRECTORY.parent, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn('test_missing_analysis_module', result.stderr)

    def test_all_and_explicit_selectors_are_mutually_exclusive(self):
        result = subprocess.run(
            [sys.executable, '-B', str(Path(run_tests.__file__)), '--all', 'test_election_catalogue'],
            cwd=ANALYSIS_DIRECTORY.parent, text=True, capture_output=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn('cannot be combined', result.stderr)


if __name__ == '__main__':
    unittest.main()
