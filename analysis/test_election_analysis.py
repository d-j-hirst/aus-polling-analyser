import importlib.util
import sys
import types
import unittest
from pathlib import Path
from unittest import mock


def load_election_analysis():
    """Load the numerical script with lightweight third-party import stubs."""

    modules = {
        "numpy": types.ModuleType("numpy"),
        "statsmodels": types.ModuleType("statsmodels"),
        "statsmodels.api": types.ModuleType("statsmodels.api"),
        "sklearn": types.ModuleType("sklearn"),
        "sklearn.linear_model": types.ModuleType("sklearn.linear_model"),
        "scipy": types.ModuleType("scipy"),
        "scipy.interpolate": types.ModuleType("scipy.interpolate"),
        "scipy.optimize": types.ModuleType("scipy.optimize"),
        "scipy.stats": types.ModuleType("scipy.stats"),
    }
    modules["sklearn.linear_model"].LinearRegression = object
    modules["scipy.interpolate"].UnivariateSpline = object
    modules["scipy.optimize"].curve_fit = object
    modules["scipy.stats"].moment = object

    module_path = Path(__file__).with_name("election_analysis.py")
    spec = importlib.util.spec_from_file_location(
        "election_analysis_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class FakeElectionResults:
    def __init__(self, votes_by_party):
        self.fp_by_party = votes_by_party

    def total_fp_votes(self):
        return sum(self.fp_by_party.values())

    def total_fp_percentage_party(self, party):
        return (
            self.fp_by_party.get(party, 0)
            / self.total_fp_votes()
            * 100
        )


class ElectionAnalysisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.analysis = load_election_analysis()

    def test_statewide_independents_up_to_eight_percent_are_others(self):
        results = FakeElectionResults({
            "Labor": 40,
            "Liberal": 40,
            "Greens": 10,
            "Independent": 7,
            "Local Party": 2,
            "Established Minor": 1,
        })

        self.assertAlmostEqual(
            self.analysis.total_others_vote_share(results),
            10,
        )

    def test_statewide_independents_above_eight_percent_are_excluded(self):
        results = FakeElectionResults({
            "Labor": 39,
            "Liberal": 39,
            "Greens": 10,
            "Independent": 9,
            "Local Party": 2,
            "Established Minor": 1,
        })

        self.assertAlmostEqual(
            self.analysis.total_others_vote_share(results),
            3,
        )

    def test_material_independent_vote_can_be_at_either_endpoint(self):
        self.assertTrue(
            self.analysis.has_material_independent_vote(12, 0)
        )
        self.assertTrue(
            self.analysis.has_material_independent_vote(0, 12)
        )
        self.assertFalse(
            self.analysis.has_material_independent_vote(7.9, 0)
        )

    def test_only_selected_regional_mix_factor_errors_are_accumulated(self):
        region_errors = {"all": [0.25], "NSW": [0.5]}
        errors_by_factor = {
            0.4: {"all": [10, 20], "NSW": [10], "VIC": [20]},
            0.7: {"all": [1, 2], "NSW": [1], "VIC": [2]},
        }

        self.analysis.extend_region_errors_with_selected_factor(
            region_errors, errors_by_factor, 0.7
        )

        self.assertEqual(region_errors["all"], [0.25, 1, 2])
        self.assertEqual(region_errors["NSW"], [0.5, 1])
        self.assertEqual(region_errors["VIC"], [2])


if __name__ == "__main__":
    unittest.main()
