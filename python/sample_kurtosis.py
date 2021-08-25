

# Calculates kurtosis for one tail of a sampled distribution
# Note: this assessment assumes the mean is 0 (as the
# calculation is being made for one tail of a distribution,
# the mean is not actually being calculated)
def one_tail_kurtosis(sample):
    numerator = sum([a ** 4 for a in sample])
    n = max(4, len(sample))
    denominator = sum([a ** 2 for a in sample]) ** 2
    sample_size_corrected = (n * (n + 1) * (n - 1)) / ((n - 2) * (n - 3))
    kurtosis_estimate = numerator * sample_size_corrected / denominator
    return kurtosis_estimate