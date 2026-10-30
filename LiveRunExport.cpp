#include "LiveRunExport.h"
#include "LiveFpIntegration.h"

#include "Date.h"
#include "LiveAnalysisArchive.h"
#include "LiveResultsInput.h"
#include "LiveV2.h"
#include "LiveTppDiagnostics.h"
#include "Log.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"
#include "json.h"

#include <wx/mstream.h>
#include <wx/zstream.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {

json number(float value)
{
	if (std::isnan(value)) return { {"non_finite", "nan"} };
	if (value == std::numeric_limits<float>::infinity()) {
		return { {"non_finite", "positive_infinity"} };
	}
	if (value == -std::numeric_limits<float>::infinity()) {
		return { {"non_finite", "negative_infinity"} };
	}
	return value;
}

json number(double value)
{
	if (std::isnan(value)) return { {"non_finite", "nan"} };
	if (value == std::numeric_limits<double>::infinity()) {
		return { {"non_finite", "positive_infinity"} };
	}
	if (value == -std::numeric_limits<double>::infinity()) {
		return { {"non_finite", "negative_infinity"} };
	}
	return value;
}

template<typename T>
json optionalNumber(std::optional<T> const& value)
{
	if (!value) return nullptr;
	return number(*value);
}

json optionalInt(std::optional<int> const& value)
{
	if (!value || *value == InvalidPartyIndex) return nullptr;
	return *value;
}

json optionalCoords(std::optional<std::pair<float, float>> const& coords)
{
	if (!coords) return nullptr;
	return json::array({number(coords->first), number(coords->second)});
}

json partyIndexOrNull(int partyIndex)
{
	if (partyIndex == InvalidPartyIndex) return nullptr;
	return partyIndex;
}

struct PartyNames {
	Simulation::Report const& report;

	// Names come from the simulation report (project parties plus Others /
	// Emerging Ind / etc.). Live-only EC parties and per-candidate independent
	// IDs (ecCandidateId + 100000) are left null; downstream analysis currently
	// uses named parties only. Prominent independents are remapped onto
	// run.indPartyIndex in deviations and projected votes, so they appear under
	// the named Independent party.
	json identity(int partyIndex) const
	{
		json record;
		record["party_index"] = partyIndex;
		auto const name = report.partyName.find(partyIndex);
		record["name"] = name == report.partyName.end() ? json(nullptr) :
			json(name->second);
		auto const abbreviation = report.partyAbbr.find(partyIndex);
		record["abbreviation"] = abbreviation == report.partyAbbr.end() ?
			json(nullptr) : json(abbreviation->second);
		return record;
	}

	template <typename Map>
	json values(Map const& items) const
	{
		json array = json::array();
		for (auto const& [partyIndex, value] : items) {
			json record = identity(partyIndex);
			using Value = std::decay_t<decltype(value)>;
			if constexpr (std::is_floating_point_v<Value>) {
				record["value"] = number(value);
			}
			else {
				record["value"] = value;
			}
			array.push_back(std::move(record));
		}
		return array;
	}

	json identities(std::set<int> const& partyIndices) const
	{
		json array = json::array();
		for (int partyIndex : partyIndices) {
			array.push_back(identity(partyIndex));
		}
		return array;
	}
};

json partiesCatalog(Simulation::Report const& report)
{
	json array = json::array();
	for (auto const& [partyIndex, name] : report.partyName) {
		json record;
		record["party_index"] = partyIndex;
		record["name"] = name;
		auto const abbreviation = report.partyAbbr.find(partyIndex);
		record["abbreviation"] = abbreviation == report.partyAbbr.end() ?
			json(nullptr) : json(abbreviation->second);
		auto const colour = report.partyColour.find(partyIndex);
		if (colour == report.partyColour.end()) {
			record["colour"] = nullptr;
		}
		else {
			record["colour"] = {
				{"r", colour->second.r},
				{"g", colour->second.g},
				{"b", colour->second.b}
			};
		}
		array.push_back(std::move(record));
	}
	return array;
}

json serializeNode(LiveV2::Node const& node, PartyNames const& parties)
{
	json result;
	result["relevance_modifier"] = number(node.relevanceModifier);
	result["running_parties"] = parties.identities(node.runningParties);
	result["fp_confidence"] = number(node.fpConfidence);
	result["fp_pooling_evidence_votes"] = parties.values(node.fpPoolingVotes);
	result["tcp_confidence"] = number(node.tcpConfidence);
	result["tpp_confidence"] = number(node.tppConfidence);
	result["preference_flow_confidence"] = number(node.preferenceFlowConfidence);
	result["fp_completion"] = number(node.fpCompletion);
	result["tcp_completion"] = number(node.tcpCompletion);
	result["tpp_completion"] = number(node.tppCompletion);
	result["fp_votes_current"] = parties.values(node.fpVotesCurrent);
	result["fp_votes_previous"] = parties.values(node.fpVotesPrevious);
	result["tcp_votes_current"] = parties.values(node.tcpVotesCurrent);
	result["tcp_votes_previous"] = parties.values(node.tcpVotesPrevious);
	result["fp_votes_projected"] = parties.values(node.fpVotesProjected);
	result["tpp_votes_projected"] = parties.values(node.tppVotesProjected);
	result["tcp_votes_projected"] = parties.values(node.tcpVotesProjected);
	result["fp_shares"] = parties.values(node.fpShares);
	result["fp_swings"] = parties.values(node.fpSwings);
	result["tcp_shares"] = parties.values(node.tcpShares);
	result["tcp_swings"] = parties.values(node.tcpSwings);
	result["fp_shares_percent"] = parties.values(node.fpSharesPercent());
	result["tcp_shares_percent"] = parties.values(node.tcpSharesPercent());
	result["tpp_share_previous"] = optionalNumber(node.tppSharePrevious);
	result["tpp_share"] = optionalNumber(node.tppShare);
	result["tpp_swing"] = optionalNumber(node.tppSwing);
	result["fp_shares_baseline"] = parties.values(node.fpSharesBaseline);
	result["fp_swings_baseline"] = parties.values(node.fpSwingsBaseline);
	result["tcp_shares_baseline"] = parties.values(node.tcpSharesBaseline);
	result["tcp_swings_baseline"] = parties.values(node.tcpSwingsBaseline);
	result["tpp_share_baseline"] = optionalNumber(node.tppShareBaseline);
	result["tpp_swing_baseline"] = optionalNumber(node.tppSwingBaseline);
	result["fp_deviations"] = parties.values(node.fpDeviations);
	result["tpp_deviation"] = optionalNumber(node.tppDeviation);
	result["specific_fp_deviations"] = parties.values(node.specificFpDeviations);
	result["specific_tpp_deviation"] = optionalNumber(node.specificTppDeviation);
	result["preference_flow_deviation"] =
		optionalNumber(node.preferenceFlowDeviation);
	result["specific_preference_flow_deviation"] =
		optionalNumber(node.specificPreferenceFlowDeviation);
	return result;
}

json projected2pp(LiveV2::Node const& node)
{
	if (!node.tppVotesProjected.contains(0) ||
		!node.tppVotesProjected.contains(1)) {
		return nullptr;
	}
	float const alp = node.tppVotesProjected.at(0);
	float const lnp = node.tppVotesProjected.at(1);
	if (!std::isfinite(alp) || !std::isfinite(lnp)) {
		return {
			{"non_finite", "projected_2pp_input"},
			{"alp_votes", number(alp)},
			{"lnp_votes", number(lnp)}
		};
	}
	if (!(alp + lnp > 0.0f)) {
		return nullptr;
	}
	return number(alp / (alp + lnp) * 100.0f);
}

template <typename Map, typename Key>
json lookupOrNull(Map const& items, Key const& key)
{
	auto const found = items.find(key);
	if (found == items.end()) return nullptr;
	return number(found->second);
}

