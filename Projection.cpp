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
	startDate = model.getStartDate() + wxDateSpan(0, 0, 0, model.adjustedSupport.begin()->second.timePoint.size());
	partyCodes = model.partyCodes;
	preferenceFlow = model.preferenceFlow;
	preferenceDeviation = model.preferenceDeviation;
	preferenceSamples = model.preferenceSamples;
	StanModel::SupportSample z;
	// Temporarily hard coded for QLD State Election 2020
	const StanModel::SupportSample historicalVote = { {"ALP", 39.29f}, {"LNP", 39.94f}, {"GRN", 8.18f}, {"ONP", 3.435f}, {"KAP", 5.26f}, {"xOTH", 5.665f} };
	std::map<std::string, std::vector<std::vector<float>>> recordedSamples;
	int totalDays = (settings.endDate - startDate).GetDays();
	logger << settings.endDate.FormatISODate() << "\n";
	logger << startDate.FormatISODate() << "\n";
	logger << model.getStartDate().FormatISODate() << "\n";
	logger << model.adjustedSupport.begin()->second.timePoint.size() + 1 << "\n";
	logger << totalDays << "\n";
	for (int iteration = 0; iteration < this->settings.numIterations; ++iteration) {
		StanModel::SupportSample originalSample = model.generateSupportSample();
		for (auto [key, vote] : originalSample) {
			z[key] = rng.normal(0.0f, 1.0f);
			recordedSamples[key].resize(totalDays);
		}
		for (int days = 1; days <= totalDays; ++days) {
			StanModel::SupportSample nextSample;
			float logDays = std::log(float(days));
			constexpr float pollWeightA = -0.0129f;
			constexpr float pollWeightB = 0.0152f;
			constexpr float pollWeightC = 0.96f;
			float pollWeight = pollWeightA * logDays * logDays + pollWeightB * logDays + pollWeightC;
			constexpr float StdevLogDaysGradient = 0.3808f;
			constexpr float StdevLogDaysIntercept = 1.5386f;
			float voteStdev = StdevLogDaysGradient * logDays + StdevLogDaysIntercept;
			float totalVote = 0.0f;
			for (auto const& [key, vote] : originalSample) {
				float historicalVoteT = transformVoteShare(historicalVote.at(key));
				float polledVoteT = transformVoteShare(vote);
				float meanProjectedVoteT = polledVoteT * pollWeight + historicalVoteT * (1.0f - pollWeight);
				float projectedVoteT = meanProjectedVoteT + z[key] * voteStdev;
				float projectedVote = detransformVoteShare(projectedVoteT);
				nextSample[key] = projectedVote;
				totalVote += projectedVote;
			}
			for (auto& [key, vote] : nextSample) {
				vote /= totalVote;
			}
			for (auto const& [key, vote] : nextSample) {
				recordedSamples[key][days - 1].push_back(nextSample[key]);
			}
		}
	}
	projectedSupport.clear();
	for (auto& [key, samples] : recordedSamples) {
		projectedSupport.insert({ key, StanModel::Series() });
		projectedSupport[key].timePoint.resize(samples.size());
		std::stringstream ss;
		ss << key << " - party\n";
		for (int timeCount = 0; timeCount < int(samples.size()); ++timeCount) {
			auto& samplesDay = samples[timeCount];
			std::sort(samplesDay.begin(), samplesDay.end());
			for (int spreadVal = 0; spreadVal < StanModel::Spread::Size; ++spreadVal) {
				projectedSupport[key].timePoint[timeCount].values[spreadVal] =
					samplesDay[std::min(samplesDay.size() - 1, samplesDay.size() * spreadVal / (StanModel::Spread::Size - 1))];
			}
		}
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

	bool success = createCachedPreferenceFlow(feedback);
	if (success) generateTppSeries(feedback);
	std::string report = textReport(models);
	auto reportMessages = splitString(report, ";");
	for (auto message : reportMessages) feedback(message);
	for (int i = 0; i < 1000; ++i) logger << generateTppSample() << "\n";
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

float Projection::generateTppSample(wxDateTime date) const
{
	return calculateTppFromSample(generateSupportSample(date));
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
	float tppSample = calculateTppFromSample(sample);
	report << "TPP: " << tppSample << "\n";
	return report.str();
}

StanModel const& Projection::getBaseModel(ModelCollection const& models) const
{
	return models.view(settings.baseModel);
}

bool Projection::createCachedPreferenceFlow(FeedbackFunc feedback)
{
	if (!cachedPreferenceFlow) {
		auto preferenceFlowVec = splitString(preferenceFlow, ",");
		auto preferenceDeviationVec = splitString(preferenceDeviation, ",");
		auto preferenceSamplesVec = splitString(preferenceSamples, ",");
		partyCodeVec = splitString(partyCodes, ",");
		for (auto& partyCode : partyCodeVec) if (partyCode == OthersCode) partyCode = ExclusiveOthersCode;
		bool validSizes = preferenceFlowVec.size() == partyCodeVec.size()
			&& preferenceDeviationVec.size() == partyCodeVec.size()
			&& preferenceSamplesVec.size() == partyCodeVec.size();
		if (!validSizes) {
			feedback("Warning: ");
			return false;
		}
		if (!projectedSupport.size()) {
			feedback("Warning: Mean and/or deviation adjustments not valid, skipping two-party-preferred series generation");
			return false;
		}
		const int timeCount = projectedSupport.begin()->second.timePoint.size();
		preferenceFlowMap.clear();
		preferenceDeviationMap.clear();
		preferenceSamplesMap.clear();
		for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
			std::string partyName = partyCodeVec[partyIndex];
			if (projectedSupport.count(partyName)) {
				if (partyName == OthersCode) partyName = ExclusiveOthersCode;
				try {
					preferenceFlowMap[partyName] = std::clamp(std::stof(preferenceFlowVec[partyIndex]), 0.0f, 100.0f) * 0.01f;
					preferenceDeviationMap[partyName] = std::clamp(std::stof(preferenceDeviationVec[partyIndex]), 0.0f, 100.0f) * 0.01f;
					preferenceSamplesMap[partyName] = std::max(std::stoi(preferenceSamplesVec[partyIndex]), 0);
				}
				catch (std::invalid_argument) {
					feedback("Warning: Invalid preference flow for party " + partyName + ", aborting two-party-preferred series generation");
					return false;
				}
			}
		}
		cachedPreferenceFlow = true;
	}
	return true;
}

