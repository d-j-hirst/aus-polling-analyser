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

std::mutex debugMutex;

RandomGenerator StanModel::rng = RandomGenerator();

StanModel::MajorPartyCodes StanModel::majorPartyCodes = 
	{ "ALP", "LNP", "LIB", "NAT", "GRN" };

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

wxDateTime StanModel::getEndDate() const
{
	if (!rawSeriesCount()) return startDate;
	return startDate + wxTimeSpan(4) + wxDateSpan::Days(rawSupport.begin()->second.timePoint.size() - 1);
}

void StanModel::loadData(FeedbackFunc feedback, int numThreads)
{
	logger << "Starting model run: " << wxDateTime::Now().FormatISOCombined() << "\n";
	if (!prepareForRun(feedback)) return;
	logger << "Model end date: " << getEndDate().FormatISOCombined() << "\n";
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
	if (!loadModelledPolls(feedback)) return false;
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

bool StanModel::loadModelledPolls(FeedbackFunc feedback)
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
	if (!contains(partyCodeVec, EmergingOthersCode)) {
		feedback("No party corresponding to Emerging Others was given. The model needs a party with code " + EmergingOthersCode + " to run properly.");
		return false;
	}
	modelledPolls.clear();

	// This section is used multiple times only inside this procedure, so define it once
	auto loadPolls = [](std::vector<ModelledPoll>& polls, std::ifstream& file) {
		polls.clear();
		std::string line;
		std::getline(file, line); // first line is just a legend, skip it
		do {
			std::getline(file, line);
			if (!file) break;
			auto pollVals = splitString(line, ",");
			ModelledPoll poll;
			poll.pollster = pollVals[0];
			poll.day = std::stoi(pollVals[1]);
			poll.base = std::stof(pollVals[2]);
			poll.adjusted = std::stof(pollVals[3]);
			if (pollVals.size() >= 5) poll.reported = std::stof(pollVals[4]);
			polls.push_back(poll);
		} while (true);
	};

	for (auto partyCode : partyCodeVec) {
		if (partyCode == EmergingOthersCode) continue;
		if (partyCode == UnnamedOthersCode) continue; // calculate this later
		std::string filename = "analysis/Outputs/fp_polls_"
			+ termCode + "_" + partyCode + " FP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
		}
		auto& polls = modelledPolls[partyCode];
		loadPolls(polls, file);
	}
	{
		auto partyCode = partyCodeVec[0];
		std::string filename = "analysis/Outputs/fp_polls_"
			+ termCode + "_@TPP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
		}
		auto& polls = modelledPolls["@TPP"];
		loadPolls(polls, file);
	}
	return true;
}

void StanModel::loadPreferenceFlows(FeedbackFunc feedback)
{
	preferenceFlowMap.clear();
	preferenceExhaustMap.clear();
	auto lines = extractElectionDataFromFile("analysis/Data/preference-estimates.csv", termCode);
	for (auto const& line : lines) {
		std::string party = splitString(line[2], " ")[0];
		float thisPreferenceFlow = std::stof(line[3]);
		preferenceFlowMap[party] = thisPreferenceFlow;
		if (line.size() >= 5 && line[4][0] != '#') {
			float thisExhaustRate = std::stof(line[4]);
			preferenceExhaustMap[party] = thisExhaustRate;
		}
		else {
			preferenceExhaustMap[party] = 0.0f;
		}
	}

	preferenceFlowMap[EmergingOthersCode] = preferenceFlowMap[OthersCode];
	preferenceFlowMap[UnnamedOthersCode] = preferenceFlowMap[OthersCode];
	preferenceExhaustMap[EmergingOthersCode] = preferenceExhaustMap[OthersCode];
	preferenceExhaustMap[UnnamedOthersCode] = preferenceExhaustMap[OthersCode];
	preferenceFlowMap[partyCodeVec[0]] = 100.0f;
	preferenceFlowMap[partyCodeVec[1]] = 0.0f;
	preferenceExhaustMap[partyCodeVec[0]] = 0.0f;
	preferenceExhaustMap[partyCodeVec[1]] = 0.0f;
}

