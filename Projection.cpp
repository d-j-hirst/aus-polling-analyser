#include "Projection.h"

#include "General.h"
#include "Log.h"
#include "Model.h"
#include "ModelCollection.h"
#include "OthersCodes.h"

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

void Projection::run(ModelCollection const& models, FeedbackFunc feedback) {
	if (!settings.endDate.IsValid()) return;
	auto const& model = getBaseModel(models);
	startDate = model.getEndDate() + wxDateSpan::Days(1);
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

		for (int time = 0; time < seriesLength; ++time) {
			wxDateTime thisDate = startDate;
			thisDate.Add(wxDateSpan(0, 0, 0, time));
			std::vector<std::array<float, NumIterations>> samples(model.partyCodeVec.size());
			std::array<float, NumIterations> tppSamples;
			for (int iteration = 0; iteration < NumIterations; ++iteration) {
				auto sample = model.generateRawSupportSample();
				int daysAhead = (startDate + wxDateSpan::Days(time) - model.getEndDate()).GetDays();
				auto adjustedSample = model.adjustRawSupportSample(sample, daysAhead);
				for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
					std::string partyName = model.partyCodeVec[partyIndex];
					if (adjustedSample.count(partyName)) {
						samples[partyIndex][iteration] = adjustedSample[partyName];
					}
					if (adjustedSample.count(TppCode)) {
						tppSamples[iteration] = adjustedSample[TppCode];
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
			std::sort(tppSamples.begin(), tppSamples.end());
			for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
				int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(StanModel::Spread::Size));
				tppSupport.timePoint[time].values[percentile] = tppSamples[sampleIndex];
			}
		}

		for (auto& [key, party] : projectedSupport) {
			for (auto& time : party.timePoint) {
				time.calculateExpectation();
			}
		}
	}

	catch (std::logic_error & e) {
		feedback(std::string("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase\n") +
			"Specific information: " + e.what());
		return;
	}

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
	//settings.endDate = model.getEffectiveEndDate() + wxDateSpan(0, 0, 0, 1);
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

StanModel::SupportSample Projection::generateSupportSample(wxDateTime date) const
{
	if (!projectedSupport.size()) return StanModel::SupportSample();
	int seriesLength = projectedSupport.begin()->second.timePoint.size();
	if (!seriesLength) return StanModel::SupportSample();
	int dayOffset = projectedSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, (date - startDate).GetDays());
	if (dayOffset < 0) dayOffset = 0;
	StanModel::SupportSample sample;
	for (auto [key, support] : projectedSupport) {
		if (key == OthersCode) continue; // only include the "xOTH" unnamed 
		float uniform = rng.uniform(0.0, 1.0);
		int lowerBucket = int(floor(uniform * float(StanModel::Spread::Size - 1)));
		float upperMix = std::fmod(uniform * float(StanModel::Spread::Size - 1), 1.0f);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = upperVote * upperMix + lowerVote * (1.0f - upperMix);
		sample.insert({ key, sampledVote });
	}
	float sampleSum = std::accumulate(sample.begin(), sample.end(), 0.0f,
		[](float a, decltype(sample)::value_type b) {return a + b.second; });
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample) {
		vote.second *= sampleAdjust;
	}
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
	auto sample = generateSupportSample();
	report << "Final sample: \n";
	for (auto [key, vote] : sample) {
		report << key << ": " << vote << "\n";
	}
	return report.str();
}

StanModel const& Projection::getBaseModel(ModelCollection const& models) const
{
	return models.view(settings.baseModel);
}