template <typename Enum, typename NameFn>
json categoryEvidenceArray(
	std::map<Enum, float> const& biases,
	std::map<Enum, float> const& stdDevs,
	std::map<Enum, float> const& raw,
	std::map<Enum, float> const& sourceCount,
	std::map<Enum, float> const& voteCount,
	NameFn nameFn)
{
	std::set<Enum> keys;
	auto addKeys = [&keys](auto const& items) {
		for (auto const& [key, _] : items) keys.insert(key);
	};
	addKeys(biases);
	addKeys(stdDevs);
	addKeys(raw);
	addKeys(sourceCount);
	addKeys(voteCount);
	json array = json::array();
	for (auto const key : keys) {
		json record;
		record["category"] = nameFn(key);
		record["bias"] = lookupOrNull(biases, key);
		record["std_dev"] = lookupOrNull(stdDevs, key);
		record["raw"] = lookupOrNull(raw, key);
		record["source_count"] = lookupOrNull(sourceCount, key);
		record["vote_count"] = lookupOrNull(voteCount, key);
		array.push_back(std::move(record));
	}
	return array;
}

std::string boothTypeCategoryName(Results2::Booth::Type type)
{
	return Results2::Booth::boothTypeName(type);
}

std::string voteTypeCategoryName(Results2::VoteType type)
{
	return Results2::voteTypeName(type);
}

template <typename Enum, typename NameFn>
json categorySensitivityArray(
	std::map<Enum, float> const& sensitivities,
	NameFn nameFn)
{
	json array = json::array();
	for (auto const& [category, sensitivity] : sensitivities) {
		array.push_back({
			{"category", nameFn(category)},
			{"expected_uncounted_votes", number(sensitivity)}
		});
	}
	return array;
}

template <typename Enum, typename NameFn>
json partyCategorySensitivityArray(
	std::map<int, std::map<Enum, float>> const& sensitivities,
	PartyNames const& parties,
	NameFn nameFn)
{
	json array = json::array();
	for (auto const& [partyIndex, categories] : sensitivities) {
		json record = parties.identity(partyIndex);
		record["categories"] = categorySensitivityArray(categories, nameFn);
		array.push_back(std::move(record));
	}
	return array;
}

template <typename Outer, typename Key>
typename Outer::mapped_type const& innerMap(
	Outer const& outer, Key const& key)
{
	static typename Outer::mapped_type const empty;
	auto const found = outer.find(key);
	return found == outer.end() ? empty : found->second;
}

json floatArray(std::vector<float> const& values)
{
	json array = json::array();
	for (float value : values) array.push_back(number(value));
	return array;
}

json seatCompletionArray(
	LiveData::Provider const& election,
	std::vector<std::string> const& seatNames,
	float (LiveData::Provider::*getter)(std::string const&) const)
{
	json array = json::array();
	for (auto const& name : seatNames) {
		array.push_back(number((election.*getter)(name)));
	}
	return array;
}

json floatArray2(std::vector<std::vector<float>> const& values)
{
	json array = json::array();
	for (auto const& inner : values) array.push_back(floatArray(inner));
	return array;
}

template <typename Map>
json binCounts(Map const& bins)
{
	json array = json::array();
	for (auto const& [bin, count] : bins) {
		array.push_back({ {"bin", bin}, {"count", count} });
	}
	return array;
}

json seatPartyMaps(
	std::vector<std::map<int, float>> const& seats, PartyNames const& parties)
{
	json array = json::array();
	for (auto const& seat : seats) array.push_back(parties.values(seat));
	return array;
}

json seatPartyBandMaps(
	std::vector<std::map<int, std::vector<float>>> const& seats,
	PartyNames const& parties)
{
	json array = json::array();
	for (auto const& seat : seats) {
		json seatJson = json::array();
		for (auto const& [partyIndex, values] : seat) {
			json record = parties.identity(partyIndex);
			record["values"] = floatArray(values);
			seatJson.push_back(std::move(record));
		}
		array.push_back(std::move(seatJson));
	}
	return array;
}

template <typename Value>
json tcpPairRecords(
	std::map<std::pair<int, int>, Value> const& items,
	PartyNames const& parties)
{
	json array = json::array();
	for (auto const& [pair, value] : items) {
		json record;
		record["party_a"] = parties.identity(pair.first);
		record["party_b"] = parties.identity(pair.second);
		if constexpr (std::is_same_v<Value, std::vector<float>>) {
			record["values"] = floatArray(value);
		}
		else if constexpr (std::is_floating_point_v<Value>) {
			record["value"] = number(value);
		}
		else {
			record["value"] = value;
		}
		array.push_back(std::move(record));
	}
	return array;
}

