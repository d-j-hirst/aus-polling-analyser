#include "StanModel.h"

#include "General.h"
#include "Log.h"
#include "SpecialPartyCodes.h"
#include "WorkspacePaths.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <future>
#include <numeric>
#include <sstream>
#include <utility>

constexpr int MedianSpreadValue = StanModel::Spread::Size / 2;

constexpr bool DoValidations = false;
// The magic value identifies generated-model caches. Version 4 adds ABI
// metadata because this temporary format contains native-width values.
constexpr std::uint32_t GeneratedDataMagic = 0x50414d32;
constexpr std::uint32_t GeneratedDataVersion = 4;
constexpr std::uint32_t GeneratedDataEndianMarker = 0x01020304;

StanModel::MajorPartyCodes StanModel::majorPartyCodes = 
	{ "ALP", "LNP", "LIB", "NAT", "GRN" };

enum class VariabilityTag : std::uint32_t {
	FpRawSupport = 1,
	TppRawSupport = 2,
  FpAdjustedSupport = 3,
	EmergingOthers = 4,
  TppPreferenceFlow = 5,
  MajorFpPreferenceFlow = 6,
	TppExhaustRate = 7,
	FpExhaustRate = 8,
	TppFirstSelection = 9,
	EmergingOthersSize = 10
};

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes)
	
	: name(std::move(name)), termCode(std::move(termCode)),
	partyCodes(std::move(partyCodes))
{
}

Date StanModel::getEndDate() const
{
	if (!startDate.isValid() || rawTppSupport.timePoint.empty()) return startDate;
	return startDate + int(rawTppSupport.timePoint.size()) - 1;
}

