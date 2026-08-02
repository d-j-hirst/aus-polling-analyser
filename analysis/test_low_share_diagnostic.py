import unittest

import numpy as np

import low_share_diagnostic as diagnostic


class ExponentialTailTransformTests(unittest.TestCase):
    def test_transform_matches_existing_low_share_examples(self):
        self.assertAlmostEqual(
            diagnostic.exponential_tail_share(0.0),
            0.5 * np.exp(-0.5),
        )
        self.assertAlmostEqual(
            diagnostic.exponential_tail_share(-1.0),
            0.5 * np.exp(-1.5),
        )
        self.assertEqual(diagnostic.exponential_tail_share(0.5), 0.5)
        self.assertEqual(diagnostic.exponential_tail_share(10.0), 10.0)
        self.assertEqual(diagnostic.exponential_tail_share(99.5), 99.5)

    def test_upper_and_lower_tails_are_symmetric(self):
        for latent_share in (-5.0, 0.0, 0.49, 20.0):
            self.assertAlmostEqual(
                diagnostic.exponential_tail_share(100.0 - latent_share),
                100.0
                - diagnostic.exponential_tail_share(latent_share),
            )

    def test_inverse_round_trip_covers_tails_and_identity_region(self):
        shares = np.array([
            0.000001,
            0.1,
            0.5,
            20.0,
            99.5,
            99.9,
            99.999999,
        ])

        latent = diagnostic.exponential_tail_inverse(shares)
        reconstructed = diagnostic.exponential_tail_share(latent)

        np.testing.assert_allclose(reconstructed, shares, rtol=1e-9)

    def test_inverse_rejects_closed_boundaries(self):
        for share in (0.0, 100.0, float("nan")):
            with self.assertRaisesRegex(ValueError, "strictly between"):
                diagnostic.exponential_tail_inverse(share)

    def test_log_jacobian_retains_existing_boundary_kink(self):
        self.assertAlmostEqual(
            diagnostic.exponential_tail_log_jacobian(0.0),
            np.log(0.5) - 0.5,
        )
        self.assertEqual(
            diagnostic.exponential_tail_log_jacobian(0.5),
            0.0,
        )
        self.assertEqual(
            diagnostic.exponential_tail_log_jacobian(50.0),
            0.0,
        )


class SmoothLogitTransformTests(unittest.TestCase):
    def test_zero_latent_is_fifty_percent(self):
        self.assertEqual(diagnostic.smooth_logit_share(0.0), 50.0)

    def test_inverse_round_trip(self):
        shares = np.array([0.0001, 0.1, 10.0, 50.0, 90.0, 99.9])

        latent = diagnostic.smooth_logit_inverse(shares)
        reconstructed = diagnostic.smooth_logit_share(latent)

        np.testing.assert_allclose(reconstructed, shares, rtol=1e-11)

    def test_transform_is_stable_for_extreme_latent_values(self):
        transformed = diagnostic.smooth_logit_share(
            np.array([-1000.0, 1000.0])
        )

        np.testing.assert_array_equal(transformed, [0.0, 100.0])
        log_jacobian = diagnostic.smooth_logit_log_jacobian(
            np.array([-1000.0, 1000.0])
        )
        self.assertTrue(np.all(np.isfinite(log_jacobian)))

    def test_inverse_rejects_closed_boundaries(self):
        for share in (0.0, 100.0, float("inf")):
            with self.assertRaisesRegex(ValueError, "strictly between"):
                diagnostic.smooth_logit_inverse(share)


class DiagnosticDataTests(unittest.TestCase):
    def test_build_stan_data_broadcasts_one_poll_sigma(self):
        data = diagnostic.build_stan_data(
            approach="exponential",
            prior_mean=0.25,
            prior_sigma=2.0,
            poll_values=[0.1, 0.2, 0.3],
            poll_sigmas=1.5,
        )

        self.assertEqual(data["approach"], 2)
        self.assertEqual(data["pollCount"], 3)
        self.assertEqual(data["pollSigmas"], [1.5, 1.5, 1.5])

    def test_build_stan_data_rejects_invalid_synthetic_data(self):
        with self.assertRaisesRegex(ValueError, "0 to 100"):
            diagnostic.build_stan_data(
                approach="raw",
                prior_mean=0.25,
                prior_sigma=2.0,
                poll_values=[-0.1],
                poll_sigmas=[1.5],
            )
        with self.assertRaisesRegex(ValueError, "match poll_values"):
            diagnostic.build_stan_data(
                approach="logit",
                prior_mean=0.25,
                prior_sigma=2.0,
                poll_values=[0.1, 0.2],
                poll_sigmas=[1.0, 2.0, 3.0],
            )

    def test_dispatchers_match_named_transforms(self):
        self.assertEqual(
            diagnostic.transform_share(-1.0, "exponential"),
            diagnostic.exponential_tail_share(-1.0),
        )
        self.assertEqual(
            diagnostic.inverse_transform_share(50.0, "logit"),
            0.0,
        )
        with self.assertRaisesRegex(ValueError, "approach"):
            diagnostic.transform_share(1.0, "unknown")


if __name__ == "__main__":
    unittest.main()
