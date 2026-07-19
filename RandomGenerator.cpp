#ifdef _MSC_VER
#define _SILENCE_CXX23_DENORM_DEPRECATION_WARNING
#endif

#include "RandomGenerator.h"

#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/students_t.hpp>

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
		boost::math::students_t_distribution<double> distribution(df);
		double const scale = std::sqrt(
			double(df - 2) / double(df));
		auto& lookup = tdistLookup[df];
		lookup.resize(LookupSize);
		for (int index = 0; index < LookupSize / 2; ++index) {
			double const probability =
				double(index + 1) / double(LookupSize + 1);
			double const value =
				boost::math::quantile(distribution, probability) * scale;
			lookup[index] = value;
			lookup[LookupSize - index - 1] = -value;
		}
	});
}

void RandomGenerator::prepareNormalLookup()
{
	std::call_once(normalOnce, []() {
		boost::math::normal_distribution<double> distribution;
		normalLookup.resize(LookupSize);
		for (int index = 0; index < LookupSize / 2; ++index) {
			double const probability =
				double(index + 1) / double(LookupSize + 1);
			double const value =
				boost::math::quantile(distribution, probability);
			normalLookup[index] = value;
			normalLookup[LookupSize - index - 1] = -value;
		}
	});
}
