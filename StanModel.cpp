#include "StanModel.h"

#include "General.h"
#include "Log.h"
#include "SpecialPartyCodes.h"

#include <fstream>
#include <future>
#include <numeric>
#include <sstream>
#include <thread>

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
	return startDate + wxDateSpan::Days(adjustedSupport.begin()->second.timePoint.size() - 1);
}

void StanModel::loadData(FeedbackFunc feedback)
{

	generateParameterMaps();
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		feedback("No party codes found!");
		return;
	}
	logger << "Starting model loading: " << wxDateTime::Now().FormatISOCombined() << "\n";
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
	logger << "Loaded model: " << wxDateTime::Now().FormatISOCombined() << "\n";
	generateUnnamedOthersSeries();
	logger << "Generated unnamed others series: " << wxDateTime::Now().FormatISOCombined() << "\n";
	updateAdjustedData(feedback);
	logger << "updated adjusted data: " << wxDateTime::Now().FormatISOCombined() << "\n";

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
		int lowerBucket = std::clamp(int(floor(uniform * float(Spread::Size - 1))), 0, int(Spread::Size) - 2);
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

StanModel::SupportSample StanModel::generateAdjustedSupportSample(wxDateTime date, int days) const
{
	auto rawSample = generateRawSupportSample(date);
	auto adjustedSample = adjustRawSupportSample(rawSample, days);
	return adjustedSample;
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
	for (auto& [key, voteShare] : sample) {
		float transformed = transformVoteShare(voteShare);

		// Remove long-term systemic bias
		float intercept = debiasInterceptMap.at(key);
		float slope = debiasSlopeMap.at(key);
		float debiasChange = intercept + slope * logDays;
		float transformedDebiasChange = debiasChange * limitedLogitDeriv(voteShare);
		transformed -= transformedDebiasChange;

		// Mix with historical value
		float thisMaxPollWeight = maxPollWeightMap.at(key);
		float thisInformationHorizon = informationHorizonMap.at(key);
		float thisHyperbolaSharpness = hyperbolaSharpnessMap.at(key);
		float thisHistorical = historicalAverageMap.at(key);
		constexpr float HistoricalVoteMin = 0.2f;
		constexpr float HistoricalVoteMax = 99.8f;
		float historicalTransformed = transformVoteShare(std::clamp(thisHistorical, HistoricalVoteMin, HistoricalVoteMax));
		float pollWeight = std::max(0.0f, (thisMaxPollWeight + 1.0f / (thisInformationHorizon * thisHyperbolaSharpness)) -
			1.0f / ((thisInformationHorizon - logDays) * thisHyperbolaSharpness));
		transformed = transformed * pollWeight + historicalTransformed * (1.0f - pollWeight);
		// Adjust for variable systemic error
		constexpr float HistoricSampleDeviation = 0.008333f; // effective sample size
		float thisDeviationSlope = deviationSlopeMap.at(key);
		float thisDeviationIntercept = deviationInterceptMap.at(key);
		float combinedDeviation = thisDeviationIntercept + thisDeviationSlope * logDays;
		float systemicDeviation = std::sqrt(std::max(0.0f, combinedDeviation * combinedDeviation - HistoricSampleDeviation * HistoricSampleDeviation));
		float normalVal = rng.normal(0.0f, systemicDeviation);
		float transformedNormalVal = normalVal * limitedLogitDeriv(voteShare);
		transformed += transformedNormalVal;

		float newVoteShare = detransformVoteShare(transformed);
		voteShare = newVoteShare;
	}
	normaliseSample(sample);
	updateOthersValue(sample);
	generateTppForSample(sample);
	return sample;
}

void StanModel::updateAdjustedData(FeedbackFunc feedback)
{
	constexpr static int NumIterations = 1000;
	adjustedSupport.clear(); // do this first as it should not be left with previous data
	try {
		generateParameterMaps();

		int seriesLength = rawSupport.begin()->second.timePoint.size();
		for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
			std::string partyName = partyCodeVec[partyIndex];
			adjustedSupport[partyName] = Series();
			adjustedSupport[partyName].timePoint.resize(seriesLength);
		}
		tppSupport.timePoint.resize(seriesLength);

		constexpr int NumThreads = 8;
		constexpr int BatchSize = 10;
		for (int timeStart1 = 0; timeStart1 < seriesLength; timeStart1 += NumThreads * BatchSize) {
			auto calculateTimeSupport = [&](int timeStart) {
				for (int time = timeStart; time < timeStart + BatchSize && time < seriesLength; ++time) {
					wxDateTime thisDate = startDate;
					thisDate.Add(wxDateSpan(0, 0, 0, time));
					std::vector<std::array<float, NumIterations>> samples(partyCodeVec.size());
					std::array<float, NumIterations> tppSamples;
					for (int iteration = 0; iteration < NumIterations; ++iteration) {
						auto sample = generateAdjustedSupportSample(thisDate);
						for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
							std::string partyName = partyCodeVec[partyIndex];
							if (sample.count(partyName)) {
								samples[partyIndex][iteration] = sample[partyName];
							}
							if (sample.count(TppCode)) {
								tppSamples[iteration] = sample[TppCode];
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
					std::sort(tppSamples.begin(), tppSamples.end());
					for (int percentile = 0; percentile < Spread::Size; ++percentile) {
						int sampleIndex = std::min(NumIterations - 1, percentile * NumIterations / int(Spread::Size));
						tppSupport.timePoint[time].values[percentile] = tppSamples[sampleIndex];
					}
				}
			};
			std::vector<std::thread> threads;
			for (int timeStart = timeStart1; timeStart < timeStart1 + NumThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
				threads.push_back(std::thread(std::bind(calculateTimeSupport, timeStart)));
			}
			for (auto& thread : threads) {
				if (thread.joinable()) thread.join();
			}
		}

		const int ModelSmoothingDays = 21;
		for (auto& [key, party] : adjustedSupport) {
			party.smooth(ModelSmoothingDays); // also automatically calculates expectations
		}
		tppSupport.smooth(ModelSmoothingDays);
	}

	catch (std::logic_error& e) {
		feedback(std::string("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase\n") + 
			"Specific information: " + e.what());
		return;
	}
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
			return (b.first == OthersCode || b.first == TppCode ? a : a + b.second); }
		);
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample) {
		vote.second *= sampleAdjust;
	}
}

