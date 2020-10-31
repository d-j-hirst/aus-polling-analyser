#include "StanModel.h"

#include "General.h"
#include "OthersCodes.h"

#include <fstream>
#include <future>
#include <sstream>
#include <numeric>

constexpr int MedianSpreadValue = StanModel::Spread::Size / 2;

constexpr bool DoValidations = false;

RandomGenerator StanModel::rng = RandomGenerator();

StanModel::MajorPartyCodes StanModel::majorPartyCodes = 
	{ "ALP", "LNP", "LIB", "NAT", "GRN", OthersCode, ExclusiveOthersCode };

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes,
	std::string meanAdjustments, std::string deviationAdjustments)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

void StanModel::loadData(FeedbackFunc feedback)
{
	auto partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		feedback("No party codes found!");
		return;
	}
	startDate = wxInvalidDateTime;
	rawSupport.clear();
	for (auto partyCode : partyCodeVec) {
		auto& series = rawSupport[partyCode];
		std::string filename = "python/Outputs/fp_trend_"
			+ termCode + "_" + partyCode + " FP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			continue;
		}
		series.timePoint.clear();
		std::string line;
		std::getline(file, line); // first line is just a legend, skip it
		std::getline(file, line);
		if (!startDate.IsValid()) {
			auto dateVals = splitString(line, ",");
			startDate = wxDateTime(std::stoi(dateVals[0]),
				wxDateTime::Month(std::stoi(dateVals[1]) - 1), std::stoi(dateVals[2]));
		}
		std::getline(file, line); // this line is just a legend, skip it
		do {
			std::getline(file, line);
			if (!file) break;
			auto trendVals = splitString(line, ",");
			series.timePoint.push_back(Spread());
			for (int percentile = 0; percentile < Spread::Size; ++percentile) {
				series.timePoint.back().values[percentile]
					= std::stof(trendVals[percentile + 2]);
			}
		} while (true);
	}
	limitMinorParties(feedback);
	updateAdjustedData(feedback);
	generateTppSeries(feedback);
	updateValidationData(feedback);

	lastUpdatedDate = wxDateTime::Now();
	feedback("Finished loading models");
}

int StanModel::rawSeriesCount() const
{
	return int(rawSupport.size());
}

int StanModel::adjustedSeriesCount() const
{
	return int(adjustedSupport.size());
}

std::string StanModel::getTextReport() const
{
	std::stringstream ss;
	ss << "Raw party support, assuming only sampling error:\n";
	for (auto [key, series] : this->rawSupport) {
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
	}
	ss << ";";
	ss << "Adjusted party support, accounting for possible systemic bias and variability:\n";
	for (auto [key, series] : this->adjustedSupport) {
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
		ss << "Expectation: " << series.timePoint.back().expectation << "\n";
	}
	ss << "TPP:\n";
	ss << "1%: " << tppSupport.timePoint.back().values[1] << "\n";
	ss << "10%: " << tppSupport.timePoint.back().values[10] << "\n";
	ss << "50%: " << tppSupport.timePoint.back().values[50] << "\n";
	ss << "90%: " << tppSupport.timePoint.back().values[90] << "\n";
	ss << "99%: " << tppSupport.timePoint.back().values[99] << "\n";
	ss << "Expectation: " << tppSupport.timePoint.back().expectation << "\n";
	if (DoValidations) {
		ss << ";";
		ss << "Post-sampling party support:\n";
		for (auto [key, series] : this->validationSupport) {
			ss << key << "\n";
			ss << "1%: " << series.timePoint.back().values[1] << "\n";
			ss << "10%: " << series.timePoint.back().values[10] << "\n";
			ss << "50%: " << series.timePoint.back().values[50] << "\n";
			ss << "90%: " << series.timePoint.back().values[90] << "\n";
			ss << "99%: " << series.timePoint.back().values[99] << "\n";
			ss << "Expectation: " << series.timePoint.back().expectation << "\n";
		}
	}
	ss << ";";
	ss << "Vote share sample:\n";
	auto sample = generateSupportSample();
	for (auto [key, vote] : sample) {
		ss << key << ": " << vote << "\n";
	}
	return ss.str();
}

