"""Fictional checks for the covariance calibration, without downloads or fitting."""
import unittest
import numpy as np
from scripts.diagnostics import calibrate_tpp_composition as model


class CompositionCalibrationTests(unittest.TestCase):
    def test_distinct_and_grouped_channels(self):
        fed = model.definition('fed', 2010)
        np.testing.assert_array_equal([r[1] for r in fed], np.eye(4))
        vic = model.definition('vic', 2018)
        np.testing.assert_array_equal(vic[0][1], [.5,.5,0,0])
        self.assertGreater(np.sum(vic[0][2]**2), 0)
        np.testing.assert_array_equal(vic[1][1], [0,0,1,0])
        nsw = model.definition('nsw', 2019)
        np.testing.assert_array_equal(nsw[1][1], [0,.5,.5,0])

    def test_missing_categories_are_not_zero_observations(self):
        accounts = {code: {f'Seat {i}': {'ordinary': np.array([50,50]), 'early': np.array([40,60]),
                                        'postal': np.array([30,70])} for i in range(35)}
                    for code in ('2019fed','2022fed')}
        self.assertIsNone(model.election_sample(accounts, '2019fed', '2022fed'))

    def test_unknown_split_retains_variance(self):
        groups = model.definition('vic', 2018)
        operator = model.moment_operator(groups)
        diagonal = np.array([4 if i == j else 0 for i, j in model.INDEX])
        # Average of independent variances is 2; uncertain mix adds another 2/3.
        self.assertAlmostEqual((operator@diagonal)[0], 8/3)

    def test_small_samples_are_regularised_and_positive_definite(self):
        groups = model.definition('fed', 2010)
        y = np.array([1,-2,4,-3])
        sample = dict(groups=groups, shared=np.outer(y,y), local=np.outer(y,y)*4)
        for kind, variance in (('shared',4),('local',16)):
            covariance = model.estimate([sample], kind, variance)
            self.assertTrue(np.all(np.linalg.eigvalsh(covariance)>0))
            self.assertLess(np.max(np.abs(covariance-np.eye(4)*variance)),variance)
            np.testing.assert_allclose(model.estimate([],kind,variance),np.eye(4)*variance)

    def test_opposite_signed_elections_have_identical_zero_mean_covariance(self):
        groups = model.definition('fed', 2010)
        y = np.array([2,1,-2,3])
        a = dict(groups=groups,shared=np.outer(y,y))
        b = dict(groups=groups,shared=np.outer(-y,-y))
        np.testing.assert_allclose(model.estimate([a],'shared',4),model.estimate([b],'shared',4))


if __name__ == '__main__':
    unittest.main()
