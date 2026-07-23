#ifdef _MSC_VER
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
#endif

#include "RandomGenerator.h"

namespace {
	constexpr double Pi = 3.141592653589793238462643383279502884;

	// For a continuous distribution with density f(x), its inverse CDF obeys
	// dx/dp = 1/f(x). Integrating outwards from the median efficiently produces
	// the dense lookup tables used by the simulation.
	template <typename InverseDensity>
	double inverseCdfStep(double value, double probabilityStep,
		InverseDensity const& inverseDensity)
	{
		double const first = inverseDensity(value);
		double const second = inverseDensity(
			value + probabilityStep * first * 0.5);
		double const third = inverseDensity(
			value + probabilityStep * second * 0.5);
		double const fourth = inverseDensity(
			value + probabilityStep * third);
		return value + probabilityStep *
			(first + 2.0 * second + 2.0 * third + fourth) / 6.0;
	}

	template <typename InverseDensity>
	void populateSymmetricLookup(std::vector<double>& lookup, int lookupSize,
		InverseDensity const& inverseDensity, double outputScale = 1.0)
	{
		lookup.resize(lookupSize);
		double value = 0.0;
		double probability = 0.5;
		for (int index = lookupSize / 2 - 1;
			index >= 0; --index) {
			double const targetProbability =
				double(index + 1) / double(lookupSize + 1);
			value = inverseCdfStep(
				value, targetProbability - probability, inverseDensity);
			probability = targetProbability;
			lookup[index] = value * outputScale;
			lookup[lookupSize - index - 1] =
				-value * outputScale;
		}
	}
}

std::random_device RandomGenerator::rd = std::random_device();
std::mt19937 RandomGenerator::gen = std::mt19937();

std::array<std::once_flag, RandomGenerator::MaxDf + 1>
	RandomGenerator::tdistOnce;
std::array<std::vector<double>, RandomGenerator::MaxDf + 1>
	RandomGenerator::tdistLookup;

std::once_flag RandomGenerator::normalOnce;
std::vector<double> RandomGenerator::normalLookup = std::vector<double>();

void RandomGenerator::prepareTdistLookup(int df)
{
	df = std::clamp(df, 3, MaxDf);
	std::call_once(tdistOnce[df], [df]() {
		double const degreesOfFreedom = double(df);
		double const logDensityAtZero =
			std::lgamma((degreesOfFreedom + 1.0) * 0.5) -
			std::lgamma(degreesOfFreedom * 0.5) -
			0.5 * std::log(degreesOfFreedom * Pi);
		auto inverseDensity = [=](double value) {
			double const logDensity = logDensityAtZero -
				(degreesOfFreedom + 1.0) * 0.5 *
				std::log1p(value * value / degreesOfFreedom);
			return std::exp(-logDensity);
		};
		populateSymmetricLookup(tdistLookup[df], LookupSize, inverseDensity,
			std::sqrt(double(df - 2) / double(df)));
	});
}

void RandomGenerator::prepareNormalLookup()
{
	std::call_once(normalOnce, []() {
		auto inverseDensity = [](double value) {
			return std::sqrt(2.0 * Pi) *
				std::exp(value * value * 0.5);
		};
		populateSymmetricLookup(normalLookup, LookupSize, inverseDensity);
	});
}
