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

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

wxDateTime StanModel::getEndDate() const
{
	if (!adjustedSeriesCount()) return startDate;
	return startDate + wxDateSpan::Days(adjustedSupport.begin()->second.timePoint.size() - 1);
}

void StanModel::loadData(FeedbackFunc feedback, int numThreads)
{
	logger << "Starting model run: " << wxDateTime::Now().FormatISOCombined() << "\n";
	prepareForRun(feedback);
	logger << "Generated unnamed others series: " << wxDateTime::Now().FormatISOCombined() << "\n";
	updateAdjustedData(feedback, numThreads);
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

StanModel::Series const& StanModel::viewRawTPPSeries() const
{
	return rawTppSupport;
}

StanModel::Series const& StanModel::viewTPPSeries() const
{
	return tppSupport;
}

std::string StanModel::rawPartyCodeByIndex(int index) const
{
	return std::next(rawSupport.begin(), index)->first;
}

bool StanModel::prepareForRun(FeedbackFunc feedback)
{
	loadPartyGroups();
	loadFundamentalsPredictions();
	loadParameters(feedback);
	loadEmergingOthersParameters(feedback);
	if (!generatePreferenceMaps(feedback)) return false;
	if (!loadTrendData(feedback)) return false;
	generateUnnamedOthersSeries();
	readyForProjection = true;
	return true;
}

void StanModel::loadPartyGroups()
{
	const std::string filename = "analysis/Data/party-groups.csv";
	auto file = std::ifstream(filename);
	if (!file) throw Exception("Party groups file not present! Expected a file at " + filename);
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		partyGroups[values[0]] = PartyGroup();
		for (auto it = std::next(values.begin()); it != values.end(); ++it) {
			std::string splitValue = splitString(*it, " ")[0];
			partyGroups[values[0]].push_back(splitValue);
		}
	} while (true);

	for (auto const& [key, values] : partyGroups) {
		for (auto const& value : values) {
			reversePartyGroups[value] = key;
		}
	}
}

void StanModel::loadFundamentalsPredictions()
{
	logger << "loading fundamentals predictions\n";
	const std::string filename = "analysis/Fundamentals/fundamentals_" + termCode + ".csv";
	auto file = std::ifstream(filename);
	if (!file) throw Exception("Fundamentals prediction file not present! Expected a file at " + filename);
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		std::string party = splitString(values[0], " ")[0];
		fundamentals[party] = std::stod(values[1]);
	} while (true);
}

void StanModel::loadParameters(FeedbackFunc feedback)
{
	parameters = {};
	for (auto const& [partyGroup, partyList] : partyGroups) {
		// If there's a specific adjustment file for this election (usually only for hindcasts) use that
		// Otherwise (as for future elections) just use the general versions that use all past elections
		std::string electionFileName = "analysis/Adjustments/adjust_" + termCode + "_" + partyGroup + ".csv";
		std::string generalFileName = "analysis/Adjustments/adjust_0none_" + partyGroup + ".csv";
		auto file = std::ifstream(electionFileName);
		if (!file) file = std::ifstream(generalFileName);
		if (!file) {
			feedback("Error: Could not find an adjustment file for party group: " + partyGroup);
			return;
		}
		std::string line;
		std::getline(file, line);
		auto coeffLine = splitString(line, ",");
		if (!numDays) numDays = coeffLine.size();
		ParameterSeries series(numDays, ParameterSet{}); // final argument braces sets to zero rather than uninitialized
		for (int day = 0; day < numDays; ++day) {
			series[day][0] = std::stod(coeffLine[day]);
		}
		for (int coeffType = 1; coeffType < int(InputParameters::Max); ++coeffType) {
			std::getline(file, line);
			coeffLine = splitString(line, ",");
			for (int day = 0; day < numDays; ++day) {
				series[day][coeffType] = std::stod(coeffLine[day]);
			}
		}
		parameters[partyGroup] = series;
	}
}