StanModel::Series const& StanModel::viewRawSeries(std::string partyCode) const
{
	return rawSupport.at(partyCode);
}

StanModel::Series const& StanModel::viewRawSeriesByIndex(int index) const
{

	return std::next(rawSupport.begin(), index)->second;
}

StanModel::Series const& StanModel::viewAdjustedSeries(std::string partyCode) const
{
	return adjustedSupport.at(partyCode);
}

StanModel::Series const& StanModel::viewAdjustedSeriesByIndex(int index) const
{
	return std::next(adjustedSupport.begin(), index)->second;
}

StanModel::Series const& StanModel::viewTPPSeries() const
{
	return tppSupport;
}

StanModel::SupportSample StanModel::generateSupportSample(wxDateTime date, bool includeTpp) const
{
	if (!adjustedSupport.size()) return SupportSample();
	int seriesLength = adjustedSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int dayOffset = adjustedSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, (date - startDate).GetDays());
	if (dayOffset < 0) dayOffset = 0;
	SupportSample sample;
	for (auto [key, support] : adjustedSupport) {
		if (key == OthersCode) continue; // only include the "xOTH" unnamed 
		float uniform = rng.uniform(0.0, 1.0);
		int lowerBucket = int(floor(uniform * float(Spread::Size - 1)));
		float upperMix = std::fmod(uniform * float(Spread::Size - 1), 1.0f);
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
	includeTpp; // do something with this later
	return sample;
}

std::string StanModel::rawPartyCodeByIndex(int index) const
{
	return std::next(rawSupport.begin(), index)->first;
}

void StanModel::updateAdjustedData(FeedbackFunc feedback)
{
	adjustedSupport.clear(); // do this first as it should not be left with previous data
	auto meanAdjustmentsVec = splitString(meanAdjustments, ",");
	auto deviationAdjustmentsVec = splitString(deviationAdjustments, ",");
	auto partyCodeVec = splitString(partyCodes, ",");
	bool validSizes = meanAdjustmentsVec.size() == partyCodeVec.size() &&
		(deviationAdjustmentsVec.size() == partyCodeVec.size());
	if (!validSizes) {
		feedback("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase");
		return;
	}

	for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
		std::string partyName = partyCodeVec[partyIndex];
		if (rawSupport.count(partyName)) {
			adjustedSupport[partyName] = rawSupport[partyName];
			float adjustMean = 0.0f;
			float adjustDeviation = 1.0f;
			try {
				adjustMean = std::stof(meanAdjustmentsVec[partyIndex]);
				adjustDeviation = std::stof(deviationAdjustmentsVec[partyIndex]);
			}
			catch (std::invalid_argument) {
				feedback("Warning: Invalid mean or deviation value for party " + partyName + ", aborting adjustment phase");
				adjustedSupport.clear();
				return;
			}
			for (auto& timePoint : adjustedSupport[partyName].timePoint) {
				// Transform party support values
				for (auto& value : timePoint.values) {
					value = transformVoteShare(value);
				}
				// Adjust transformed party support values by a set amount
				// and also according to its distance from the mean
				float meanValue = timePoint.values[MedianSpreadValue];
				for (auto& value : timePoint.values) {
					float deviation = (value - meanValue);
					value = meanValue + adjustMean + deviation * adjustDeviation;
				}
				// Transform back into a regular vote share
				for (auto& value : timePoint.values) {
					value = detransformVoteShare(value);
				}
			}
		}
	}

	// Create a series for estimated others, excluding "others" parties that have their own series
	if (adjustedSupport.count(OthersCode)) {
		adjustedSupport[ExclusiveOthersCode] = adjustedSupport[OthersCode];
		for (auto const& [key, series] : adjustedSupport) {
			if (key != OthersCode && key != ExclusiveOthersCode && !majorPartyCodes.count(key)) {
				for (int time = 0; time < int(series.timePoint.size()); ++time) {
					float partyMedian = series.timePoint[time].values[MedianSpreadValue];
					for (float& value : adjustedSupport[ExclusiveOthersCode].timePoint[time].values) {
						// Should remain with at least a small, ascending sequence
						value = std::max(value - partyMedian, 0.1f + value * 0.01f);
					}
				}
			}
		}
	}

	limitMinorParties(feedback);

	for (auto& [key, party] : adjustedSupport) {
		for (auto& time : party.timePoint) {
			time.calculateExpectation();
		}
	}
}

