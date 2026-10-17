import math
import tempfile
import unittest
from pathlib import Path

from lib.election_analysis.seat_volatility import (
    Observation, calculate, fit_seat, historical_comparisons, regularised_sd, transformed_change)
from scripts.pipeline.seat_volatility import write_parameters


def history(jurisdiction='fed'):
    return [Observation(year, jurisdiction, 'North', name, swing, swing)
            for year in (2000, 2004, 2008, 2012, 2016)
            for name, swing in [('Harbour', 2), ('Valley', -2), ('Hill', 0)]]


class SeatVolatilityTests(unittest.TestCase):
    def test_future_and_target_elections_cannot_change_parameters(self):
        rows = history()
        expected = calculate(rows, '2020fed')
        future = [Observation(y, 'fed', 'North', 'Harbour', 70, 100)
                  for y in (2020, 2024)]
        self.assertEqual(calculate(rows + future, '2020fed'), expected)
        self.assertEqual(expected['training_years'], [2000, 2004, 2008, 2012, 2016])

    def test_transform_uses_adjusted_baseline_and_preserves_direction(self):
        self.assertAlmostEqual(transformed_change(60, 5),
            25*math.log(60/40)-25*math.log(55/45))
        self.assertGreater(transformed_change(80, 5), transformed_change(55, 5))
        self.assertIsNone(transformed_change(51.4, 51.4))
        self.assertIsNone(transformed_change(60.91, -39.09))
        self.assertIsNone(transformed_change(60, None))

    def test_padding_stops_at_four_actual_observations(self):
        G = 3
        for n in range(7):
            rows = [dict(weight=.5, residual=2) for _ in range(n)]
            k = max(0, 4-n)
            expected = max(2, math.sqrt((n*.5*4+k*9+36)/(n*.5+k+1)))
            self.assertAlmostEqual(regularised_sd(rows, G), expected)
        self.assertAlmostEqual(regularised_sd([], G), math.sqrt(8/5)*G)

    def test_floor_also_applies_to_quiet_well_observed_seats(self):
        self.assertEqual(regularised_sd([dict(weight=1, residual=0)]*30, 2), 2)

    def test_disruption_is_directionless_and_does_not_change_elasticity(self):
        rows = [dict(x=i, y=2*i, weight=1, year=2000+i) for i in range(1, 5)]
        elasticity, residuals = fit_seat(rows)
        self.assertAlmostEqual(elasticity, 1.38)
        # Residuals are around the coefficient used in the forecast, not the
        # unconstrained fit of 2 which would incorrectly claim zero error.
        self.assertGreater(sum(r['residual']**2 for r in residuals), 0)
        self.assertGreater(regularised_sd(residuals, 5), regularised_sd(residuals, 2))
        self.assertEqual(fit_seat(rows)[0], elasticity)

    def test_age_tracks_elections_even_if_a_seat_has_missing_history(self):
        rows = [r for r in history() if r.seat != 'Harbour' or r.year == 2000]
        comparisons = historical_comparisons(rows, 2020)
        self.assertEqual(comparisons[('fed', 'Harbour', 'North')][0]['weight'], .5)

    def test_unknown_seats_have_a_finite_election_specific_fallback(self):
        result = calculate(history(), '2020fed')
        self.assertGreaterEqual(result['generic_sd'], 2)
        self.assertAlmostEqual(result['fallback_sd'], result['generic_sd']*math.sqrt(8/5))
        self.assertGreaterEqual(result['kurtosis'], 3)
        with self.assertRaisesRegex(ValueError, 'No earlier'):
            calculate(history(), '1990fed')

    def test_state_pool_is_used_only_for_sparse_jurisdiction_history(self):
        rows = history('vic') + [Observation(2016, 'nsw', 'none', 'Bay', 2, 2)]
        result = calculate(rows, '2020nsw')
        self.assertEqual(result['generic_source'], 'pooled states')
        self.assertEqual([s['name'] for s in result['seats']], ['Bay'])
        self.assertEqual(calculate(history('vic'), '2020vic')['generic_source'], 'vic')

    def test_runtime_file_contains_final_scales_and_exact_term(self):
        result = calculate(history(), '2020fed')
        with tempfile.TemporaryDirectory() as directory:
            paths = write_parameters(result, Path(directory))
            self.assertIn('election,2020fed\n', paths[0].read_text())
            self.assertIn('fallback_sd,', paths[0].read_text())
            self.assertTrue(paths[1].exists())


if __name__ == '__main__':
    unittest.main()