bool StanModel::loadData(
	WorkspacePaths const& paths, FeedbackFunc feedback, int numThreads)
{
	logger << "Starting model run: " << Timestamp::now().formatIsoLocal() << "\n";
	if (!prepareForRun(paths, feedback)) return false;
	logger << "Model end date: " << getEndDate().formatIso() << "\n";
	logger << "Prepared model inputs: " << Timestamp::now().formatIsoLocal() << "\n";
	if (!updateAdjustedData(feedback, numThreads)) {
		readyForProjection = false;
		return false;
	}
	logger << "updated adjusted data: " << Timestamp::now().formatIsoLocal() << "\n";
	lastUpdatedDate = Timestamp::now();
	feedback("Finished loading models");
	return true;
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
	for (auto const& [key, series] : rawSupport) {
		if (series.timePoint.empty()) continue;
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
	}
	ss << ";";
	ss << "Adjusted party support, accounting for possible systemic bias and variability:\n";
	for (auto const& [key, series] : adjustedSupport) {
		if (series.timePoint.empty()) continue;
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
		ss << "Expectation: " << series.timePoint.back().expectation << "\n";
	}
	if (!tppSupport.timePoint.empty()) {
		ss << "TPP:\n";
		ss << "1%: " << tppSupport.timePoint.back().values[1] << "\n";
		ss << "10%: " << tppSupport.timePoint.back().values[10] << "\n";
		ss << "50%: " << tppSupport.timePoint.back().values[50] << "\n";
		ss << "90%: " << tppSupport.timePoint.back().values[90] << "\n";
		ss << "99%: " << tppSupport.timePoint.back().values[99] << "\n";
		ss << "Expectation: " << tppSupport.timePoint.back().expectation << "\n";
	}
	if (DoValidations) {
		ss << ";";
		ss << "Post-sampling party support:\n";
		for (auto const& [key, series] : validationSupport) {
			if (series.timePoint.empty()) continue;
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
	if (index < 0 || index >= int(rawSupport.size())) return {};
	return std::next(rawSupport.begin(), index)->first;
}

bool StanModel::prepareForRun(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	readyForProjection = false;
	rawSupport.clear();
	rawTppSupport = {};
	adjustedSupport.clear();
	tppSupport = {};
	validationSupport.clear();
	modelledPolls.clear();
	if (!loadPartyCodes(feedback)) return false;
	if (!loadPartyGroups(paths, feedback)) return false;
	if (!loadFundamentalsPredictions(paths, feedback)) return false;
	if (!loadParameters(paths, feedback)) return false;
	if (!loadEmergingOthersParameters(paths, feedback)) return false;
	if (!generatePreferenceMaps(paths, feedback)) return false;
	if (!loadModelledPolls(paths, feedback)) return false;
	if (!loadTrendData(paths, feedback)) return false;
	generateUnnamedOthersSeries();
	readyForProjection = true;
	return true;
}

std::string StanModel::generatedDataCacheFilename() const
{
	if (termCode.empty()) {
		throw std::logic_error(
			"The model needs an election term code before its cache can be used.");
	}
	for (unsigned char character : termCode) {
		if (!std::isalnum(character) &&
			character != '-' && character != '_') {
			throw std::logic_error(
				"The model term code contains a character that cannot be used "
				"in a cache filename: " + termCode);
		}
	}
	return "model-" + termCode + ".bin";
}

bool StanModel::loadPartyCodes(FeedbackFunc feedback)
{
	partyCodeVec = splitString(partyCodes, ",");
	if (partyCodeVec.size() < 2) {
		feedback("At least two party codes are required.");
		return false;
	}
	std::set<std::string> uniqueCodes;
	for (auto const& partyCode : partyCodeVec) {
		if (partyCode.empty()) {
			feedback("Party codes cannot be empty.");
			return false;
		}
		if (!uniqueCodes.insert(partyCode).second) {
			feedback("Duplicate party code in model: " + partyCode);
			return false;
		}
	}
	for (auto const& requiredCode : { OthersCode, UnnamedOthersCode,
		EmergingOthersCode }) {
		if (!contains(partyCodeVec, requiredCode)) {
			feedback("The model needs a party with code " + requiredCode +
				" to run properly.");
			return false;
		}
	}
	return true;
}

bool StanModel::loadPartyGroups(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	const std::string filename = paths.resolveString(
		"analysis/Data/party-groups.csv");
	auto file = std::ifstream(filename);
	if (!file) {
		feedback("Party groups file not present. Expected a file at " + filename);
		return false;
	}
	partyGroups.clear();
	reversePartyGroups.clear();
	for (std::string line; std::getline(file, line);) {
		if (line.empty()) continue;
		auto values = splitString(line, ",");
		if (values.size() < 2 || values[0].empty()) {
			feedback("Invalid party-group row: " + line);
			return false;
		}
		auto& group = partyGroups[values[0]];
		group.clear();
		for (auto it = std::next(values.begin()); it != values.end(); ++it) {
			auto splitValues = splitString(*it, " ");
			if (splitValues.empty() || splitValues[0].empty()) {
				feedback("Invalid party code in party-group row: " + line);
				return false;
			}
			group.push_back(splitValues[0]);
		}
	}

	for (auto const& [key, values] : partyGroups) {
		for (auto const& value : values) {
			if (!reversePartyGroups.emplace(value, key).second) {
				feedback("Party code appears in more than one party group: " + value);
				return false;
			}
		}
	}
	for (auto const& partyCode : partyCodeVec) {
		if (partyCode == EmergingOthersCode) continue;
		if (!reversePartyGroups.count(partyCode)) {
			feedback("No party group configured for party code: " + partyCode);
			return false;
		}
	}
	if (!reversePartyGroups.count(TppCode)) {
		feedback("No party group configured for " + TppCode + ".");
		return false;
	}
	return true;
}

bool StanModel::loadFundamentalsPredictions(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	logger << "loading fundamentals predictions\n";
	const std::string filename = paths.resolveString(
		"analysis/Fundamentals/fundamentals_" + termCode + ".csv");
	auto file = std::ifstream(filename);
	if (!file) {
		feedback("Fundamentals prediction file not present. Expected a file at " + filename);
		return false;
	}
	fundamentals.clear();
	try {
		for (std::string line; std::getline(file, line);) {
			if (line.empty()) continue;
			auto values = splitString(line, ",");
			if (values.size() < 2) throw Exception("Invalid fundamentals row: " + line);
			auto partyValues = splitString(values[0], " ");
			if (partyValues.empty() || partyValues[0].empty()) {
				throw Exception("Invalid party code in fundamentals row: " + line);
			}
			double const value = std::stod(values[1]);
			if (!std::isfinite(value) || value <= 0.0 || value >= 100.0) {
				throw Exception("Fundamentals value must be between 0 and 100: " + line);
			}
			if (!fundamentals.emplace(partyValues[0], value).second) {
				throw Exception("Duplicate fundamentals row for " + partyValues[0]);
			}
		}
	}
	catch (std::exception const& e) {
		feedback(std::string("Could not load fundamentals predictions: ") + e.what());
		return false;
	}
	for (auto const& partyCode : partyCodeVec) {
		if (partyCode == EmergingOthersCode) continue;
		if (!fundamentals.count(partyCode)) {
			feedback("No fundamentals prediction configured for party code: " + partyCode);
			return false;
		}
	}
	if (!fundamentals.count(TppCode)) {
		feedback("No fundamentals prediction configured for " + TppCode + ".");
		return false;
	}
	return true;
}

bool StanModel::loadParameters(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	parameters = {};
	numDays = 0;
	constexpr int ParameterCount = int(InputParameters::Max);
	for (auto const& [partyGroup, partyList] : partyGroups) {
		// If there's a specific adjustment file for this election (usually only for hindcasts) use that
		// Otherwise (as for future elections) just use the general versions that use all past elections
		std::string electionFileName = paths.resolveString(
			"analysis/Adjustments/adjust_" + termCode + "_" + partyGroup + ".csv");
		std::string generalFileName = paths.resolveString(
			"analysis/Adjustments/adjust_0none_" + partyGroup + ".csv");
		std::string loadedFileName = electionFileName;
		auto file = std::ifstream(electionFileName);
		if (!file) {
			file = std::ifstream(generalFileName);
			loadedFileName = generalFileName;
		}
		if (!file) {
			feedback("Error: Could not find an adjustment file for party group: " + partyGroup);
			return false;
		}

		std::vector<std::vector<std::string>> rows;
		for (std::string line; std::getline(file, line);) {
			if (!line.empty()) rows.push_back(splitString(line, ","));
		}
		// Legacy files contain one eight-row parameter series. Current files
		// contain one such block per transformed support anchor, with the
		// anchor repeated in the first column of every row in its block.
		bool const legacyFormat = rows.size() == ParameterCount;
		if (!legacyFormat && (rows.empty() || rows.size() % ParameterCount)) {
			feedback("Error: Adjustment file has an invalid row count: " + loadedFileName);
			return false;
		}

		int const levelCount = legacyFormat ? 1 : int(rows.size()) / ParameterCount;
		int const firstValueColumn = legacyFormat ? 0 : 1;
		int const thisNumDays = int(rows[0].size()) - firstValueColumn;
		if (thisNumDays <= 0) {
			feedback("Error: Adjustment file has no daily values: " + loadedFileName);
			return false;
		}
		if (!numDays) {
			numDays = thisNumDays;
		}
		else if (numDays != thisNumDays) {
			feedback("Error: Adjustment files have inconsistent daily value counts: " + loadedFileName);
			return false;
		}

		ParameterGrid grid;
		try {
			auto parseFinite = [](std::string const& text) {
				double const value = std::stod(text);
				if (!std::isfinite(value)) {
					throw std::domain_error("non-finite adjustment parameter");
				}
				return value;
			};
			for (int levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
				int const firstRow = levelIndex * ParameterCount;
				auto const& levelRow = rows[firstRow];
				if (int(levelRow.size()) != numDays + firstValueColumn) {
					feedback("Error: Adjustment rows have inconsistent daily value counts: " + loadedFileName);
					return false;
				}
				double const trendLevel = legacyFormat
					? 0.0
					: parseFinite(levelRow[0]);
				if (!grid.empty() && trendLevel <= grid.back().trendLevel) {
					feedback("Error: Adjustment trend levels are not strictly increasing: " + loadedFileName);
					return false;
				}
				ParameterSeries series(numDays, ParameterSet{});
				for (int parameter = 0; parameter < ParameterCount; ++parameter) {
					auto const& row = rows[firstRow + parameter];
					if (int(row.size()) != numDays + firstValueColumn) {
						feedback("Error: Adjustment rows have inconsistent daily value counts: " + loadedFileName);
						return false;
					}
					if (!legacyFormat && parseFinite(row[0]) != trendLevel) {
						feedback("Error: Adjustment block contains inconsistent trend levels: " + loadedFileName);
						return false;
					}
					for (int day = 0; day < numDays; ++day) {
						double const value =
							parseFinite(row[day + firstValueColumn]);
						if ((parameter == int(InputParameters::LowerError) ||
							parameter == int(InputParameters::UpperError)) && value < 0.0) {
							throw std::domain_error("negative adjustment error");
						}
						if ((parameter == int(InputParameters::LowerKurtosis) ||
							parameter == int(InputParameters::UpperKurtosis)) && value < 1.0) {
							throw std::domain_error("invalid adjustment kurtosis");
						}
						if (parameter == int(InputParameters::MixFactor) &&
							(value < 0.0 || value > 1.0)) {
							throw std::domain_error("mix factor outside [0, 1]");
						}
						series[day][parameter] = value;
					}
				}
				grid.push_back(ParameterLevel{ trendLevel, std::move(series) });
			}
		}
		catch (std::exception const& e) {
			feedback("Error: Adjustment file contains an invalid value: " +
				loadedFileName + " (" + e.what() + ")");
			return false;
		}
		parameters[partyGroup] = std::move(grid);
	}
	return true;
}

bool StanModel::loadEmergingOthersParameters(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	logger << "loading emerging others parameters\n";
	const std::string filename = paths.resolveString(
		"analysis/Seat Statistics/statistics_emerging_party.csv");
	auto file = std::ifstream(filename);
	if (!file) {
		feedback("Emerging others parameters not present. Expected a file at " + filename);
		return false;
	}
	emergingParameters = {};
	try {
		for (int parameter = 0; parameter < int(EmergingPartyParameters::Max); ++parameter) {
			std::string line;
			if (!std::getline(file, line) || line.empty()) {
				throw Exception("expected four parameter rows");
			}
			double const value = std::stod(line);
			if (!std::isfinite(value)) throw Exception("parameter is not finite");
			emergingParameters[parameter] = value;
		}
	}
	catch (std::exception const& e) {
		feedback(std::string("Could not load emerging-party parameters: ") + e.what());
		return false;
	}
	if (emergingParameters[int(EmergingPartyParameters::Threshold)] <= 0.0 ||
		emergingParameters[int(EmergingPartyParameters::Threshold)] >= 100.0 ||
		emergingParameters[int(EmergingPartyParameters::EmergenceRate)] < 0.0 ||
		emergingParameters[int(EmergingPartyParameters::EmergenceRate)] > 1.0 ||
		emergingParameters[int(EmergingPartyParameters::Rmse)] < 0.0 ||
		emergingParameters[int(EmergingPartyParameters::Kurtosis)] < 1.0) {
		feedback("Emerging-party parameters are outside their valid ranges.");
		return false;
	}
	return true;
}

bool StanModel::loadModelledPolls(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	modelledPolls.clear();

	// This section is used multiple times only inside this procedure, so define it once
	auto loadPolls = [&feedback](std::vector<ModelledPoll>& polls,
		std::ifstream& file, std::string const& filename) {
		polls.clear();
		std::string line;
		std::getline(file, line); // first line is just a legend, skip it
		try {
			while (std::getline(file, line)) {
				if (line.empty()) continue;
				auto pollVals = splitString(line, ",");
				if (pollVals.size() < 4 || pollVals[0].empty()) {
					throw Exception("invalid poll row");
				}
				ModelledPoll poll;
				poll.pollster = pollVals[0];
				poll.day = std::stoi(pollVals[1]);
				poll.base = std::stof(pollVals[2]);
				poll.adjusted = std::stof(pollVals[3]);
				if (!std::isfinite(poll.base) || !std::isfinite(poll.adjusted)) {
					throw Exception("non-finite poll value");
				}
				if (pollVals.size() >= 5 && !pollVals[4].empty()) {
					poll.reported = std::stof(pollVals[4]);
					// NaN represents a poll with a TPP derived from its FP values but
					// no separately reported TPP figure.
					if (std::isinf(poll.reported)) {
						throw Exception("infinite reported poll value");
					}
				}
				polls.push_back(poll);
			}
		}
		catch (std::exception const& e) {
			feedback("Could not parse " + filename + ": " + e.what());
			return false;
		}
		return true;
	};

	for (auto partyCode : partyCodeVec) {
		if (partyCode == EmergingOthersCode) continue;
		if (partyCode == UnnamedOthersCode) continue; // calculate this later
		std::string filename = paths.resolveString(
			"analysis/Outputs/fp_polls_" + termCode + "_" +
			partyCode + " FP.csv");
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
		}
		auto& polls = modelledPolls[partyCode];
		if (!loadPolls(polls, file, filename)) return false;
	}
	{
		std::string filename = paths.resolveString(
			"analysis/Outputs/fp_polls_" + termCode + "_@TPP.csv");
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
		}
		auto& polls = modelledPolls[TppCode];
		if (!loadPolls(polls, file, filename)) return false;
	}
	return true;
}

