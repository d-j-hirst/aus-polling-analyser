import unittest


try:
    import sample_kurtosis
except ModuleNotFoundError as error:
    sample_kurtosis = None
    NUMERICAL_DEPENDENCIES_AVAILABLE = False
    NUMERICAL_DEPENDENCY_ERROR = str(error)
else:
    NUMERICAL_DEPENDENCIES_AVAILABLE = True
    NUMERICAL_DEPENDENCY_ERROR = ""


@unittest.skipUnless(
    NUMERICAL_DEPENDENCIES_AVAILABLE,
    f"numerical dependencies unavailable: {NUMERICAL_DEPENDENCY_ERROR}",
)
class SampleKurtosisTests(unittest.TestCase):
    def test_rmse_rejects_small_or_nonfinite_samples(self):
        with self.assertRaisesRegex(ValueError, "at least 2"):
            sample_kurtosis.calc_rmse([1.0])
        with self.assertRaisesRegex(ValueError, "finite"):
            sample_kurtosis.calc_rmse([1.0, float("nan")])
        with self.assertRaisesRegex(ValueError, "numeric"):
            sample_kurtosis.calc_rmse([1.0, "not a number"])

    def test_one_tail_rejects_mismatched_or_invalid_weights(self):
        with self.assertRaisesRegex(ValueError, "same length"):
            sample_kurtosis.one_tail_kurtosis([1.0, 2.0], [1.0])
        with self.assertRaisesRegex(ValueError, "non-negative"):
            sample_kurtosis.one_tail_kurtosis([1.0], [-1.0])
        with self.assertRaisesRegex(ValueError, "positive total weight"):
            sample_kurtosis.one_tail_kurtosis([1.0], [0.0])

    def test_one_tail_all_zero_uses_neutral_kurtosis(self):
        self.assertEqual(
            sample_kurtosis.one_tail_kurtosis([0.0, 0.0]),
            sample_kurtosis.NORMAL_KURTOSIS,
        )

    def test_one_tail_prior_only_effective_size_remains_supported(self):
        kurtosis = sample_kurtosis.one_tail_kurtosis(
            [1.0], weights=[150.0], weight_scale=50.0
        )
        self.assertEqual(kurtosis, 10.0)

    def test_two_tail_centres_inputs_and_handles_zero_variance(self):
        baseline = sample_kurtosis.two_tail_kurtosis([1.0, 2.0, 3.0, 4.0])
        shifted = sample_kurtosis.two_tail_kurtosis([11.0, 12.0, 13.0, 14.0])
        self.assertAlmostEqual(baseline, shifted)
        self.assertEqual(
            sample_kurtosis.two_tail_kurtosis([2.0, 2.0, 2.0, 2.0]),
            sample_kurtosis.NORMAL_KURTOSIS,
        )

    def test_two_tail_rejects_small_or_nonfinite_samples(self):
        with self.assertRaisesRegex(ValueError, "at least 4"):
            sample_kurtosis.two_tail_kurtosis([1.0, 2.0, 3.0])
        with self.assertRaisesRegex(ValueError, "finite"):
            sample_kurtosis.two_tail_kurtosis(
                [1.0, 2.0, 3.0, float("inf")]
            )


if __name__ == "__main__":
    unittest.main()