bool StanModel::generatePreferenceMaps(FeedbackFunc feedback)
{
	// partyCodeVec is already created by loadData
	partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size()) throw Exception("No party codes in this model!");
	try {
		loadPreferenceFlows(feedback);
		//auto preferenceFlowVec = splitStringF(preferenceFlow, ",");
		auto preferenceDeviationVec = splitStringF(preferenceDeviation, ",");
		auto preferenceSamplesVec = splitStringF(preferenceSamples, ",");
		bool validSizes = 
			preferenceDeviationVec.size() == partyCodeVec.size() &&
			preferenceSamplesVec.size() == partyCodeVec.size();
		if (!validSizes) throw Exception("Party codes and parameter lines do not match!");
		//preferenceFlowMap.clear();
		preferenceDeviationMap.clear();
		preferenceSamplesMap.clear();
		for (int index = 0; index < int(partyCodeVec.size()); ++index) {
			//preferenceFlowMap.insert({ partyCodeVec[index], preferenceFlowVec[index] });
			preferenceDeviationMap.insert({ partyCodeVec[index], preferenceDeviationVec[index] });
			preferenceSamplesMap.insert({ partyCodeVec[index], preferenceSamplesVec[index] });
		}
	}
	catch (std::invalid_argument) {
		feedback("One or more model paramater lists could not be converted to floats!");
		return false;
	}
	//preferenceFlowMap[EmergingOthersCode] = preferenceFlowMap[OthersCode];
	preferenceDeviationMap[EmergingOthersCode] = preferenceDeviationMap[OthersCode];
	preferenceSamplesMap[EmergingOthersCode] = preferenceSamplesMap[OthersCode];
	PA_LOG_VAR(preferenceFlowMap);
	PA_LOG_VAR(preferenceExhaustMap);
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
		auto& series = rawSupport[partyCode]; // this needs to go here so that the Unnamed Others series is generated later on
		if (partyCode == EmergingOthersCode) continue;
		if (partyCode == UnnamedOthersCode) continue;
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
		PA_LOG_VAR(startDate.FormatISOCombined());
		PA_LOG_VAR(series.timePoint.size());
	}
	return true;
}