bool StanModel::loadPreferenceFlows(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	preferenceFlowMap.clear();
	preferenceExhaustMap.clear();
	try {
		auto lines = extractElectionDataFromFile(
			paths.resolveString("analysis/Data/preference-estimates.csv"),
			termCode);
		if (lines.empty()) throw Exception("no rows found for " + termCode);
		for (auto const& line : lines) {
			if (line.size() < 4) throw Exception("preference row has too few columns");
			auto partyValues = splitString(line[2], " ");
			if (partyValues.empty() || partyValues[0].empty()) {
				throw Exception("preference row has no party code");
			}
			std::string const& party = partyValues[0];
			if (preferenceFlowMap.count(party)) {
				throw Exception("duplicate preference row for " + party);
			}
			float const estimatedPreferenceFlow = std::stof(line[3]);
			if (!std::isfinite(estimatedPreferenceFlow) ||
				estimatedPreferenceFlow <= 0.0f || estimatedPreferenceFlow >= 100.0f) {
				throw Exception("preference flow must be between 0 and 100 for " + party);
			}
			float exhaustRate = 0.0f;
			if (line.size() >= 5 && !line[4].empty() && line[4][0] != '#') {
				exhaustRate = std::stof(line[4]);
				if (!std::isfinite(exhaustRate) ||
					exhaustRate < 0.0f || exhaustRate >= 100.0f) {
					throw Exception("exhaust rate must be in [0, 100) for " + party);
				}
			}
			preferenceFlowMap[party] = estimatedPreferenceFlow;
			preferenceExhaustMap[party] = exhaustRate;
		}
	}
	catch (std::exception const& e) {
		feedback(std::string("Could not load preference estimates: ") + e.what());
		return false;
	}

	if (!preferenceFlowMap.count(OthersCode)) {
		feedback("No preference estimate was supplied for " + OthersCode + ".");
		return false;
	}
	preferenceFlowMap[EmergingOthersCode] = preferenceFlowMap[OthersCode];
	preferenceFlowMap[UnnamedOthersCode] = preferenceFlowMap[OthersCode];
	preferenceExhaustMap[EmergingOthersCode] = preferenceExhaustMap[OthersCode];
	preferenceExhaustMap[UnnamedOthersCode] = preferenceExhaustMap[OthersCode];
	preferenceFlowMap[partyCodeVec[0]] = 100.0f;
	preferenceFlowMap[partyCodeVec[1]] = 0.0f;
	preferenceExhaustMap[partyCodeVec[0]] = 0.0f;
	preferenceExhaustMap[partyCodeVec[1]] = 0.0f;
	return true;
}

bool StanModel::generatePreferenceMaps(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	try {
		if (!loadPreferenceFlows(paths, feedback)) return false;
		auto preferenceDeviationVec = splitStringF(preferenceDeviation, ",");
		auto preferenceSamplesVec = splitStringF(preferenceSamples, ",");
		bool validSizes = 
			preferenceDeviationVec.size() == partyCodeVec.size() &&
			preferenceSamplesVec.size() == partyCodeVec.size();
		if (!validSizes) throw Exception("Party codes and preference parameter lists do not match.");
		preferenceDeviationMap.clear();
		preferenceSamplesMap.clear();
		for (int index = 0; index < int(partyCodeVec.size()); ++index) {
			float const deviation = preferenceDeviationVec[index];
			float const samples = preferenceSamplesVec[index];
			if (!std::isfinite(deviation) || deviation < 0.0f ||
				!std::isfinite(samples) || samples < 0.0f) {
				throw Exception("Preference deviations and sample counts must be finite and non-negative.");
			}
			preferenceDeviationMap[partyCodeVec[index]] = deviation;
			preferenceSamplesMap[partyCodeVec[index]] = samples;
		}
		preferenceDeviationMap[EmergingOthersCode] = preferenceDeviationMap[OthersCode];
		preferenceSamplesMap[EmergingOthersCode] = preferenceSamplesMap[OthersCode];
		for (auto const& partyCode : partyCodeVec) {
			if (partyCode == OthersCode) continue;
			if (!preferenceFlowMap.count(partyCode) ||
				!preferenceExhaustMap.count(partyCode) ||
				!preferenceDeviationMap.count(partyCode) ||
				!preferenceSamplesMap.count(partyCode)) {
				throw Exception("Incomplete preference settings for party code: " + partyCode);
			}
		}
	}
	catch (std::exception const& e) {
		feedback(std::string("Could not prepare preference settings: ") + e.what());
		return false;
	}
	PA_LOG_VAR(preferenceFlowMap);
	PA_LOG_VAR(preferenceExhaustMap);
	return true;
}