float Projection::calculateTppFromSample(StanModel::SupportSample const& sample, FeedbackFunc feedback) const
{
	float tpp = 0.0f;
	for (auto [key, support] : sample) {
		float flow = preferenceFlowMap.at(key);
		float deviation = preferenceDeviationMap.at(key);
		int historicalSamples = preferenceSamplesMap.at(key);
		float randomisedFlow = (historicalSamples >= 2
			? rng.t_dist(historicalSamples - 1, flow, deviation)
			: flow);
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		tpp += support * randomisedFlow;
	}
	return tpp;
}

void Projection::generateTppSeries(FeedbackFunc feedback)
{
	constexpr static int NumIterations = 5000;
	tppSupport = StanModel::Series(); // do this first as it should not be left with previous data

	const int timeCount = projectedSupport.begin()->second.timePoint.size();
	tppSupport.timePoint.resize(timeCount);
	// Set up calculation function
	typedef std::pair<int, int> Bounds;
	auto determineTpp = [&](Bounds b) {
		for (int time = b.first; time < b.second; ++time) {
			wxDateTime thisDate = startDate;
			thisDate.Add(wxDateSpan(0, 0, 0, time));
			std::array<float, NumIterations> samples;
			for (int iteration = 0; iteration < NumIterations; ++iteration) {
				auto fpSample = generateSupportSample(thisDate);
				samples[iteration] = calculateTppFromSample(fpSample, feedback);
			}
			std::sort(samples.begin(), samples.end());
			for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
				int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(StanModel::Spread::Size));
				tppSupport.timePoint[time].values[percentile] = samples[sampleIndex];
			}
			tppSupport.timePoint[time].calculateExpectation();
		}
	};

	// Perform multi-threaded validation for each time point
	constexpr int NumThreads = 8;
	const int TimePointsPerThread = timeCount / NumThreads + 1;
	std::array<std::thread, NumThreads> threadArray;
	for (int threadIndex = 0; threadIndex < NumThreads; ++threadIndex) {
		Bounds bounds = { threadIndex * TimePointsPerThread, std::min((threadIndex + 1) * TimePointsPerThread, timeCount) };
		threadArray[threadIndex] = std::thread([&, bounds]() {determineTpp(bounds); });
	}
	for (int threadIndex = 0; threadIndex < NumThreads; ++threadIndex) {
		threadArray[threadIndex].join();
	}
}