json serializeReport(
	Simulation::Report const& report, PartyNames const& parties)
{
	json result;
	result["date_code"] = report.dateCode;
	result["majority_percent"] = parties.values(report.majorityPercent);
	result["minority_percent"] = parties.values(report.minorityPercent);
	result["most_seats_percent"] = parties.values(report.mostSeatsPercent);
	result["tied_percent"] = number(report.tiedPercent);
	result["party_one_swing"] = number(report.partyOneSwing);
	json primaryFrequency = json::array();
	for (auto const& [partyIndex, bins] : report.partyPrimaryFrequency) {
		json record = parties.identity(partyIndex);
		record["bins"] = binCounts(bins);
		primaryFrequency.push_back(std::move(record));
	}
	result["party_primary_frequency"] = std::move(primaryFrequency);
	result["tpp_frequency"] = binCounts(report.tppFrequency);
	result["coalition_fp_frequency"] = binCounts(report.coalitionFpFrequency);
	result["party_win_expectation"] = parties.values(report.partyWinExpectation);
	result["party_win_median"] = parties.values(report.partyWinMedian);
	result["coalition_win_expectation"] = number(report.coalitionWinExpectation);
	result["coalition_win_median"] = number(report.coalitionWinMedian);
	json regionPartyWins = json::array();
	for (auto const& region : report.regionPartyWinExpectation) {
		regionPartyWins.push_back(parties.values(region));
	}
	result["region_party_win_expectation"] = std::move(regionPartyWins);
	json regionCoalitionWins = json::array();
	for (float value : report.regionCoalitionWinExpectation) {
		regionCoalitionWins.push_back(number(value));
	}
	result["region_coalition_win_expectation"] = std::move(regionCoalitionWins);
	json partySeatWins = json::array();
	for (auto const& [partyIndex, counts] : report.partySeatWinFrequency) {
		json record = parties.identity(partyIndex);
		record["values"] = counts;
		partySeatWins.push_back(std::move(record));
	}
	result["party_seat_win_frequency"] = std::move(partySeatWins);
	result["seat_party_one_margin_average"] =
		floatArray(report.seatPartyOneMarginAverage);
	result["coalition_seat_win_frequency"] = report.coalitionSeatWinFrequency;
	result["others_seat_win_frequency"] = report.othersSeatWinFrequency;
	result["total_2cp_percent_counted"] = number(report.getTcpPercentCounted());
	result["total_fp_percent_counted"] = report.countProgress ? number(report.countProgress->fpPercent()) : json(nullptr);
	if (report.countProgress) {
		auto const& progress = *report.countProgress;
		result["count_progress"] = {{"fp_votes",progress.fpVotes}, {"tcp_votes",progress.tcpVotes},
			{"expected_formal_votes",progress.expectedFormalVotes}};
	}
	result["party_one_probability_bounds"] = report.partyOneProbabilityBounds;
	result["party_two_probability_bounds"] = report.partyTwoProbabilityBounds;
	result["coalition_probability_bounds"] = report.coalitionProbabilityBounds;
	result["others_probability_bounds"] = report.othersProbabilityBounds;
	result["region_name"] = report.regionName;
	result["seat_name"] = report.seatName;
	result["seat_incumbents"] = report.seatIncumbents;
	result["seat_margins"] = floatArray(report.seatMargins);
	result["seat_incumbent_margins"] = floatArray(report.seatIncumbentMargins);
	result["party_one_win_proportion"] = floatArray(report.partyOneWinProportion);
	result["party_two_win_proportion"] = floatArray(report.partyTwoWinProportion);
	result["others_win_proportion"] = floatArray(report.othersWinProportion);
	result["region_party_incumbents"] = report.regionPartyIncumbents;
	result["region_coalition_incumbents"] = report.regionCoalitionIncumbents;
	result["seat_party_win_percent"] =
		seatPartyMaps(report.seatPartyWinPercent, parties);
	result["seat_party_mean_fp_share"] =
		seatPartyMaps(report.seatPartyMeanFpShare, parties);
	result["swing_factors"] = report.swingFactors;
	result["probability_bands"] = floatArray(report.probabilityBands);
	result["tpp_probability_band"] = floatArray(report.tppProbabilityBand);
	result["fp_probability_band"] = floatArray2(report.fpProbabilityBand);
	result["seat_fp_probability_band"] =
		seatPartyBandMaps(report.seatFpProbabilityBand, parties);
	json seatTcpBands = json::array();
	for (auto const& seat : report.seatTcpProbabilityBand) {
		seatTcpBands.push_back(tcpPairRecords(seat, parties));
	}
	result["seat_tcp_probability_band"] = std::move(seatTcpBands);
	json seatTcpScenarios = json::array();
	for (auto const& seat : report.seatTcpScenarioPercent) {
		seatTcpScenarios.push_back(tcpPairRecords(seat, parties));
	}
	result["seat_tcp_scenario_percent"] = std::move(seatTcpScenarios);
	json seatTcpWins = json::array();
	for (auto const& seat : report.seatTcpWinPercent) {
		seatTcpWins.push_back(tcpPairRecords(seat, parties));
	}
	result["seat_tcp_win_percent"] = std::move(seatTcpWins);
	result["seat_tpp_probability_band"] = floatArray2(report.seatTppProbabilityBand);
	json regionFpBands = json::array();
	for (auto const& region : report.regionFpProbabilityBand) {
		json regionJson = json::array();
		for (auto const& [partyIndex, values] : region) {
			json record = parties.identity(partyIndex);
			record["values"] = floatArray(values);
			regionJson.push_back(std::move(record));
		}
		regionFpBands.push_back(std::move(regionJson));
	}
	result["region_fp_probability_band"] = std::move(regionFpBands);
	result["region_tpp_probability_band"] =
		floatArray2(report.regionTppProbabilityBand);
	json electionFp = json::array();
	for (auto const& [partyIndex, values] : report.electionFpProbabilityBand) {
		json record = parties.identity(partyIndex);
		record["values"] = floatArray(values);
		electionFp.push_back(std::move(record));
	}
	result["election_fp_probability_band"] = std::move(electionFp);
	result["election_tpp_probability_band"] =
		floatArray(report.electionTppProbabilityBand);
	result["seat_hide_tcps"] = report.seatHideTcps;
	result["trend_prob_bands"] = report.trendProbBands;
	json seatCandidates = json::array();
	for (auto const& seat : report.seatCandidateNames) {
		json seatJson = json::array();
		for (auto const& [partyIndex, candidateName] : seat) {
			json record = parties.identity(partyIndex);
			record["value"] = candidateName;
			seatJson.push_back(std::move(record));
		}
		seatCandidates.push_back(std::move(seatJson));
	}
	result["seat_candidate_names"] = std::move(seatCandidates);
	result["trend_period"] = report.trendPeriod;
	result["final_trend_value"] = report.finalTrendValue;
	result["trend_start_date"] = report.trendStartDate;
	result["tpp_trend"] = floatArray2(report.tppTrend);
	json fpTrend = json::array();
	for (auto const& [partyIndex, series] : report.fpTrend) {
		json record = parties.identity(partyIndex);
		record["values"] = floatArray2(series);
		fpTrend.push_back(std::move(record));
	}
	result["fp_trend"] = std::move(fpTrend);
	json modelledPolls = json::array();
	for (auto const& [partyCode, polls] : report.modelledPolls) {
		json group;
		group["party_code"] = partyCode;
		json pollArray = json::array();
		for (auto const& poll : polls) {
			pollArray.push_back({
				{"pollster", poll.pollster},
				{"day", poll.day},
				{"base", number(poll.base)},
				{"adjusted", number(poll.adjusted)},
				{"reported", number(poll.reported)}
			});
		}
		group["polls"] = std::move(pollArray);
		modelledPolls.push_back(std::move(group));
	}
	result["modelled_polls"] = std::move(modelledPolls);
	result["prev_election_2pp"] = number(report.prevElection2pp);
	return result;
}

std::string filenameSafeCode(std::string_view code)
{
	if (code.empty()) return "unknown";
	for (unsigned char character : code) {
		if (!std::isalnum(character)) return "unknown";
	}
	return std::string(code);
}

std::string compactRunStamp(Timestamp const& completedAt)
{
	auto const local = completedAt.formatIsoLocal();
	if (local.size() < 19) return "unknown";
	std::string stamp;
	stamp.reserve(15);
	for (char character : local) {
		if (std::isdigit(static_cast<unsigned char>(character)) ||
			character == 'T') {
			stamp.push_back(character);
		}
	}
	return stamp.empty() ? "unknown" : stamp;
}

bool nativeFolderIsSingleComponent(std::filesystem::path const& folder)
{
	if (folder.empty() || folder.is_absolute() || folder.has_root_name() ||
		folder.has_root_directory()) {
		return false;
	}
	return std::distance(folder.begin(), folder.end()) == 1;
}

void reportExportFailure(
	LiveRunExport::FeedbackFunc const&,
	LiveRunExport::ActionRequiredFunc const& actionRequired,
	std::string const& detail)
{
	std::string const message =
		"Could not write live diagnostic export: " + detail +
		" The simulation report was still saved.";
	logger << message << "\n";
	actionRequired(message);
}

json liveAnalysisSummaryFrom(json const& analysis)
{
	json summary = {
		{"booth_count", 0},
		{"seat_count", 0}
	};
	if (analysis.contains("booths") && analysis["booths"].is_array()) {
		summary["booth_count"] = analysis["booths"].size();
		if (!analysis["booths"].empty() && analysis["booths"][0].is_object()) {
			auto const& booth = analysis["booths"][0];
			if (booth.contains("seat_name")) {
				summary["first_booth_seat"] = booth["seat_name"];
			}
			if (booth.contains("name")) {
				summary["first_booth_name"] = booth["name"];
			}
		}
	}
	if (analysis.contains("seats") && analysis["seats"].is_array()) {
		summary["seat_count"] = analysis["seats"].size();
	}
	summary["booth_type"] = json::array();
	summary["vote_type"] = json::array();
	summary["raw_2pp_deviation"] = 0.0;
	if (analysis.contains("category_biases") &&
		analysis["category_biases"].is_object()) {
		auto const& biases = analysis["category_biases"];
		if (biases.contains("tpp") && biases["tpp"].is_object()) {
			auto const& tpp = biases["tpp"];
			if (tpp.contains("booth_type") && tpp["booth_type"].is_array()) {
				summary["booth_type"] = tpp["booth_type"];
			}
			if (tpp.contains("vote_type") && tpp["vote_type"].is_array()) {
				summary["vote_type"] = tpp["vote_type"];
			}
		}
	}
	if (analysis.contains("election") && analysis["election"].is_object()) {
		auto const& election = analysis["election"];
		if (election.contains("projected_2pp")) {
			summary["projected_2pp"] = election["projected_2pp"];
		}
		if (election.contains("node") && election["node"].is_object()) {
			auto const& node = election["node"];
			if (node.contains("tpp_deviation") &&
				!node["tpp_deviation"].is_null()) {
				summary["raw_2pp_deviation"] = node["tpp_deviation"];
			}
		}
	}
	// Persist only small scalar diagnostics for the asynchronous history viewer.
	// Detailed state and observation arrays remain in the analysis sidecar.
	if (analysis.contains("tpp_integration"))
		summary.update(LiveTppDiagnostics::viewerSummary(analysis["tpp_integration"]));
	return summary;
}