bool StanModel::loadTrendData(
	WorkspacePaths const& paths, FeedbackFunc feedback)
{
	startDate = {};
	rawSupport.clear();
	rawTppSupport = {};
	int expectedSeriesLength = 0;

	auto loadTrendSeries = [&](std::string const& filename, Series& series) {
		auto file = std::ifstream(filename);
		if (!file) {
			feedback("Could not load file: " + filename);
			return false;
		}
		try {
			std::string line;
			if (!std::getline(file, line) || !std::getline(file, line)) {
				throw Exception("missing date row");
			}
			auto dateVals = splitString(line, ",");
			if (dateVals.size() < 3) throw Exception("invalid date row");
			int const day = std::stoi(dateVals[0]);
			int const month = std::stoi(dateVals[1]);
			int const year = std::stoi(dateVals[2]);
			if (month < 1 || month > 12) throw Exception("invalid month in date row");
			auto const seriesStartDate = Date::fromYmd(year, month, day);
			if (!seriesStartDate) throw Exception("invalid start date");
			if (!startDate.isValid()) {
				startDate = *seriesStartDate;
			}
			else if (startDate != *seriesStartDate) {
				throw Exception("start date does not match the other trend files");
			}

			if (!std::getline(file, line)) throw Exception("missing percentile legend");
			series.timePoint.clear();
			while (std::getline(file, line)) {
				if (line.empty()) continue;
				auto trendVals = splitString(line, ",");
				if (trendVals.size() < Spread::Size + 2) {
					throw Exception("trend row has too few percentile values");
				}
				Spread spread;
				float previousValue = -std::numeric_limits<float>::infinity();
				for (int percentile = 0; percentile < Spread::Size; ++percentile) {
					float const value = std::stof(trendVals[percentile + 2]);
					if (!std::isfinite(value) || value < 0.0f || value > 100.0f) {
						throw Exception("trend values must be finite and in [0, 100]");
					}
					if (value < previousValue) {
						throw Exception("trend percentiles are not ordered");
					}
					spread.values[percentile] = value;
					previousValue = value;
				}
				series.timePoint.push_back(spread);
			}
			if (series.timePoint.empty()) throw Exception("trend series contains no days");
			if (!expectedSeriesLength) {
				expectedSeriesLength = int(series.timePoint.size());
			}
			else if (int(series.timePoint.size()) != expectedSeriesLength) {
				throw Exception("trend length does not match the other trend files");
			}
		}
		catch (std::exception const& e) {
			feedback("Could not parse " + filename + ": " + e.what());
			return false;
		}
		return true;
	};

	for (auto partyCode : partyCodeVec) {
		auto& series = rawSupport[partyCode]; // this needs to go here so that the Unnamed Others series is generated later on
		if (partyCode == EmergingOthersCode) continue;
		if (partyCode == UnnamedOthersCode) continue;
		std::string filename = paths.resolveString(
			"analysis/Outputs/fp_trend_" + termCode + "_" +
			partyCode + " FP.csv");
		if (!loadTrendSeries(filename, series)) return false;
	}
	{
		std::string filename = paths.resolveString(
			"analysis/Outputs/fp_trend_" + termCode + "_@TPP.csv");
		if (!loadTrendSeries(filename, rawTppSupport)) return false;
		PA_LOG_VAR(startDate.formatIso());
		PA_LOG_VAR(rawTppSupport.timePoint.size());
	}
	return true;
}

int StanModel::rawSupportDayOffset(Date date) const
{
	int const finalOffset = int(rawTppSupport.timePoint.size()) - 1;
	if (finalOffset < 0) return 0;
	if (!date.isValid()) return finalOffset;
	int const requestedOffset = date - startDate;
	return std::clamp(requestedOffset, 0, finalOffset);
}

double StanModel::rawMedianTrend(std::string const& partyCode, Date date) const
{
	int const dayOffset = rawSupportDayOffset(date);
	Series const& series = partyCode == TppCode ? rawTppSupport : rawSupport.at(partyCode);
	// Select the adjustment regime from the underlying trend, not this
	// iteration's random draw, so parameter choice doesn't add extra variance.
	double const medianVote = std::clamp(
		double(series.timePoint[dayOffset].values[MedianSpreadValue]),
		0.1, 99.9);
	return transformVoteShare(medianVote);
}

StanModel::ParameterSet StanModel::interpolatedParameters(
	std::string const& partyGroup, int day, double transformedTrend) const
{
	ParameterGrid const& grid = parameters.at(partyGroup);
	// Legacy adjustment files are represented as a single unconditioned level.
	if (grid.size() == 1 || transformedTrend <= grid.front().trendLevel) {
		return grid.front().series[day];
	}
	if (transformedTrend >= grid.back().trendLevel) {
		return grid.back().series[day];
	}
	auto const upper = std::lower_bound(
		grid.begin(), grid.end(), transformedTrend,
		[](ParameterLevel const& level, double value) {
			return level.trendLevel < value;
		});
	auto const lower = std::prev(upper);
	double const upperWeight = (transformedTrend - lower->trendLevel)
		/ (upper->trendLevel - lower->trendLevel);
	ParameterSet result{};
	for (int parameter = 0; parameter < int(InputParameters::Max); ++parameter) {
		result[parameter] = mix(
			lower->series[day][parameter],
			upper->series[day][parameter],
			upperWeight);
	}
	return result;
}

StanModel::SupportSample StanModel::generateRawSupportSample(Date date, int iterationIndex) const
{
	if (!rawSupport.size()) return SupportSample();
	int seriesLength = rawSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int const dayOffset = rawSupportDayOffset(date);
	SupportSample sample;
	int index = 0;
	for (auto const& [key, support] : rawSupport) {
		++index;
		if (key == EmergingOthersCode) {
			sample.voteShare.insert({ key, 0.0f });
			continue;
		}
		// Mirroring ensures the underlying uniform distribution is symmetric, so that the
		// median never deviates as a result of random variation.
		float quantile = mirroredQuantile(
			iterationIndex, uint32_t(VariabilityTag::FpRawSupport), index);

		float const spreadPosition = std::clamp(quantile, 0.0f, 1.0f) *
			float(Spread::Size - 1);
		int lowerBucket = std::clamp(
			int(std::floor(spreadPosition)), 0, int(Spread::Size) - 2);
		float upperMix = spreadPosition - float(lowerBucket);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = mix(lowerVote, upperVote, upperMix);
		sample.voteShare.insert({ key, sampledVote });
	}

	// This "raw TPP" may not be congruent with the primary votes above, but the adjustment
	// process will only use one or the other, so that doesn't matter
	float quantile = mirroredQuantile(
		iterationIndex, uint32_t(VariabilityTag::TppRawSupport), index);

	float const spreadPosition = std::clamp(quantile, 0.0f, 1.0f) *
		float(Spread::Size - 1);
	int lowerBucket = std::clamp(
		int(std::floor(spreadPosition)), 0, int(Spread::Size) - 2);
	float upperMix = spreadPosition - float(lowerBucket);
	float lowerVote = rawTppSupport.timePoint[dayOffset].values[lowerBucket];
	float upperVote = rawTppSupport.timePoint[dayOffset].values[lowerBucket + 1];
	float sampledVote = mix(lowerVote, upperVote, upperMix);
	sample.voteShare.insert({ TppCode, sampledVote });

	updateOthersValue(sample);

	return sample;
}

