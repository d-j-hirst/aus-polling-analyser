#include "Projection.h"

#include "Log.h"
#include "Model.h"
#include "ModelCollection.h"

#include <algorithm>
#include <cmath>
#include <random>

#undef max

Projection::Projection(SaveData saveData)
	: settings(saveData.settings), lastUpdated(saveData.lastUpdated),
	meanProjection(saveData.meanProjection), sdProjection(saveData.sdProjection)
{}

void Projection::replaceSettings(Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxInvalidDateTime;
}

void Projection::run(ModelCollection const& models) {
	if (!settings.endDate.IsValid()) return;

	auto model = models.view(settings.baseModel);

	// Set up random variables
	std::random_device rd;
	std::mt19937 gen(rd());
	std::normal_distribution<double> nDist(0.0f, settings.dailyChange);
	std::normal_distribution<double> campaignDist(0.0f, settings.dailyChange * 2.0f);
	// t-distribution since we've estimated the SD from a random sample
	std::student_t_distribution<double> initialDist(std::max(settings.numElections - 1, 1));
	// additional uncertainty from the state of the polling at the moment
	float pollingStdDev = model.getFinalStandardDeviation();
	std::normal_distribution<double> pollingDist(0.0f, pollingStdDev);

	std::vector<std::vector<double>> tempProjections;
	int nDays = std::max(1, (settings.endDate - model.getEffectiveEndDate()).GetDays() + 1);
	tempProjections.resize(settings.numIterations);
	meanProjection.resize(nDays);
	sdProjection.resize(nDays);
	double modelEndpoint = double(std::prev(model.end())->trend2pp);
	for (auto& projVec : tempProjections) {
		projVec.resize(nDays);
		double systematicVariation = initialDist(gen) * settings.initialStdDev;
		double samplingVariation = pollingDist(gen) * 2;
		double initialDistributionResult = modelEndpoint + systematicVariation + samplingVariation;
		initialDistributionResult = std::clamp(initialDistributionResult, 0.0, 100.0);
		projVec[0] = initialDistributionResult;
		for (int day = 1; day < nDays; ++day) {
			double randomVariance = (day > nDays - 30 ? campaignDist(gen) : nDist(gen));
			double nextDayStatisticProjection = projVec[day - 1] - settings.leaderVoteDecay * (projVec[day - 1] - 50) + randomVariance;
			nextDayStatisticProjection = std::clamp(nextDayStatisticProjection, 0.0, 100.0);
			projVec[day] = nextDayStatisticProjection;
		}
	}
	for (int day = 0; day < nDays; ++day) {
		for (auto& projVec : tempProjections) {
			meanProjection[day] += projVec[day];
		}
		meanProjection[day] /= double(settings.numIterations);
		for (auto& projVec : tempProjections) {
			sdProjection[day] += std::pow(projVec[day] - meanProjection[day], 2);
		}
		sdProjection[day] = std::sqrt(sdProjection[day] / double(settings.numIterations));
	}

	logRunStatistics();
	lastUpdated = wxDateTime::Now();
}


void Projection::logRunStatistics()
{
	logger << "--------------------------------\n";
	logger << "Projection completed.\n";
	logger << "Final 2PP mean value: " << meanProjection.back() << "\n";
	logger << "Final 2PP standard deviation: " << sdProjection.back() << "\n";
}

void Projection::setAsNowCast(ModelCollection const& models) {
	auto model = models.view(settings.baseModel);
	settings.endDate = model.getEffectiveEndDate() + wxDateSpan(0, 0, 0, 1);
}