bool isHighPrecisionFloatKey(std::string_view key)
{
	return key == "seat_fp_completion" ||
		key == "seat_tpp_completion" ||
		key == "seat_tcp_completion" ||
		key == "bias" ||
		key == "std_dev" ||
		key == "raw" ||
		key == "raw_2pp_deviation";
}

std::string formatExportFloat(double value, int decimals)
{
	if (!std::isfinite(value)) return "null";
	if (decimals < 0) decimals = 0;
	double const scale = std::pow(10.0, decimals);
	double const rounded = std::round(value * scale) / scale;
	if (rounded == 0.0) return "0";
	char buffer[64];
	int const length = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, rounded);
	if (length <= 0 || length >= int(sizeof(buffer))) return "0";
	int end = length;
	while (end > 0 && buffer[end - 1] == '0') --end;
	if (end > 0 && buffer[end - 1] == '.') --end;
	if (end <= 0) return "0";
	return std::string(buffer, buffer + end);
}

void appendCompactJson(std::string& out, json const& value, std::string_view parentKey = {})
{
	if (value.is_number_float()) {
		int const decimals = isHighPrecisionFloatKey(parentKey) ? 4 : 3;
		out += formatExportFloat(value.get<double>(), decimals);
		return;
	}
	if (value.is_array()) {
		out.push_back('[');
		bool first = true;
		for (auto const& item : value) {
			if (!first) out.push_back(',');
			first = false;
			appendCompactJson(out, item, parentKey);
		}
		out.push_back(']');
		return;
	}
	if (value.is_object()) {
		out.push_back('{');
		bool first = true;
		for (auto it = value.begin(); it != value.end(); ++it) {
			if (!first) out.push_back(',');
			first = false;
			out += json(it.key()).dump();
			out.push_back(':');
			appendCompactJson(out, it.value(), it.key());
		}
		out.push_back('}');
		return;
	}
	out += value.dump();
}

std::string dumpCompactJson(json const& value)
{
	std::string out;
	out.reserve(256);
	appendCompactJson(out, value);
	return out;
}

std::string gzipAnalysis(std::string const& contents)
{
	// Compress only the diagnostic sidecar. The main snapshot remains directly
	// readable by Live Booths; neither export encoding affects simulation state.
	wxMemoryOutputStream memory;
	wxZlibOutputStream compressed(memory, 6, wxZLIB_GZIP);
	compressed.Write(contents.data(), contents.size());
	if (!compressed.IsOk() || !compressed.Close() || !memory.IsOk()) {
		throw std::runtime_error("Could not finish compressing the live analysis archive.");
	}
	std::string result(memory.GetSize(), '\0');
	if (memory.CopyTo(result.data(), result.size()) != result.size()) {
		throw std::runtime_error("Could not retain the compressed live analysis archive.");
	}
	return result;
}

bool writeFileAtomically(
	std::filesystem::path const& finalPath,
	std::string const& relativePath,
	std::string const& contents,
	LiveRunExport::FeedbackFunc const& feedback,
	LiveRunExport::ActionRequiredFunc const& actionRequired)
{
	if (std::filesystem::exists(finalPath)) {
		reportExportFailure(feedback, actionRequired,
			relativePath + " already exists.");
		return false;
	}
	auto temporaryPath = finalPath;
	temporaryPath += ".tmp";
	{
		std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!output) {
			reportExportFailure(feedback, actionRequired,
				"could not open a temporary file for " + relativePath + ".");
			return false;
		}
		output << contents;
		output.close();
		if (output.fail()) {
			std::error_code removeError;
			std::filesystem::remove(temporaryPath, removeError);
			reportExportFailure(feedback, actionRequired,
				"could not finish writing " + relativePath + ".");
			return false;
		}
	}
	std::error_code renameError;
	std::filesystem::rename(temporaryPath, finalPath, renameError);
	if (renameError) {
		std::error_code removeError;
		std::filesystem::remove(temporaryPath, removeError);
		reportExportFailure(feedback, actionRequired,
			"could not replace " + relativePath + ".");
		return false;
	}
	return true;
}

} // namespace

nlohmann::json LiveV2::Election::tppConversionDiagnostic() const
{
	// Compact current-snapshot robustness measures. These describe observation
	// uncertainty, not a measured signed preference-flow correction.
	json result;
	json calibration = json::array();
	double matchedTotal = tppPooledPreferenceUnits();
	for (auto const& [code, units] : tppMatchedPreferenceUnits()) {
		double sd = LiveTppComposition::conversionSd(matchedTotal);
		calibration.push_back({{"category_code", code}, {"matched_units", units}, {"full_pool_shared_sd_pp", sd}});
	}
	// Use actual received row weights, including measured rows with zero pool.
	// RMS is a scale diagnostic, not the SD of a national aggregate.
	double weight = 0, pool = 0, localVariance = 0, sharedVariance = 0;
	for (auto const& row : *tppReceivedObservations) {
		weight += row.comparisonWeight;
		pool += row.comparisonWeight*row.unresolvedPreferenceFraction;
		localVariance += row.comparisonWeight*row.localConversionSd*row.localConversionSd;
		auto const& covariance = tppTransferCalibration->factors.covariance;
		sharedVariance += row.comparisonWeight*(row.sharedConversionSd*row.sharedConversionSd
			+ row.greensTransferSd*row.greensTransferSd*covariance[0][0]
			+ row.independentTransferSd*row.independentTransferSd*covariance[1][1]
			+ 2*row.greensTransferSd*row.independentTransferSd*covariance[0][1]);
	}
	result["conversion_calibration"] = std::move(calibration);
	result["conversion_matched_units"] = matchedTotal;
	result["conversion_pool_percent"] = weight > 0 ? json(100*pool/weight) : json(nullptr);
	result["conversion_local_sd"] = weight > 0 ? json(std::sqrt(localVariance/weight)) : json(nullptr);
	result["conversion_shared_sd"] = weight > 0 ? json(std::sqrt(sharedVariance/weight)) : json(nullptr);
	return result;
}