StanModel::SupportSample StanModel::generateRawSupportSample(wxDateTime date) const
{
	thread_local static std::map<std::string, float> mirroring;
	if (!rawSupport.size()) return SupportSample();
	int seriesLength = rawSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int dayOffset = rawSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, date.Subtract(startDate).GetDays());
	//if (dayOffset < 0) dayOffset = 0;
	SupportSample sample;
	for (auto [key, support] : rawSupport) {
		if (key == EmergingOthersCode) {
			sample.voteShare.insert({ key, 0.0 });
			continue;
		}
		// Mirroring ensures the underlying uniform distribution is symmetric, so that the
		// median never deviates as a result of random variation.
		float quantile = 0.0f;
		if (mirroring.contains(key)) {
			quantile = mirroring[key];
			mirroring.erase(key);
		}
		else {
			quantile = rng.uniform(0.0f, 1.0f);
			mirroring[key] = 1.0f - quantile;
		}
		int lowerBucket = std::clamp(int(floor(quantile * float(Spread::Size - 1))), 0, int(Spread::Size) - 2);
		float upperMix = std::fmod(quantile * float(Spread::Size - 1), 1.0f);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = mix(lowerVote, upperVote, upperMix);
		sample.voteShare.insert({ key, sampledVote });
	}

	// This "raw TPP" may not be congruent with the primary votes above, but the adjustment
	// process will only use one or the other, so that doesn't matter
	float quantile = 0.0f;
	if (mirroring.contains(TppCode)) {
		quantile = mirroring[TppCode];
		mirroring.erase(TppCode);
	}
	else {
		quantile = rng.uniform(0.0f, 1.0f);
		mirroring[TppCode] = 1.0f - quantile;
	}
	int lowerBucket = std::clamp(int(floor(quantile * float(Spread::Size - 1))), 0, int(Spread::Size) - 2);
	float upperMix = std::fmod(quantile * float(Spread::Size - 1), 1.0f);
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
	thread_local static std::map<std::string, double> mirroring;
	constexpr int MinDays = 0;
	// the "days" parameter used for trend adjustment calculation is the number of days to the end
	// of the series, not the election itself - and the typical series ends 3 days before the election proper
	// So, reduce the number of days by 3 so that it's approximating the "end of series" since that's what the
	// adjustment model is trained on (in the average case)
	constexpr int DaysOffset = 4;
	days = std::clamp(days - DaysOffset, MinDays, numDays - 1);
	auto sample = rawSupportSample;
	constexpr float TppFirstChance = 0.5f;
	bool tppFirst = rng.uniform() < TppFirstChance;
	constexpr bool IncludeVariation = true;
	for (auto& [key, voteShare] : sample.voteShare) {
		if (key == EmergingOthersCode) continue;
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

		// for debugging purposes we often want to avoid adding additional variation here
		if (IncludeVariation) {
			// Get parameters for spread
			const double lowerError = parameters.at(partyGroup)[days][int(InputParameters::LowerError)];
			const double upperError = parameters.at(partyGroup)[days][int(InputParameters::UpperError)];
			const double lowerKurtosis = parameters.at(partyGroup)[days][int(InputParameters::LowerKurtosis)];
			const double upperKurtosis = parameters.at(partyGroup)[days][int(InputParameters::UpperKurtosis)];

			double quantile = 0.0;
			if (mirroring.contains(key)) {
				quantile = mirroring[key];
				mirroring.erase(key);
			}
			else {
				quantile = rng.uniform(0.0, 1.0);
				mirroring[key] = 1.0 - quantile;
			}

			const double additionalVariation = rng.flexibleDist(0.0, lowerError, upperError, lowerKurtosis, upperKurtosis, quantile);
			const double voteWithVariation = mixedDebiasedVote + additionalVariation;

			double newVoteShare = detransformVoteShare(voteWithVariation);
			voteShare = float(newVoteShare);
		}
		else {
			double newVoteShare = detransformVoteShare(mixedDebiasedVote);
			voteShare = float(newVoteShare);

			//if ((key == "ALP" || key == "LNP") && days == 0) {
			//	std::lock_guard lock(debugMutex);
			//	PA_LOG_VAR(days);
			//	PA_LOG_VAR(voteShare);
			//	PA_LOG_VAR(partyGroup);
			//	PA_LOG_VAR(transformedPolls);
			//	PA_LOG_VAR(pollBiasToday);
			//	PA_LOG_VAR(debiasedPolls);
			//	PA_LOG_VAR(fundamentalsPrediction);
			//	PA_LOG_VAR(fundamentalsBiasToday);
			//	PA_LOG_VAR(debiasedFundamentalsAverage);
			//	PA_LOG_VAR(mixFactor);
			//	PA_LOG_VAR(mixedVoteShare);
			//	PA_LOG_VAR(mixedBiasToday);
			//	PA_LOG_VAR(mixedDebiasedVote);
			//	PA_LOG_VAR(newVoteShare);
			//	PA_LOG_VAR(voteShare);
			//}
		}
	}

	addEmergingOthers(sample, days);
	if (!tppFirst) {
		//if (days == 0) {
		//	std::lock_guard lock(debugMutex);
		//	PA_LOG_VAR("Checkpoint A");
		//	PA_LOG_VAR(sample.voteShare["ALP"]);
		//	PA_LOG_VAR(sample.voteShare["LNP"]);
		//}
		normaliseSample(sample);
		//if (days == 0) {
		//	std::lock_guard lock(debugMutex);
		//	PA_LOG_VAR("Checkpoint A");
		//	PA_LOG_VAR(sample.voteShare["ALP"]);
		//	PA_LOG_VAR(sample.voteShare["LNP"]);
		//}
		updateOthersValue(sample);
		//if (days == 0) {
		//	std::lock_guard lock(debugMutex);
		//	PA_LOG_VAR("Checkpoint A");
		//	PA_LOG_VAR(sample.voteShare["ALP"]);
		//	PA_LOG_VAR(sample.voteShare["LNP"]);
		//}
		generateTppForSample(sample);
		//if (days == 0) {
		//	std::lock_guard lock(debugMutex);
		//	PA_LOG_VAR("Checkpoint A");
		//	PA_LOG_VAR(sample.voteShare["ALP"]);
		//	PA_LOG_VAR(sample.voteShare["LNP"]);
		//}
	}
	else {
		normaliseSample(sample);
		generateMajorFpForSample(sample);
		updateOthersValue(sample);
	}
	sample.daysToElection = days;
	return sample;
}

