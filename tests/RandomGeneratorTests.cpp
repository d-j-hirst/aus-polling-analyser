#include "../RandomGenerator.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace {
	bool closeTo(double actual, double expected, double tolerance)
	{
		return std::abs(actual - expected) <= tolerance;
	}
}

int main()
{
	assert(closeTo(RandomGenerator::scaledNormalQuantile(0.5), 0.0, 0.0001));
	assert(closeTo(RandomGenerator::scaledNormalQuantile(0.025),
		-1.959963985, 0.0002));
	assert(closeTo(RandomGenerator::scaledNormalQuantile(0.975),
		1.959963985, 0.0002));
	assert(closeTo(RandomGenerator::scaledTdistQuantile(3, 0.025),
		-1.837386231, 0.0003));
	assert(closeTo(RandomGenerator::scaledTdistQuantile(10, 0.975),
		1.992907975, 0.0003));
	assert(closeTo(RandomGenerator::scaledTdistQuantile(5, 0.1),
		-RandomGenerator::scaledTdistQuantile(5, 0.9), 0.0001));
	for (double probability : { 0.0, 0.00001, 0.01, 0.5, 0.99, 0.99999, 1.0 }) {
		assert(std::isfinite(
			RandomGenerator::scaledNormalQuantile(probability)));
		for (int degreesOfFreedom : { 3, 4, 10, 30, 100 }) {
			assert(std::isfinite(RandomGenerator::scaledTdistQuantile(
				degreesOfFreedom, probability)));
		}
	}
	assert(RandomGenerator::scaledNormalQuantile(0.001) <
		RandomGenerator::scaledNormalQuantile(0.01));
	assert(RandomGenerator::scaledTdistQuantile(3, 0.001) <
		RandomGenerator::scaledTdistQuantile(3, 0.01));

	bool rejectedInvalidParameters = false;
	try {
		RandomGenerator::beta(-1.0, 2.0);
	}
	catch (std::invalid_argument const&) {
		rejectedInvalidParameters = true;
	}
	assert(rejectedInvalidParameters);

	RandomGenerator::setSeed(481516);
	constexpr std::size_t SampleCount = 200000;
	double sum = 0.0;
	double squaredSum = 0.0;
	for (std::size_t sample = 0; sample < SampleCount; ++sample) {
		double const value = RandomGenerator::beta(2.0, 5.0);
		assert(value >= 0.0 && value <= 1.0);
		sum += value;
		squaredSum += value * value;
	}
	double const mean = sum / double(SampleCount);
	double const variance = squaredSum / double(SampleCount) - mean * mean;
	assert(closeTo(mean, 2.0 / 7.0, 0.002));
	assert(closeTo(variance, 10.0 / 392.0, 0.001));

	std::mt19937_64 keyedEngine(918273);
	float const keyedValue =
		RandomGenerator::betaFromEngine(0.725f, 2.0f, keyedEngine);
	assert(std::isfinite(keyedValue));
	assert(keyedValue >= 0.0f && keyedValue <= 1.0f);
}
