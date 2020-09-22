#include "Projection.h"

#include "Log.h"
#include "Model.h"
#include "ModelCollection.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

#undef max

Projection::Projection(SaveData saveData)
	: settings(saveData.settings), lastUpdated(saveData.lastUpdated),
	projection(saveData.projection)
{
}

void Projection::replaceSettings(Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxInvalidDateTime;
}

void Projection::run(ModelCollection const& models) {
	models;
	if (!settings.endDate.IsValid()) return;
	//auto const& model = models.view(settings.baseModel);

	//InternalProjections internalProjections;
	//runInternalProjections(internalProjections, model);
	//combineInternalProjections(internalProjections, model);

	//logRunStatistics();
	lastUpdated = wxDateTime::Now();
}


void Projection::logRunStatistics()
{
	logger << "--------------------------------\n";
	logger << "Projection completed.\n";
	logger << "Final 2PP mean value: " << projection.back().mean << "\n";
	logger << "Final 2PP standard deviation: " << projection.back().sd << "\n";
}

void Projection::setAsNowCast(ModelCollection const& models) {
	auto model = models.view(settings.baseModel);
	//settings.endDate = model.getEffectiveEndDate() + wxDateSpan(0, 0, 0, 1);
}

std::string Projection::textReport(ModelCollection const& models) const
{
	std::stringstream report;
	report << "Reporting Projection: \n";
	report << " Name: " << settings.name << "\n";
	report << " Number of iterations: " << settings.numIterations << "\n";
	report << " Base model: " << models.view(settings.baseModel).getName() << "\n";
	report << " End Date: " << getEndDateString() << "\n";
	report << " Last Updated: " << getLastUpdatedString() << "\n";
	report << " Daily Change: " << settings.dailyChange << "\n";
	report << " Initial Standard Deviation: " << settings.initialStdDev << "\n";
	report << " Leader Vote Decay: " << settings.leaderVoteDecay << "\n";
	report << " Number of Previous Elections: " << settings.numElections << "\n";
	return report.str();
}

void Projection::runInternalProjections(InternalProjections& internalProjections, Model const& model)
{
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
	int nDays = std::max(1, (settings.endDate - model.getEffectiveEndDate()).GetDays() + 1);
	internalProjections.resize(settings.numIterations);
	projection.resize(nDays);
	double modelEndpoint = double(std::prev(model.end())->trend2pp);
	for (auto& projVec : internalProjections) {
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
}

void Projection::combineInternalProjections(InternalProjections & internalProjections, Model const& model)
{
	int nDays = std::max(1, (settings.endDate - model.getEffectiveEndDate()).GetDays() + 1);
	for (int day = 0; day < nDays; ++day) {
		double projectionSum = 0;
		for (auto& projVec : internalProjections) {
			projectionSum += projVec[day];
		}
		projection[day].mean = projectionSum / double(settings.numIterations);
		double deviationSquaredSum = 0;
		for (auto& projVec : internalProjections) {
			deviationSquaredSum += std::pow(projVec[day] - projection[day].mean, 2);
		}
		projection[day].sd = std::sqrt(deviationSquaredSum / double(settings.numIterations));
	}
}
