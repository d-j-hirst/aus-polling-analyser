import importlib.util
import io
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


def load_approvals():
    """Load parsing and diagnostic helpers without numerical dependencies."""

    numpy = types.ModuleType("numpy")
    pandas = types.ModuleType("pandas")
    statsmodels = types.ModuleType("statsmodels")
    statsmodels_api = types.ModuleType("statsmodels.api")
    provenance = types.ModuleType("approvals_provenance")
    provenance.SyntheticTppRecorder = object

    module_path = Path(__file__).with_name("approvals.py")
    spec = importlib.util.spec_from_file_location(
        "approvals_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(
        sys.modules,
        {
            "numpy": numpy,
            "pandas": pandas,
            "statsmodels": statsmodels,
            "statsmodels.api": statsmodels_api,
            "approvals_provenance": provenance,
        },
    ):
        spec.loader.exec_module(module)
    return module


class ApprovalsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.approvals = load_approvals()

    def test_pure_trend_median_is_selected_by_header(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "trend.csv"
            path.write_text(
                "Start date day,Month,Year\n"
                "01,02,2020\n"
                "Day,Party,0%,5%,50%,100%\n"
                "0,@TPP,45,46,51.25,56\n"
                "1,@TPP,46,47,51.5,57\n",
                encoding="utf-8",
            )
            trend, start, end = self.approvals.load_pure_trend(path)

        self.assertEqual(trend, {0: 51.25, 1: 51.5})
        self.assertEqual(start.isoformat(), "2020-02-01")
        self.assertEqual(end.isoformat(), "2020-02-03")

    def test_nonfinite_pure_trend_median_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "trend.csv"
            path.write_text(
                "Start date day,Month,Year\n"
                "01,02,2020\n"
                "Day,Party,50%\n"
                "0,@TPP,nan\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                self.approvals.ApprovalDataError, "non-finite"
            ):
                self.approvals.load_pure_trend(path)

    def test_pure_trend_days_must_be_contiguous(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "trend.csv"
            path.write_text(
                "Start date day,Month,Year\n"
                "01,02,2020\n"
                "Day,Party,50%\n"
                "0,@TPP,50\n"
                "2,@TPP,51\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                self.approvals.ApprovalDataError, "contiguous"
            ):
                self.approvals.load_pure_trend(path)

    def test_pure_poll_file_must_contain_a_poll(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "polls.csv"
            path.write_text("Party,Day\n", encoding="utf-8")
            with self.assertRaisesRegex(
                self.approvals.ApprovalDataError,
                "no voting-intention polls",
            ):
                self.approvals.load_pure_poll_days(path)

    def test_approval_confidence_factor_is_clamped_at_both_bounds(self):
        confidence_factor = self.approvals.approval_confidence_factor

        self.assertEqual(confidence_factor(-1.8), 0.0)
        self.assertEqual(confidence_factor(-0.3), 0.0)
        self.assertAlmostEqual(confidence_factor(0.2), 0.25)
        self.assertEqual(confidence_factor(0.7), 1.0)
        self.assertEqual(confidence_factor(2.0), 1.0)

    def test_diagnostic_thresholds_do_not_accumulate_previous_errors(self):
        analysis = self.approvals.Approvals.__new__(
            self.approvals.Approvals
        )
        election = ("2020", "fed")
        analysis.start_dates = {
            election: self.approvals.datetime.date(2020, 1, 1)
        }
        analysis.trends = {election: {0: 50.0, 1: 50.0, 2: 50.0}}
        analysis.synthetic_tpps = {
            election: [
                (
                    self.approvals.datetime.date(2020, 1, 1),
                    "Example",
                    49.0,
                    0.01,
                ),
                (
                    self.approvals.datetime.date(2020, 1, 2),
                    "Example",
                    47.0,
                    0.01,
                ),
                (
                    self.approvals.datetime.date(2020, 1, 3),
                    "Example",
                    42.0,
                    0.05,
                ),
            ]
        }

        output = io.StringIO()
        with redirect_stdout(output):
            analysis.analyse_synthetic_tpps()

        lines = output.getvalue().splitlines()
        self.assertEqual(lines[1], "2.0")
        # At threshold 0.1 the three unique observations average 4.0.
        self.assertEqual(lines[5], "4.0")


if __name__ == "__main__":
    unittest.main()