void StanModel::loadEmergingOthersParameters(FeedbackFunc feedback)
{
	logger << "loading emerging others parameters\n";
	const std::string filename = "analysis/Seat Statistics/statistics_emerging_party.csv";
	auto file = std::ifstream(filename);
	if (!file) throw Exception("Emerging others parameters not present! Expected a file at " + filename);
	for (int parameter = 0; parameter < int(EmergingPartyParameters::Max); ++parameter) {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		double value = std::stod(line);
		emergingParameters[parameter] = value;
	}
}

bool StanModel::generatePreferenceMaps(FeedbackFunc feedback)
{
	// partyCodeVec is already created by loadData
	partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size()) throw Exception("No party codes in this model!");
	try {
		auto preferenceFlowVec = splitStringF(preferenceFlow, ",");
		auto preferenceDeviationVec = splitStringF(preferenceDeviation, ",");
		auto preferenceSamplesVec = splitStringF(preferenceSamples, ",");
		bool validSizes = preferenceFlowVec.size() == partyCodeVec.size() &&
			preferenceDeviationVec.size() == partyCodeVec.size() &&
			preferenceSamplesVec.size() == partyCodeVec.size();
		if (!validSizes) throw Exception("Party codes and parameter lines do not match!");
		preferenceFlowMap.clear();
		preferenceDeviationMap.clear();
		preferenceSamplesMap.clear();
		for (int index = 0; index < int(partyCodeVec.size()); ++index) {
			preferenceFlowMap.insert({ partyCodeVec[index], preferenceFlowVec[index] });
			preferenceDeviationMap.insert({ partyCodeVec[index], preferenceDeviationVec[index] });
			preferenceSamplesMap.insert({ partyCodeVec[index], preferenceSamplesVec[index] });
		}
	}
	catch (std::invalid_argument) {
		feedback("One or more model paramater lists could not be converted to floats!");
		return false;
	}
	preferenceFlowMap[EmergingOthersCode] = preferenceFlowMap[OthersCode];
	preferenceDeviationMap[EmergingOthersCode] = preferenceDeviationMap[OthersCode];
	preferenceSamplesMap[EmergingOthersCode] = preferenceSamplesMap[OthersCode];
	return true;
}

bool StanModel::loadTrendData(FeedbackFunc feedback)
{
	partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		feedback("No party codes found!");
		return false;
	}
	if (!contains(partyCodeVec, OthersCode)) {
		feedback("No party corresponding to Others was given. The model needs a party with code " + OthersCode + " to run properly.");
		return false;
	}
	if (!contains(partyCodeVec, UnnamedOthersCode)) {
		feedback("No party corresponding to Unnamed Others was given. The model needs a party with code " + UnnamedOthersCode + " to run properly.");
		return false;
	}
	startDate = wxInvalidDateTime;
	rawSupport.clear();
	for (auto partyCode : partyCodeVec) {
		auto& series = rawSupport[partyCode];
		if (partyCode == EmergingOthersCode) continue;
		if (partyCode == UnnamedOthersCode) continue; // calculate this later
		std::string filename = "analysis/Outputs/fp_trend_"
			+ termCode + "_" + partyCode + " FP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
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
	{
		auto partyCode = partyCodeVec[0];
		auto& series = rawTppSupport;
		std::string filename = "analysis/Outputs/fp_trend_"
			+ termCode + "_@TPP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
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
	return true;
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
		if (key == EmergingOthersCode) {
			sample.voteShare.insert({ key, 0.0 });
			continue;
		}
		float uniform = rng.uniform(0.0, 1.0);
		int lowerBucket = std::clamp(int(floor(uniform * float(Spread::Size - 1))), 0, int(Spread::Size) - 2);
		float upperMix = std::fmod(uniform * float(Spread::Size - 1), 1.0f);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = mix(lowerVote, upperVote, upperMix);
		sample.voteShare.insert({ key, sampledVote });
	}

	// This "raw TPP" may not be congruent with the primary votes above, but the adjustment
	// process will only use one or the other, so that doesn't matter
	float uniform = rng.uniform(0.0, 1.0);
	int lowerBucket = std::clamp(int(floor(uniform * float(Spread::Size - 1))), 0, int(Spread::Size) - 2);
	float upperMix = std::fmod(uniform * float(Spread::Size - 1), 1.0f);
	float lowerVote = rawTppSupport.timePoint[dayOffset].values[lowerBucket];
	float upperVote = rawTppSupport.timePoint[dayOffset].values[lowerBucket + 1];
	float sampledVote = mix(lowerVote, upperVote, upperMix);
	sample.voteShare.insert({ TppCode, sampledVote });

	updateOthersValue(sample);

	return sample;
}

