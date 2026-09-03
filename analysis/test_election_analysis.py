import importlib.util
import sys
import tempfile
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

    def test_seat_swing_analysis_retains_classic_major_party_set(self):
        major_parties = self.analysis.analyse_seat_swings.__globals__[
            "MAJOR_PARTIES"
        ]

        self.assertEqual(
            major_parties,
            {
                "Liberal",
                "National",
                "Liberal National",
                "Labor",
                "Country Liberal",
            },
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

    def test_provenance_publishes_each_supported_federal_term(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            base = Path(temporary_directory)
            regional = base / "Regional"
            regional.mkdir()

            def output_paths(election):
                paths = [
                    regional / "{}-{}.csv".format(election, index)
                    for index in range(4)
                ]
                for path in paths:
                    path.write_text("data\n", encoding="utf-8")
                return paths

            captured = {}

            def update_manifest(path, records, runs, **kwargs):
                captured.update(records)

            provenance = self.analysis.generated_provenance
            with mock.patch.object(
                self.analysis, "ANALYSIS_DIRECTORY", base
            ), mock.patch.object(
                self.analysis.federal_regional_provenance,
                "output_paths",
                side_effect=output_paths,
            ), mock.patch.object(
                provenance, "source_manifest_dependency", return_value={}
            ), mock.patch.object(
                provenance, "load_manifest",
                return_value={"records": {}},
            ), mock.patch.object(
                provenance, "generated_manifest_dependency", return_value={}
            ), mock.patch.object(
                provenance, "current_source_revision", return_value="revision"
            ), mock.patch.object(
                provenance, "current_environment", return_value={}
            ), mock.patch.object(
                provenance, "generation_run",
                return_value=("run", {"command": []}),
            ), mock.patch.object(
                provenance, "generation_record",
                side_effect=lambda **kwargs: kwargs,
            ), mock.patch.object(
                provenance, "output_fingerprints",
                side_effect=lambda paths, _base: {
                    str(path): {} for path in paths
                },
            ), mock.patch.object(
                provenance, "update_manifest", side_effect=update_manifest
            ):
                self.analysis.record_generated_provenance()

        self.assertTrue(
            {
                "federal_regional_statistics:2022fed",
                "federal_regional_statistics:2025fed",
                "federal_regional_statistics:2028fed",
            }.issubset(captured)
        )


if __name__ == "__main__":
    unittest.main()
