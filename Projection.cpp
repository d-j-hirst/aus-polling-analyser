#include "Projection.h"

#include "General.h"
#include "Log.h"
#include "Model.h"
#include "ModelCollection.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

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

void Projection::run(ModelCollection const& models, FeedbackFunc feedback, int numThreads) {
	if (!settings.endDate.IsValid()) return;
	auto const& model = getBaseModel(models);
	if (!model.isReadyForProjection()) {
		feedback("The base model (" + model.name + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return;
	}
	startDate = model.getEndDate() + wxDateSpan::Days(1);

	logger << "Starting projection run: " << wxDateTime::Now().FormatISOCombined() << "\n";

	constexpr static int NumIterations = 5000;
	projectedSupport.clear(); // do this first as it should not be left with previous data
	try {
		int seriesLength = (settings.endDate - startDate).GetDays();
		for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
			std::string partyName = model.partyCodeVec[partyIndex];
			projectedSupport[partyName] = StanModel::Series();
			projectedSupport[partyName].timePoint.resize(seriesLength);
		}
		tppSupport.timePoint.resize(seriesLength);


		constexpr int BatchSize = 10;
		for (int timeStart1 = 0; timeStart1 < seriesLength; timeStart1 += numThreads * BatchSize) {
			auto calculateTimeSupport = [&](int timeStart) {
				for (int time = timeStart; time < timeStart + BatchSize && time < seriesLength; ++time) {
					wxDateTime thisDate = startDate;
					thisDate.Add(wxDateSpan(0, 0, 0, time));
					std::vector<std::array<float, NumIterations>> samples(model.partyCodeVec.size());
					std::unique_ptr<std::array<float, NumIterations>> tppSamples; // on heap to avoid 
					tppSamples.reset(new std::array<float, NumIterations>());
					for (int iteration = 0; iteration < NumIterations; ++iteration) {
						auto projectedDate = startDate + wxDateSpan::Days(time);
						auto sample = generateSupportSample(models, projectedDate);
						for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
							std::string partyName = model.partyCodeVec[partyIndex];
							if (sample.voteShare.count(partyName)) {
								samples[partyIndex][iteration] = sample.voteShare[partyName];
							}
							if (sample.voteShare.count(TppCode)) {
								(*tppSamples)[iteration] = sample.voteShare[TppCode];
							}
						}

					}
					for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
						std::string partyName = model.partyCodeVec[partyIndex];
						std::sort(samples[partyIndex].begin(), samples[partyIndex].end());
						for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
							int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(StanModel::Spread::Size));
							projectedSupport[partyName].timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
						}
					}
					std::sort(tppSamples->begin(), tppSamples->end());
					for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
						int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(StanModel::Spread::Size));
						tppSupport.timePoint[time].values[percentile] = (*tppSamples)[sampleIndex];
					}
				}
			};
			std::vector<std::thread> threads;
			for (int timeStart = timeStart1; timeStart < timeStart1 + numThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
				threads.push_back(std::thread(std::bind(calculateTimeSupport, timeStart)));
			}
			for (auto& thread : threads) {
				if (thread.joinable()) thread.join();
			}
		}

		const int ProjectionSmoothingDays = 21;
		for (auto& [key, party] : projectedSupport) {
			party.smooth(ProjectionSmoothingDays); // also automatically calculates expectations
		}
		tppSupport.smooth(ProjectionSmoothingDays);
	}
	catch (std::logic_error & e) {
		feedback(std::string("Could not generate projection\n") +
			"Specific information: " + e.what());
		return;
	}

	logger << "Completed projection run: " << wxDateTime::Now().FormatISOCombined() << "\n";

	for (auto& [key, support] : projectedSupport) {
		for (int timeCount = 0; timeCount < int(support.timePoint.size()); timeCount += 50) {
			std::stringstream ss;
			ss << key << " - day " << timeCount << "\n";
			for (int spreadVal = 1; spreadVal < 100; ++spreadVal) {
				ss << spreadVal << "%: " << support.timePoint[timeCount].values[spreadVal] << "\n";
			}
		}
	}

	std::string report = textReport(models);
	auto reportMessages = splitString(report, ";");
	for (auto message : reportMessages) feedback(message);
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
	settings.endDate = model.getEndDate() + wxDateSpan(0, 0, 0, 2);
}

StanModel::SeriesOutput Projection::viewPrimarySeries(std::string partyCode) const
{
	if (!projectedSupport.count(partyCode)) return StanModel::SeriesOutput();
	return &projectedSupport.at(partyCode);
}

StanModel::SeriesOutput Projection::viewPrimarySeriesByIndex(int index) const
{
	if (index < 0 || index >= int(projectedSupport.size())) return StanModel::SeriesOutput();
	return &std::next(projectedSupport.begin(), index)->second;
}

StanModel::SupportSample Projection::generateSupportSample(ModelCollection const& models, wxDateTime date) const
{
	auto const& model = getBaseModel(models);
	// *** This is where to account for differing end dates
	if (!date.IsValid()) date = settings.endDate;
	int daysAfterModelEnd = (date - model.getEndDate()).GetDays();
	auto sample = model.generateAdjustedSupportSample(model.getEndDate(), daysAfterModelEnd);
	return sample;
}

int Projection::getPartyIndexFromCode(std::string code) const
{
	int index = 0;
	for (auto const& [key, series] : projectedSupport) {
		if (code == key) return index;
		++index;
	}
	return -1;
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
	report << ";"; // delimiter
	auto sample = generateSupportSample(models);
	report << "Final sample: \n";
	for (auto [key, vote] : sample.voteShare) {
		report << key << ": " << vote << "\n";
	}
	return report.str();
}

StanModel const& Projection::getBaseModel(ModelCollection const& models) const
{
	return models.view(settings.baseModel);
}