void StanModel::generateTppForSample(StanModel::SupportSample& sample) const
{
	float tpp = 0.0f;
	for (auto [key, support] : sample) {
		if (key == OthersCode) continue;
		if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) {
			tpp = 0.0f;
			break;
		}
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomisedFlow = (historicalSamples >= 2
			? rng.t_dist(int(std::floor(historicalSamples)) - 1, flow, deviation)
			: flow);
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		tpp += support * randomisedFlow;
	}
	sample.insert({ TppCode, tpp });
}

void StanModel::generateParameterMaps()
{
	// partyCodeVec is already created by loadData
	partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size()) throw Exception("No party codes in this model!");
	try {
		auto debiasInterceptVec = splitStringF(debiasIntercept, ",");
		auto debiasSlopeVec = splitStringF(debiasSlope, ",");
		auto maxPollWeightVec = splitStringF(maxPollWeight, ",");
		auto informationHorizonVec = splitStringF(informationHorizon, ",");
		auto hyperbolaSharpnessVec = splitStringF(hyperbolaSharpness, ",");
		auto historicalAverageVec = splitStringF(historicalAverage, ",");
		auto deviationSlopeVec = splitStringF(deviationSlope, ",");
		auto deviationInterceptVec = splitStringF(deviationIntercept, ",");
		auto preferenceFlowVec = splitStringF(preferenceFlow, ",");
		auto preferenceDeviationVec = splitStringF(preferenceDeviation, ",");
		auto preferenceSamplesVec = splitStringF(preferenceSamples, ",");
		bool validSizes = debiasInterceptVec.size() == partyCodeVec.size() &&
			debiasSlopeVec.size() == partyCodeVec.size() &&
			maxPollWeightVec.size() == partyCodeVec.size() &&
			informationHorizonVec.size() == partyCodeVec.size() &&
			hyperbolaSharpnessVec.size() == partyCodeVec.size() &&
			historicalAverageVec.size() == partyCodeVec.size() &&
			deviationSlopeVec.size() == partyCodeVec.size() &&
			deviationInterceptVec.size() == partyCodeVec.size() &&
			preferenceFlowVec.size() == partyCodeVec.size() &&
			preferenceDeviationVec.size() == partyCodeVec.size() &&
			preferenceSamplesVec.size() == partyCodeVec.size();
		if (!validSizes) throw Exception("Party codes and parameter lines do not match!");

		debiasInterceptMap.clear();
		debiasSlopeMap.clear();
		maxPollWeightMap.clear();
		informationHorizonMap.clear();
		hyperbolaSharpnessMap.clear();
		historicalAverageMap.clear();
		deviationSlopeMap.clear();
		deviationInterceptMap.clear();
		preferenceFlowMap.clear();
		preferenceDeviationMap.clear();
		preferenceSamplesMap.clear();
		for (int index = 0; index < int(partyCodeVec.size()); ++index) {
			debiasInterceptMap.insert({ partyCodeVec[index], debiasInterceptVec[index] });
			debiasSlopeMap.insert({ partyCodeVec[index], debiasSlopeVec[index] });
			maxPollWeightMap.insert({ partyCodeVec[index], maxPollWeightVec[index] });
			informationHorizonMap.insert({ partyCodeVec[index], informationHorizonVec[index] });
			hyperbolaSharpnessMap.insert({ partyCodeVec[index], hyperbolaSharpnessVec[index] });
			historicalAverageMap.insert({ partyCodeVec[index], historicalAverageVec[index] });
			deviationSlopeMap.insert({ partyCodeVec[index], deviationSlopeVec[index] });
			deviationInterceptMap.insert({ partyCodeVec[index], deviationInterceptVec[index] });
			preferenceFlowMap.insert({ partyCodeVec[index], preferenceFlowVec[index] });
			preferenceDeviationMap.insert({ partyCodeVec[index], preferenceDeviationVec[index] });
			preferenceSamplesMap.insert({ partyCodeVec[index], preferenceSamplesVec[index] });
		}
	}
	catch (std::invalid_argument) {
		throw Exception("One or more model paramater lists could not be converted to floats!");
	}
}

void StanModel::Series::smooth(int smoothingFactor)
{
	Series newSeries = *this;
	for (int index = 0; index < int(timePoint.size()); ++index) {
		int thisSmoothing = std::min(smoothingFactor, std::min(std::abs(index), std::abs(int(timePoint.size()) - index - 1)));
		for (int percentile = 0; percentile < Spread::Size; ++percentile) {
			double numerator = 0.0f;
			double denominator = 0.0f;
			for (int offset = -thisSmoothing; offset <= thisSmoothing; ++offset) {
				int source = index + offset;
				double weight = nCr(thisSmoothing, offset + thisSmoothing);
				numerator += double(timePoint[source].values[percentile]) * weight;
				denominator += weight;
			}
			double result = numerator / denominator;
			newSeries.timePoint[index].values[percentile] = float(result);
		}
		newSeries.timePoint[index].calculateExpectation();
	}
	*this = newSeries;
}