StanModel::SupportSample StanModel::generateAdjustedSupportSample(Date date, int days, int iterationIndex) const
{
	if (!date.isValid()) date = getEndDate();
	auto rawSample = generateRawSupportSample(date, iterationIndex);
	auto adjustedSample = adjustRawSupportSample(rawSample, date, days, iterationIndex);
	return adjustedSample;
}

void StanModel::generateUnnamedOthersSeries()
{
	if (rawSupport.count(OthersCode) && rawSupport.count(UnnamedOthersCode)) {
		auto& unnamedOthers = rawSupport[UnnamedOthersCode].timePoint;
		unnamedOthers.clear();
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
			unnamedOthers.push_back(spread);
		}
	}
}

StanModel::SupportSample StanModel::adjustRawSupportSample(
	SupportSample const& rawSupportSample, Date date,
	int days, int iterationIndex) const
{
	constexpr int MinDays = 0;
	if (numDays <= 0) throw Exception("No adjustment days are available.");
	// Adjustment files are indexed to the end of their trend series rather than
	// election day. Historical series typically stop about three days beforehand;
	// the established four-day offset aligns the runtime index with that training data.
	constexpr int DaysOffset = 4;
	days = std::clamp(days - DaysOffset, MinDays, numDays - 1);
	auto sample = rawSupportSample;
	constexpr float TppFirstChance = 0.5f;

	float tppFirstQuantile = variabilityUniform(
		0.0f, 1.0f, 0, 0,
		uint32_t(VariabilityTag::TppFirstSelection), iterationIndex);
	bool tppFirst = tppFirstQuantile < TppFirstChance;
	sample.coherenceBasis = tppFirst ?
		SupportSample::CoherenceBasis::TwoPartyPreferred :
		SupportSample::CoherenceBasis::FirstPreferences;
	constexpr bool IncludeVariation = true;
	int index = 0;
	for (auto& [key, voteShare] : sample.voteShare) {
		++index;
		if (key == EmergingOthersCode) continue;
		if (!tppFirst && key == TppCode) continue;
		// Some historical trend tails are exactly zero. Keep the raw series intact,
		// but avoid passing a boundary value through the logit transform.
		double const transformedPolls = transformVoteShare(
			std::clamp(double(voteShare), 0.001, 99.999));

		const std::string partyGroup = reversePartyGroups.at(key);
		ParameterSet const parameterSet = interpolatedParameters(
			partyGroup, days, rawMedianTrend(key, date));

		// remove systemic bias in poll results
		const double pollBiasToday = parameterSet[int(InputParameters::PollBias)];
		const double debiasedPolls = transformedPolls - pollBiasToday;

		// remove systemic bias in previous-election average
		const double fundamentalsPrediction = transformVoteShare(fundamentals.at(key));
		const double fundamentalsBiasToday = parameterSet[int(InputParameters::FundamentalsBias)];
		const double debiasedFundamentalsAverage = fundamentalsPrediction - fundamentalsBiasToday;

		// mix poll and previous values
		const double mixFactor = parameterSet[int(InputParameters::MixFactor)];
		const double mixedVoteShare = mix(debiasedFundamentalsAverage, debiasedPolls, mixFactor);

		// adjust for residual bias in the mixed vote share
		const double mixedBiasToday = parameterSet[int(InputParameters::MixedBias)];
		const double mixedDebiasedVote = mixedVoteShare - mixedBiasToday;

		// for debugging purposes we often want to avoid adding additional variation here
		if (IncludeVariation) {
			// Get parameters for spread
			const double lowerError = parameterSet[int(InputParameters::LowerError)];
			const double upperError = parameterSet[int(InputParameters::UpperError)];
			const double lowerKurtosis = parameterSet[int(InputParameters::LowerKurtosis)];
			const double upperKurtosis = parameterSet[int(InputParameters::UpperKurtosis)];

			double quantile = mirroredQuantile(
				iterationIndex,
				uint32_t(VariabilityTag::FpAdjustedSupport), index);

			const double additionalVariation = RandomGenerator::flexibleDist(
				0.0, lowerError, upperError, lowerKurtosis, upperKurtosis,
				quantile);
			const double voteWithVariation = mixedDebiasedVote + additionalVariation;

			double newVoteShare = detransformVoteShare(voteWithVariation);
			voteShare = float(newVoteShare);
		}
		else {
			double newVoteShare = detransformVoteShare(mixedDebiasedVote);
			voteShare = float(newVoteShare);
		}
	}

	addEmergingOthers(sample, days, iterationIndex);
	finaliseSupportSample(sample, iterationIndex);
	sample.daysToElection = days;
	return sample;
}

bool StanModel::updateAdjustedData(FeedbackFunc feedback, int numThreads)
{
	constexpr int BaseIterations = 400;
	numThreads = std::max(1, numThreads);
	adjustedSupport.clear(); // Never retain adjusted values from a previous run.
	tppSupport.timePoint.clear();
	try {
		int const seriesLength = int(rawTppSupport.timePoint.size());
		if (!seriesLength) throw Exception("Raw trend data contains no time points.");
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
					Date const thisDate = startDate + time;
					// Extra accuracy for the final data point, since it's much more important than the rest
					int const localIterations = time == seriesLength - 1
						? BaseIterations * 100 : BaseIterations;
					std::vector<std::vector<float>> samples(partyCodeVec.size(), std::vector<float>(localIterations));
					std::vector<float> tppSamples(localIterations);
					for (int iteration = 0; iteration < localIterations; ++iteration) {
						// Key variation by iteration so worker scheduling cannot
						// change which random quantiles this time point receives.
						auto sample = generateAdjustedSupportSample(
							thisDate, 0, iteration);
						for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
							std::string partyName = partyCodeVec[partyIndex];
							samples[partyIndex][iteration] = sample.voteShare.at(partyName);
						}
						tppSamples[iteration] = sample.voteShare.at(TppCode);
					}
					for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
						std::string partyName = partyCodeVec[partyIndex];
						std::sort(samples[partyIndex].begin(), samples[partyIndex].end());
						for (int percentile = 0; percentile < Spread::Size; ++percentile) {
							int const sampleIndex = int(std::lround(
								double(percentile) * double(localIterations - 1) /
								double(Spread::Size - 1)));
							adjustedSupport.at(partyName).timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
						}
					}
					std::sort(tppSamples.begin(), tppSamples.end());
					for (int percentile = 0; percentile < Spread::Size; ++percentile) {
						int const sampleIndex = int(std::lround(
							double(percentile) * double(localIterations - 1) /
							double(Spread::Size - 1)));
						tppSupport.timePoint[time].values[percentile] = tppSamples[sampleIndex];
					}
				}
			};
			std::vector<std::future<void>> workers;
			for (int timeStart = timeStart1; timeStart < timeStart1 + numThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
				workers.push_back(std::async(
					std::launch::async, calculateTimeSupport, timeStart));
			}
			for (auto& worker : workers) {
				worker.get();
			}
		}

		const int ModelSmoothingDays = 7;
		for (auto& [key, party] : adjustedSupport) {
			party.smooth(ModelSmoothingDays); // also automatically calculates expectations
		}
		tppSupport.smooth(ModelSmoothingDays);
	}

	catch (std::logic_error const& e) {
		feedback(std::string("Warning: Model \"") + name +
			"\" adjustments could not be generated.\n" +
			"Specific information: " + e.what());
		adjustedSupport.clear();
		tppSupport.timePoint.clear();
		return false;
	}
	return true;
}