void StanModel::limitMinorParties(FeedbackFunc feedback)
{
	for (auto& [key, series] : adjustedSupport) {
		if (!majorPartyCodes.count(key)) {
			for (int time = 0; time < int(series.timePoint.size()); ++time) {
				for (int value = 0; value < Spread::Size; ++value) {
					series.timePoint[time].values[value] = std::min(
						series.timePoint[time].values[value],
						adjustedSupport[OthersCode].timePoint[time].values[value]);
				}
			}
		}
	}
}

void StanModel::generateTppSeries(FeedbackFunc feedback)
{
	constexpr static int NumIterations = 5000;
	tppSupport = Series(); // do this first as it should not be left with previous data
	auto preferenceFlowVec = splitString(preferenceFlow, ",");
	auto preferenceDeviationVec = splitString(preferenceDeviation, ",");
	auto preferenceSamplesVec = splitString(preferenceSamples, ",");
	auto partyCodeVec = splitString(partyCodes, ",");
	bool validSizes = preferenceFlowVec.size() == partyCodeVec.size()
		&& preferenceDeviationVec.size() == partyCodeVec.size()
		&& preferenceSamplesVec.size() == partyCodeVec.size();
	if (!validSizes) {
		feedback("Warning: ");
		return;
	}
	if (!adjustedSupport.size()) {
		feedback("Warning: Mean and/or deviation adjustments not valid, skipping two-party-preferred series generation");
		return;
	}
	const int timeCount = adjustedSupport.begin()->second.timePoint.size();
	std::map<std::string, float> preferenceFlowMap;
	std::map<std::string, float> preferenceDeviationMap;
	std::map<std::string, int> preferenceSamplesMap;
	for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
		std::string partyName = partyCodeVec[partyIndex];
		if (adjustedSupport.count(partyName)) {
			if (partyName == OthersCode) partyName = ExclusiveOthersCode;
			try {
				preferenceFlowMap[partyName] = std::clamp(std::stof(preferenceFlowVec[partyIndex]), 0.0f, 100.0f) * 0.01f;
				preferenceDeviationMap[partyName] = std::clamp(std::stof(preferenceDeviationVec[partyIndex]), 0.0f, 100.0f) * 0.01f;
				preferenceSamplesMap[partyName] = std::max(std::stoi(preferenceSamplesVec[partyIndex]), 0);
			}
			catch (std::invalid_argument) {
				feedback("Warning: Invalid preference flow for party " + partyName + ", aborting two-party-preferred series generation");
				return;
			}
		}
	}

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
				float tpp = 0.0f;
				for (auto [key, support] : fpSample) {
					if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) {
						feedback("Warning: Invalid preference flow for party " + key + ", aborting two-party-preferred series generation");
						tppSupport = Series();
						return;
					}
					float flow = preferenceFlowMap[key];
					float deviation = preferenceDeviationMap[key];
					int historicalSamples = preferenceSamplesMap[key];
					float randomisedFlow = (historicalSamples >= 2 
						? rng.t_dist(historicalSamples - 1, flow, deviation)
						: flow);
					randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
					tpp += support * randomisedFlow;
				}
				samples[iteration] = tpp;
			}
			std::sort(samples.begin(), samples.end());
			for (int percentile = 0; percentile < Spread::Size; ++percentile) {
				int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(Spread::Size));
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

