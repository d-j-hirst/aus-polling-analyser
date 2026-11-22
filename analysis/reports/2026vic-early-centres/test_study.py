"""Check the recorded results, vote conservation and missing-evidence behaviour."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

from application import balanced_shares, primary_shares
from evidence import FIRST_REPORTS, HERE, read_csv
from study import primary_prediction


class EarlyCentreTest(unittest.TestCase):
    def test_published_forecast_errors_can_be_checked_without_counts(self):
        rows = read_csv(HERE / 'replay-comparisons.csv')
        self.assertEqual(set(rows[0]), {'election', 'district', 'comparison', 'actual_pct', 'first_pct',
                                        'predicted_pct', 'uniform_error_pp', 'location_error_pp'})
        for row in rows:
            actual = float(row['actual_pct'])
            for prediction, error in [('first_pct', 'uniform_error_pp'), ('predicted_pct', 'location_error_pp')]:
                self.assertAlmostEqual(abs(float(row[prediction]) - actual), float(row[error]), delta=.00002)

    def test_district_shares_and_pairwise_contrasts_survive_centring(self):
        local = np.array([[220, 780], [300, 100], [np.nan, np.nan]])
        weight = np.array([11000, 6500, 1400])
        for target in (0.01, 0.38, 0.72, 0.99):
            p = balanced_shares(local, [True, True, False], weight, [target, 1-target], 0.84)
            np.testing.assert_allclose(p.sum(axis=1), 1)
            np.testing.assert_allclose(np.average(p, weights=weight, axis=0), [target, 1-target])
            self.assertEqual(p[2, 0], target)
            odds = p[:, 0] / p[:, 1]
            local_odds = (local[:2, 0] + 0.5) / (local[:2, 1] + 0.5)
            self.assertAlmostEqual(odds[1] / odds[0], (local_odds[1] / local_odds[0]) ** 0.84)

    def test_joint_primary_reproduces_the_scored_prediction(self):
        local = np.array([[275, 340, 64, 80], [405, 306, 0, 15]])
        target = np.array([0.34, 0.37, 0.09, 0.20])
        p = primary_shares(local, [True, True], [3700, 8500], target, 0.8)
        ratio = np.log((local[1:] + 0.5) / (local[:1] + 0.5))
        np.testing.assert_allclose(primary_prediction(p[:1], ratio, 0.8, True)[0], p[1])
        np.testing.assert_allclose(np.average(p, weights=[3700, 8500], axis=0), target)
        np.testing.assert_allclose(p[:, 3], target[3])
        absent = primary_shares(local, [True, True], [1, 2], [0.4, 0.4, 0, 0.2], 0.8)
        np.testing.assert_array_equal(absent[:, 2], 0)
        np.testing.assert_allclose(absent.sum(axis=1), 1)

    def test_recorded_historical_results_reproduce_offline(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, '-B', str(HERE / 'study.py'),
                            '--historical-only', '--output', tmp], check=True, capture_output=True)
            for name in ('scores.csv', 'catchments.csv'):
                self.assertEqual((HERE / name).read_text(), (Path(tmp) / name).read_text())
            if (HERE / FIRST_REPORTS).is_file():
                name = 'replay-comparisons.csv'
                self.assertEqual((HERE / name).read_text(), (Path(tmp) / name).read_text())

    @unittest.skipUnless((HERE / 'operator/2026-inputs.csv').exists(), 'requires prepared 2026 roster')
    def test_full_roster_reproduces_and_reconciles(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, '-B', str(HERE / 'study.py'), '--output', tmp],
                           check=True, capture_output=True)
            for name in ('2026-districts.csv', 'operator/2026-centres.csv'):
                self.assertEqual((HERE / name).read_text(), (Path(tmp) / name).read_text())
            rows = read_csv(Path(tmp) / 'operator/2026-centres.csv')
            source = read_csv(HERE / 'operator/2026-inputs.csv')
            identity = lambda r: (r['district'], r['centre'])
            self.assertEqual(sorted(map(identity, rows)), sorted(map(identity, source)))
            for r in rows:
                if r['tpp_evidence'] == 'district fallback':
                    self.assertEqual(r['local_labor_tpp_pct'], '')
                    self.assertEqual(float(r['labor_tpp_delta_at_50_pp']), 0)


if __name__ == '__main__':
    unittest.main()
