"""Test election exclusion and regional uncertainty with fictional elections."""

import copy
import csv
import math
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import numpy as np

from lib.election_analysis import regional_calibration as regional


def fictional_evidence():
    elections, history = {}, {}
    for index, year in enumerate([2000, 2003, 2006, 2009, 2012]):
        national = [-2, 1, 3, -1, 2][index]
        local = [1, -2, 2, 0, -1][index]
        rows = {}
        history[year] = {"all": national}
        for state, direction in [("east", 1), ("west", -1)]:
            history[year][state] = national + direction * local
            previous = 48 + direction
            actual = previous + national + direction * local
            rows[state] = [previous, actual, 10000,
                           previous + national + direction * (0.4 * local + 0.3),
                           previous + national + direction * (0.2 * local - 0.2),
                           previous + national]
        elections[year] = regional.prepare_election(rows)
    return dict(history=history, elections=elections)


class RegionalCalibrationTests(unittest.TestCase):
    def test_consumer_mean_fixtures(self):
        # Matched by RegionalSwingMathTests.cpp, including nonzero intercepts.
        relationship = dict(base={"east": np.array([1., 0.])},
                            poll={"east": np.array([.8, 1.]), "all": np.array([.4, -.2])})
        election = dict(states=["east"], national=2., deviations=np.array([[3., 0., -3.]]))
        naive, polled = regional.prediction_components(relationship, election)
        np.testing.assert_allclose(naive, [0.])
        np.testing.assert_allclose(polled, [[2.2, .4, -1.4]])

    def test_excluded_election_removed_from_every_training_stage(self):
        evidence = fictional_evidence()
        training = regional.without_election(evidence, 2006)
        original = regional.fit_relationships
        seen = []

        def checked_fit(data):
            self.assertNotIn(2006, data["history"])
            self.assertNotIn(2006, data["elections"])
            seen.append(set(data["elections"]))
            return original(data)

        with mock.patch.object(regional, "fit_relationships", side_effect=checked_fit):
            predictor = regional.fit_predictor(training)
        self.assertEqual(len(seen), 5)  # Four inner exclusions and final fit.
        self.assertEqual(predictor["training_years"], [2000, 2003, 2009, 2012])
        self.assertTrue(all(len(years) == 3 for years in seen[:-1]))

    def test_test_election_outcomes_cannot_choose_its_polling_weight(self):
        evidence = fictional_evidence()
        first = regional.prediction_errors(evidence)
        changed = copy.deepcopy(evidence)
        changed["history"][2006]["east"] += 15
        changed["history"][2006]["west"] -= 15
        changed["elections"][2006]["swing"] += np.array([15., -15.])
        second = regional.prediction_errors(changed)
        old = [s for s in first if s["year"] == 2006]
        new = [s for s in second if s["year"] == 2006]
        for a, b in zip(old, new):
            np.testing.assert_array_equal(a["mix"], b["mix"])
            self.assertFalse(np.allclose(a["errors"], b["errors"]))
        self.assertTrue(all(s["year"] not in s["training_years"] for s in second))

    def test_common_national_error_is_removed(self):
        evidence = fictional_evidence()
        election = evidence["elections"][2006]
        components = regional.prediction_components(regional.fit_relationships(evidence), election)
        error, jacobian = regional.reconciled_prediction(components, election, 0, [.7, .1])
        self.assertAlmostEqual(float(election["weights"] @ error), 0)
        np.testing.assert_allclose(election["weights"] @ jacobian, 0, atol=1e-12)

    def test_reconciled_variance_matches_independent_draws(self):
        election = regional.prepare_election({
            "a": [48, 50, 50000, 49], "b": [52, 53, 30000, 53], "c": [43, 45, 20000, 46]})
        components = (np.zeros(3), np.array([[0.], [1.], [-1.]]))
        _, jacobian = regional.reconciled_prediction(components, election, 0, [.6, .1])
        sample = dict(states=election["states"], jacobian=jacobian)
        modifiers = dict(a=.8, b=1.1, c=1.3)
        rng = np.random.default_rng(100)
        draws = rng.normal(size=(100000, 3)) * [.8, 1.1, 1.3]
        actual_variance = np.var(draws @ jacobian.T, axis=0)
        np.testing.assert_allclose(actual_variance, regional.projected_variance(sample, modifiers), rtol=.02)

    def test_duplicate_horizons_do_not_count_as_more_elections(self):
        samples = []
        for year, error in [(2000, 1.), (2003, 2.), (2006, 1.5)]:
            samples.append(dict(year=year, horizon=0, states=["a", "b"], errors=np.array([error, 0.1]), jacobian=np.eye(2)))
        once = regional.pooled_modifiers(samples, {0: 1.})
        many = regional.pooled_modifiers(samples * 20, {0: 1.})
        for state in once:
            self.assertAlmostEqual(once[state], many[state])
        self.assertGreater(once["b"], .85)  # Very quiet state still borrows variance.
        self.assertLess(once["a"], 1.3)     # Noisy state is moderated too.

    def test_scale_curve_preserves_day_zero_and_is_non_decreasing(self):
        parameters = regional.smooth_scales({0: 2.2, 1: 1.8, 2: 2.5, 4: 2.1})
        a, b, c = parameters
        values = [a * math.exp(-b * h) + c for h in [0, .1, 1, 4, 40]]
        self.assertAlmostEqual(values[0], 2.2)
        self.assertEqual(values, sorted(values))
        self.assertGreater(min(values), 0)

    def test_future_elections_are_not_loaded(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            history, polls = [], []
            for year in [2000, 2003, 2006, 2009, 2012, 2015]:
                history.extend([[year, "fed", "all", 50, 1], [year, "fed", "a", 50, 1], [year, "fed", "b", 50, 1]])
                polls.extend([[year, "fed", "a", 49, 50, 100, 51], [year, "fed", "b", 49, 50, 100, 49]])
            for name, rows in [("tpp-fed-regions.csv", history), ("region-polls-fed.csv", polls)]:
                with (directory / name).open("w", newline="") as output:
                    csv.writer(output).writerows(rows)
            evidence = regional.load_evidence(2012, directory)
            self.assertEqual(set(evidence["history"]), {2000, 2003, 2006, 2009})
            self.assertEqual(set(evidence["elections"]), {2000, 2003, 2006, 2009})

    def test_complete_calibration_exports_finite_consumer_files(self):
        calibration = regional.calibrate_regions(fictional_evidence())
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            regional.write_parameters(calibration, 2022, directory)
            self.assertEqual(len(list(directory.glob("*.csv"))), 4)
            for path in directory.glob("*.csv"):
                with path.open() as source:
                    for row in csv.reader(source):
                        self.assertTrue(all(math.isfinite(float(v)) for v in row[1:]))
            with (directory / "2022fed-mix-regions.csv").open() as source:
                self.assertTrue(all(float(row[1]) == 0 for row in csv.reader(source)))

    def test_standalone_provenance_certifies_only_selected_regional_outputs(self):
        from scripts.pipeline import regional_analysis
        provenance = regional_analysis.generated_provenance
        # Other seat/Coalition records share the manifest. A regional-only run
        # must neither replace nor claim to have regenerated those outputs.
        with mock.patch.object(provenance, "file_dependency", return_value={}), \
                mock.patch.object(provenance, "current_source_revision", return_value={}), \
                mock.patch.object(provenance, "generation_run", return_value=("test", {})), \
                mock.patch.object(provenance, "generation_record", side_effect=lambda **kw: kw), \
                mock.patch.object(provenance, "output_fingerprints", return_value={}), \
                mock.patch.object(provenance, "update_manifest") as update:
            regional_analysis.record_outputs(["2022fed"])
        records = update.call_args.args[1]
        self.assertEqual(list(records), ["federal_regional_statistics:2022fed"])
        self.assertEqual(records["federal_regional_statistics:2022fed"]["scope"]["elections"], ["2022fed"])


if __name__ == "__main__":
    unittest.main()