void StanModel::addEmergingOthers(StanModel::SupportSample& sample, int days, int iterationIndex) const
{
	const double threshold = emergingParameters[int(EmergingPartyParameters::Threshold)];
	const double transformedThreshold = transformVoteShare(threshold);
	const double baseEmergenceRate = emergingParameters[int(EmergingPartyParameters::EmergenceRate)];
	const double baseEmergenceRmse = emergingParameters[int(EmergingPartyParameters::Rmse)];
	const double kurtosis = emergingParameters[int(EmergingPartyParameters::Kurtosis)];
	// Guessed values for a curve that makes the chance of a new party emerging
	// decrease approaching election day. E.g. 100 days out it is halved, on election day it's about 16%
	double emergenceChance = baseEmergenceRate * (1.0 - 1.0 / (double(days) * 0.01 + 1.2));
	double const emergenceQuantile = variabilityUniform(
		0.0f, 1.0f, 0, 0,
		uint32_t(VariabilityTag::EmergingOthers), iterationIndex);
	if (emergenceQuantile > emergenceChance) {
		sample.voteShare[EmergingOthersCode] = 0.0;
		return;
	}
	// As above, but slightly different numbers and the curve takes longer to drop.
	double rmse = baseEmergenceRmse * (1.0 - 1.0 / (double(days) * 0.03 + 1.4));
	double const sizeQuantile = variabilityUniform(
		0.0f, 1.0f, 0, 0,
		uint32_t(VariabilityTag::EmergingOthersSize), iterationIndex);
	// Fold the size distribution into one tail so every successful emergence
	// remains above the threshold used to define and calibrate an emerging party.
	double emergingOthersFpTargetTransformed = transformedThreshold
		+ std::abs(RandomGenerator::flexibleDist(
			0.0, rmse, rmse, kurtosis, kurtosis, sizeQuantile));
	constexpr double MaxEmergingPartyFpShare = 99.0;
	double emergingOthersFpTarget = std::min(
		detransformVoteShare(emergingOthersFpTargetTransformed),
		MaxEmergingPartyFpShare);
	// Correct for the reduction that will occur when the full sample is normalised.
	double correctedFp = 100.0 * emergingOthersFpTarget / (100.0 - emergingOthersFpTarget);
	sample.voteShare[EmergingOthersCode] = float(correctedFp);
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
			if (!std::isfinite(b.second) || b.second < 0.0f) {
				throw Exception("Vote sample contains an invalid party share.");
			}
			return (b.first == OthersCode || b.first == TppCode ? a : a + b.second); }
		);
	if (!std::isfinite(sampleSum) || sampleSum <= 0.0f) {
		throw Exception("Vote sample cannot be normalised.");
	}
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample.voteShare) {
		if (vote.first != TppCode) {
			vote.second *= sampleAdjust;
		}
	}
}

void StanModel::finaliseSupportSample(
	StanModel::SupportSample& sample, int iterationIndex) const
{
	// Each sample treats either FP or TPP as primary. Re-derive the dependent
	// values after any later adjustment so FP, TPP and preference flows remain
	// internally coherent.
	normaliseSample(sample);
	if (sample.coherenceBasis ==
		SupportSample::CoherenceBasis::FirstPreferences) {
		updateOthersValue(sample);
		generateTppForSample(sample, iterationIndex);
	}
	else {
		generateMajorFpForSample(sample, iterationIndex);
		updateOthersValue(sample);
	}
}

void StanModel::generateTppForSample(StanModel::SupportSample& sample, int iterationIndex) const
{
	float partyOneTpp = 0.0f;
	float totalTpp = 0.0f;
	int partyIndex = 0;
	for (auto const& [key, support] : sample.voteShare) {
		int const variationIndex = partyIndex++;
		if (key == OthersCode || key == TppCode) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f;
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomPreferenceVariation = 0.0f;
		if (historicalSamples >= 2) {
			float quantile = variabilityUniform(
				0.0f, 1.0f, variationIndex, 0,
				uint32_t(VariabilityTag::TppPreferenceFlow), iterationIndex);
			randomPreferenceVariation = RandomGenerator::scaledTdistQuantile(
				int(std::floor(historicalSamples)) - 1, quantile,
				0.0f, deviation);
		}
		float randomisedFlow = randomPreferenceVariation == 0.0f
			? flow
			: basicTransformedSwing(
				flow * 100.0f, randomPreferenceVariation * 100.0f) * 0.01f;
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		float exhaustRate = preferenceExhaustMap.at(key) * 0.01f;
		// distribution approximately taken from NSW elections
		float quantile = variabilityUniform(
			0.0f, 1.0f, variationIndex, 0,
			uint32_t(VariabilityTag::TppExhaustRate), iterationIndex);
		float randomExhaustVariation =
			RandomGenerator::scaledTdistQuantile(
				6, quantile, 0.0f, 0.054f);
		float randomisedExhaustRate = exhaustRate
			? basicTransformedSwing(
				exhaustRate * 100.0f, randomExhaustVariation * 100.0f) * 0.01f
			: 0.0f;
		randomisedExhaustRate = std::clamp(randomisedExhaustRate, 0.0f, 1.0f);
		sample.preferenceFlow[key] = randomisedFlow * 100.0f;
		sample.exhaustRate[key] = randomisedExhaustRate;
		partyOneTpp += support * randomisedFlow * (1.0f - randomisedExhaustRate);
		totalTpp += support * (1.0f - randomisedExhaustRate);
	}
	if (!std::isfinite(totalTpp) || totalTpp <= 0.0f) {
		throw Exception("No non-exhausted votes were available to calculate TPP.");
	}
	sample.voteShare[TppCode] = partyOneTpp * (100.0f / totalTpp);
}

