#include "Projection.h"
#include "Model.h"
#include <random>
#include <cmath>
#include <algorithm>

#undef max

void Projection::run() {
	if (!endDate.IsValid()) return;

	// Set up random variables
	std::random_device rd;
	std::mt19937 gen(rd());
	std::normal_distribution<double> nDist(0.0f, dailyChange);
	std::normal_distribution<double> campaignDist(0.0f, dailyChange * 2.0f);
	// t-distribution since we've estimated the SD from a random sample
	std::student_t_distribution<double> initialDist(std::max(numElections - 1, 1));
	// additional uncertainty from the state of the polling at the moment
	float pollingStdDev = baseModel->finalStandardDeviation;
	std::normal_distribution<double> pollingDist(0.0f, pollingStdDev);

	std::vector<std::vector<double>> tempProjections;
	int nDays = std::max(1, (endDate - baseModel->effEndDate).GetDays() + 1);
	tempProjections.resize(numIterations);
	meanProjection.resize(nDays);
	sdProjection.resize(nDays);
	double modelEndpoint = double(baseModel->day.back().trend2pp);
	for (auto& projVec : tempProjections) {
		projVec.resize(nDays);
		double systematicVariation = initialDist(gen) * initialStdDev;
		double samplingVariation = pollingDist(gen) * 2;
		double initialDistributionResult = modelEndpoint + systematicVariation + samplingVariation;
		initialDistributionResult = std::clamp(initialDistributionResult, 0.0, 100.0);
		projVec[0] = initialDistributionResult;
		for (int day = 1; day < nDays; ++day) {
			double randomVariance = (day > nDays - 30 ? campaignDist(gen) : nDist(gen));
			double nextDayStatisticProjection = projVec[day - 1] - leaderVoteLoss * (projVec[day - 1] - 50) + randomVariance;
			nextDayStatisticProjection = std::clamp(nextDayStatisticProjection, 0.0, 100.0);
			projVec[day] = nextDayStatisticProjection;
		}
	}
	for (int day = 0; day < nDays; ++day) {
		for (auto& projVec : tempProjections) {
			meanProjection[day] += projVec[day];
		}
		meanProjection[day] /= double(numIterations);
		for (auto& projVec : tempProjections) {
			sdProjection[day] += std::pow(projVec[day] - meanProjection[day], 2);
		}
		sdProjection[day] = std::sqrt(sdProjection[day] / double(numIterations));
	}

	logRunStatistics();
	lastUpdated = wxDateTime::Now();
}


void Projection::logRunStatistics()
{
	PrintDebugLine("--------------------------------");
	PrintDebugLine("Projection completed.");
	PrintDebug("Final 2PP mean value: ");
	PrintDebugDouble(meanProjection.back());
	PrintDebugNewLine();
	PrintDebug("Final 2PP standard deviation: ");
	PrintDebugDouble(sdProjection.back());
	PrintDebugNewLine();
}

void Projection::setAsNowCast() {
	if (baseModel != nullptr) {
		endDate = baseModel->effEndDate + wxDateSpan(0, 0, 0, 1);
	}
}