#include "StanModel.h"

#include "General.h"
#include "Log.h"
#include "OthersCodes.h"

#include <fstream>
#include <future>
#include <sstream>
#include <numeric>

constexpr int MedianSpreadValue = StanModel::Spread::Size / 2;

constexpr bool DoValidations = false;

RandomGenerator StanModel::rng = RandomGenerator();

StanModel::MajorPartyCodes StanModel::majorPartyCodes = 
	{ "ALP", "LNP", "LIB", "NAT", "GRN" };

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes,
	std::string meanAdjustments, std::string deviationAdjustments)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

wxDateTime StanModel::getEndDate() const
{
	if (!adjustedSeriesCount()) return startDate;
	return startDate + wxDateSpan(0, 0, 0, adjustedSupport.begin()->second.timePoint.size() - 1);
}

void StanModel::loadData(FeedbackFunc feedback)
{
	partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		feedback("No party codes found!");
		return;
	}
	startDate = wxInvalidDateTime;
	rawSupport.clear();
	for (auto partyCode : partyCodeVec) {
		auto& series = rawSupport[partyCode];
		if (partyCode == UnnamedOthersCode) continue; // calculate this later
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
	generateUnnamedOthersSeries();
	updateAdjustedData(feedback);
	//generateTppSeries(feedback);
	//updateValidationData(feedback);

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

StanModel::SeriesOutput StanModel::viewRawSeries(std::string partyCode) const
{
	if (!rawSupport.count(partyCode)) return nullptr;
	return &rawSupport.at(partyCode);
}

StanModel::SeriesOutput StanModel::viewRawSeriesByIndex(int index) const
{
	if (index < 0 || index >= int(rawSupport.size())) return nullptr;
	return &std::next(rawSupport.begin(), index)->second;
}

StanModel::SeriesOutput StanModel::viewAdjustedSeries(std::string partyCode) const
{
	if (!adjustedSupport.count(partyCode)) return nullptr;
	return &adjustedSupport.at(partyCode);
}

StanModel::SeriesOutput StanModel::viewAdjustedSeriesByIndex(int index) const
{
	if (index < 0 || index >= int(adjustedSupport.size())) return nullptr;
	return &std::next(adjustedSupport.begin(), index)->second;
}

StanModel::Series const& StanModel::viewTPPSeries() const
{
	return tppSupport;
}

StanModel::SupportSample StanModel::generateSupportSample(wxDateTime date) const
{
	if (!adjustedSupport.size()) return SupportSample();
	int seriesLength = adjustedSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int dayOffset = adjustedSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, (date - startDate).GetDays());
	if (dayOffset < 0) dayOffset = 0;
	SupportSample sample;
	for (auto [key, support] : adjustedSupport) {
		//if (key == OthersCode) continue; // only include the "xOTH" unnamed 
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
	return sample;
}

std::string StanModel::rawPartyCodeByIndex(int index) const
{
	return std::next(rawSupport.begin(), index)->first;
}

StanModel::SupportSample StanModel::generateRawSupportSample(wxDateTime date) const
{
	if (!rawSupport.size()) return SupportSample();
	int seriesLength = rawSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int dayOffset = rawSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, (date - startDate).GetDays());
	if (dayOffset < 0) dayOffset = 0;
	SupportSample sample;
	for (auto [key, support] : rawSupport) {
		float uniform = rng.uniform(0.0, 1.0);
		int lowerBucket = int(floor(uniform * float(Spread::Size - 1)));
		float upperMix = std::fmod(uniform * float(Spread::Size - 1), 1.0f);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = upperVote * upperMix + lowerVote * (1.0f - upperMix);
		sample.insert({ key, sampledVote });
	}

	normaliseSample(sample);

	updateOthersValue(sample);

	return sample;
}

void StanModel::generateUnnamedOthersSeries()
{
	if (rawSupport.count(OthersCode) && rawSupport.count(UnnamedOthersCode)) {
		for (int time = 0; time < int(rawSupport[OthersCode].timePoint.size()); ++time) {
			float namedMinorTotal = 0.0f;
			for (auto const& [code, series] : rawSupport) {
				if (code != UnnamedOthersCode && code != OthersCode && !majorPartyCodes.count(code)) {
					namedMinorTotal += series.timePoint[time].values[MedianSpreadValue];
				}
			}
			float othersMedian = rawSupport[OthersCode].timePoint[time].values[MedianSpreadValue];
			const float UnnamedMinorsBase = 3.0f;
			othersMedian = std::max(othersMedian, namedMinorTotal + UnnamedMinorsBase);
			float proportion = 1.0f - namedMinorTotal / othersMedian;
			Spread spread;
			for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
				spread.values[percentile] = rawSupport[OthersCode].timePoint[time].values[percentile] * proportion;
			}
			rawSupport[UnnamedOthersCode].timePoint.push_back(spread);
		}
	}
}