void StanModel::generateMajorFpForSample(StanModel::SupportSample& sample, int iterationIndex) const
{
	float partyOneTpp = 0.0f;
	float totalFp = 0.0f;
	float exhaustedFp = 0.0f;
	// First add up all party-one preference from minor parties
	int partyIndex = 0;
	for (auto const& [key, support] : sample.voteShare) {
		int const variationIndex = partyIndex++;
		if (key == OthersCode) continue;
		if (key == TppCode || key == partyCodeVec[0] || key == partyCodeVec[1]) continue;
		float flow = preferenceFlowMap.at(key) * 0.01f; // this is expressed textually as a percentage, convert to a proportion here
		float deviation = preferenceDeviationMap.at(key) * 0.01f;
		float historicalSamples = preferenceSamplesMap.at(key);
		float randomVariation = 0.0f;
		if (historicalSamples >= 2) {
			float quantile = variabilityUniform(
				0.0f, 1.0f, variationIndex, 0,
				uint32_t(VariabilityTag::MajorFpPreferenceFlow),
				iterationIndex);
			randomVariation = RandomGenerator::scaledTdistQuantile(
				int(std::floor(historicalSamples)) - 1, quantile,
				0.0f, deviation);
		}

		float randomisedFlow = randomVariation == 0.0f
			? flow
			: basicTransformedSwing(
				flow * 100.0f, randomVariation * 100.0f) * 0.01f;
		randomisedFlow = std::clamp(randomisedFlow, 0.0f, 1.0f);
		float exhaustRate = preferenceExhaustMap.at(key) * 0.01f;
		// distribution approximately taken from NSW elections
		float quantile = variabilityUniform(
			0.0f, 1.0f, variationIndex, 0,
			uint32_t(VariabilityTag::FpExhaustRate), iterationIndex);
		float randomExhaustVariation =
			RandomGenerator::scaledTdistQuantile(
				6, quantile, 0.0f, 0.054f);
		float randomisedExhaustRate = exhaustRate
			? basicTransformedSwing(
				exhaustRate * 100.0f, randomExhaustVariation * 100.0f) * 0.01f
			: 0.0f;
		randomisedExhaustRate = std::clamp(randomisedExhaustRate, 0.0f, 1.0f);
		sample.preferenceFlow[key] = randomisedFlow * 100.0f;
		sample.exhaustRate[key] = randomisedExhaustRate;
		partyOneTpp += support * randomisedFlow * (1.0f - randomisedExhaustRate);
		totalFp += support;
		exhaustedFp += support * randomisedExhaustRate;
	}
	sample.preferenceFlow[partyCodeVec[0]] = 100.0f;
	sample.preferenceFlow[partyCodeVec[1]] = 0.0f;
	sample.exhaustRate[partyCodeVec[0]] = 0.0f;
	sample.exhaustRate[partyCodeVec[1]] = 0.0f;
	// Convert the target TPP share to non-exhausted vote points, then remove
	// the preference points already supplied by minor parties. The preference
	// contribution is already net of exhaustion and must not be scaled again.
	float targetTpp = sample.voteShare[TppCode];
	if (!std::isfinite(targetTpp) || targetTpp <= 0.0f || targetTpp >= 100.0f) {
		throw Exception("TPP target must be between 0 and 100.");
	}
	float const nonExhaustedFp = 100.0f - exhaustedFp;
	float partyOneFp =
		targetTpp * nonExhaustedFp * 0.01f -
		partyOneTpp;
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
	if (timePoint.empty()) return;
	smoothingFactor = std::max(0, smoothingFactor);
	Series newSeries = *this;
	for (int index = 0; index < int(timePoint.size()); ++index) {
		int const thisSmoothing = std::min(
			smoothingFactor,
			std::min(index, int(timePoint.size()) - index - 1));
		for (int percentile = 0; percentile < Spread::Size; ++percentile) {
			double numerator = 0.0f;
			double denominator = 0.0f;
			for (int offset = -thisSmoothing; offset <= thisSmoothing; ++offset) {
				int source = index + offset;
				double weight = double(nCr(
					thisSmoothing, offset + thisSmoothing));
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

bool StanModel::dumpGeneratedData(std::string const& filename) const {
	if (!readyForProjection || termCode.empty() || partyCodes.empty() ||
		!startDate.isValid() || adjustedSupport.empty() ||
		tppSupport.timePoint.empty()) {
		return false;
	}
	std::ofstream file(filename, std::ios::binary);
	if (!file) return false;
	
	// Define helper lambdas for serialization
	auto writeString = [&file](const std::string& str) {
		size_t size = str.size();
		file.write(reinterpret_cast<const char*>(&size), sizeof(size));
		file.write(str.c_str(), size);
	};

	file.write(reinterpret_cast<const char*>(&GeneratedDataMagic),
		sizeof(GeneratedDataMagic));
	file.write(reinterpret_cast<const char*>(&GeneratedDataVersion),
		sizeof(GeneratedDataVersion));
	std::array<std::uint8_t, 5> const scalarSizes = {
		sizeof(std::size_t),
		sizeof(int),
		sizeof(float),
		sizeof(double),
		sizeof(bool),
	};
	file.write(
		reinterpret_cast<char const*>(scalarSizes.data()),
		scalarSizes.size());
	std::uint32_t const parameterSetSize = sizeof(ParameterSet);
	std::uint32_t const emergingParameterSetSize =
		sizeof(EmergingPartyParameterSet);
	file.write(
		reinterpret_cast<char const*>(&parameterSetSize),
		sizeof(parameterSetSize));
	file.write(
		reinterpret_cast<char const*>(&emergingParameterSetSize),
		sizeof(emergingParameterSetSize));
	file.write(
		reinterpret_cast<char const*>(&GeneratedDataEndianMarker),
		sizeof(GeneratedDataEndianMarker));
	writeString(termCode);
	writeString(startDate.formatIso());
	writeString(partyCodes);
	
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
	
	auto writeParameters = [&file, &writeString](const ParameterGridByPartyGroup& params) {
		size_t mapSize = params.size();
		file.write(reinterpret_cast<const char*>(&mapSize), sizeof(mapSize));
		for (const auto& [groupKey, grid] : params) {
			writeString(groupKey);
			size_t gridSize = grid.size();
			file.write(reinterpret_cast<const char*>(&gridSize), sizeof(gridSize));
			for (auto const& level : grid) {
				file.write(reinterpret_cast<const char*>(&level.trendLevel),
					sizeof(level.trendLevel));
				size_t seriesSize = level.series.size();
				file.write(reinterpret_cast<const char*>(&seriesSize), sizeof(seriesSize));
				for (const auto& paramSet : level.series) {
					file.write(reinterpret_cast<const char*>(&paramSet), sizeof(paramSet));
				}
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
	
	return bool(file);
}

bool StanModel::loadGeneratedData(
	std::string const& filename, FeedbackFunc feedback)
{
	if (!feedback) feedback = [](std::string) {};
	std::ifstream file(filename, std::ios::binary);
	if (!file) {
		feedback("Generated model cache not found: " + filename);
		return false;
	}

	auto readString = [&file](std::string& str) {
		size_t size = 0;
		file.read(reinterpret_cast<char*>(&size), sizeof(size));
		if (!file) return;
		str.resize(size);
		if (size) file.read(str.data(), size);
	};

	std::uint32_t magic = 0;
	file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	if (!file || magic != GeneratedDataMagic) {
		feedback(
			"Generated model cache has an unsupported legacy format. "
			"Run dump-model again to regenerate it: " + filename);
		return false;
	}
	std::uint32_t version = 0;
	file.read(reinterpret_cast<char*>(&version), sizeof(version));
	if (!file || version != GeneratedDataVersion) {
		feedback(
			"Generated model cache version is not supported. Run dump-model "
			"again to regenerate it: " + filename);
		return false;
	}
	std::array<std::uint8_t, 5> cachedScalarSizes{};
	file.read(
		reinterpret_cast<char*>(cachedScalarSizes.data()),
		cachedScalarSizes.size());
	std::uint32_t cachedParameterSetSize = 0;
	std::uint32_t cachedEmergingParameterSetSize = 0;
	std::uint32_t cachedEndianMarker = 0;
	file.read(
		reinterpret_cast<char*>(&cachedParameterSetSize),
		sizeof(cachedParameterSetSize));
	file.read(
		reinterpret_cast<char*>(&cachedEmergingParameterSetSize),
		sizeof(cachedEmergingParameterSetSize));
	file.read(
		reinterpret_cast<char*>(&cachedEndianMarker),
		sizeof(cachedEndianMarker));
	std::array<std::uint8_t, 5> const expectedScalarSizes = {
		sizeof(std::size_t),
		sizeof(int),
		sizeof(float),
		sizeof(double),
		sizeof(bool),
	};
	if (!file ||
		cachedScalarSizes != expectedScalarSizes ||
		cachedParameterSetSize != sizeof(ParameterSet) ||
		cachedEmergingParameterSetSize !=
			sizeof(EmergingPartyParameterSet) ||
		cachedEndianMarker != GeneratedDataEndianMarker) {
		feedback(
			"Generated model cache is incompatible with this executable's "
			"architecture or binary layout. Run dump-model again using this "
			"build: " + filename);
		return false;
	}

	std::string cachedTermCode;
	std::string cachedStartDate;
	std::string cachedPartyCodes;
	readString(cachedTermCode);
	readString(cachedStartDate);
	readString(cachedPartyCodes);
	std::string const expectedStartDate =
		startDate.formatIso();
	if (!file || cachedTermCode != termCode ||
		cachedStartDate != expectedStartDate ||
		cachedPartyCodes != partyCodes) {
		feedback(
			"Generated model cache does not match this model's election, "
			"start date and party configuration: " + filename);
		return false;
	}
	
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
	
	auto readParameters = [&file, &readString](ParameterGridByPartyGroup& params) {
		params.clear();
		size_t mapSize;
		file.read(reinterpret_cast<char*>(&mapSize), sizeof(mapSize));
		for (size_t i = 0; i < mapSize; i++) {
			std::string groupKey;
			readString(groupKey);
			size_t gridSize;
			file.read(reinterpret_cast<char*>(&gridSize), sizeof(gridSize));
			auto& grid = params[groupKey];
			grid.resize(gridSize);
			for (auto& level : grid) {
				file.read(reinterpret_cast<char*>(&level.trendLevel),
					sizeof(level.trendLevel));
				size_t seriesSize;
				file.read(reinterpret_cast<char*>(&seriesSize), sizeof(seriesSize));
				level.series.resize(seriesSize);
				for (auto& parameterSet : level.series) {
					file.read(reinterpret_cast<char*>(&parameterSet),
						sizeof(parameterSet));
				}
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
	
	// Read into temporary state so a truncated or invalid cache cannot partially
	// replace a model which was already usable.
	int loadedNumDays = 0;
	PartySupport loadedRawSupport;
	Series loadedRawTppSupport;
	PartySupport loadedAdjustedSupport;
	Series loadedTppSupport;
	ModelledPolls loadedModelledPolls;
	ReversePartyGroups loadedReversePartyGroups;
	Fundamentals loadedFundamentals;
	ParameterGridByPartyGroup loadedParameters;
	EmergingPartyParameterSet loadedEmergingParameters{};
	PartyCodes loadedPartyCodeVec;
	PartyParameters loadedPreferenceFlowMap;
	PartyParameters loadedPreferenceExhaustMap;
	PartyParameters loadedPreferenceDeviationMap;
	PartyParameters loadedPreferenceSamplesMap;

	file.read(reinterpret_cast<char*>(&loadedNumDays), sizeof(loadedNumDays));
	readSupportSeries(loadedRawSupport);
	readSeries(loadedRawTppSupport);
	readSupportSeries(loadedAdjustedSupport);
	readSeries(loadedTppSupport);
	readModelledPolls(loadedModelledPolls);
	readMap(loadedReversePartyGroups);
	readFundamentals(loadedFundamentals);
	readParameters(loadedParameters);
	file.read(reinterpret_cast<char*>(&loadedEmergingParameters),
		sizeof(loadedEmergingParameters));

	size_t partyCodeSize = 0;
	file.read(reinterpret_cast<char*>(&partyCodeSize), sizeof(partyCodeSize));
	loadedPartyCodeVec.resize(partyCodeSize);
	for (auto& partyCode : loadedPartyCodeVec) {
		readString(partyCode);
	}

	readPartyParameters(loadedPreferenceFlowMap);
	readPartyParameters(loadedPreferenceExhaustMap);
	readPartyParameters(loadedPreferenceDeviationMap);
	readPartyParameters(loadedPreferenceSamplesMap);

	bool loadedReadyForProjection = false;
	file.read(reinterpret_cast<char*>(&loadedReadyForProjection),
		sizeof(loadedReadyForProjection));
	if (!file || !loadedReadyForProjection || loadedNumDays <= 0 ||
		loadedPartyCodeVec != splitString(partyCodes, ",") ||
		loadedRawTppSupport.timePoint.empty() ||
		loadedAdjustedSupport.empty() ||
		loadedTppSupport.timePoint.empty()) {
		feedback(
			"Generated model cache is incomplete or inconsistent with this "
			"model: " + filename);
		return false;
	}

	numDays = loadedNumDays;
	rawSupport = std::move(loadedRawSupport);
	rawTppSupport = std::move(loadedRawTppSupport);
	adjustedSupport = std::move(loadedAdjustedSupport);
	tppSupport = std::move(loadedTppSupport);
	modelledPolls = std::move(loadedModelledPolls);
	reversePartyGroups = std::move(loadedReversePartyGroups);
	fundamentals = std::move(loadedFundamentals);
	parameters = std::move(loadedParameters);
	emergingParameters = loadedEmergingParameters;
	partyCodeVec = std::move(loadedPartyCodeVec);
	preferenceFlowMap = std::move(loadedPreferenceFlowMap);
	preferenceExhaustMap = std::move(loadedPreferenceExhaustMap);
	preferenceDeviationMap = std::move(loadedPreferenceDeviationMap);
	preferenceSamplesMap = std::move(loadedPreferenceSamplesMap);
	validationSupport.clear();
	readyForProjection = true;
	return true;
}

float StanModel::variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag, int iterationIndex) const {
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	return RandomGenerator::uniform01_from_key(key) * (high - low) + low;
}

float StanModel::mirroredQuantile(
	int iterationIndex, std::uint32_t tag, int index) const
{
	// Antithetic pairing keeps the sampled distribution centred exactly.
	int pairIndex = iterationIndex / 2;
	float base = variabilityUniform(0.0f, 1.0f, 0, index, tag, pairIndex);
	return (iterationIndex & 1) ? (1.0f - base) : base;
}
