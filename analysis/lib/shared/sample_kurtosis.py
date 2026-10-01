"""Validate samples and calculate the dispersion measures used by models.

This module contains no pipeline loading or output code. It centralises the
finite-value and effective-sample checks so numerical stages can rely on
well-defined RMSE and tail-kurtosis estimates.

Main functions:
* ``calc_rmse`` calculates sample dispersion around a nominated centre.
* ``one_tail_kurtosis`` estimates a weighted one-sided tail shape.
* ``two_tail_kurtosis`` estimates ordinary two-sided sample kurtosis.
"""

import math
import numpy as np


NORMAL_KURTOSIS = 3.0


def _finite_values(values, description, minimum_size=1):
    """Return finite numeric values, rejecting underspecified samples early."""

    values = list(values)
    if len(values) < minimum_size:
        raise ValueError(
            f"{description} must contain at least {minimum_size} value(s)."
        )
    try:
        finite_values = all(math.isfinite(value) for value in values)
    except TypeError as error:
        raise ValueError(
            f"{description} must contain only finite numeric values."
        ) from error
    if not finite_values:
        raise ValueError(f"{description} must contain only finite values.")
    return values


def calc_rmse(sample, center=0):
    """Return the sample RMS about ``center`` using an n - 1 denominator."""

    values = _finite_values(sample, "RMSE sample", minimum_size=2)
    try:
        finite_center = math.isfinite(center)
    except TypeError as error:
        raise ValueError("RMSE center must be a finite numeric value.") from error
    if not finite_center:
        raise ValueError("RMSE center must be finite.")
    return math.sqrt(sum((value - center) ** 2 for value in values)
                     / (len(values) - 1))

# Calculates kurtosis for one tail of a sampled distribution
# Note: this assessment assumes the mean is 0 (as the
# calculation is being made for one tail of a distribution,
# the mean is not actually being calculated)
def one_tail_kurtosis(sample, weights=None, weight_scale=1):
    """Estimate Pearson kurtosis about zero for one tail of a distribution.

    The values are deliberately not centred: callers provide distances from a
    shared zero point, such as a median error or an emergence threshold. A
    An all-zero tail uses normal kurtosis because its downstream scale is
    zero and the shape parameter is consequently immaterial.
    """

    values = _finite_values(sample, "one-tail kurtosis sample")
    if weights is None:
        weights = [1.0 for _ in values]
    else:
        weights = list(weights)
        if len(weights) != len(values):
            raise ValueError(
                "One-tail kurtosis weights must have the same length as "
                "the sample."
            )
        weights = _finite_values(weights, "one-tail kurtosis weights")
    if any(weight < 0.0 for weight in weights):
        raise ValueError("One-tail kurtosis weights must be non-negative.")
    try:
        valid_weight_scale = (
            math.isfinite(weight_scale) and weight_scale > 0.0
        )
    except TypeError as error:
        raise ValueError(
            "One-tail kurtosis weight scale must be a finite numeric value."
        ) from error
    if not valid_weight_scale:
        raise ValueError(
            "One-tail kurtosis weight scale must be positive and finite."
        )

    frequency_weights = [weight / weight_scale for weight in weights]
    effective_sample_size = sum(frequency_weights)
    if effective_sample_size <= 0.0:
        raise ValueError("One-tail kurtosis requires a positive total weight.")

    numerator = sum(
        value ** 4 * weight
        for value, weight in zip(values, frequency_weights))
    second_moment_sum = sum(
        value ** 2 * weight
        for value, weight in zip(values, frequency_weights))
    if second_moment_sum == 0.0:
        return NORMAL_KURTOSIS

    # Trend-adjustment priors contribute weight 150 with a scale of 50, so a
    # tail supported only by its prior has effective size 3. The correction
    # needs n >= 4; preserve that established prior-based minimum explicitly.
    n = max(4.0, effective_sample_size)
    denominator = second_moment_sum ** 2
    sample_size_corrected = (n * (n + 1) * (n - 1)) / ((n - 2) * (n - 3))
    kurtosis_estimate = numerator * sample_size_corrected / denominator
    if not math.isfinite(kurtosis_estimate):
        raise ValueError(
            "One-tail kurtosis calculation produced a non-finite value."
        )
    return kurtosis_estimate


def two_tail_kurtosis(sample):
    """Estimate bias-corrected Pearson kurtosis after centring the sample."""

    values = _finite_values(
        sample, "two-tail kurtosis sample", minimum_size=4
    )
    values_array = np.array(values)
    residuals = values_array - np.mean(values_array)
    s2 = np.mean(residuals ** 2)  # Variance
    if s2 == 0.0:
        return NORMAL_KURTOSIS
    m4 = np.mean(residuals ** 4)  # Fourth central moment
    n = len(values)

    # Kurtosis formula with bias correction
    kurtosis = 1.0 /(n - 2)/(n - 3) * ((n**2 - 1.0)* m4 / s2 ** 2.0 - 3 * (n - 1) ** 2.0)
    kurtosis += NORMAL_KURTOSIS
    if not math.isfinite(kurtosis):
        raise ValueError(
            "Two-tail kurtosis calculation produced a non-finite value."
        )
    return kurtosis