nlohmann::json LiveV2::Election::tppIntegrationDiagnostic() const
{
	if (!tppIntegration) return nullptr;
	auto const& update = *tppIntegration;
	auto const& names = sim.getLiveBaselineReport()->seatName;
	json result = {{"method", "joint_category_composition_v18"},
        {"evidence_group_partition", "squared_odds_information"},
        {"evidence_group_half_weight_gap", LiveTppIntegration::EvidenceGroupHalfWeightGap},
		{"sparse_sample_vote_scale", LiveTppIntegration::SparseSampleVoteScale},
		{"comparison_completion_start", LiveTppIntegration::ComparisonCompletionStart},
		{"comparison_completion_curve", "smoothstep"},
		{"conversion_uncertainty_method", "matched_conversion_with_external_transfer_states"},
		{"greens_transfer_flow_sd_pp", LiveTppComposition::GreensTransferFlowSd},
		{"independent_transfer_flow_sd_pp", LiveTppComposition::IndependentTransferFlowSd},
		{"greens_strength_slope", LivePartyConversion::GreensStrengthSlope},
		{"greens_history_retention", LivePartyConversion::GreensHistoryRetention},
		{"conversion_pool_variance", LiveTppComposition::ConversionPoolVariance},
		{"greens_conversion_pool_variance", LiveTppComposition::GreensConversionPoolVariance},
		{"conversion_local_variance_floor", LiveTppComposition::LocalConversionVarianceFloor},
		{"conversion_local_evidence_rate", LiveTppComposition::LocalConversionEvidenceRate},
		{"inferred_evidence_percent", optionalNumber(tppInferredEvidencePercent())},
		{"composition_covariance_budget_scale", tppCompositionBudgetScale},
		{"category_parameter_version", "historical_14_pairs_v1"},
		{"category_departure_sd_multiplier", LiveTppComposition::CategoryDepartureSdMultiplier},
		{"scale", "25_times_log_odds"}, {"observation_count", update.steps.size()}};
	result.update(tppConversionDiagnostic());
	// These factors are calibrated solely from matched current FP/TPP accounts,
	// before conditioning seats. Export the source accounts so prior disagreement
	// can be distinguished from an actual held-out conversion residual.
	json matched = json::array();
	for (std::size_t s = 0; s < tppTransferCalibration->districts.size(); ++s) {
		auto const& a = tppTransferCalibration->districts[s];
		if (a.pool <= 0) continue;
		matched.push_back({{"seat", seats[s].name}, {"locations", a.locations},
			{"preference_votes", a.pool}, {"expected_alp_preferences", a.expected},
			{"observed_alp_preferences", a.observed}, {"total_mismatch", a.mismatch},
			{"group_votes", a.groups}});
	}
	result["matched_transfer_calibration"] = {{"districts", std::move(matched)},
		{"district_flow_sd_pp", LivePartyConversion::CalibrationDistrictFlowSd},
		{"other_flow_sd_pp", LivePartyConversion::CalibrationOtherFlowSd},
		{"transfer_variance_floor", LivePartyConversion::TransferVarianceFloor},
		{"factor_mean", tppTransferCalibration->factors.mean},
		{"factor_covariance", tppTransferCalibration->factors.covariance}};
	result["greens_transfer_error_pp"] = tppTransferCalibration->factors.mean[0]*LiveTppComposition::GreensTransferFlowSd;
	result["independent_transfer_error_pp"] = tppTransferCalibration->factors.mean[1]*LiveTppComposition::IndependentTransferFlowSd;
	result["greens_transfer_error_sd_pp"] = std::sqrt(tppTransferCalibration->factors.covariance[0][0])*LiveTppComposition::GreensTransferFlowSd;
	result["independent_transfer_error_sd_pp"] = std::sqrt(tppTransferCalibration->factors.covariance[1][1])*LiveTppComposition::IndependentTransferFlowSd;
	// Archive the covariance needed for numerical comparisons, but never copy
	// it into compact viewer history. Reporting-order and transfer states are observation errors,
	// not additional prior uncertainty about seat totals.
	result["prior_covariance"] = update.prior.covariance;
	result["reporting_order_initial_sd"] = LiveTppComposition::ReportingOrderInitialSd;
	result["reporting_order_coverage_scale"] = LiveTppComposition::ReportingOrderCoverageScale;
	result["reporting_order_compensation_strength"] = LiveTppComposition::ReportingOrderCompensationStrength;
	result["reporting_order_correlation"] = LiveTppComposition::ReportingOrderCorrelation;
	result["ordinary_fp_order_sd"] = LiveTppComposition::reportingOrderSd(tppOrdinaryReportingProgress, false);
	result["ordinary_tcp_order_sd"] = LiveTppComposition::reportingOrderSd(tppOrdinaryReportingProgress, true);
	result["ordinary_fp_order_coverage"] = tppOrdinaryReportingProgress.coverage(false);
	result["ordinary_tcp_order_coverage"] = tppOrdinaryReportingProgress.coverage(true);
	auto orderStart = names.size() + tppCategoryCodes.size();
	auto transferStart = orderStart + LiveTppComposition::ReportingOrderFactors;
	auto syntheticStart = transferStart+LiveTppComposition::PartyTransferFactors;
	std::vector<int> liveIndex(names.size());
	for (std::size_t s = 0; s < tppProjectSeatIndices.size(); ++s) liveIndex[tppProjectSeatIndices[s]] = int(s);
	json states = json::array();
	for (std::size_t i = 0; i < update.prior.mean.size(); ++i) {
		std::string name;
		if (i < names.size()) name = names[i];
		else if (i < orderStart) {
			int code = tppCategoryCodes[i - names.size()];
			name = code >= 100 ? std::string(LiveData::voteTypeName(LiveData::VoteType(code - 100)))
				: std::string(LiveData::boothTypeName(LiveData::BoothType(code)));
		}
		else if (i < transferStart) name = i == orderStart ? "Ordinary FP reporting order" : "Ordinary TCP reporting order";
		else if (i < syntheticStart) name = i == transferStart ? "Greens conversion error" : "Independent conversion error";
		else name = "Synthetic category baseline " + std::to_string(tppSyntheticCategoryCodes.at(i-syntheticStart));
		states.push_back({{"name", name}, {"kind", i < names.size() ? "seat" :
			(i < orderStart ? "shared_error" : (i < transferStart ? "reporting_order" : (i < syntheticStart ? "party_transfer" : "synthetic_baseline")))},
			{"prior_mean", number(update.prior.mean[i])},
			{"posterior_mean", number(update.posterior.mean[i])},
			{"accounted_posterior_mean", number(i < names.size() ? accountedTpp(liveIndex[i],update.posterior.mean[i]) : update.posterior.mean[i])},
			{"prior_variance", number(update.prior.covariance[i][i])},
			{"posterior_variance", number(update.posterior.covariance[i][i])}});
	}
	result["states"] = std::move(states);
	// Reference translations are not swings or recovered vote records. Export
	// them separately so synthetic-history discrepancies remain inspectable.
	json references = json::array();
	double expected = tppExpectedVotes();
	for (std::size_t s = 0; s < tppCategoryAccounts->size(); ++s)
		references.push_back({{"seat", names.at(tppProjectSeatIndices.at(s))},
			{"conversion_budget_fraction", expected > 0 ? tppCategoryAccounts->at(s).expectedVotes()/expected : 0},
			{"shift", number(tppCategoryAccounts->at(s).referenceShift)}});
	result["booth_reference_shifts"] = std::move(references);
	json baselines = json::array(), directCounts = json::array();
	for (std::size_t s = 0; s < tppCategoryAccounts->size(); ++s) {
		auto const& account = tppCategoryAccounts->at(s);
		if (account.direct.votes > 0) directCounts.push_back({{"seat",seats[s].name},
			{"received_votes",account.direct.votes},{"measured_first_votes",account.direct.first},
			{"inferred_votes",account.direct.inferredVotes},{"inferred_first_votes",account.direct.inferredFirst},
			{"inferred_share_variance",account.direct.conversionVariance}});
		for (auto const& [code,source] : account.sources)
			baselines.push_back({{"seat",seats[s].name},{"category_code",code},
				{"source",LiveTppComposition::baselineSourceName(source)}});
	}
	result["category_baselines"] = std::move(baselines);
	result["direct_category_counts"] = std::move(directCounts);
	result["synthetic_baseline_local_sd"] = LiveTppComposition::SyntheticBaselineSd;
	result["synthetic_baseline_shared_sd"] = LiveTppComposition::SyntheticBaselineSharedSd;
	json observations = json::array();
	// Export the received category rows, before within-seat decorrelation.
	// The update's numerical rows are linear combinations of these categories.
	for (auto const& o : *tppReceivedObservations) {
		json factors = json::array();
		for (std::size_t i = names.size(); i < orderStart; ++i)
			factors.push_back(number(o.h[i]));
		json orderFactors = json::array();
		for (std::size_t i = orderStart; i < transferStart; ++i)
			orderFactors.push_back(number(o.h[i]));
		json transferFactors = json::array();
		for (std::size_t i = transferStart; i < syntheticStart; ++i)
			transferFactors.push_back(number(o.h[i]));
		json syntheticFactors = json::array();
		for (std::size_t i = syntheticStart; i < o.h.size(); ++i) syntheticFactors.push_back(number(o.h[i]));
		observations.push_back({{"seat", names.at(o.seat)}, {"category_code", o.category},
			{"seat_loading", number(o.h.at(o.seat))},
			{"value", number(o.value)}, {"residual_variance", number(o.variance)},
			{"inferred_fraction", number(o.inferredFraction)},
			{"unresolved_preference_fraction", number(o.unresolvedPreferenceFraction)},
			{"greens_preference_fraction", number(o.greensPreferenceFraction)},
			{"independent_preference_fraction", number(o.independentPreferenceFraction)},
			{"greens_transfer_sd", number(o.greensTransferSd)},
			{"independent_transfer_sd", number(o.independentTransferSd)},
			{"conversion_share_jacobian", number(o.conversionJacobian)},
			{"comparison_weight", number(o.comparisonWeight)},
			{"sample_votes", number(o.sampleVotes)},
			{"sample_coverage", number(o.sampleCoverage)},
			{"sparse_sample_variance", number(o.sparseSampleVariance)},
			{"category_fp_coverage", number(o.categoryFpCoverage)},
			{"comparison_quality_floor", number(o.comparisonQualityFloor)},
			{"restored_comparison_fraction", number(o.restoredComparisonFraction)},
			{"compensation_reporting_order_sd", number(o.compensationReportingOrderSd)},
			{"unfaded_location_variance", number(o.unfadedLocationVariance)},
			{"retained_small_sample_variance", number(o.retainedSmallSampleVariance)},
			{"location_group", o.fpDerivedGroup ? "includes_inferred" : "measured"},
			{"local_matched_units", o.localMatchedUnits}, {"shared_matched_units", o.sharedMatchedUnits},
			{"local_conversion_sd", o.localConversionSd}, {"shared_conversion_sd", o.sharedConversionSd},
			{"greens_local_conversion_sd", o.greensLocalConversionSd},
			{"other_local_conversion_sd", o.otherLocalConversionSd},
			{"conversion_variance", number(o.conversionVariance)},
			{"shared_error_loadings", std::move(factors)},
			{"reporting_order_loadings", std::move(orderFactors)},
			{"party_transfer_loadings", std::move(transferFactors)},
			{"synthetic_baseline_sd", o.syntheticBaselineSd},
			{"synthetic_baseline_loadings", std::move(syntheticFactors)}});
	}
	result["observations"] = std::move(observations);
	// These are district priors before existing booth and observed corrections,
	// not counted preferences. Raw candidate IDs distinguish returning independents.
	json partyPriors = json::array();
	for (auto const& seat : seats) for (auto const& [party, flow] : seat.partyPreferenceFlows)
		partyPriors.push_back({{"seat", seat.name}, {"party", party}, {"alp_flow_percent", 100*flow}});
	result["party_conversion_priors"] = std::move(partyPriors);
	// Use the same historical FP weights as the national histogram. This is a
	// transformed-scale composition diagnostic, before finite-count bounds and
	// sampled turnout; it is not a substitute for the exported TPP quantiles.
	if (run.pastSeatResults.size() == names.size()) {
		double total = 0, priorVariance = 0, posteriorVariance = 0, posteriorCentre = 0;
		for (auto const& seat : run.pastSeatResults) total += seat.turnoutCount;
		if (total > 0) {
			for (std::size_t i = 0; i < names.size(); ++i) {
				// Convert each seat centre back to vote share before aggregation.
				// This is a prepared centre, not the mean of bounded simulation draws.
				posteriorCentre += run.pastSeatResults[i].turnoutCount / total
					* LiveTppComposition::share(accountedTpp(liveIndex[i],update.posterior.mean[i]));
				for (std::size_t j = 0; j < names.size(); ++j) {
					double w = double(run.pastSeatResults[i].turnoutCount) * run.pastSeatResults[j].turnoutCount / (total * total);
					priorVariance += w * update.prior.covariance[i][j];
					posteriorVariance += w * update.posterior.covariance[i][j]
						* accountedTppDerivative(liveIndex[i],update.posterior.mean[i])
						* accountedTppDerivative(liveIndex[j],update.posterior.mean[j]);
				}
			}
			for (std::size_t i = 0; i < names.size(); ++i) {
				auto const& account = tppCategoryAccounts->at(liveIndex[i]);
				double amount = std::max(account.expectedVotes(),account.direct.votes);
				if (amount <= 0) continue;
				double exposure = run.pastSeatResults[i].turnoutCount/total*account.direct.inferredVotes/amount
					* LiveTppComposition::conversionShareJacobian(LiveTppComposition::share(accountedTpp(liveIndex[i],update.posterior.mean[i])));
				posteriorVariance += exposure*exposure*account.direct.conversionVariance;
			}
			result["weighted_posterior_centre"] = posteriorCentre;
			result["weighted_prior_sd"] = std::sqrt(std::max(0., priorVariance));
			result["weighted_posterior_sd"] = std::sqrt(std::max(0., posteriorVariance));
		}
	}
	return result;
}

