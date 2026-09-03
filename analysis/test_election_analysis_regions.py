import importlib.util
import sys
import types
import unittest
from pathlib import Path
from unittest import mock

import federal_regional_provenance


def load_regions_module():
    modules = {
        "numpy": types.ModuleType("numpy"),
        "scipy": types.ModuleType("scipy"),
        "scipy.optimize": types.ModuleType("scipy.optimize"),
        "sklearn": types.ModuleType("sklearn"),
        "sklearn.linear_model": types.ModuleType("sklearn.linear_model"),
        "election_analysis_common":
            types.ModuleType("election_analysis_common"),
        "poll_transform": types.ModuleType("poll_transform"),
        "sample_kurtosis": types.ModuleType("sample_kurtosis"),
    }
    modules["scipy.optimize"].curve_fit = object
    modules["sklearn.linear_model"].LinearRegression = object
    modules["election_analysis_common"].extend_region_errors_with_selected_factor = object
    modules["election_analysis_common"].one_tail_kurtosis = object
    modules["poll_transform"].clamp = object
    modules["sample_kurtosis"].calc_rmse = object
    modules["sample_kurtosis"].two_tail_kurtosis = object
    path = Path(__file__).with_name("election_analysis_regions.py")
    spec = importlib.util.spec_from_file_location(
        "election_analysis_regions_under_test", path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class FakeElectionCode:
    def __init__(self, year):
        self._year = year

    def year(self):
        return self._year


class ElectionAnalysisRegionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.regions = load_regions_module()

    def test_training_elections_must_precede_target(self):
        self.assertTrue(
            self.regions.is_before_target(
                FakeElectionCode(2019), 2022
            )
        )
        self.assertFalse(
            self.regions.is_before_target(
                FakeElectionCode(2022), 2022
            )
        )
        self.assertTrue(
            self.regions.is_before_target(
                FakeElectionCode(2022), 2025
            )
        )

    def test_supported_terms_have_four_cpp_outputs_each(self):
        self.assertEqual(
            federal_regional_provenance.SUPPORTED_ELECTIONS,
            ("2022fed", "2025fed", "2028fed"),
        )
        for election in federal_regional_provenance.SUPPORTED_ELECTIONS:
            with self.subTest(election=election):
                outputs = federal_regional_provenance.output_paths(election)
                self.assertEqual(len(outputs), 4)
                self.assertTrue(
                    all(path.name.startswith(election) for path in outputs)
                )

    def test_required_work_can_be_scoped(self):
        work = federal_regional_provenance.required_work_units(
            ["2025fed"]
        )

        self.assertEqual(
            list(work),
            ["federal_regional_statistics:2025fed"],
        )


if __name__ == "__main__":
    unittest.main()