StanModel::SupportSample StanModel::adjustRawSupportSample(SupportSample const& rawSupportSample, int days) const
{
	constexpr int MinDays = 2;
	float logDays = std::log(float(std::max(days, MinDays)));
	auto sample = rawSupportSample;
	for (auto& [key, support] : sample) {
		float supportChange = debiasInterceptMap.at(key) + debiasSlopeMap.at(key) * logDays;
		support += supportChange;
	}
	normaliseSample(sample);
	updateOthersValue(sample);
	return sample;
}

void StanModel::updateAdjustedData(FeedbackFunc feedback)
{
	constexpr static int NumIterations = 5000;
	adjustedSupport.clear(); // do this first as it should not be left with previous data
	try {
		generateParameterMaps();

		int seriesLength = rawSupport.begin()->second.timePoint.size();
		for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
			std::string partyName = partyCodeVec[partyIndex];
			adjustedSupport[partyName] = Series();
			adjustedSupport[partyName].timePoint.resize(seriesLength);
		}

		for (int time = 0; time < seriesLength; ++time) {
			wxDateTime thisDate = startDate;
			thisDate.Add(wxDateSpan(0, 0, 0, time));
			std::vector<std::array<float, NumIterations>> samples(partyCodeVec.size());
			for (int iteration = 0; iteration < NumIterations; ++iteration) {
				auto sample = generateRawSupportSample(thisDate);
				auto adjustedSample = adjustRawSupportSample(sample);
				for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
					std::string partyName = partyCodeVec[partyIndex];
					if (adjustedSample.count(partyName)) {
						samples[partyIndex][iteration] = adjustedSample[partyName];
					}
				}
			}
			for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
				std::string partyName = partyCodeVec[partyIndex];
				std::sort(samples[partyIndex].begin(), samples[partyIndex].end());
				for (int percentile = 0; percentile < Spread::Size; ++percentile) {
					int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(Spread::Size));
					adjustedSupport[partyName].timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
				}
			}
		}

		for (auto& [key, party] : adjustedSupport) {
			for (auto& time : party.timePoint) {
				time.calculateExpectation();
			}
		}
	}

	catch (std::logic_error& e) {
		feedback(std::string("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase\n") + 
			"Specific information: " + e.what());
		return;
	}
}

void StanModel::generateTppSeries(FeedbackFunc feedback)
{
	constexpr static int NumIterations = 5000;
	tppSupport = Series(); // do this first as it should not be left with previous data
	auto preferenceFlowVec = splitString(preferenceFlow, ",");
	auto preferenceDeviationVec = splitString(preferenceDeviation, ",");
	auto preferenceSamplesVec = splitString(preferenceSamples, ",");
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
			if (partyName == OthersCode) partyName = UnnamedOthersCode;
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
	//	if (key != OthersCode) {
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

void StanModel::updateOthersValue(StanModel::SupportSample& sample) {
	// make sure "others" is actually equal to sum of non-major parties
	float otherSum = std::accumulate(sample.begin(), sample.end(), 0.0f,
		[](float a, StanModel::SupportSample::value_type b) {
			return (b.first == OthersCode || majorPartyCodes.count(b.first) ? a : a + b.second);
		});
	sample[OthersCode] = otherSum;
}

void StanModel::normaliseSample(StanModel::SupportSample& sample)
{
	float sampleSum = std::accumulate(sample.begin(), sample.end(), 0.0f,
		[](float a, StanModel::SupportSample::value_type b) {
			return (b.first == OthersCode ? a : a + b.second); }
		);
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample) {
		vote.second *= sampleAdjust;
	}
}

void StanModel::generateParameterMaps()
{
	// partyCodeVec is already created by loadData
	if (!partyCodeVec.size()) throw std::logic_error("No party codes in this model!");
	auto debiasInterceptVec = splitStringF(debiasIntercept, ",");
	auto debiasSlopeVec = splitStringF(debiasSlope, ",");
	bool validSizes = debiasInterceptVec.size() == partyCodeVec.size() &&
		(debiasSlopeVec.size() == partyCodeVec.size());
	if (!validSizes) throw std::logic_error("Party codes and parameter lines do not match!");

	debiasInterceptMap.clear();
	debiasSlopeMap.clear();
	for (int index = 0; index < int(partyCodeVec.size()); ++index) {
		debiasInterceptMap.insert({ partyCodeVec[index], debiasInterceptVec[index] });
		debiasSlopeMap.insert({ partyCodeVec[index], debiasSlopeVec[index] });
	}
}