nlohmann::json LiveV2::Election::getDiagnosticSnapshot() const
{
	PartyNames const parties{sim.getLatestReport()};
	json electionJson;
	electionJson["projected_2pp"] = projected2pp(node);
	electionJson["final_specific_fp_deviations"] =
		parties.values(finalSpecificFpDeviations);
	electionJson["final_specific_tpp_deviation"] =
		optionalNumber(finalSpecificTppDeviation);
	electionJson["offset_specific_fp_deviations"] =
		parties.values(offsetSpecificFpDeviations);
	electionJson["offset_specific_tpp_deviation"] =
		optionalNumber(offsetSpecificTppDeviation);
	electionJson["node"] = serializeNode(node, parties);

	json regions = json::array();
	for (auto const& region : largeRegions) {
		json regionJson;
		regionJson["name"] = region.name;
		regionJson["final_specific_fp_deviations"] =
			parties.values(region.finalSpecificFpDeviations);
		regionJson["final_specific_tpp_deviation"] =
			optionalNumber(region.finalSpecificTppDeviation);
		regionJson["offset_specific_fp_deviations"] =
			parties.values(region.offsetSpecificFpDeviations);
		regionJson["offset_specific_tpp_deviation"] =
			optionalNumber(region.offsetSpecificTppDeviation);
		regionJson["node"] = serializeNode(region.node, parties);
		regions.push_back(std::move(regionJson));
	}

	json seatsJson = json::array();
	json boothsJson = json::array();
	for (auto const& seat : seats) {
		json seatJson;
		seatJson["name"] = seat.name;
		seatJson["parent_region_index"] = seat.parentRegionIndex;
		if (seat.parentRegionIndex >= 0 &&
			seat.parentRegionIndex < int(largeRegions.size())) {
			seatJson["region_name"] = largeRegions[seat.parentRegionIndex].name;
		}
		else {
			seatJson["region_name"] = nullptr;
		}
		seatJson["final_specific_fp_deviations"] =
			parties.values(seat.finalSpecificFpDeviations);
		seatJson["final_specific_tpp_deviation"] =
			optionalNumber(seat.finalSpecificTppDeviation);
		seatJson["offset_specific_fp_deviations"] =
			parties.values(seat.offsetSpecificFpDeviations);
		seatJson["offset_specific_tpp_deviation"] =
			optionalNumber(seat.offsetSpecificTppDeviation);
		seatJson["fp_all_booths_std_dev"] = parties.values(seat.fpAllBoothsStdDev);
		seatJson["independent_pooling_baseline_share"] = number(seat.independentPoolingBaselineShare);
		if (fpEvidence) {
			auto const& evidence = fpEvidence->at(&seat-seats.data());
			LiveFpEvidence::Shares extraVariance, normalVariance, disagreementGaps;
			for (auto const& [candidate,share] : evidence.future) {
				extraVariance[candidate] = LiveFpEvidence::additionalVariance(evidence,candidate);
				normalVariance[candidate] = LiveFpEvidence::earlyVariance(evidence,candidate);
				disagreementGaps[candidate] = LiveFpEvidence::disagreementGap(evidence,candidate);
			}
			seatJson["fp_evidence"] = {
				{"disagreement_distribution","two_estimate_endpoints"},
				{"completion",number(evidence.evidence.completion)},
				{"reporting_breadth",number(evidence.evidence.breadth)},
				{"precision_evidence_gate",number(LiveFpEvidence::breadthGate(evidence.evidence))},
				{"absolute_weights",parties.values(evidence.weight)},
				{"historical_remaining",parties.values(evidence.evidence.historical)},
				{"absolute_remaining",parties.values(evidence.evidence.absolute)},
				{"additional_log_odds_variance",parties.values(extraVariance)},
				{"normal_additional_log_odds_variance",parties.values(normalVariance)},
				{"disagreement_log_odds_gap",parties.values(disagreementGaps)},
				{"combined_remaining_shares",parties.values(evidence.future)}
			};
		}
		if (fpIntegration) {
			auto const& moments = fpIntegration->at(&seat-seats.data());
			std::map<int,double> weights;
			for (auto const& [party,variance] : moments.count.logOddsVariance)
				if (moments.baseline.logOddsVariance.contains(party))
					weights[party] = LiveFpIntegration::countWeight(
						moments.baseline.logOddsVariance.at(party),variance,moments.evidenceGate);
			seatJson["fp_integration"] = {
				{"method","local_precision"},
				{"evidence_gate",number(moments.evidenceGate)},
				{"shared_deviation",parties.values(moments.sharedDeviation)},
				{"baseline_log_odds_mean",parties.values(moments.baseline.logOddsMean)},
				{"count_log_odds_mean",parties.values(moments.count.logOddsMean)},
				{"baseline_log_odds_variance",parties.values(moments.baseline.logOddsVariance)},
				{"count_log_odds_variance",parties.values(moments.count.logOddsVariance)},
				{"individual_count_weights",parties.values(weights)}
			};
		}
		// This optional legacy diagnostic is not the active joint TPP scale.
		seatJson["tpp_all_booths_std_dev"] = optionalNumber(seat.tppAllBoothsStdDev);
		// Source weights explain which part of the shared conversion adjustment
		// reaches this seat. They are not counts or extra turnout observations.
		seatJson["estimated_tpp"] = {
			{"expected_votes",number(seat.estimatedTppEvidence.expectedVotes)},
			{"inferred_evidence_votes",number(seat.estimatedTppEvidence.inferredEvidenceVotes)},
			{"observed_evidence_votes",number(seat.estimatedTppEvidence.observedEvidenceVotes)},
			{"confidence",number(seat.estimatedTppEvidence.confidence())},
			{"inferred_share",number(seat.estimatedTppEvidence.inferredShare())},
			{"influence",number(seat.estimatedTppInfluence)}
		};
		seatJson["tcp_all_booths_std_dev"] = optionalNumber(seat.tcpAllBoothsStdDev);
		seatJson["live_preference_flow_deviation"] =
			number(seat.livePreferenceFlowDeviation);
		seatJson["tcp_focus_party_index"] = optionalInt(seat.tcpFocusPartyIndex);
		seatJson["tcp_focus_party_pref_flow"] =
			optionalNumber(seat.tcpFocusPartyPrefFlow);
		seatJson["tcp_focus_party_confidence"] =
			optionalNumber(seat.tcpFocusPartyConfidence);
		seatJson["independent_party_index"] =
			partyIndexOrNull(seat.independentPartyIndex);
		seatJson["live_independent_party_index"] =
			partyIndexOrNull(seat.liveIndependentPartyIndex);
		seatJson["tpp_vote_type_sensitivity"] = categorySensitivityArray(
			seat.tppVoteTypeSensitivity, &voteTypeCategoryName);
		seatJson["tpp_booth_type_sensitivity"] = categorySensitivityArray(
			seat.tppBoothTypeSensitivity, &boothTypeCategoryName);
		seatJson["fp_vote_type_sensitivity"] = partyCategorySensitivityArray(
			seat.fpVoteTypeSensitivity, parties, &voteTypeCategoryName);
		seatJson["fp_booth_type_sensitivity"] = partyCategorySensitivityArray(
			seat.fpBoothTypeSensitivity, parties, &boothTypeCategoryName);
		seatJson["nationals_proportion"] = optionalNumber(seat.nationalsProportion);
		seatJson["node"] = serializeNode(seat.node, parties);
		seatsJson.push_back(std::move(seatJson));

		for (int boothIndex : seat.booths) {
			if (boothIndex < 0 || boothIndex >= int(readBooths().size())) continue;
			auto const& booth = readBooths()[boothIndex];
			json boothJson;
			boothJson["name"] = booth.name;
			boothJson["seat_name"] = seat.name;
			boothJson["vote_type"] = Results2::voteTypeName(booth.voteType);
			boothJson["booth_type"] =
				Results2::Booth::boothTypeName(booth.boothType);
			boothJson["coords"] = optionalCoords(booth.coords);
			boothJson["same_seat"] = booth.sameSeat;
			boothJson["tpp_votes_estimated"] =
				parties.values(booth.tppVotesEstimated);
			if (booth.node.preferenceFlowDeviation) {
				auto historical = historicalPreferenceTransfer(booth);
				if (historical) {
					double reliability = LivePartialCount::historicalFlowReliability(historical->share());
					// Retain the reason for reduced analytical confidence even when
					// the comparison is too small to warrant an operator notice.
					if (reliability < 1) boothJson["historical_preference_evidence"] = {
						{"inferred_flow_percent",number(historical->share()*100)},
						{"nonfinalist_votes",number(historical->votes)},
						{"extrapolation_reliability",number(reliability)}};
				}
			}
			if (newVoteContexts && !booth.futureFpShares.empty()) {
				// Keep the two primary compositions and their influence available
				// for replay inspection without recording draws or extra identities.
				auto category = booth.voteType == Results2::VoteType::Early || booth.voteType == Results2::VoteType::PrePoll
					? LiveNewVotes::Category::AggregatedEarly : LiveNewVotes::Category::General;
				boothJson["new_vote_composition"] = {
					{"observed_weight",number(LiveNewVotes::observedWeight(booth.node.totalFpVotesCurrent(),turnoutFpTarget(boothIndex),category))},
					{"context_shares",parties.values(newVoteContexts->at(boothIndex))},
					{"future_shares",parties.values(booth.futureFpShares)}};
			}
			// Keep the local batch explanation beside its projection so replay
			// analysis can distinguish an identified older FP cohort from an
			// unmatched allocation. Fully counted booths need no extra audit row.
			if (booth.preferenceEvidence && (booth.voteType != Results2::VoteType::Ordinary ||
				booth.node.totalFpVotesCurrent() > booth.node.totalTcpVotesCurrent())) {
				auto const& evidence = *booth.preferenceEvidence;
				boothJson["preference_batch"] = {{"exact_current_tcp",evidence.matchingCurrentTcp},
					{"fp_source_time",evidence.fpSourceTime},{"tcp_source_time",evidence.tcpSourceTime},
					{"nonfinalist_votes",number(evidence.preferenceVotes)},
					{"flow_log_odds_offset",number(evidence.offset)},
					{"fp_votes",parties.values(evidence.fp)}};
			}
			if (booth.hasTcpRevision()) {
				auto const& basis = *booth.tcpBasis;
				int first = basis.tcp.begin()->first, second = std::next(basis.tcp.begin())->first;
				if (auto revised = revisedBoothTcp(booth,first,second)) {
					boothJson["tcp_fp_revision"] = {{"original_fp",parties.values(basis.fp)},
						{"original_tcp",parties.values(basis.tcp)},
						{"estimated_tcp",parties.values(std::map<int,double>{{first,revised->first},{second,revised->votes-revised->first}})},
						{"lower_bounds",parties.values(LivePartialCount::revisedLowerBounds(booth.node.fpVotesCurrent,basis.tcp,basis))},
						{"uncertain_transfer_votes",number(revised->uncertainVotes())},
						{"candidate_transfers_available",!basis.transfers.empty()}};
				}
			}
			boothJson["node"] = serializeNode(booth.node, parties);
			boothsJson.push_back(std::move(boothJson));
		}
	}

	json liveAnalysis;
	liveAnalysis["tpp_integration"] = tppIntegrationDiagnostic();
	liveAnalysis["election"] = std::move(electionJson);
	liveAnalysis["regions"] = std::move(regions);
	liveAnalysis["seats"] = std::move(seatsJson);
	liveAnalysis["booths"] = std::move(boothsJson);
	if (turnoutDiagnostic) liveAnalysis["turnout"] = *turnoutDiagnostic;
	if (preferenceCorrectionDiagnostic) liveAnalysis["preference_rechecking"] = *preferenceCorrectionDiagnostic;

	json tppBiases;
	tppBiases["booth_type"] = categoryEvidenceArray(
		boothTypeTppBiases,
		boothTypeTppBiasStdDev,
		boothTypeTppBiasesRaw,
		boothTypeTppSourceCount,
		boothTypeTppVoteCount,
		&boothTypeCategoryName);
	tppBiases["vote_type"] = categoryEvidenceArray(
		voteTypeTppBiases,
		voteTypeTppBiasStdDev,
		voteTypeTppBiasesRaw,
		voteTypeTppSourceCount,
		voteTypeTppVoteCount,
		&voteTypeCategoryName);
	tppBiases["non_classic"] = {
		{"bias_percentage_points", number(nonClassicTppBiasPercentagePoints)},
		{"confidence", number(nonClassicTppBiasConfidence)}
	};

	std::set<int> fpParties;
	auto addPartyKeys = [&fpParties](auto const& items) {
		for (auto const& [partyIndex, _] : items) fpParties.insert(partyIndex);
	};
	addPartyKeys(boothTypeFpBiases);
	addPartyKeys(voteTypeFpBiases);
	addPartyKeys(boothTypeFpBiasStdDev);
	addPartyKeys(voteTypeFpBiasStdDev);
	addPartyKeys(boothTypeFpBiasesRaw);
	addPartyKeys(voteTypeFpBiasesRaw);
	addPartyKeys(boothTypeFpSourceCount);
	addPartyKeys(voteTypeFpSourceCount);
	addPartyKeys(boothTypeFpVoteCount);
	addPartyKeys(voteTypeFpVoteCount);

	json fpBiases = json::array();
	for (int partyIndex : fpParties) {
		json record = parties.identity(partyIndex);
		record["booth_type"] = categoryEvidenceArray(
			innerMap(boothTypeFpBiases, partyIndex),
			innerMap(boothTypeFpBiasStdDev, partyIndex),
			innerMap(boothTypeFpBiasesRaw, partyIndex),
			innerMap(boothTypeFpSourceCount, partyIndex),
			innerMap(boothTypeFpVoteCount, partyIndex),
			&boothTypeCategoryName);
		record["vote_type"] = categoryEvidenceArray(
			innerMap(voteTypeFpBiases, partyIndex),
			innerMap(voteTypeFpBiasStdDev, partyIndex),
			innerMap(voteTypeFpBiasesRaw, partyIndex),
			innerMap(voteTypeFpSourceCount, partyIndex),
			innerMap(voteTypeFpVoteCount, partyIndex),
			&voteTypeCategoryName);
		fpBiases.push_back(std::move(record));
	}

	json categoryBiases;
	categoryBiases["tpp"] = std::move(tppBiases);
	categoryBiases["fp"] = std::move(fpBiases);
	liveAnalysis["category_biases"] = std::move(categoryBiases);
	return liveAnalysis;
}

