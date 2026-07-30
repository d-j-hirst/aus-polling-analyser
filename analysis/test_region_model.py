import csv
import contextlib
import importlib.util
import io
import math
import sys
import types
import unittest
from pathlib import Path
from unittest import mock

from election_code import ElectionCode


def load_region_model():
    """Load orchestration helpers without importing numerical packages."""

    pandas = types.ModuleType("pandas")
    pandas.isna = lambda value: (
        value is None
        or isinstance(value, float) and math.isnan(value)
    )
    stan_cache = types.ModuleType("stan_cache")
    stan_cache.stan_cache = object

    module_path = Path(__file__).with_name("region_model.py")
    spec = importlib.util.spec_from_file_location(
        "region_model_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(
        sys.modules,
        {
            "pandas": pandas,
            "stan_cache": stan_cache,
        },
    ):
        spec.loader.exec_module(module)
    return module


class SummaryValues:
    def __init__(self, values):
        self.values = values

    def tolist(self):
        return self.values


class FakeFit:
    def __init__(self, row_names, values):
        self.row_names = row_names
        self.values = values

    def summary(self, probs):
        return {
            "summary_rownames": self.row_names,
            "summary": SummaryValues(self.values),
        }


class RegionModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = load_region_model()

    def test_nsw_baseline_rejects_fraction_scaled_regions(self):
        contract = self.model.model_contract(
            ElectionCode(2027, "nsw")
        )
        baseline = {
            "State": 54.7383,
            "Metro": 0.555322752,
            "Regional": 0.525140711,
        }

        with self.assertRaisesRegex(
            self.model.ConfigError, "0-100 scale"
        ):
            self.model.validate_election_baseline(
                baseline, contract, "2027nsw-polls.csv"
            )

    def test_nsw_baseline_accepts_percentage_scaled_regions(self):
        contract = self.model.model_contract(
            ElectionCode(2027, "nsw")
        )
        baseline = {
            "State": 54.7383,
            "Metro": 55.5322752,
            "Regional": 52.5140711,
        }

        self.model.validate_election_baseline(
            baseline, contract, "2027nsw-polls.csv"
        )

    def test_qld_2024_requires_complete_csv_baseline(self):
        contract = self.model.model_contract(
            ElectionCode(2024, "qld")
        )
        self.assertTrue(contract["requires_baseline"])

        input_path = (
            Path(__file__).with_name("Regional")
            / "2024qld-polls.csv"
        )
        with input_path.open(encoding="utf-8-sig", newline="") as file:
            rows = list(csv.DictReader(file))
        baseline = next(
            row for row in rows if row["Firm"].casefold() == "election"
        )

        self.model.validate_election_baseline(
            baseline, contract, str(input_path)
        )

    def test_latest_means_are_selected_by_parameter_name(self):
        fit = FakeFit(
            [
                "regionalSwingDev[2]",
                "metroSwingDev[1]",
                "metroSwingDev[2]",
            ],
            [
                [-1.25, 0.1],
                [99.0, 0.1],
                [0.75, 0.1],
            ],
        )

        self.assertEqual(
            self.model.latest_parameter_means(
                fit,
                ["metroSwingDev", "regionalSwingDev"],
                day_count=2,
            ),
            [0.75, -1.25],
        )

    def test_main_returns_failure_for_configuration_errors(self):
        with mock.patch.object(
            self.model,
            "run_models",
            side_effect=self.model.ConfigError("invalid input"),
        ):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(self.model.main(), 2)


if __name__ == "__main__":
    unittest.main()
