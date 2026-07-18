import math
import numpy as np
from scipy.stats import moment

def calc_rmse(sample, center=0):
    return math.sqrt(sum([(a - center) ** 2 for a in sample])
                     / (len(sample) - 1))

# Calculates kurtosis for one tail of a sampled distribution
# Note: this assessment assumes the mean is 0 (as the
# calculation is being made for one tail of a distribution,
# the mean is not actually being calculated)
def one_tail_kurtosis(sample, weights=None, weight_scale=1):
    if weights is None:
        weights = [1 for _ in sample]
    frequency_weights = [weight / weight_scale for weight in weights]
    numerator = sum(
        value ** 4 * weight
        for value, weight in zip(sample, frequency_weights))
    n = max(4, sum(frequency_weights))
    denominator = sum(
        value ** 2 * weight
        for value, weight in zip(sample, frequency_weights)) ** 2
    sample_size_corrected = (n * (n + 1) * (n - 1)) / ((n - 2) * (n - 3))
    kurtosis_estimate = numerator * sample_size_corrected / denominator
    return kurtosis_estimate

def two_tail_kurtosis(residuals):
    n = len(residuals)
    if n < 4:
        raise ValueError("Sample size must be at least 4 for kurtosis calculation.")

    s2 = np.mean(np.array(residuals) ** 2)  # Variance
    m4 = np.mean(np.array(residuals) ** 4)  # Fourth moment

    # Kurtosis formula with bias correction
    kurtosis = 1.0 /(n - 2)/(n - 3) * ((n**2 - 1.0)* m4 / s2 ** 2.0 - 3 * (n - 1) ** 2.0)
    return kurtosis + 3
