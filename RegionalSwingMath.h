#pragma once

#include <algorithm>
#include <cmath>

// Federal regional calibration uses the same mean rule in Python's
// regional_calibration.py. Keep the ordinary polling path partly pooled: a
// handful of elections cannot establish precise state-specific coefficients.
namespace RegionalSwingMath {

inline double quartersUntilElection(double days)
{
	// The fitted curves describe pre-election lead time. After polling day use
	// their election-day value rather than extrapolating to a negative SD.
	return std::max(0.0, days) / 91.315;
}

inline double pollingDeviation(double stateCoefficient, double stateIntercept,
	double pooledCoefficient, double pooledIntercept, double rawDeviation)
{
	constexpr double stateWeight = 0.5;
	return (stateWeight * stateCoefficient + (1.0 - stateWeight) * pooledCoefficient) * rawDeviation
		+ stateWeight * stateIntercept + (1.0 - stateWeight) * pooledIntercept;
}

inline double pollingWeight(double electionDayWeight, double decay, double quarters)
{
	// A mixture stays between its two estimates, including when reading older
	// unconstrained calibration curves or a date just beyond election day.
	return std::clamp(electionDayWeight * std::exp(-decay * std::max(0.0, quarters)), 0.0, 1.0);
}

}
