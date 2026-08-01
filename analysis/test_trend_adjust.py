import importlib.util
import statistics
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from election_code import ElectionCode


def _load_trend_adjust_without_optional_dependencies():
    """Load calculation helpers without requiring the full Stan environment."""

    fake_numpy = types.ModuleType("numpy")
    fake_numpy.array = lambda values: list(values)
    fake_numpy.transpose = lambda columns: [
        list(row) for row in zip(*columns)
    ]
    for name in ("dot", "average", "amax", "amin"):
        setattr(fake_numpy, name, lambda *args, **kwargs: None)
    fake_numpy.median = statistics.median

    fake_scipy = types.ModuleType("scipy")
    fake_scipy_interpolate = types.ModuleType("scipy.interpolate")
    fake_scipy_interpolate.UnivariateSpline = object
    fake_sklearn = types.ModuleType("sklearn")
    fake_sklearn_linear = types.ModuleType("sklearn.linear_model")
    fake_sklearn_linear.ElasticNetCV = object
    fake_sklearn_metrics = types.ModuleType("sklearn.metrics")
    fake_sklearn_metrics.mean_squared_error = lambda *args, **kwargs: None
    fake_kurtosis = types.ModuleType("sample_kurtosis")
    fake_kurtosis.one_tail_kurtosis = lambda *args, **kwargs: None
    replacements = {
        "numpy": fake_numpy,
        "scipy": fake_scipy,
        "scipy.interpolate": fake_scipy_interpolate,
        "sklearn": fake_sklearn,
        "sklearn.linear_model": fake_sklearn_linear,
        "sklearn.metrics": fake_sklearn_metrics,
        "sample_kurtosis": fake_kurtosis,
    }

    module_path = Path(__file__).with_name("trend_adjust.py")
    spec = importlib.util.spec_from_file_location(
        "_trend_adjust_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, replacements):
        spec.loader.exec_module(module)
        module.data_module = sys.modules["trend_adjust_data"]
        module.fundamentals_module = sys.modules["trend_adjust_fundamentals"]
        module.io_module = sys.modules["trend_adjust_io"]
        module.mixing_module = sys.modules["trend_adjust_mixing"]
    return module


class TrendAdjustmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.trend_adjust = _load_trend_adjust_without_optional_dependencies()
        cls.data = cls.trend_adjust.data_module
        cls.fundamentals = cls.trend_adjust.fundamentals_module
        cls.io = cls.trend_adjust.io_module
        cls.mixing = cls.trend_adjust.mixing_module
        cls.party_groups = cls.data.PartyGroupConfig.load()

    def test_unnamed_others_is_included_exactly_once(self):
        parties = ["ALP FP", "xOTH FP", "xOTH FP"]

        result = self.data.parties_with_unnamed_others(
            parties, self.party_groups.unnamed_others_code
        )

        self.assertEqual(result, ["ALP FP", "xOTH FP"])

    def test_unnamed_others_contributes_one_training_observation(self):
        historical = ElectionCode(2023, "nsw")
        studied = ElectionCode(2027, "nsw")
        party_code = self.data.ElectionPartyCode(
            historical, "xOTH FP"
        )
        inputs = types.SimpleNamespace(
            past_elections=[historical],
            past_parties={historical: ["xOTH FP", "xOTH FP"]},
            eventual_results={party_code: 8.0},
            incumbency={historical: ("ALP FP", "LNP FP", 4.0)},
            federal_situation={historical: ("ALP FP", "LNP FP", 1.0)},
            party_groups=self.party_groups,
            safe_prior_average=lambda count, code: 5.0,
        )

        _, outcomes = self.fundamentals.build_fundamentals_training_set(
            inputs, studied, ["xOTH FP"], 1
        )

        self.assertEqual(outcomes, [3.0])

    def test_short_prior_history_uses_available_values(self):
        result = self.data.prior_result_average([10, 20, 30], 6)

        self.assertEqual(result, 20)

    def test_tpp_alignment_accounts_for_continuing_vote_denominator(self):
        election = ElectionCode(2028, "fed")
        inputs = types.SimpleNamespace(
            preference_estimates={}, party_groups=self.party_groups
        )
        predictions = {
            "ALP FP": 40.0,
            "LNP FP": 35.0,
            "GRN FP": 10.0,
            "OTH FP": 25.0,
            "@TPP": 50.0,
        }

        self.fundamentals.align_major_party_fundamentals_with_tpp(
            inputs, election, predictions
        )

        self.assertAlmostEqual(predictions["ALP FP"], 42.5)
        self.assertAlmostEqual(predictions["LNP FP"], 32.5)
        self.assertAlmostEqual(
            predictions["ALP FP"]
            / (predictions["ALP FP"] + predictions["LNP FP"] + 10.0)
            * 100,
            50.0,
        )

    def test_adjustment_day_must_be_within_saved_grid(self):
        grids = [(None, [[value] for value in range(8)])]

        with self.assertRaisesRegex(ValueError, "outside the available range"):
            self.io.adjustment_parameters_at(grids, 0, 1)

    def test_configuration_failure_returns_nonzero_status(self):
        with mock.patch.object(
            self.trend_adjust,
            "Config",
            side_effect=self.trend_adjust.ConfigError("invalid"),
        ), mock.patch("builtins.print"):
            status = self.trend_adjust.trend_adjust()

        self.assertEqual(status, 2)

    def test_generic_adjustment_uses_current_reference_year(self):
        self.assertEqual(
            self.data.adjustment_reference_year(
                self.data.no_target_election_marker,
                current_year=2026,
            ),
            2026,
        )
        self.assertEqual(
            self.data.adjustment_reference_year(
                ElectionCode(2027, "nsw"), current_year=2026
            ),
            2027,
        )

    def test_incomplete_staged_set_does_not_replace_canonical_files(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            staged = root / "staged"
            fundamentals = root / "Fundamentals"
            adjustments = root / "Adjustments"
            staged.mkdir()
            fundamentals.mkdir()
            adjustments.mkdir()
            staged_fundamentals = staged / "fundamentals_0none.csv"
            staged_fundamentals.write_text("ALP FP,40\n", encoding="utf-8")
            canonical_fundamentals = fundamentals / staged_fundamentals.name
            canonical_fundamentals.write_text("old\n", encoding="utf-8")

            with self.assertRaisesRegex(
                self.data.TrendAdjustmentDataError,
                "every party group",
            ):
                self.io.promote_staged_outputs(
                    str(staged_fundamentals),
                    {},
                    self.party_groups,
                    fundamentals_directory=fundamentals,
                    adjustments_directory=adjustments,
                )

            self.assertEqual(
                canonical_fundamentals.read_text(encoding="utf-8"), "old\n"
            )
            self.assertTrue(staged_fundamentals.exists())

    def test_complete_staged_set_replaces_canonical_files(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            staged = root / "staged"
            fundamentals = root / "Fundamentals"
            adjustments = root / "Adjustments"
            staged.mkdir()
            staged_fundamentals = staged / "fundamentals_0none.csv"
            staged_fundamentals.write_text("new fundamentals\n", encoding="utf-8")
            staged_adjustments = {}
            for group in self.party_groups.groups:
                path = staged / f"adjust_0none_{group}.csv"
                path.write_text(f"new {group}\n", encoding="utf-8")
                staged_adjustments[group] = str(path)

            promoted_fundamentals, promoted_adjustments = (
                self.io.promote_staged_outputs(
                    str(staged_fundamentals),
                    staged_adjustments,
                    self.party_groups,
                    fundamentals_directory=fundamentals,
                    adjustments_directory=adjustments,
                )
            )

            self.assertEqual(
                Path(promoted_fundamentals).read_text(encoding="utf-8"),
                "new fundamentals\n",
            )
            for group, path in promoted_adjustments.items():
                self.assertEqual(
                    Path(path).read_text(encoding="utf-8"), f"new {group}\n"
                )

    def test_mix_search_finds_best_coarse_basin_then_refines_it(self):
        evaluated = []

        def objective(factor):
            evaluated.append(factor)
            score = min(
                (factor - 0.23) ** 2,
                (factor - 0.81) ** 2 + 0.02,
            )
            return score, factor

        factor, data = self.mixing.find_best_mix(objective)

        self.assertAlmostEqual(factor, 0.23, delta=0.0001)
        self.assertEqual(data, factor)
        self.assertLessEqual(len(evaluated), 50)

    def test_mix_search_can_select_boundary(self):
        factor, _ = self.mixing.find_best_mix(
            lambda candidate: (candidate, candidate)
        )

        self.assertEqual(factor, 0.0)

    def test_mix_search_refines_second_candidate_basin(self):
        def objective(factor):
            score = min(
                (factor - 0.2) ** 2 + 0.004,
                10 * (factor - 0.83) ** 2,
            )
            return score, factor

        factor, _ = self.mixing.find_best_mix(objective)

        # The 0.2 basin looks better on the 0.1 grid, but refinement reveals
        # the narrower 0.83 basin as the true minimum.
        self.assertAlmostEqual(factor, 0.83, delta=0.0001)


if __name__ == "__main__":
    unittest.main()
