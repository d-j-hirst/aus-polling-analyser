#include "RandomGenerator.h"

#include "Log.h"

#include <set>

std::random_device RandomGenerator::rd = std::random_device();
std::mt19937 RandomGenerator::gen = std::mt19937();

std::array<std::mutex, RandomGenerator::MaxDf + 1> RandomGenerator::tdistMutex = std::array<std::mutex, RandomGenerator::MaxDf + 1>();
std::map<int, std::vector<double>> RandomGenerator::tdistLookup = std::map<int, std::vector<double>>();
std::array<bool, RandomGenerator::MaxDf + 1> RandomGenerator::tdistReady = {};

std::mutex RandomGenerator::normalMutex;
std::vector<double> RandomGenerator::normalLookup = std::vector<double>();
bool RandomGenerator::normalReady = false;

void RandomGenerator::prepareTdistLookup(int df)
{
	if (!tdistLookup.size()) {
		std::lock_guard<std::mutex> lock_general(tdistMutex[df]);
		for (int a = 0; a <= MaxDf; ++a) {
			tdistLookup[a] = std::vector<double>();
		}
	}
	df = std::clamp(df, 0, MaxDf);
	if (tdistReady[df]) return;
	std::lock_guard<std::mutex> lock(tdistMutex[df]);
	// Before trying to generate the lookup, make sure it hasn't been created while waiting for the lock to be free
	if (tdistReady[df]) return;

	std::set<double> orderedValues;
	while (orderedValues.size() < LookupSize) {
		orderedValues.insert(scaledTdist(df, 0.0, 1.0));
	}
	int index = 0;
	auto& lookup = tdistLookup.at(df);
	lookup.resize(LookupSize);
	for (auto it = orderedValues.begin(); it != orderedValues.end(); ++it, ++index) {
		lookup[index] = *it;
	}
	logger << "df = " << df << " lookup generated\n";
	for (int testQuantile = 0; testQuantile < LookupSize; testQuantile += 10000) {
		logger << "quantile = " << testQuantile << "  value = " << lookup[testQuantile] << "\n";
	}
	logger << "quantile = " << 10 << "  value = " << lookup[10] << "\n";
	logger << "quantile = " << LookupSize - 11 << "  value = " << lookup[LookupSize - 11] << "\n";
	logger << "quantile = " << 100 << "  value = " << lookup[100] << "\n";
	logger << "quantile = " << LookupSize - 101 << "  value = " << lookup[LookupSize - 101] << "\n";
	tdistReady[df] = true;
}

void RandomGenerator::prepareNormalLookup()
{
	if (normalReady) return;
	std::lock_guard<std::mutex> lock(normalMutex);
	// Before trying to generate the lookup, make sure it hasn't been created while waiting for the lock to be free
	if (normalReady) return;

	std::set<double> orderedValues;
	while (orderedValues.size() < LookupSize) {
		orderedValues.insert(normal(0.0, 1.0));
	}
	int index = 0;
	auto& lookup = normalLookup;
	lookup.resize(LookupSize);
	for (auto it = orderedValues.begin(); it != orderedValues.end(); ++it, ++index) {
		lookup[index] = *it;
	}
	logger << "normal lookup generated\n";
	for (int testQuantile = 0; testQuantile < LookupSize; testQuantile += 10000) {
		logger << "quantile = " << testQuantile << "  value = " << lookup[testQuantile] << "\n";
	}
	normalReady = true;
}
