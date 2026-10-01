import unittest

import numpy as np

import prior_chain_diagnostic as diagnostic


class ExactGaussianPosteriorTests(unittest.TestCase):
    def test_two_day_solution_matches_hand_calculation(self):
        posterior = diagnostic.exact_gaussian_posterior(
            prior_means=[0.0, 0.0],
            prior_sigmas=[1.0, 1.0],
            transition_sigmas=[1.0],
            poll_days=[1],
            poll_values=[3.0],
            poll_sigmas=[1.0],
        )

        np.testing.assert_allclose(
            posterior.mean,
            [1.2, 0.6],
            atol=1e-12,
        )
        np.testing.assert_allclose(
            posterior.covariance,
            [[0.4, 0.2], [0.2, 0.6]],
            atol=1e-12,
        )

    def test_repeated_poll_day_adds_independent_precision(self):
        posterior = diagnostic.exact_gaussian_posterior(
            prior_means=[10.0],
            prior_sigmas=[2.0],
            transition_sigmas=[],
            poll_days=[1, 1],
            poll_values=[14.0, 6.0],
            poll_sigmas=[2.0, 2.0],
        )

        self.assertAlmostEqual(float(posterior.mean[0]), 10.0)
        self.assertAlmostEqual(
            float(posterior.covariance[0, 0]),
            4.0 / 3.0,
        )

    def test_constant_prior_mean_is_preserved_without_polls(self):
        posterior = diagnostic.exact_gaussian_posterior(
            prior_means=[4.25] * 7,
            prior_sigmas=16.0,
            transition_sigmas=0.25,
        )

        np.testing.assert_allclose(posterior.mean, [4.25] * 7, atol=1e-11)
        np.testing.assert_allclose(
            posterior.covariance,
            posterior.covariance.T,
            atol=1e-12,
        )
        self.assertTrue(
            np.all(np.linalg.eigvalsh(posterior.covariance) > 0.0)
        )

    def test_endpoint_poll_influence_weakens_with_distance(self):
        scenario = diagnostic.make_scenario(
            chain_length=9,
            poll_position="first",
            prior_sigma=1.0,
            transition_sigma=1.0,
            poll_offset=10.0,
            poll_sigma=1.0,
        )
        posterior = diagnostic.solve_scenario(scenario)

        self.assertGreater(posterior.mean[0], posterior.mean[1])
        self.assertGreater(posterior.mean[1], posterior.mean[2])
        self.assertGreater(posterior.mean[2], posterior.mean[-1])

    def test_invalid_scales_and_poll_days_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "positive"):
            diagnostic.exact_gaussian_posterior(
                prior_means=[0.0, 0.0],
                prior_sigmas=[1.0, 0.0],
                transition_sigmas=[1.0],
            )
        with self.assertRaisesRegex(ValueError, "between 1 and day_count"):
            diagnostic.exact_gaussian_posterior(
                prior_means=[0.0, 0.0],
                prior_sigmas=[1.0, 1.0],
                transition_sigmas=[1.0],
                poll_days=[0],
                poll_values=[1.0],
                poll_sigmas=[1.0],
            )


class ScenarioTests(unittest.TestCase):
    def test_poll_positions_use_stan_one_based_days(self):
        self.assertEqual(diagnostic.poll_day_for_position(10, "first"), 1)
        self.assertEqual(diagnostic.poll_day_for_position(10, "middle"), 5)
        self.assertEqual(diagnostic.poll_day_for_position(10, "last"), 10)
        self.assertIsNone(diagnostic.poll_day_for_position(10, "none"))

    def test_prior_only_scenario_has_no_synthetic_poll(self):
        data = diagnostic.make_scenario(5, "none").stan_data()

        self.assertEqual(data["pollCount"], 0)
        self.assertEqual(data["pollDays"], [])
        self.assertEqual(data["pollValues"], [])
        self.assertEqual(data["pollSigmas"], [])

    def test_boundary_convergence_uses_longest_interior_as_reference(self):
        rows = diagnostic.boundary_convergence(
            [5, 15, 31],
            prior_sigma=4.0,
            transition_sigma=1.0,
        )

        self.assertEqual([row["chain_length"] for row in rows], [5, 15, 31])
        self.assertGreater(rows[0]["left_to_interior_sd"], 1.0)
        self.assertGreater(rows[-1]["right_to_interior_sd"], 1.0)
        self.assertAlmostEqual(
            rows[-1]["interior_sd_relative_difference_from_longest"],
            0.0,
        )
        self.assertGreater(
            rows[0]["interior_sd"],
            rows[-1]["interior_sd"],
        )

    def test_distance_profile_reports_both_boundaries(self):
        posterior = diagnostic.solve_scenario(
            diagnostic.make_scenario(5, "first")
        )
        profile = diagnostic.distance_profile(posterior)
        self.assertEqual(
            (
                profile[0]["distance_from_left"],
                profile[0]["distance_from_right"],
            ),
            (0, 4),
        )
        self.assertEqual(
            (
                profile[-1]["distance_from_left"],
                profile[-1]["distance_from_right"],
            ),
            (4, 0),
        )

    def test_linear_centre_solver_matches_dense_exact_solution(self):
        posterior = diagnostic.exact_gaussian_posterior(
            prior_means=[0.0] * 31,
            prior_sigmas=16.0,
            transition_sigmas=0.25,
        )
        centre_variance = diagnostic._homogeneous_chain_centre_variance(
            31, 16.0, 0.25
        )
        self.assertAlmostEqual(
            centre_variance,
            posterior.covariance[15, 15],
            places=10,
        )

    def test_asymptotic_threshold_reaches_exact_infinite_limit(self):
        result = diagnostic.chain_length_to_asymptotic_interior(
            prior_sigma=4.0,
            transition_sigma=1.0,
            relative_tolerance=0.01,
        )
        self.assertGreaterEqual(result["chain_length"], 3)
        self.assertLessEqual(result["relative_difference"], 0.01)
        self.assertAlmostEqual(
            result["infinite_chain_standard_deviation"],
            diagnostic.infinite_chain_interior_standard_deviation(4.0, 1.0),
        )


if __name__ == "__main__":
    unittest.main()