std::optional<std::filesystem::path> LiveRunExport::exportCompletedAutomaticLiveRun(
	PollingProject& project,
	Simulation const& sim,
	SimulationRun const& run,
	int iterations,
	FeedbackFunc feedback,
	ActionRequiredFunc actionRequired)
{
	if (!actionRequired) actionRequired = feedback;
	std::string const folder = sim.getSettings().liveOutputFolder;
	if (folder.empty()) return std::nullopt;

	auto const validationError = validateOutputFolder(folder);
	if (validationError) {
		reportExportFailure(feedback, actionRequired, *validationError);
		return std::nullopt;
	}

	auto const nativeFolder = LiveResultsInput::pathFromUtf8(folder);
	if (!nativeFolderIsSingleComponent(nativeFolder)) {
		reportExportFailure(feedback, actionRequired,
			"Live diagnostic output folder must be a single folder name, "
			"not a path.");
		return std::nullopt;
	}

	try {
		auto const* election =
			dynamic_cast<LiveV2::Election const*>(run.getLiveElection());
		if (!election) {
			reportExportFailure(feedback, actionRequired,
				"live analysis data was not available.");
			return std::nullopt;
		}

		auto const& report = sim.getLatestReport();
		PartyNames const parties{report};
		Timestamp const completedAt = Timestamp::now();
		std::string const snapshotCode = filenameSafeCode(report.dateCode);
		std::string const runStamp = compactRunStamp(completedAt);
		std::string const filename =
			"snapshot_" + snapshotCode + "__run_" + runStamp + ".json";
		std::string const relativeDir = "live_runs/" + folder;
		std::string const relativePath = relativeDir + "/" + filename;

		json document;
		document["format_version"] = 2;
		document["run"] = {
			{"simulation_name", sim.getSettings().name},
			{"term_code", run.getTermCode()},
			{"snapshot_code", report.dateCode.empty() ? json(nullptr) :
				json(report.dateCode)},
			{"completed_at", completedAt.formatIsoLocalOffset()},
			{"iterations", iterations},
			{"output_set", folder}
		};
		if (run.getSnapshotInput()) {
			document["run"]["input_snapshot_code"] = run.getSnapshotInput()->timestamp.value_or("");
			document["run"]["input_snapshot_file"] = LiveResultsInput::pathToUtf8(run.getSnapshotInput()->path.filename());
		}
		document["parties"] = partiesCatalog(report);
		json reportJson = serializeReport(report, parties);
		reportJson["seat_fp_completion"] = seatCompletionArray(
			*election, report.seatName, &LiveData::Provider::getSeatFpCompletion);
		reportJson["seat_tpp_completion"] = seatCompletionArray(
			*election, report.seatName, &LiveData::Provider::getSeatTppCompletion);
		reportJson["seat_tcp_completion"] = seatCompletionArray(
			*election, report.seatName, &LiveData::Provider::getSeatTcpCompletion);
		document["simulation_report"] = std::move(reportJson);
		json analysis = election->getDiagnosticSnapshot();
		// The compact summary is available without loading the large sidecar.
		// Detailed candidate vectors remain private analysis material.
		document["input_issues"] = run.inputDiagnostic();
		document["input_issues"]["completed"] = true;
		document["input_issues"]["failure"] = "";
		analysis["input_recovery"] = run.inputRecoveryDetails();
		document["live_analysis_summary"] = liveAnalysisSummaryFrom(analysis);

		std::string analysisSerialised;
		try {
			analysisSerialised = gzipAnalysis(LiveAnalysisArchive::serialize(std::move(analysis)));
		}
		catch (json::exception const& error) {
			reportExportFailure(feedback, actionRequired,
				std::string("the live analysis sidecar could not be serialized: ") +
					error.what());
			return std::nullopt;
		}
		analysis = json();

		std::string serialised;
		try {
			serialised = dumpCompactJson(document);
		}
		catch (json::exception const& error) {
			reportExportFailure(feedback, actionRequired,
				std::string("the report could not be serialized: ") +
					error.what());
			return std::nullopt;
		}

		auto const outputDir = project.paths().resolve(
			std::filesystem::path("live_runs") / nativeFolder);
		std::error_code directoryError;
		std::filesystem::create_directories(outputDir, directoryError);
		if (directoryError) {
			reportExportFailure(feedback, actionRequired,
				"could not create " + relativeDir + ".");
			return std::nullopt;
		}

		auto const finalPath = outputDir / filename;
		if (!writeFileAtomically(
			finalPath, relativePath, serialised, feedback, actionRequired)) {
			return std::nullopt;
		}

		std::string const analysisFilename =
			filename.substr(0, filename.size() - 5) + ".analysis.json.gz";
		std::string const analysisRelativePath =
			relativeDir + "/" + analysisFilename;
		if (!writeFileAtomically(
			outputDir / analysisFilename,
			analysisRelativePath,
			analysisSerialised,
			feedback,
			actionRequired)) {
			return std::nullopt;
		}

		logger << "Wrote live diagnostic export to " << relativePath
			<< " and " << analysisRelativePath << "\n";
		return finalPath;
	}
	catch (std::exception const& error) {
		reportExportFailure(feedback, actionRequired, error.what());
	}
	return std::nullopt;
}
