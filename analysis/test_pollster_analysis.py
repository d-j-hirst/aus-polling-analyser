import contextlib
import importlib.util
import io
import sys
import types
import unittest
from pathlib import Path
from unittest import mock

from election_code import ElectionCode


def load_pollster_analysis():
    """Load parsing helpers without optional numerical dependencies."""

    numpy = types.ModuleType("numpy")
    numpy.array = lambda values: values
    pandas = types.ModuleType("pandas")
    pandas.Timestamp = lambda value: value
    statsmodels = types.ModuleType("statsmodels")
    stats = types.ModuleType("statsmodels.stats")
    weightstats = types.ModuleType("statsmodels.stats.weightstats")
    weightstats.DescrStatsW = object

    module_path = Path(__file__).with_name("pollster_analysis.py")
    spec = importlib.util.spec_from_file_location(
        "pollster_analysis_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(
        sys.modules,
        {
            "numpy": numpy,
            "pandas": pandas,
            "statsmodels": statsmodels,
            "statsmodels.stats": stats,
            "statsmodels.stats.weightstats": weightstats,
        },
    ):
        spec.loader.exec_module(module)
    return module


class PollsterAnalysisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.analysis = load_pollster_analysis()

    def test_liberal_party_is_pooled_as_coalition_then_restored(self):
        self.assertEqual(
            self.analysis.canonical_party("LIB FP"), "LNP FP"
        )
        self.assertEqual(
            self.analysis.output_party("LNP FP", True), "LIB FP"
        )
        self.assertEqual(
            self.analysis.output_party("LNP FP", False), "LNP FP"
        )

    def test_poll_counts_canonicalise_liberal_party(self):
        evidence = self.analysis.CalibrationEvidence(
            (),
            (
                self.analysis.BiasEvidence(
                    ElectionCode(2025, "wa"),
                    "LIB FP",
                    50.0,
                    {"Example": 0.25},
                    {"Example": 2},
                ),
            ),
        )
        from pollster_analysis_house_effects import get_n_polls

        counts = get_n_polls(evidence)

        election = ElectionCode(2025, "wa")
        self.assertEqual(
            counts[(election, "Example", "LNP FP")], 2
        )
        self.assertEqual(
            counts[(election, "all", "LNP FP")], 2
        )

    def test_handled_failure_returns_nonzero(self):
        with mock.patch.object(
            self.analysis,
            "run_analysis",
            side_effect=self.analysis.ConfigError("invalid input"),
        ), mock.patch.object(
            self.analysis, "write_completion_status"
        ) as write_status, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(self.analysis.main([]), 2)

        write_status.assert_called_once_with(2)


if __name__ == "__main__":
    unittest.main()