StanModel::SupportSample StanModel::generateAdjustedSupportSample(wxDateTime date, int days) const
{
	if (!date.IsValid()) date = getEndDate();
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
				if (!isOthersCode(code) && !majorPartyCodes.count(code)) {
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
	constexpr int MinDays = 0;
	// the "days" parameter used for trend adjustment calculation is the number of days to the end
	// of the series, not the election itself - and the typical series ends 2 days before the election proper
	// So, reduce the number of days by 2 so that it's approximating the "end of series" since that's what the
	// adjustment model is trained on
	constexpr int DaysOffset = 2;
	days = std::clamp(days - DaysOffset, MinDays, numDays - 1);
	auto sample = rawSupportSample;
	constexpr float TppFirstChance = 0.5f;
	const bool tppFirst = rng.uniform() < TppFirstChance;
	for (auto& [key, voteShare] : sample.voteShare) {
		if (key == EmergingOthersCode) continue;
		if (tppFirst && (key == partyCodeVec[0] || key == partyCodeVec[1])) continue;
		if (!tppFirst && key == TppCode) continue;
		double transformedPolls = transformVoteShare(double(voteShare));

		const std::string partyGroup = reversePartyGroups.at(key);
		
		// remove systemic bias in poll results
		const double pollBiasToday = parameters.at(partyGroup)[days][int(InputParameters::PollBias)];
		const double debiasedPolls = transformedPolls - pollBiasToday;

		// remove systemic bias in previous-election average
		const double fundamentalsPrediction = transformVoteShare(fundamentals.at(key));
		const double fundamentalsBiasToday = parameters.at(partyGroup)[days][int(InputParameters::FundamentalsBias)];
		const double debiasedFundamentalsAverage = fundamentalsPrediction - fundamentalsBiasToday;

		// mix poll and previous values
		const double mixFactor = parameters.at(partyGroup)[days][int(InputParameters::MixFactor)];
		const double mixedVoteShare = mix(debiasedFundamentalsAverage, debiasedPolls, mixFactor);

		// adjust for residual bias in the mixed vote share
		const double mixedBiasToday = parameters.at(partyGroup)[days][int(InputParameters::MixedBias)];
		const double mixedDebiasedVote = mixedVoteShare - mixedBiasToday;

		// Get parameters for spread
		const double lowerError = parameters.at(partyGroup)[days][int(InputParameters::LowerError)];
		const double upperError = parameters.at(partyGroup)[days][int(InputParameters::UpperError)];
		const double lowerKurtosis = parameters.at(partyGroup)[days][int(InputParameters::LowerKurtosis)];
		const double upperKurtosis = parameters.at(partyGroup)[days][int(InputParameters::UpperKurtosis)];
		const double additionalVariation = rng.flexibleDist(0.0, lowerError, upperError, lowerKurtosis, upperKurtosis);
		const double voteWithVariation = mixedDebiasedVote + additionalVariation;

		double newVoteShare = detransformVoteShare(voteWithVariation);
		voteShare = float(newVoteShare);
	}

	addEmergingOthers(sample, days);
	if (!tppFirst) {
		normaliseSample(sample);
		updateOthersValue(sample);
		generateTppForSample(sample);
	}
	else {
		// Add rough approximations for major party fps so that normalisation works properly
		// Normalisation is needed so that high-rating minor parties crowd each other out as you'd expect
		for (int partyIndex = 0; partyIndex < 2; ++partyIndex) {
			sample.voteShare[partyCodeVec[partyIndex]] = fundamentals.at(reversePartyGroups.at(partyCodeVec[partyIndex]));
		}
		normaliseSample(sample);
		generateMajorFpForSample(sample);
		updateOthersValue(sample);
	}
	sample.daysToElection = days;
	return sample;
}

void StanModel::updateAdjustedData(FeedbackFunc feedback, int numThreads)
{
	constexpr static int NumIterations = 1000;
	adjustedSupport.clear(); // do this first as it should not be left with previous data
	try {
		int seriesLength = rawSupport.begin()->second.timePoint.size();
		for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
			std::string partyName = partyCodeVec[partyIndex];
			adjustedSupport[partyName] = Series();
			adjustedSupport[partyName].timePoint.resize(seriesLength);
		}
		tppSupport.timePoint.resize(seriesLength);

		constexpr int BatchSize = 10;
		for (int timeStart1 = 0; timeStart1 < seriesLength; timeStart1 += numThreads * BatchSize) {
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
							if (sample.voteShare.count(partyName)) {
								samples[partyIndex][iteration] = sample.voteShare[partyName];
							}
							if (sample.voteShare.count(TppCode)) {
								tppSamples[iteration] = sample.voteShare[TppCode];
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
			for (int timeStart = timeStart1; timeStart < timeStart1 + numThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
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

void StanModel::addEmergingOthers(StanModel::SupportSample& sample, int days) const
{
	const double threshold = emergingParameters[int(EmergingPartyParameters::Threshold)];
	const double transformedThreshold = transformVoteShare(threshold);
	const double baseEmergenceRate = emergingParameters[int(EmergingPartyParameters::EmergenceRate)];
	const double baseEmergenceRmse = emergingParameters[int(EmergingPartyParameters::Rmse)];
	const double kurtosis = emergingParameters[int(EmergingPartyParameters::Kurtosis)];
	// Guessed values for a curve that makes the chance of a new party emerging
	// decrease approaching election day. E.g. 100 days out it is halved, on election day it's about 16%
	double emergenceChance = baseEmergenceRate * (1.0 - 1.0 / (double(days) * 0.01 + 1.2));
	if (rng.uniform() > emergenceChance) {
		sample.voteShare[EmergingOthersCode] = 0.0;
		return;
	}
	// As above, but slightly different numbers and the curve takes longer to drop.
	double rmse = baseEmergenceRmse * (1.0 - 1.0 / (double(days) * 0.03 + 1.4));
	double emergingOthersFpTargetTransformed = transformedThreshold + abs(rng.flexibleDist(0.0, rmse, rmse, kurtosis, kurtosis));
	double emergingOthersFpTarget = detransformVoteShare(emergingOthersFpTargetTransformed);
	// The normalisation procedure will reduce the value of 
	double correctedFp = 100.0 * emergingOthersFpTarget / (100.0 - emergingOthersFpTarget) ;
	//PA_LOG_VAR(sample);
	//PA_LOG_VAR(days);
	//PA_LOG_VAR(threshold);
	//PA_LOG_VAR(transformedThreshold);
	//PA_LOG_VAR(baseEmergenceRate);
	//PA_LOG_VAR(baseEmergenceRmse);
	//PA_LOG_VAR(kurtosis);
	//PA_LOG_VAR(emergenceChance);
	//PA_LOG_VAR(rmse);
	//PA_LOG_VAR(emergingOthersFpTargetTransformed);
	//PA_LOG_VAR(emergingOthersFpTarget);
	//PA_LOG_VAR(correctedFp);
	sample.voteShare[EmergingOthersCode] = correctedFp;
}

void StanModel::Spread::calculateExpectation()
{
	float sum = std::accumulate(values.begin(), values.end(), 0.0f,
		[](float a, float b) {return a + b; });
	expectation = sum / float(values.size());
}

void StanModel::updateOthersValue(StanModel::SupportSample& sample) {
	// make sure "others" is actually equal to sum of non-major parties
	// note that this relies on there being an "exclusive others" component
	float otherSum = std::accumulate(sample.voteShare.begin(), sample.voteShare.end(), 0.0f,
		[](float a, decltype(sample.voteShare)::value_type b) {
			return (b.first == OthersCode || b.first == TppCode || majorPartyCodes.count(b.first) ? a : a + b.second);
		});
	sample.voteShare[OthersCode] = otherSum;
}

void StanModel::normaliseSample(StanModel::SupportSample& sample)
{
	float sampleSum = std::accumulate(sample.voteShare.begin(), sample.voteShare.end(), 0.0f,
		[](float a, decltype(sample.voteShare)::value_type b) {
			return (b.first == OthersCode || b.first == TppCode ? a : a + b.second); }
		);
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample.voteShare) {
		if (vote.first != TppCode) {
			vote.second *= sampleAdjust;
		}
	}
}

void StanModel::generateTppForSample(StanModel::SupportSample& sample) const
{
	float tpp = 0.0f;
	for (auto [key, support] : sample.voteShare) {
		if (key == OthersCode) continue;
		if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomisedFlow = (historicalSamples >= 2
			? rng.scaledTdist(int(std::floor(historicalSamples)) - 1, flow, deviation)
			: flow);
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		sample.preferenceFlow.insert({ key, randomisedFlow * 100.0f });
		tpp += support * randomisedFlow;
	}
	sample.voteShare[TppCode] = tpp;
}

void StanModel::generateMajorFpForSample(StanModel::SupportSample& sample) const
{
	float tpp = 0.0f;
	float totalFp = 0.0f;
	// First add up all party-one preference from minor parties
	for (auto [key, support] : sample.voteShare) {
		if (key == OthersCode) continue;
		if (key == TppCode || key == partyCodeVec[0] || key == partyCodeVec[1]) continue;
		if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomisedFlow = (historicalSamples >= 2
			? rng.scaledTdist(int(std::floor(historicalSamples)) - 1, flow, deviation)
			: flow);
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		sample.preferenceFlow.insert({ key, randomisedFlow * 100.0f });
		tpp += support * randomisedFlow;
		totalFp += support;
	}
	sample.preferenceFlow[partyCodeVec[0]] = 100.0f;
	sample.preferenceFlow[partyCodeVec[1]] = 0.0f;
	// Now we have the contribution to tpp from minors, so the difference between this and the total tpp gives the party-one fp
	float targetTpp = sample.voteShare[TppCode];
	float partyOneFp = sample.voteShare[TppCode] - tpp;
	float partyTwoFp = 100.0f - (totalFp + partyOneFp);
	float minMajorFp = std::min(partyOneFp, partyTwoFp);
	if (minMajorFp >= 1.0f) {
		sample.voteShare[partyCodeVec[0]] = partyOneFp;
		sample.voteShare[partyCodeVec[1]] = partyTwoFp;
	}
	else {
		if (partyOneFp < 1.0f) {
			float deficit = 1.0f - partyOneFp;
			partyOneFp = 1.0f;
			partyTwoFp += deficit * ((100.0f - targetTpp) / targetTpp);
		}
		if (partyTwoFp < 1.0f) {
			float deficit = 1.0f - partyTwoFp;
			partyTwoFp = 1.0f;
			partyOneFp += deficit * (targetTpp / (100.0f - targetTpp));
		}
		sample.voteShare[partyCodeVec[0]] = partyOneFp;
		sample.voteShare[partyCodeVec[1]] = partyTwoFp;
		normaliseSample(sample);
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
