import math
import os
import unittest

import fp_model


@unittest.skipUnless(
    os.environ.get("FP_MODEL_STAN_SMOKE") == "1",
    "set FP_MODEL_STAN_SMOKE=1 to compile and sample the production model",
)
class ProductionStanSmokeTests(unittest.TestCase):
    def test_small_seeded_fit_returns_finite_adjusted_vote_share(self):
        model = fp_model.load_stan_model()
        fit = model.sampling(
            data={
                "pollCount": 1,
                "dayCount": 6,
                "houseCount": 1,
                "discontinuityCount": 1,
                "pollObservations": [5.0],
                "missingObservations": [0],
                "pollHouse": [1],
                "pollDay": [3],
                "sigmas": [2.0],
                "heWeights": [1.0],
                "biases": [0.0],
                "priorSeries": [5.0] * 6,
                "priorVoteShareSigma": [20.0] * 6,
                "dailySigma": 0.25,
                "campaignSigma": 0.45,
                "finalSigma": 0.7,
                "campaignStartDay": 1,
                "finalStartDay": 3,
                "discontinuities": [0],
                "houseEffectSigma": 1.2,
                "houseEffectSumSigma": 0.001,
                "houseEffectNew": 2,
                "houseEffectOld": 4,
            },
            iter=40,
            warmup=20,
            chains=1,
            n_jobs=1,
            seed=12345,
            control={"adapt_delta": 0.8, "max_treedepth": 12},
            refresh=0,
        )
        values = fit.extract("adjustedVoteShare")[
            "adjustedVoteShare"
        ]
        self.assertTrue(values.size)
        self.assertTrue(
            all(math.isfinite(float(value)) for value in values.ravel())
        )


if __name__ == "__main__":
    unittest.main()
