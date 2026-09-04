#include "LiveRunExport.h"

#include "Date.h"
#include "LiveResultsInput.h"
#include "LiveV2.h"
#include "Log.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"
#include "json.h"

#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
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

json optionalNumber(std::optional<float> const& value)
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
	result["total_2cp_percent_counted"] = number(report.total2cpPercentCounted);
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

} // namespace

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
		seatJson["tpp_all_booths_std_dev"] = number(seat.tppAllBoothsStdDev);
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
			if (boothIndex < 0 || boothIndex >= int(booths.size())) continue;
			auto const& booth = booths[boothIndex];
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
			boothJson["node"] = serializeNode(booth.node, parties);
			boothsJson.push_back(std::move(boothJson));
		}
	}

	json liveAnalysis;
	liveAnalysis["election"] = std::move(electionJson);
	liveAnalysis["regions"] = std::move(regions);
	liveAnalysis["seats"] = std::move(seatsJson);
	liveAnalysis["booths"] = std::move(boothsJson);

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

void LiveRunExport::exportCompletedAutomaticLiveRun(
	PollingProject& project,
	Simulation const& sim,
	SimulationRun const& run,
	int iterations,
	FeedbackFunc feedback,
	ActionRequiredFunc actionRequired)
{
	if (!actionRequired) actionRequired = feedback;
	std::string const folder = sim.getSettings().liveOutputFolder;
	if (folder.empty()) return;

	auto const validationError = validateOutputFolder(folder);
	if (validationError) {
		reportExportFailure(feedback, actionRequired, *validationError);
		return;
	}

	auto const nativeFolder = LiveResultsInput::pathFromUtf8(folder);
	if (!nativeFolderIsSingleComponent(nativeFolder)) {
		reportExportFailure(feedback, actionRequired,
			"Live diagnostic output folder must be a single folder name, "
			"not a path.");
		return;
	}

	try {
		auto const* election =
			dynamic_cast<LiveV2::Election const*>(run.getLiveElection());
		if (!election) {
			reportExportFailure(feedback, actionRequired,
				"live analysis data was not available.");
			return;
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
		document["parties"] = partiesCatalog(report);
		document["simulation_report"] = serializeReport(report, parties);
		auto const& baselineReport = sim.getLiveBaselineReport();
		if (baselineReport) {
			PartyNames const baselineParties{*baselineReport};
			document["live_baseline_report"] =
				serializeReport(*baselineReport, baselineParties);
		}
		else {
			document["live_baseline_report"] = nullptr;
		}
		document["live_analysis"] = election->getDiagnosticSnapshot();

		std::string serialised;
		try {
			serialised = document.dump(2);
		}
		catch (json::exception const& error) {
			reportExportFailure(feedback, actionRequired,
				std::string("the report could not be serialized: ") +
					error.what());
			return;
		}

		auto const outputDir = project.paths().resolve(
			std::filesystem::path("live_runs") / nativeFolder);
		std::error_code directoryError;
		std::filesystem::create_directories(outputDir, directoryError);
		if (directoryError) {
			reportExportFailure(feedback, actionRequired,
				"could not create " + relativeDir + ".");
			return;
		}

		auto const finalPath = outputDir / filename;
		if (std::filesystem::exists(finalPath)) {
			reportExportFailure(feedback, actionRequired,
				relativePath + " already exists.");
			return;
		}

		auto const temporaryPath = outputDir / (filename + ".tmp");
		{
			std::ofstream output(temporaryPath,
				std::ios::binary | std::ios::trunc);
			if (!output) {
				reportExportFailure(feedback, actionRequired,
					"could not open a temporary file in " + relativeDir + ".");
				return;
			}
			output << serialised;
			output.close();
			if (output.fail()) {
				std::error_code removeError;
				std::filesystem::remove(temporaryPath, removeError);
				reportExportFailure(feedback, actionRequired,
					"could not finish writing " + relativePath + ".");
				return;
			}
		}

		std::error_code renameError;
		std::filesystem::rename(temporaryPath, finalPath, renameError);
		if (renameError) {
			std::error_code removeError;
			std::filesystem::remove(temporaryPath, removeError);
			reportExportFailure(feedback, actionRequired,
				"could not replace " + relativePath + ".");
			return;
		}

		logger << "Wrote live diagnostic export to " << relativePath << "\n";
	}
	catch (std::exception const& error) {
		reportExportFailure(feedback, actionRequired, error.what());
	}
}
