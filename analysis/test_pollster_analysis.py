import contextlib
import importlib.util
import io
import sys
import tempfile
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
        filename = "fp_polls_2025wa_LIB FP_biascal.csv"
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / filename
            path.write_text(
                "Firm,Day,LIB FP\n"
                "Example,10,30\n"
                "Example,20,31\n",
                encoding="utf-8",
            )
            with mock.patch.object(
                self.analysis, "directory", temporary_directory
            ):
                counts = self.analysis.get_n_polls([filename])

        election = ElectionCode(2025, "wa")
        self.assertEqual(
            counts[(election, "Example", "LNP FP")], 2
        )
        self.assertEqual(
            counts[(election, "all", "LNP FP")], 2
        )

    def test_trend_median_is_selected_by_header(self):
        trend = io.StringIO(
            "Start date day,Month,Year\n"
            "01,01,2020\n"
            "Day,Party,0%,50%,100%\n"
            "0,@TPP,45,51.25,56\n"
        )
        self.assertEqual(
            self.analysis.load_final_trend_median(
                trend, "trend.csv"
            ),
            51.25,
        )

    def test_house_effect_median_is_selected_by_header(self):
        house_effects = io.StringIO(
            "House,Party,0%,5%,50%,100%\n"
            "New house effects\n"
            "Example,@TPP,-2,-1.5,0.25,2\n"
            "Old house effects\n"
        )
        self.assertEqual(
            self.analysis.load_new_house_effects(
                house_effects, "house-effects.csv"
            ),
            {"Example": 0.25},
        )

    def test_nonfinite_trend_median_is_rejected(self):
        trend = io.StringIO(
            "Start date day,Month,Year\n"
            "01,01,2020\n"
            "Day,Party,50%\n"
            "0,@TPP,nan\n"
        )
        with self.assertRaisesRegex(
            self.analysis.ConfigError, "not finite"
        ):
            self.analysis.load_final_trend_median(
                trend, "trend.csv"
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