void StanModel::updateAdjustedData(FeedbackFunc feedback, int numThreads)
{
	int numIterations = 2000;
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
					// Add 12 hours to avoid DST related shenanigans
					thisDate.Add(wxTimeSpan(4)).Add(wxDateSpan(0, 0, 0, time));
					// Extra accuracy for the final data point, since it's much more important than the rest
					int localIterations = time == seriesLength - 1 ? numIterations * 100 : numIterations;
					std::vector<std::vector<float>> samples(partyCodeVec.size(), std::vector<float>(localIterations));
					std::vector<float> tppSamples(localIterations);
					for (int iteration = 0; iteration < localIterations; ++iteration) {
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
							int sampleIndex = std::min(localIterations - 1, percentile * localIterations / int(Spread::Size));
							adjustedSupport[partyName].timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
						}
					}
					std::sort(tppSamples.begin(), tppSamples.end());
					for (int percentile = 0; percentile < Spread::Size; ++percentile) {
						int sampleIndex = std::min(localIterations - 1, percentile * localIterations / int(Spread::Size));
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

		const int ModelSmoothingDays = 7;
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
	double correctedFp = 100.0 * emergingOthersFpTarget / (100.0 - emergingOthersFpTarget);
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
	float partyOneTpp = 0.0f;
	float totalTpp = 0.0f;
	for (auto [key, support] : sample.voteShare) {
		if (key == OthersCode) continue;
		if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomPreferenceVariation = historicalSamples >= 2
			? rng.scaledTdist(int(std::floor(historicalSamples)) - 1, 0.0f, deviation)
			: 0.0f;
		float randomisedFlow = basicTransformedSwing(flow, randomPreferenceVariation);
		float exhaustRate = preferenceExhaustMap.at(key) * 0.01f;
		// distribution approximately taken from NSW elections
		float randomExhaustVariation = rng.scaledTdist(6, 0.0f, 0.054f);
		float randomisedExhaustRate = exhaustRate ? basicTransformedSwing(exhaustRate, randomExhaustVariation) : 0.0f;
		sample.preferenceFlow.insert({ key, randomisedFlow * 100.0f });
		sample.exhaustRate.insert({ key, randomisedExhaustRate });
		partyOneTpp += support * randomisedFlow * (1.0f - randomisedExhaustRate);
		totalTpp += support * (1.0f - randomisedExhaustRate);
	}
	sample.voteShare[TppCode] = partyOneTpp * (100.0f / totalTpp);
}