void StanModel::updateValidationData(FeedbackFunc feedback)
{
	//if (!DoValidations) return;
	//constexpr size_t NumIterations = 1000;
	//if (!adjustedSupport.size()) {
	//	feedback("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase");
	//	return;
	//}
	//const int timeCount = adjustedSupport.begin()->second.timePoint.size();

	//validationSupport.clear();
	//for (auto const& [key, series] : adjustedSupport) {
	//	if (key != "OTH") {
	//		validationSupport.insert({ key, Series() });
	//		validationSupport[key].timePoint.resize(timeCount);
	//	}
	//}

	//// Set up validation function
	//typedef std::pair<int, int> Bounds;
	//auto runValidation = [&](Bounds b) {
	//	for (int time = b.first; time < b.second; ++time) {
	//		std::array<SupportSample, NumIterations> samples;
	//		for (auto& sample : samples) {
	//			wxDateTime thisDate = startDate;
	//			thisDate += wxDateSpan(0, 0, 0, time);
	//			sample = generateSupportSample(thisDate);
	//		}
	//		std::map<std::string, std::vector<float>> samplesByParty;
	//		for (auto const& [key, support] : *samples.begin()) {
	//			samplesByParty.insert({ key, {} });
	//		}
	//		for (auto const& sample : samples) {
	//			for (auto [key, support] : sample) {
	//				samplesByParty[key].push_back(support);
	//			}
	//		}
	//		for (auto& [key, partySamples] : samplesByParty) {
	//			std::sort(partySamples.begin(), partySamples.end());
	//		}
	//		for (auto const& [key, partySamples] : samplesByParty) {
	//			for (int percentile = 0; percentile < Spread::Size; ++percentile) {
	//				int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(Spread::Size));
	//				validationSupport[key].timePoint[time].values[percentile] = partySamples[sampleIndex];
	//			}
	//			calculateExpectation(validationSupport[key].timePoint[time]);
	//		}
	//	}
	//};

	//// Perform multi-threaded validation for each time point
	//constexpr int NumThreads = 8;
	//const int TimePointsPerThread = timeCount / NumThreads + 1;
	//std::array<std::thread, NumThreads> threadArray;
	//for (int threadIndex = 0; threadIndex < NumThreads; ++threadIndex) {
	//	Bounds bounds = { threadIndex * TimePointsPerThread, std::min((threadIndex + 1) * TimePointsPerThread, timeCount) };
	//	threadArray[threadIndex] = std::thread([&]() {runValidation(bounds); });
	//}
	//for (int threadIndex = 0; threadIndex < NumThreads; ++threadIndex) {
	//	threadArray[threadIndex].join();
	//}

	//// Calculate post-validation adjustments so that medians of the sample can match medians of the adjusted trend
	//for (auto& [key, series] : validationSupport) {
	//	supportAdjustments[key] = std::vector<float>(timeCount);
	//	for (int time = 0; time < timeCount; ++time) {
	//		float originalSupportMedianT = transformVoteShare(adjustedSupport[key].timePoint[time].values[MedianSpreadValue]);
	//		float sampleSupportMedianT = transformVoteShare(validationSupport[key].timePoint[time].values[MedianSpreadValue]);
	//		supportAdjustments[key][time] = sampleSupportMedianT - originalSupportMedianT;
	//	}
	//	feedback(key + " adjustment: " + formatFloat(supportAdjustments[key][timeCount - 1], 2));
	//}
}

StanModel::Series& StanModel::addSeries(std::string partyCode)
{
	return rawSupport.insert({ partyCode, Series() }).first->second;
}

void StanModel::Spread::calculateExpectation()
{
	float sum = std::accumulate(values.begin(), values.end(), 0.0f,
		[](float a, float b) {return a + b; });
	expectation = sum / float(values.size());
}
