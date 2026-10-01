"""Protect data locations and the import boundary after internal-module moves."""

import json
import os
import subprocess
import sys
import tempfile
import unittest

from tests import ANALYSIS_DIRECTORY
from lib.paths import ANALYSIS_DIRECTORY as LIB_ANALYSIS_DIRECTORY
from lib.poll_models.fp_model_constants import fp_model_source_files
from lib.provenance import booth_result_provenance, federal_regional_provenance
from lib.shared import election_catalogue


class InternalModuleLayoutTests(unittest.TestCase):
    def test_pipeline_imports_do_not_load_model_dependencies(self):
        # Use a fresh process because other tests intentionally import numerical
        # modules. A different cwd also exposes accidental module-relative data
        # lookups while leaving the operational analysis working directory intact.
        environment = os.environ.copy()
        environment['PYTHONPATH'] = str(ANALYSIS_DIRECTORY)
        script = (
            'import json, sys; import pipeline, analysis_provenance; '
            'print(json.dumps([name for name in '
            '["pystan", "numpy", "pandas", "scipy", "sklearn", "statsmodels"] '
            'if name in sys.modules]))'
        )
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [sys.executable, '-B', '-c', script],
                cwd=directory,
                env=environment,
                capture_output=True,
                text=True,
                check=True,
            )
        self.assertEqual(json.loads(result.stdout), [])

    def test_relocated_helpers_keep_durable_analysis_paths(self):
        self.assertEqual(LIB_ANALYSIS_DIRECTORY, ANALYSIS_DIRECTORY)
        self.assertEqual(
            election_catalogue.DATA_DIRECTORY, ANALYSIS_DIRECTORY / 'Data'
        )
        self.assertEqual(
            booth_result_provenance.BOOTH_RESULTS_DIRECTORY,
            ANALYSIS_DIRECTORY / 'Booth Results',
        )
        self.assertEqual(
            federal_regional_provenance.MANIFEST_PATH,
            ANALYSIS_DIRECTORY / 'Seat Statistics' / 'generated-provenance.json',
        )

    def test_model_source_fingerprints_find_registered_relocated_files(self):
        # The checkpoint/fitting source list is consumed without importing Stan.
        # Every path must still resolve to the same monitored source category.
        manifest = json.loads(
            (ANALYSIS_DIRECTORY / 'provenance.json').read_text(encoding='utf-8')
        )
        monitored = manifest['categories']['fp_model_script']['files']
        paths = fp_model_source_files()
        for path in paths:
            with self.subTest(path=path):
                self.assertTrue(path.is_file())
                self.assertIn(path.relative_to(ANALYSIS_DIRECTORY).as_posix(), monitored)
        self.assertEqual(paths[0], ANALYSIS_DIRECTORY / 'fp_model.py')


if __name__ == '__main__':
    unittest.main()