void StanModel::generateMajorFpForSample(StanModel::SupportSample& sample) const
{
	float partyOneTpp = 0.0f;
	float totalFp = 0.0f;
	float exhaustedFp = 0.0f;
	// First add up all party-one preference from minor parties
	for (auto [key, support] : sample.voteShare) {
		if (key == OthersCode) continue;
		if (key == TppCode || key == partyCodeVec[0] || key == partyCodeVec[1]) continue;
		if (!preferenceFlowMap.count(key) || !preferenceDeviationMap.count(key) || !preferenceSamplesMap.count(key)) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomVariation = historicalSamples >= 2
			? rng.scaledTdist(int(std::floor(historicalSamples)) - 1, 0.0f, deviation)
			: 0.0f;
		float randomisedFlow = basicTransformedSwing(flow, randomVariation);
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		float exhaustRate = preferenceExhaustMap.at(key) * 0.01f;
		// distribution approximately taken from NSW elections
		float randomExhaustVariation = rng.scaledTdist(6, 0.0f, 0.054f);
		float randomisedExhaustRate = exhaustRate ? basicTransformedSwing(exhaustRate, randomExhaustVariation) : 0.0f;
		sample.preferenceFlow.insert({ key, randomisedFlow * 100.0f });
		sample.exhaustRate.insert({ key, randomisedExhaustRate });
		partyOneTpp += support * randomisedFlow * (1.0f - randomisedExhaustRate);
		totalFp += support;
		exhaustedFp += support * randomisedExhaustRate;
	}
	sample.preferenceFlow[partyCodeVec[0]] = 100.0f;
	sample.preferenceFlow[partyCodeVec[1]] = 0.0f;
	sample.exhaustRate[partyCodeVec[0]] = 0.0f;
	sample.exhaustRate[partyCodeVec[1]] = 0.0f;
	// Now we have the contribution to tpp from minors, so the difference between this and the total tpp gives the party-one fp
	float targetTpp = sample.voteShare[TppCode];
	float partyOneFp = (sample.voteShare[TppCode] - partyOneTpp) * (100.0f - exhaustedFp) / 100.0f;
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

bool StanModel::dumpGeneratedData(std::string filename) const {
	std::ofstream file(filename, std::ios::binary);
	if (!file) return false;
	
	// Define helper lambdas for serialization
	auto writeString = [&file](const std::string& str) {
		size_t size = str.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		file.write(str.c_str(), size);
	};
	
	auto writePartyParameters = [&file, &writeString](const PartyParameters& params) {
		size_t size = params.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		for (const auto& [key, value] : params) {
			writeString(key);
			file.write(reinterpret_cast<const char*>(&value), sizeof(value));
		}
	};
	
	auto writeSeries = [&file](const Series& series) {
		size_t size = series.timePoint.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		for (const auto& tp : series.timePoint) {
			file.write(reinterpret_cast<const char*>(&tp.values), sizeof(tp.values));
			file.write(reinterpret_cast<const char*>(&tp.expectation), sizeof(tp.expectation));
		}
	};
	
	auto writeSupportSeries = [&file, &writeString, &writeSeries](const PartySupport& support) {
		size_t size = support.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		for (const auto& [key, series] : support) {
			writeString(key);
			writeSeries(series);
		}
	};
	
	auto writeMap = [&file, &writeString](const auto& map) {
		size_t size = map.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		for (const auto& [key, value] : map) {
			writeString(key);
			writeString(value);
		}
	};
	
	auto writeFundamentals = [&file, &writeString](const Fundamentals& fundamentals) {
		size_t size = fundamentals.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		for (const auto& [key, value] : fundamentals) {
			writeString(key);
			file.write(reinterpret_cast<const char*>(&value), sizeof(value));
		}
	};
	
	auto writeParameters = [&file, &writeString](const ParameterSeriesByPartyGroup& params) {
		size_t mapSize = params.size();
		file.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
		for (const auto& [groupKey, series] : params) {
			writeString(groupKey);
			size_t seriesSize = series.size();
			file.write(reinterpret_cast<const char*>(&seriesSize), sizeof(seriesSize));
			for (const auto& paramSet : series) {
				file.write(reinterpret_cast<const char*>(&paramSet), sizeof(paramSet));
			}
		}
	};
	
	auto writeModelledPolls = [&file, &writeString](const ModelledPolls& polls) {
		size_t mapSize = polls.size();
		file.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
		for (const auto& [partyKey, pollVec] : polls) {
			writeString(partyKey);
			size_t vecSize = pollVec.size();
			file.write(reinterpret_cast<const char*>(&vecSize), sizeof(vecSize));
			for (const auto& poll : pollVec) {
				writeString(poll.pollster);
				file.write(reinterpret_cast<const char*>(&poll.day), sizeof(poll.day));
				file.write(reinterpret_cast<const char*>(&poll.base), sizeof(poll.base));
				file.write(reinterpret_cast<const char*>(&poll.adjusted), sizeof(poll.adjusted));
				file.write(reinterpret_cast<const char*>(&poll.reported), sizeof(poll.reported));
			}
		}
	};
	
	// Write numDays
	file.write(reinterpret_cast<const char*>(&numDays), sizeof(numDays));
	
	// Write support series (rawSupport, adjustedSupport, tppSupport)
	writeSupportSeries(rawSupport);
	writeSeries(rawTppSupport);
	writeSupportSeries(adjustedSupport);
	writeSeries(tppSupport);
	
	// Write modelled polls
	writeModelledPolls(modelledPolls);
	
	// Write reversePartyGroups (needed for party group lookup)
	writeMap(reversePartyGroups);
	
	// Write fundamentals (needed for predictions)
	writeFundamentals(fundamentals);
	
	// Write parameters
	writeParameters(parameters);
	
	// Write emerging parameters
	file.write(reinterpret_cast<const char*>(&emergingParameters), 
			   sizeof(emergingParameters));
	
	// Write party codes vector
	size_t partyCodeSize = partyCodeVec.size();
	file.write(reinterpret_cast<const char*>(&partyCodeSize), sizeof(partyCodeSize));
	for (const auto& code : partyCodeVec) {
		writeString(code);
	}
	
	// Write preference maps
	writePartyParameters(preferenceFlowMap);
	writePartyParameters(preferenceExhaustMap);
	writePartyParameters(preferenceDeviationMap);
	writePartyParameters(preferenceSamplesMap);
	
	// Write readyForProjection flag
	file.write(reinterpret_cast<const char*>(&readyForProjection), 
			   sizeof(readyForProjection));
	
	return true;
}

bool StanModel::loadGeneratedData(std::string filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file) return false;
	
	// Define helper lambdas for deserialization
	auto readString = [&file](std::string& str) {
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		str.resize(size);
		file.read(&str[0], size);
	};
	
	auto readPartyParameters = [&file, &readString](PartyParameters& params) {
		params.clear();
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		for (size_t i = 0; i < size; i++) {
			std::string key;
			float value;
			readString(key);
			file.read(reinterpret_cast<char*>(&value), sizeof(value));
			params[key] = value;
		}
	};
	
	auto readSeries = [&file](Series& series) {
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		series.timePoint.resize(size);
		for (size_t i = 0; i < size; i++) {
			file.read(reinterpret_cast<char*>(&series.timePoint[i].values), 
					 sizeof(series.timePoint[i].values));
			file.read(reinterpret_cast<char*>(&series.timePoint[i].expectation), 
					 sizeof(series.timePoint[i].expectation));
		}
	};
	
	auto readSupportSeries = [&file, &readString, &readSeries](PartySupport& support) {
		support.clear();
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		for (size_t i = 0; i < size; i++) {
			std::string key;
			readString(key);
			readSeries(support[key]);
		}
	};
	
	auto readMap = [&file, &readString](auto& map) {
		map.clear();
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		for (size_t i = 0; i < size; i++) {
			std::string key, value;
			readString(key);
			readString(value);
			map[key] = value;
		}
	};
	
	auto readFundamentals = [&file, &readString](Fundamentals& fundamentals) {
		fundamentals.clear();
		size_t size;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		for (size_t i = 0; i < size; i++) {
			std::string key;
			double value;
			readString(key);
			file.read(reinterpret_cast<char*>(&value), sizeof(value));
			fundamentals[key] = value;
		}
	};
	
	auto readParameters = [&file, &readString](ParameterSeriesByPartyGroup& params) {
		params.clear();
		size_t mapSize;
		file.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
		for (size_t i = 0; i < mapSize; i++) {
			std::string groupKey;
			readString(groupKey);
			size_t seriesSize;
			file.read(reinterpret_cast<char*>(&seriesSize), sizeof(seriesSize));
			auto& series = params[groupKey];
			series.resize(seriesSize);
			for (size_t j = 0; j < seriesSize; j++) {
				file.read(reinterpret_cast<char*>(&series[j]), sizeof(series[j]));
			}
		}
	};
	
	auto readModelledPolls = [&file, &readString](ModelledPolls& polls) {
		polls.clear();
		size_t mapSize;
		file.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
		for (size_t i = 0; i < mapSize; i++) {
			std::string partyKey;
			readString(partyKey);
			size_t vecSize;
			file.read(reinterpret_cast<char*>(&vecSize), sizeof(vecSize));
			auto& pollVec = polls[partyKey];
			pollVec.resize(vecSize);
			for (size_t j = 0; j < vecSize; j++) {
				readString(pollVec[j].pollster);
				file.read(reinterpret_cast<char*>(&pollVec[j].day), sizeof(pollVec[j].day));
				file.read(reinterpret_cast<char*>(&pollVec[j].base), sizeof(pollVec[j].base));
				file.read(reinterpret_cast<char*>(&pollVec[j].adjusted), sizeof(pollVec[j].adjusted));
				file.read(reinterpret_cast<char*>(&pollVec[j].reported), sizeof(pollVec[j].reported));
			}
		}
	};
	
	// Read numDays
	file.read(reinterpret_cast<char*>(&numDays), sizeof(numDays));
	
	// Read support series (rawSupport, adjustedSupport, tppSupport)
	readSupportSeries(rawSupport);
	readSeries(rawTppSupport);
	readSupportSeries(adjustedSupport);
	readSeries(tppSupport);
	
	// Read modelled polls
	readModelledPolls(modelledPolls);
	
	// Read reversePartyGroups
	readMap(reversePartyGroups);
	
	// Read fundamentals
	readFundamentals(fundamentals);
	
	// Read parameters
	readParameters(parameters);
	
	// Read emerging parameters
	file.read(reinterpret_cast<char*>(&emergingParameters), 
			  sizeof(emergingParameters));
	
	// Read party codes vector
	size_t partyCodeSize;
	file.read(reinterpret_cast<char*>(&partyCodeSize), sizeof(partyCodeSize));
	partyCodeVec.resize(partyCodeSize);
	for (size_t i = 0; i < partyCodeSize; i++) {
		readString(partyCodeVec[i]);
	}
	
	// Read preference maps
	readPartyParameters(preferenceFlowMap);
	readPartyParameters(preferenceExhaustMap);
	readPartyParameters(preferenceDeviationMap);
	readPartyParameters(preferenceSamplesMap);
	
	// Read readyForProjection flag
	file.read(reinterpret_cast<char*>(&readyForProjection), 
			  sizeof(readyForProjection));
	
	return true;
}
