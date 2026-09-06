#include "LiveSnapshotData.h"

#include "LiveResultsInput.h"
#include "LiveRunExport.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <istream>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

using json = nlohmann::json;

namespace LiveSnapshot {
namespace {

std::string toLower(std::string text)
{
	for (char& character : text) {
		character = char(std::tolower(static_cast<unsigned char>(character)));
	}
	return text;
}

bool isSnapshotFilename(std::string const& filename)
{
	auto const lower = toLower(filename);
	return lower.size() > 13 &&
		lower.starts_with("snapshot_") &&
		lower.ends_with(".json") &&
		!lower.ends_with(".tmp") &&
		!lower.ends_with(".analysis.json");
}

bool isHeavySnapshotKey(std::string_view key)
{
	return key == "live_analysis" || key == "live_baseline_report";
}

std::optional<int> jsonInt(json const& value)
{
	if (value.is_number_integer()) return value.get<int>();
	if (value.is_number_unsigned()) {
		auto const number = value.get<std::uint64_t>();
		if (number > std::uint64_t(int(INT_MAX))) return std::nullopt;
		return int(number);
	}
	return std::nullopt;
}

std::optional<std::string> jsonString(json const& value)
{
	if (!value.is_string()) return std::nullopt;
	return value.get<std::string>();
}

json parseSnapshotJson(std::istream& stream)
{
	bool skipValue = false;
	return json::parse(stream,
		[&](int depth, json::parse_event_t event, json& parsed) {
			if (event == json::parse_event_t::key &&
				depth == 1 &&
				parsed.is_string() &&
				isHeavySnapshotKey(parsed.get<std::string>())) {
				skipValue = true;
				return false;
			}
			if (skipValue &&
				(event == json::parse_event_t::object_start ||
					event == json::parse_event_t::array_start ||
					event == json::parse_event_t::value)) {
				skipValue = false;
				return false;
			}
			return true;
		});
}

void applyBoothArraySummary(SnapshotRecord& record, json const& analysis)
{
	if (analysis.contains("booths") && analysis["booths"].is_array()) {
		record.boothCount = int(analysis["booths"].size());
		if (!analysis["booths"].empty() && analysis["booths"][0].is_object()) {
			auto const& booth = analysis["booths"][0];
			if (booth.contains("seat_name") && booth["seat_name"].is_string()) {
				record.firstBoothSeat = booth["seat_name"].get<std::string>();
			}
			if (booth.contains("name") && booth["name"].is_string()) {
				record.firstBoothName = booth["name"].get<std::string>();
			}
		}
	}
	if (analysis.contains("seats") && analysis["seats"].is_array()) {
		record.analysisSeatCount = int(analysis["seats"].size());
	}
}

void applyLiveAnalysisSummary(SnapshotRecord& record, json const& document)
{
	if (document.contains("live_analysis_summary") &&
		document["live_analysis_summary"].is_object()) {
		auto const& summary = document["live_analysis_summary"];
		if (summary.contains("booth_count")) {
			if (auto count = jsonInt(summary["booth_count"])) {
				record.boothCount = *count;
			}
		}
		if (summary.contains("seat_count")) {
			if (auto count = jsonInt(summary["seat_count"])) {
				record.analysisSeatCount = *count;
			}
		}
		if (summary.contains("first_booth_seat") &&
			summary["first_booth_seat"].is_string()) {
			record.firstBoothSeat = summary["first_booth_seat"].get<std::string>();
		}
		if (summary.contains("first_booth_name") &&
			summary["first_booth_name"].is_string()) {
			record.firstBoothName = summary["first_booth_name"].get<std::string>();
		}
		return;
	}
	if (document.contains("live_analysis") &&
		document["live_analysis"].is_object()) {
		applyBoothArraySummary(record, document["live_analysis"]);
	}
}

json slimSnapshotDocument(json document)
{
	json slim = json::object();
	if (document.contains("format_version")) {
		slim["format_version"] = std::move(document["format_version"]);
	}
	if (document.contains("run")) slim["run"] = std::move(document["run"]);
	if (document.contains("parties")) {
		slim["parties"] = std::move(document["parties"]);
	}
	if (document.contains("simulation_report")) {
		slim["simulation_report"] = std::move(document["simulation_report"]);
	}
	if (document.contains("live_analysis_summary")) {
		slim["live_analysis_summary"] =
			std::move(document["live_analysis_summary"]);
	}
	return slim;
}

std::optional<PartyColour> parseColour(json const& value)
{
	if (!value.is_object()) return std::nullopt;
	auto const red = value.contains("r") ? jsonInt(value["r"]) : std::nullopt;
	auto const green = value.contains("g") ? jsonInt(value["g"]) : std::nullopt;
	auto const blue = value.contains("b") ? jsonInt(value["b"]) : std::nullopt;
	if (!red || !green || !blue) return std::nullopt;
	PartyColour colour;
	colour.r = std::clamp(*red, 0, 255);
	colour.g = std::clamp(*green, 0, 255);
	colour.b = std::clamp(*blue, 0, 255);
	return colour;
}

std::vector<PartyIdentity> parsePartyCatalog(json const& document)
{
	std::vector<PartyIdentity> parties;
	if (!document.contains("parties") || !document["parties"].is_array()) {
		return parties;
	}
	for (auto const& item : document["parties"]) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index) continue;
		PartyIdentity party;
		party.partyIndex = *index;
		if (item.contains("name") && item["name"].is_string()) {
			party.name = item["name"].get<std::string>();
		}
		if (item.contains("abbreviation") && item["abbreviation"].is_string()) {
			party.abbreviation = item["abbreviation"].get<std::string>();
		}
		if (item.contains("colour")) {
			party.colour = parseColour(item["colour"]);
		}
		parties.push_back(std::move(party));
	}
	return parties;
}

PartyIdentity const* findParty(
	std::vector<PartyIdentity> const& parties, int partyIndex)
{
	for (auto const& party : parties) {
		if (party.partyIndex == partyIndex) return &party;
	}
	return nullptr;
}

enum class NumberKind { Finite, Omitted, NonFinite };

struct ParsedNumber {
	NumberKind kind = NumberKind::Omitted;
	double value = 0.0;
	std::string tag;
};

ParsedNumber parseNumber(json const& value)
{
	ParsedNumber parsed;
	if (value.is_null()) return parsed;
	if (value.is_number()) {
		parsed.value = value.get<double>();
		if (std::isfinite(parsed.value)) {
			parsed.kind = NumberKind::Finite;
			return parsed;
		}
		parsed.kind = NumberKind::NonFinite;
		if (std::isnan(parsed.value)) parsed.tag = "nan";
		else if (parsed.value > 0.0) parsed.tag = "positive_infinity";
		else parsed.tag = "negative_infinity";
		return parsed;
	}
	if (value.is_object() && value.contains("non_finite")) {
		parsed.kind = NumberKind::NonFinite;
		parsed.tag = jsonString(value["non_finite"]).value_or("invalid");
		if (parsed.tag.empty()) parsed.tag = "invalid";
		return parsed;
	}
	parsed.kind = NumberKind::NonFinite;
	parsed.tag = "invalid";
	return parsed;
}

ParsedNumber outcomeValue(json const& report, char const* key, int partyIndex)
{
	if (!report.contains(key) || !report[key].is_array()) return {};
	for (auto const& item : report[key]) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index || *index != partyIndex) continue;
		if (!item.contains("value")) return {};
		return parseNumber(item["value"]);
	}
	return {};
}

void collectOutcomeParties(
	json const& report,
	char const* key,
	std::vector<int>& partyIndices)
{
	if (!report.contains(key) || !report[key].is_array()) return;
	for (auto const& item : report[key]) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index) continue;
		if (std::find(partyIndices.begin(), partyIndices.end(), *index) ==
			partyIndices.end()) {
			partyIndices.push_back(*index);
		}
	}
}

std::string partyLabel(PartyIdentity const* party, int partyIndex)
{
	if (partyIndex == EmergingIndIndex) return "IND*";
	if (party && party->name) {
		auto const name = toLower(*party->name);
		if (name == "emerging ind" || name == "emerging independent") {
			return "IND*";
		}
	}
	if (party && party->abbreviation && !party->abbreviation->empty()) {
		return *party->abbreviation;
	}
	if (party && party->name && !party->name->empty()) {
		return *party->name;
	}
	return "Party " + std::to_string(partyIndex);
}

std::string twoDigits(int value)
{
	std::ostringstream out;
	out << std::setw(2) << std::setfill('0') << value;
	return out.str();
}

std::string formatYmdHms(
	int year, int month, int day, int hour, int minute, int second)
{
	int const yearTwoDigits = std::abs(year) % 100;
	return twoDigits(day) + "-" + twoDigits(month) + "-" +
		twoDigits(yearTwoDigits) + " " + twoDigits(hour) + ":" +
		twoDigits(minute) + ":" + twoDigits(second);
}

struct DateTimeParts {
	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int offsetMinutes = 0;
	bool hasOffset = false;
};

bool inRange(int value, int min, int max)
{
	return value >= min && value <= max;
}

std::optional<DateTimeParts> parseIsoDateTime(std::string_view text)
{
	if (text.size() < 19) return std::nullopt;
	if (text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
		text[13] != ':' || text[16] != ':') {
		return std::nullopt;
	}
	auto slice = [&](std::size_t start, std::size_t length) {
		return std::string(text.substr(start, length));
	};
	try {
		DateTimeParts parts;
		parts.year = std::stoi(slice(0, 4));
		parts.month = std::stoi(slice(5, 2));
		parts.day = std::stoi(slice(8, 2));
		parts.hour = std::stoi(slice(11, 2));
		parts.minute = std::stoi(slice(14, 2));
		parts.second = std::stoi(slice(17, 2));
		if (!inRange(parts.month, 1, 12) || !inRange(parts.day, 1, 31) ||
			!inRange(parts.hour, 0, 23) || !inRange(parts.minute, 0, 59) ||
			!inRange(parts.second, 0, 60)) {
			return std::nullopt;
		}
		std::size_t offsetAt = 19;
		if (offsetAt < text.size() && text[offsetAt] == '.') {
			++offsetAt;
			while (offsetAt < text.size() &&
				std::isdigit(static_cast<unsigned char>(text[offsetAt]))) {
				++offsetAt;
			}
		}
		if (offsetAt < text.size()) {
			if (text[offsetAt] == 'Z' && offsetAt + 1 == text.size()) {
				parts.hasOffset = true;
				parts.offsetMinutes = 0;
			}
			else if ((text[offsetAt] == '+' || text[offsetAt] == '-') &&
				offsetAt + 6 == text.size() && text[offsetAt + 3] == ':') {
				int const hours = std::stoi(slice(offsetAt + 1, 2));
				int const minutes = std::stoi(slice(offsetAt + 4, 2));
				if (!inRange(hours, 0, 14) || !inRange(minutes, 0, 59)) {
					return std::nullopt;
				}
				parts.hasOffset = true;
				parts.offsetMinutes = hours * 60 + minutes;
				if (text[offsetAt] == '-') parts.offsetMinutes = -parts.offsetMinutes;
			}
			else {
				return std::nullopt;
			}
		}
		return parts;
	}
	catch (...) {
		return std::nullopt;
	}
}

std::optional<std::int64_t> dateTimePartsToSeconds(DateTimeParts const& parts)
{
	using namespace std::chrono;
	year_month_day const ymd{
		year{parts.year},
		month{unsigned(parts.month)},
		day{unsigned(parts.day)}};
	if (!ymd.ok()) return std::nullopt;
	auto timePoint = sys_days{ymd} + hours{parts.hour} +
		minutes{parts.minute} + seconds{parts.second};
	if (parts.hasOffset) timePoint -= minutes{parts.offsetMinutes};
	return duration_cast<seconds>(timePoint.time_since_epoch()).count();
}

std::optional<DateTimeParts> parseSnapshotCode(std::string_view snapshotCode)
{
	if (snapshotCode.size() < 14) return std::nullopt;
	for (std::size_t index = 0; index < 14; ++index) {
		if (!std::isdigit(static_cast<unsigned char>(snapshotCode[index]))) {
			return std::nullopt;
		}
	}
	try {
		DateTimeParts parts;
		parts.year = std::stoi(std::string(snapshotCode.substr(0, 4)));
		parts.month = std::stoi(std::string(snapshotCode.substr(4, 2)));
		parts.day = std::stoi(std::string(snapshotCode.substr(6, 2)));
		parts.hour = std::stoi(std::string(snapshotCode.substr(8, 2)));
		parts.minute = std::stoi(std::string(snapshotCode.substr(10, 2)));
		parts.second = std::stoi(std::string(snapshotCode.substr(12, 2)));
		if (!inRange(parts.month, 1, 12) || !inRange(parts.day, 1, 31) ||
			!inRange(parts.hour, 0, 23) || !inRange(parts.minute, 0, 59) ||
			!inRange(parts.second, 0, 60)) {
			return std::nullopt;
		}
		return parts;
	}
	catch (...) {
		return std::nullopt;
	}
}

std::optional<std::int64_t> completedAtSortKey(std::string_view text)
{
	auto const parts = parseIsoDateTime(text);
	if (!parts) return std::nullopt;
	return dateTimePartsToSeconds(*parts);
}

CellValue cellFromNumber(ParsedNumber const& number)
{
	CellValue cell;
	if (number.kind == NumberKind::NonFinite) {
		cell.kind = CellValue::Kind::Diagnostic;
		cell.text = formatNonFiniteMarker(number.tag);
		return cell;
	}
	cell.kind = CellValue::Kind::Percent;
	cell.percent = number.kind == NumberKind::Finite ? number.value : 0.0;
	cell.text = formatPercent(cell.percent);
	return cell;
}

CellValue cellFromSeatNumber(ParsedNumber const& number)
{
	CellValue cell;
	if (number.kind == NumberKind::NonFinite) {
		cell.kind = CellValue::Kind::Diagnostic;
		cell.text = formatNonFiniteMarker(number.tag);
		return cell;
	}
	cell.kind = CellValue::Kind::Value;
	cell.percent = number.kind == NumberKind::Finite ? number.value : 0.0;
	cell.text = formatSeatExpectation(cell.percent);
	return cell;
}

CellValue cellFromSeatCount(ParsedNumber const& number)
{
	CellValue cell;
	if (number.kind == NumberKind::NonFinite) {
		cell.kind = CellValue::Kind::Diagnostic;
		cell.text = formatNonFiniteMarker(number.tag);
		return cell;
	}
	cell.kind = CellValue::Kind::Value;
	cell.percent = number.kind == NumberKind::Finite ? number.value : 0.0;
	cell.text = formatSeatCount(cell.percent);
	return cell;
}

std::vector<int> jsonCountArray(json const& value)
{
	std::vector<int> counts;
	if (!value.is_array()) return counts;
	counts.reserve(value.size());
	for (auto const& item : value) {
		auto const number = jsonInt(item);
		counts.push_back(number && *number > 0 ? *number : 0);
	}
	return counts;
}

int seatCountPercentile(std::vector<int> const& frequency, float percentile)
{
	int const totalCount =
		std::accumulate(frequency.begin(), frequency.end(), 0);
	if (!totalCount || frequency.empty()) return 0;
	percentile = std::clamp(percentile, 0.0f, 100.0f);
	int const targetCount =
		int(std::floor(float(totalCount) * percentile * 0.01f));
	int currentCount = 0;
	for (int seatCount = 0; seatCount < int(frequency.size()); ++seatCount) {
		currentCount += frequency[seatCount];
		if (currentCount > targetCount) return seatCount;
	}
	return int(frequency.size()) - 1;
}

std::vector<int> partySeatFrequency(json const& report, int partyIndex)
{
	if (!report.contains("party_seat_win_frequency") ||
		!report["party_seat_win_frequency"].is_array()) {
		return {};
	}
	for (auto const& item : report["party_seat_win_frequency"]) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index || *index != partyIndex) continue;
		if (!item.contains("values")) return {};
		return jsonCountArray(item["values"]);
	}
	return {};
}

std::vector<int> coalitionSeatFrequency(json const& report)
{
	if (!report.contains("coalition_seat_win_frequency")) return {};
	return jsonCountArray(report["coalition_seat_win_frequency"]);
}

bool catalogHasNationals(std::vector<PartyIdentity> const& catalog);

struct SeatPartySet {
	std::vector<PartyIdentity> catalog;
	std::vector<int> partyIndices;
	bool hasCoalition = false;
};

SeatPartySet collectPartySet(
	std::vector<SnapshotRecord> const& records,
	char const* reportKey)
{
	SeatPartySet set;
	std::vector<int> reportOrder;
	for (auto const& record : records) {
		for (auto const& party : record.parties) {
			if (!findParty(set.catalog, party.partyIndex)) {
				set.catalog.push_back(party);
			}
		}
		if (!record.document.contains("simulation_report") ||
			!record.document["simulation_report"].is_object()) {
			continue;
		}
		collectOutcomeParties(
			record.document["simulation_report"],
			reportKey,
			reportOrder);
	}

	for (auto const& party : set.catalog) {
		if (party.partyIndex == CoalitionPartnerIndex) continue;
		if (std::find(reportOrder.begin(), reportOrder.end(),
			party.partyIndex) != reportOrder.end()) {
			set.partyIndices.push_back(party.partyIndex);
		}
	}
	for (int partyIndex : reportOrder) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (std::find(set.partyIndices.begin(), set.partyIndices.end(),
			partyIndex) == set.partyIndices.end()) {
			set.partyIndices.push_back(partyIndex);
		}
	}
	set.hasCoalition = catalogHasNationals(set.catalog);
	return set;
}

SeatPartySet collectSeatPartySet(std::vector<SnapshotRecord> const& records)
{
	return collectPartySet(records, "party_win_expectation");
}

SeatPartySet collectVoteSharePartySet(std::vector<SnapshotRecord> const& records)
{
	return collectPartySet(records, "party_primary_frequency");
}

void collectNestedSeatParties(
	json const& report,
	char const* key,
	std::vector<int>& partyIndices)
{
	if (!report.contains(key) || !report[key].is_array()) return;
	for (auto const& seat : report[key]) {
		if (!seat.is_array()) continue;
		for (auto const& item : seat) {
			if (!item.is_object() || !item.contains("party_index")) continue;
			auto const index = jsonInt(item["party_index"]);
			if (!index) continue;
			if (std::find(partyIndices.begin(), partyIndices.end(), *index) ==
				partyIndices.end()) {
				partyIndices.push_back(*index);
			}
		}
	}
}

SeatPartySet collectNestedSeatPartySet(
	std::vector<SnapshotRecord> const& records,
	char const* key)
{
	SeatPartySet set;
	std::vector<int> reportOrder;
	for (auto const& record : records) {
		for (auto const& party : record.parties) {
			if (!findParty(set.catalog, party.partyIndex)) {
				set.catalog.push_back(party);
			}
		}
		if (!record.document.contains("simulation_report") ||
			!record.document["simulation_report"].is_object()) {
			continue;
		}
		collectNestedSeatParties(
			record.document["simulation_report"], key, reportOrder);
	}

	for (auto const& party : set.catalog) {
		if (party.partyIndex == CoalitionPartnerIndex) continue;
		if (std::find(reportOrder.begin(), reportOrder.end(),
			party.partyIndex) != reportOrder.end()) {
			set.partyIndices.push_back(party.partyIndex);
		}
	}
	for (int partyIndex : reportOrder) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (std::find(set.partyIndices.begin(), set.partyIndices.end(),
			partyIndex) == set.partyIndices.end()) {
			set.partyIndices.push_back(partyIndex);
		}
	}
	return set;
}

std::vector<std::string> seatNamesFromReport(json const& report)
{
	std::vector<std::string> names;
	if (!report.contains("seat_name") || !report["seat_name"].is_array()) {
		return names;
	}
	for (auto const& item : report["seat_name"]) {
		if (item.is_string()) names.push_back(item.get<std::string>());
	}
	return names;
}

void appendUniqueName(std::vector<std::string>& names, std::string const& name)
{
	if (std::find(names.begin(), names.end(), name) == names.end()) {
		names.push_back(name);
	}
}

ParsedNumber nestedSeatPartyValue(
	json const& report,
	char const* key,
	std::string_view seatName,
	int partyIndex)
{
	auto const names = seatNamesFromReport(report);
	auto const found = std::find(names.begin(), names.end(), seatName);
	if (found == names.end()) return {};
	auto const seatIndex = std::size_t(found - names.begin());
	if (!report.contains(key) || !report[key].is_array()) return {};
	auto const& seats = report[key];
	if (seatIndex >= seats.size()) return {};
	auto const& seat = seats[seatIndex];
	if (!seat.is_array()) return {};
	for (auto const& item : seat) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index || *index != partyIndex) continue;
		if (!item.contains("value")) return {};
		return parseNumber(item["value"]);
	}
	return {};
}

json const* simulationReport(SnapshotRecord const& record)
{
	if (record.document.contains("simulation_report") &&
		record.document["simulation_report"].is_object()) {
		return &record.document["simulation_report"];
	}
	return nullptr;
}

json const* liveAnalysisSummary(SnapshotRecord const& record)
{
	if (record.document.contains("live_analysis_summary") &&
		record.document["live_analysis_summary"].is_object()) {
		return &record.document["live_analysis_summary"];
	}
	return nullptr;
}

PartyColour mixRgbTowardWhite(PartyColour colour, double whiteAmount)
{
	whiteAmount = std::clamp(whiteAmount, 0.0, 1.0);
	auto const mix = [whiteAmount](int channel) {
		return int(std::lround(channel * (1.0 - whiteAmount) + 255.0 * whiteAmount));
	};
	colour.r = std::clamp(mix(colour.r), 0, 255);
	colour.g = std::clamp(mix(colour.g), 0, 255);
	colour.b = std::clamp(mix(colour.b), 0, 255);
	return colour;
}

PartyColour mixRgbTowardBlack(PartyColour colour, double blackAmount)
{
	blackAmount = std::clamp(blackAmount, 0.0, 1.0);
	auto const mix = [blackAmount](int channel) {
		return int(std::lround(channel * (1.0 - blackAmount)));
	};
	colour.r = std::clamp(mix(colour.r), 0, 255);
	colour.g = std::clamp(mix(colour.g), 0, 255);
	colour.b = std::clamp(mix(colour.b), 0, 255);
	return colour;
}

double metricChangeSaturation(double absDeltaPercent, double fullSatPercent)
{
	if (!(fullSatPercent > 0.0) || !std::isfinite(absDeltaPercent)) return 0.0;
	double const t = std::clamp(absDeltaPercent / fullSatPercent, 0.0, 1.0);
	if (t <= 0.0) return 0.0;
	double const exponent = std::log(0.5) / std::log(0.1);
	return std::pow(t, exponent);
}

struct ThresholdSpec {
	char const* header = "";
	ParliamentColumn::Outcome outcome = ParliamentColumn::Outcome::Majority;
	bool median = false;
	float percentile = 50.0f;
};

ThresholdSpec const SeatThresholds[] = {
	{"0.1%", ParliamentColumn::Outcome::MostSeats, false, 0.1f},
	{"5%", ParliamentColumn::Outcome::Minority, false, 5.0f},
	{"Median", ParliamentColumn::Outcome::Majority, true, 50.0f},
	{"95%", ParliamentColumn::Outcome::Minority, false, 95.0f},
	{"99.9%", ParliamentColumn::Outcome::MostSeats, false, 99.9f}
};

ParsedNumber thresholdCellNumber(
	json const* report,
	int partyIndex,
	ThresholdSpec const& spec)
{
	if (!report) return {};
	bool const coalition = partyIndex == CoalitionPartnerIndex;
	if (spec.median) {
		ParsedNumber number;
		if (coalition) {
			if (report->contains("coalition_win_median")) {
				number = parseNumber((*report)["coalition_win_median"]);
			}
		}
		else {
			number = outcomeValue(*report, "party_win_median", partyIndex);
		}
		if (number.kind != NumberKind::Omitted) return number;
	}

	auto const frequency = coalition ?
		coalitionSeatFrequency(*report) : partySeatFrequency(*report, partyIndex);
	ParsedNumber number;
	number.kind = NumberKind::Finite;
	number.value = double(seatCountPercentile(frequency, spec.percentile));
	return number;
}

std::map<int, int> parseBinCounts(json const& value)
{
	std::map<int, int> bins;
	if (!value.is_array()) return bins;
	for (auto const& item : value) {
		if (!item.is_object() || !item.contains("bin") || !item.contains("count")) {
			continue;
		}
		auto const bin = jsonInt(item["bin"]);
		auto const count = jsonInt(item["count"]);
		if (!bin || !count || *count <= 0) continue;
		bins[*bin] += *count;
	}
	return bins;
}

int tppSampleCount(std::map<int, int> const& frequency)
{
	int total = 0;
	for (auto const& [bin, count] : frequency) total += count;
	return total;
}

float tppSampleExpectation(std::map<int, int> const& frequency)
{
	int const totalCount = tppSampleCount(frequency);
	if (!totalCount) return 0.0f;
	float total = 0.0f;
	for (auto const& [bin, count] : frequency) {
		total += float(count) * (float(bin) * 0.1f + 0.05f);
	}
	return total / float(totalCount);
}

float tppSamplePercentile(std::map<int, int> const& frequency, float percentile)
{
	int const totalCount = tppSampleCount(frequency);
	if (!totalCount) return 0.0f;
	percentile = std::clamp(percentile, 0.0f, 100.0f);
	int const targetCount =
		int(std::floor(float(totalCount) * percentile * 0.01f));
	int currentCount = 0;
	for (auto const& [bin, count] : frequency) {
		int const previousCount = currentCount;
		currentCount += count;
		if (currentCount > targetCount) {
			float const fractionThroughBucket =
				float(targetCount - previousCount) /
				float(currentCount - previousCount);
			return float(bin) * 0.1f + fractionThroughBucket * 0.1f;
		}
	}
	return std::min(100.0f, float(frequency.rbegin()->first) * 0.1f + 0.1f);
}

std::map<int, int> tppFrequency(json const* report)
{
	if (!report || !report->contains("tpp_frequency")) return {};
	return parseBinCounts((*report)["tpp_frequency"]);
}

std::map<int, int> partyPrimaryBins(json const& report, int partyIndex)
{
	if (!report.contains("party_primary_frequency") ||
		!report["party_primary_frequency"].is_array()) {
		return {};
	}
	for (auto const& item : report["party_primary_frequency"]) {
		if (!item.is_object() || !item.contains("party_index")) continue;
		auto const index = jsonInt(item["party_index"]);
		if (!index || *index != partyIndex) continue;
		if (!item.contains("bins")) return {};
		return parseBinCounts(item["bins"]);
	}
	return {};
}

std::map<int, int> coalitionFpBins(json const& report)
{
	if (!report.contains("coalition_fp_frequency")) return {};
	return parseBinCounts(report["coalition_fp_frequency"]);
}

ParsedNumber voteShareNumber(std::map<int, int> const& frequency)
{
	ParsedNumber number;
	number.kind = NumberKind::Finite;
	number.value = double(tppSampleExpectation(frequency));
	return number;
}

bool isNationalsParty(PartyIdentity const& party)
{
	auto const abbr = toLower(party.abbreviation.value_or(""));
	if (abbr == "nat" || abbr == "npa") return true;
	auto const name = toLower(party.name.value_or(""));
	if (name.find("one nation") != std::string::npos) return false;
	return name == "nationals" ||
		name == "the nationals" ||
		name == "national party" ||
		name.find("nationals") != std::string::npos;
}

bool isEmergingParty(PartyIdentity const& party, int partyIndex)
{
	if (partyIndex == EmergingPartyIndex) return true;
	auto const abbr = toLower(party.abbreviation.value_or(""));
	if (abbr == toLower(EmergingOthersCode)) return true;
	auto const name = toLower(party.name.value_or(""));
	return name == "emerging party";
}

bool isIndependentParty(PartyIdentity const& party, int partyIndex)
{
	if (partyIndex == EmergingIndIndex) return true;
	auto const abbr = toLower(party.abbreviation.value_or(""));
	if (abbr == "ind") return true;
	auto const name = toLower(party.name.value_or(""));
	return name.find("independent") != std::string::npos;
}

PartyColour arbitrarySeatColour(int partyIndex)
{
	static PartyColour const palette[] = {
		{0, 128, 128},
		{255, 140, 0},
		{128, 0, 128},
		{139, 69, 19},
		{70, 130, 180},
		{46, 139, 87},
		{199, 21, 133},
		{85, 107, 47}
	};
	unsigned const key = unsigned(partyIndex + 100000);
	return palette[key % (sizeof(palette) / sizeof(palette[0]))];
}

PartyColour resolveSeatPartyColour(
	PartyIdentity const* party,
	int partyIndex,
	int& independentShade)
{
	if (party && party->colour) return *party->colour;
	PartyIdentity identity;
	identity.partyIndex = partyIndex;
	if (party) identity = *party;
	if (isEmergingParty(identity, partyIndex)) return {0, 0, 0};
	if (isIndependentParty(identity, partyIndex)) {
		int const level = std::clamp(168 - independentShade * 32, 40, 200);
		++independentShade;
		return {level, level, level};
	}
	return arbitrarySeatColour(partyIndex);
}

bool catalogHasNationals(std::vector<PartyIdentity> const& catalog)
{
	for (auto const& party : catalog) {
		if (isNationalsParty(party)) return true;
	}
	return false;
}

PartyColour coalitionSeatColour(std::vector<PartyIdentity> const& catalog)
{
	PartyIdentity const* liberals = findParty(catalog, 1);
	PartyIdentity const* nationals = nullptr;
	for (auto const& party : catalog) {
		if (isNationalsParty(party)) {
			nationals = &party;
			break;
		}
	}
	if (liberals && liberals->colour && nationals && nationals->colour) {
		return {
			(liberals->colour->r + nationals->colour->r) / 2,
			(liberals->colour->g + nationals->colour->g) / 2,
			(liberals->colour->b + nationals->colour->b) / 2
		};
	}
	if (liberals && liberals->colour) return *liberals->colour;
	if (nationals && nationals->colour) return *nationals->colour;
	return {0, 51, 153};
}

ParliamentRow makeTableRow(
	SnapshotRecord const& record, ParliamentSelection const& selection)
{
	ParliamentRow row;
	row.snapshotCode = record.snapshotCode;
	row.timestampLabel = formatSnapshotTimestamp(record.snapshotCode);
	row.duplicate = selection.duplicateCount > 1;
	if (row.duplicate) row.timestampLabel += "*";
	row.selectedFilename = record.filename;
	row.completedAt = record.completedAt;
	row.duplicateCount = selection.duplicateCount;
	row.recordIndex = selection.recordIndex;
	return row;
}

LoadResult makeError(
	LoadStatus status,
	std::string simulationName,
	std::string outputSet,
	std::string message)
{
	LoadResult result;
	result.status = status;
	result.simulationName = std::move(simulationName);
	result.outputSet = std::move(outputSet);
	result.errorMessage = std::move(message);
	return result;
}

std::optional<SnapshotRecord> parseRecord(
	std::string filename,
	json document,
	std::string& warning)
{
	if (!document.is_object()) {
		warning = "root value is not a JSON object.";
		return std::nullopt;
	}
	if (!document.contains("format_version") ||
		!document["format_version"].is_number_integer() ||
		document["format_version"].get<int>() != 2) {
		warning = "unsupported format_version.";
		return std::nullopt;
	}
	if (!document.contains("run") || !document["run"].is_object()) {
		warning = "missing run object.";
		return std::nullopt;
	}
	if (!document.contains("simulation_report") ||
		!document["simulation_report"].is_object()) {
		warning = "missing simulation_report object.";
		return std::nullopt;
	}
	auto const& run = document["run"];
	auto const snapshotCode = run.contains("snapshot_code") ?
		jsonString(run["snapshot_code"]) : std::nullopt;
	auto const completedAt = run.contains("completed_at") ?
		jsonString(run["completed_at"]) : std::nullopt;
	if (!snapshotCode || snapshotCode->empty()) {
		warning = "missing snapshot_code.";
		return std::nullopt;
	}
	if (!completedAt || completedAt->empty()) {
		warning = "missing completed_at.";
		return std::nullopt;
	}

	SnapshotRecord record;
	record.filename = std::move(filename);
	record.snapshotCode = *snapshotCode;
	record.completedAt = *completedAt;
	record.simulationName =
		run.contains("simulation_name") ?
			jsonString(run["simulation_name"]).value_or("") : "";
	record.termCode =
		run.contains("term_code") ? jsonString(run["term_code"]).value_or("") : "";
	record.outputSet =
		run.contains("output_set") ? jsonString(run["output_set"]).value_or("") : "";
	record.parties = parsePartyCatalog(document);
	applyLiveAnalysisSummary(record, document);
	record.document = slimSnapshotDocument(std::move(document));
	return record;
}

}

std::string formatPercent(double value)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << value << '%';
	return out.str();
}

std::string formatSeatExpectation(double value)
{
	std::ostringstream out;
	out << std::fixed << std::setprecision(2) << value;
	return out.str();
}

std::string formatSeatCount(double value)
{
	return std::to_string(int(std::lround(value)));
}

std::string formatNonFiniteMarker(std::string_view tag)
{
	if (tag == "nan") return "nan";
	if (tag == "positive_infinity") return "+inf";
	if (tag == "negative_infinity") return "-inf";
	if (tag.empty()) return "invalid";
	return std::string(tag);
}

std::string formatSnapshotTimestamp(std::string_view snapshotCode)
{
	auto const parts = parseSnapshotCode(snapshotCode);
	if (!parts) return std::string(snapshotCode);
	return formatYmdHms(
		parts->year, parts->month, parts->day,
		parts->hour, parts->minute, parts->second);
}

std::optional<std::int64_t> snapshotCodeTimeSeconds(std::string_view snapshotCode)
{
	auto const parts = parseSnapshotCode(snapshotCode);
	if (!parts) return std::nullopt;
	return dateTimePartsToSeconds(*parts);
}

std::string formatCompletedAt(std::string_view completedAt)
{
	auto const parts = parseIsoDateTime(completedAt);
	if (!parts) return std::string(completedAt);
	return formatYmdHms(
		parts->year, parts->month, parts->day,
		parts->hour, parts->minute, parts->second);
}

std::string formatRunSelectorLabel(SnapshotRecord const& record)
{
	return formatSnapshotTimestamp(record.snapshotCode) + "  ·  " +
		formatCompletedAt(record.completedAt);
}

std::string formatStatusLine(LoadResult const& result)
{
	if (result.status != LoadStatus::Ok) return result.errorMessage;
	std::string line = result.simulationName;
	if (!result.outputSet.empty()) {
		if (!line.empty()) line += "  ·  ";
		line += result.outputSet;
	}
	line += "  ·  " + std::to_string(result.records.size()) + " run";
	if (result.records.size() != 1) line += "s";
	if (!result.warnings.empty()) {
		line += "  ·  " + std::to_string(result.warnings.size()) + " skipped";
	}
	return line;
}

int compareCompletedAt(std::string_view left, std::string_view right)
{
	auto const leftKey = completedAtSortKey(left);
	auto const rightKey = completedAtSortKey(right);
	if (leftKey && rightKey) {
		if (*leftKey < *rightKey) return -1;
		if (*leftKey > *rightKey) return 1;
		return 0;
	}
	if (leftKey) return 1;
	if (rightKey) return -1;
	if (left < right) return -1;
	if (left > right) return 1;
	return 0;
}

bool isMajorParliamentParty(int partyIndex)
{
	// Simulation::MajorParty::One/Two. Minority government is only recorded
	// for these two parties.
	return partyIndex == 0 || partyIndex == 1;
}

PartyColour fadeParliamentHeaderColour(
	PartyColour colour,
	ParliamentColumn::Outcome outcome)
{
	int const colourWeight = 1;
	int whiteWeight = 2;
	if (outcome == ParliamentColumn::Outcome::Minority) {
		whiteWeight = 5;
	}
	else if (outcome == ParliamentColumn::Outcome::MostSeats) {
		whiteWeight = 11;
	}
	int const total = colourWeight + whiteWeight;
	colour.r = (colour.r * colourWeight + 255 * whiteWeight) / total;
	colour.g = (colour.g * colourWeight + 255 * whiteWeight) / total;
	colour.b = (colour.b * colourWeight + 255 * whiteWeight) / total;
	return colour;
}

LoadResult loadDirectory(
	std::filesystem::path const& directory,
	std::string simulationName,
	std::string outputSet)
{
	LoadResult result;
	result.simulationName = std::move(simulationName);
	result.outputSet = std::move(outputSet);

	std::error_code error;
	if (!std::filesystem::exists(directory, error) || error) {
		result.status = LoadStatus::MissingFolder;
		result.errorMessage = result.simulationName + " (" + result.outputSet +
			"): output folder was not found.";
		return result;
	}
	if (!std::filesystem::is_directory(directory, error) || error) {
		result.status = LoadStatus::UnreadableFolder;
		result.errorMessage = result.simulationName + " (" + result.outputSet +
			"): output folder could not be read.";
		return result;
	}

	std::error_code iteratorError;
	auto iterator = std::filesystem::directory_iterator(directory, iteratorError);
	if (iteratorError) {
		result.status = LoadStatus::UnreadableFolder;
		result.errorMessage = result.simulationName + " (" + result.outputSet +
			"): output folder could not be read.";
		return result;
	}

	result.status = LoadStatus::Ok;
	for (auto const& entry : iterator) {
		std::error_code entryError;
		if (!entry.is_regular_file(entryError) || entryError) continue;
		std::string const filename = LiveResultsInput::pathToUtf8(entry.path().filename());
		if (!isSnapshotFilename(filename)) continue;

		std::ifstream stream(entry.path(), std::ios::binary);
		if (!stream) {
			result.warnings.push_back({filename, "could not open file."});
			continue;
		}
		json document;
		try {
			document = parseSnapshotJson(stream);
		}
		catch (json::exception const& parseError) {
			result.warnings.push_back({
				filename,
				std::string("malformed JSON: ") + parseError.what()
			});
			continue;
		}

		std::string warning;
		auto record = parseRecord(filename, std::move(document), warning);
		if (!record) {
			result.warnings.push_back({filename, warning});
			continue;
		}
		result.records.push_back(std::move(*record));
	}

	std::sort(
		result.records.begin(),
		result.records.end(),
		[](SnapshotRecord const& left, SnapshotRecord const& right) {
			if (left.snapshotCode != right.snapshotCode) {
				return left.snapshotCode < right.snapshotCode;
			}
			int const completed = compareCompletedAt(
				left.completedAt, right.completedAt);
			if (completed != 0) return completed < 0;
			return left.filename < right.filename;
		});
	return result;
}

LoadResult loadFromProject(PollingProject const& project)
{
	Simulation const* selected = nullptr;
	for (auto const& [id, simulation] : project.simulations()) {
		if (simulation.isLive() &&
			!simulation.getSettings().liveOutputFolder.empty()) {
			selected = &simulation;
			break;
		}
	}
	if (!selected) {
		return makeError(
			LoadStatus::NoSimulation,
			"",
			"",
			"No live simulation has a diagnostic output folder configured.");
	}

	auto const& settings = selected->getSettings();
	auto const validationError =
		LiveRunExport::validateOutputFolder(settings.liveOutputFolder);
	if (validationError) {
		return makeError(
			LoadStatus::InvalidFolder,
			settings.name,
			settings.liveOutputFolder,
			settings.name + " (" + settings.liveOutputFolder + "): " +
				*validationError);
	}

	auto const nativeFolder =
		LiveResultsInput::pathFromUtf8(settings.liveOutputFolder);
	auto const directory = project.paths().resolve(
		std::filesystem::path("live_runs") / nativeFolder);
	auto result = loadDirectory(directory, settings.name, settings.liveOutputFolder);
	if (result.status == LoadStatus::Ok) {
		std::map<int, PartyColour> colours;
		for (int index = 0; index < project.parties().count(); ++index) {
			auto const& colour = project.parties().viewByIndex(index).colour;
			colours[index] = {colour.r, colour.g, colour.b};
		}
		overlayPartyColours(result.records, colours);
	}
	return result;
}

void overlayPartyColours(
	std::vector<SnapshotRecord>& records,
	std::map<int, PartyColour> const& colours)
{
	if (colours.empty()) return;
	for (auto& record : records) {
		for (auto& party : record.parties) {
			auto const found = colours.find(party.partyIndex);
			if (found == colours.end()) continue;
			party.colour = found->second;
		}
	}
}

std::vector<ParliamentSelection> selectParliamentRows(
	std::vector<SnapshotRecord> const& records)
{
	std::vector<ParliamentSelection> rows;
	for (std::size_t index = 0; index < records.size(); ++index) {
		if (!rows.empty() &&
			records[rows.back().recordIndex].snapshotCode ==
				records[index].snapshotCode) {
			++rows.back().duplicateCount;
			auto const& current = records[rows.back().recordIndex];
			auto const& candidate = records[index];
			int const completed = compareCompletedAt(
				current.completedAt, candidate.completedAt);
			if (completed < 0 ||
				(completed == 0 && current.filename < candidate.filename)) {
				rows.back().recordIndex = index;
			}
			continue;
		}
		ParliamentSelection row;
		row.recordIndex = index;
		row.duplicateCount = 1;
		rows.push_back(row);
	}
	return rows;
}

ParliamentView buildParliamentView(std::vector<SnapshotRecord> const& records)
{
	ParliamentView view;
	std::vector<int> outcomeOrder;
	std::vector<PartyIdentity> catalog;
	for (auto const& record : records) {
		for (auto const& party : record.parties) {
			if (!findParty(catalog, party.partyIndex)) {
				catalog.push_back(party);
			}
		}
		if (!record.document.contains("simulation_report") ||
			!record.document["simulation_report"].is_object()) {
			continue;
		}
		auto const& report = record.document["simulation_report"];
		collectOutcomeParties(report, "majority_percent", outcomeOrder);
		collectOutcomeParties(report, "minority_percent", outcomeOrder);
		collectOutcomeParties(report, "most_seats_percent", outcomeOrder);
	}

	std::vector<int> partyIndices;
	for (auto const& party : catalog) {
		if (std::find(outcomeOrder.begin(), outcomeOrder.end(), party.partyIndex) !=
			outcomeOrder.end()) {
			partyIndices.push_back(party.partyIndex);
		}
	}
	for (int partyIndex : outcomeOrder) {
		if (std::find(partyIndices.begin(), partyIndices.end(), partyIndex) ==
			partyIndices.end()) {
			partyIndices.push_back(partyIndex);
		}
	}

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	view.columns.push_back(std::move(snapshot));

	auto const outcomeHeader = [](PartyIdentity const* party, int partyIndex,
		char const* suffix) {
		return partyLabel(party, partyIndex) + " " + suffix;
	};
	for (int partyIndex : partyIndices) {
		auto const* party = findParty(catalog, partyIndex);
		ParliamentColumn majority;
		majority.kind = ParliamentColumn::Kind::PartyOutcome;
		majority.outcome = ParliamentColumn::Outcome::Majority;
		majority.partyIndex = partyIndex;
		majority.header = outcomeHeader(party, partyIndex, "Majority");
		if (party) majority.colour = party->colour;
		view.columns.push_back(majority);

		if (isMajorParliamentParty(partyIndex)) {
			ParliamentColumn minority = majority;
			minority.outcome = ParliamentColumn::Outcome::Minority;
			minority.header = outcomeHeader(party, partyIndex, "Minority");
			view.columns.push_back(minority);
		}

		ParliamentColumn mostSeats = majority;
		mostSeats.outcome = ParliamentColumn::Outcome::MostSeats;
		mostSeats.header = outcomeHeader(party, partyIndex, "Most Seats");
		view.columns.push_back(mostSeats);
	}

	ParliamentColumn exactTie;
	exactTie.kind = ParliamentColumn::Kind::ExactTie;
	exactTie.header = "Exact Tie";
	view.columns.push_back(std::move(exactTie));

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row;
		row.snapshotCode = record.snapshotCode;
		row.timestampLabel = formatSnapshotTimestamp(record.snapshotCode);
		row.duplicate = selection.duplicateCount > 1;
		if (row.duplicate) row.timestampLabel += "*";
		row.selectedFilename = record.filename;
		row.completedAt = record.completedAt;
		row.duplicateCount = selection.duplicateCount;
		row.recordIndex = selection.recordIndex;

		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = nullptr;
		if (record.document.contains("simulation_report") &&
			record.document["simulation_report"].is_object()) {
			report = &record.document["simulation_report"];
		}
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::ExactTie) {
				ParsedNumber number;
				if (report && report->contains("tied_percent")) {
					number = parseNumber((*report)["tied_percent"]);
				}
				cells.push_back(cellFromNumber(number));
				continue;
			}
			ParsedNumber number;
			if (report) {
				char const* key = "majority_percent";
				if (column.outcome == ParliamentColumn::Outcome::Minority) {
					key = "minority_percent";
				}
				else if (column.outcome == ParliamentColumn::Outcome::MostSeats) {
					key = "most_seats_percent";
				}
				number = outcomeValue(*report, key, column.partyIndex);
			}
			cells.push_back(cellFromNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildSeatExpectationView(std::vector<SnapshotRecord> const& records)
{
	ParliamentView view;
	auto const parties = collectSeatPartySet(records);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	int independentShade = 0;
	for (int partyIndex : parties.partyIndices) {
		auto const* party = findParty(parties.catalog, partyIndex);
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::PartySeats;
		column.partyIndex = partyIndex;
		column.header = partyLabel(party, partyIndex);
		column.colour = resolveSeatPartyColour(party, partyIndex, independentShade);
		view.columns.push_back(std::move(column));
	}

	if (parties.hasCoalition) {
		ParliamentColumn separator;
		separator.kind = ParliamentColumn::Kind::Separator;
		separator.includeInGraph = false;
		view.columns.push_back(std::move(separator));

		ParliamentColumn coalition;
		coalition.kind = ParliamentColumn::Kind::CoalitionSeats;
		coalition.header = "Coalition Win Expectation";
		coalition.colour = coalitionSeatColour(parties.catalog);
		coalition.includeInGraph = false;
		view.columns.push_back(std::move(coalition));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = nullptr;
		if (record.document.contains("simulation_report") &&
			record.document["simulation_report"].is_object()) {
			report = &record.document["simulation_report"];
		}
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::Separator) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::CoalitionSeats) {
				ParsedNumber number;
				if (report && report->contains("coalition_win_expectation")) {
					number = parseNumber((*report)["coalition_win_expectation"]);
				}
				cells.push_back(cellFromSeatNumber(number));
				continue;
			}
			ParsedNumber number;
			if (report) {
				number = outcomeValue(*report, "party_win_expectation", column.partyIndex);
			}
			cells.push_back(cellFromSeatNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

std::vector<ThresholdPartyOption> listSeatThresholdParties(
	std::vector<SnapshotRecord> const& records)
{
	auto const parties = collectSeatPartySet(records);
	std::vector<ThresholdPartyOption> options;
	int independentShade = 0;
	for (int partyIndex : parties.partyIndices) {
		auto const* party = findParty(parties.catalog, partyIndex);
		ThresholdPartyOption option;
		option.partyIndex = partyIndex;
		option.label = partyLabel(party, partyIndex);
		option.colour = resolveSeatPartyColour(party, partyIndex, independentShade);
		options.push_back(std::move(option));
	}
	if (parties.hasCoalition) {
		ThresholdPartyOption coalition;
		coalition.partyIndex = CoalitionPartnerIndex;
		coalition.label = "Coalition";
		coalition.colour = coalitionSeatColour(parties.catalog);
		options.push_back(std::move(coalition));
	}
	return options;
}

ParliamentView buildSeatThresholdView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex)
{
	ParliamentView view;
	auto const options = listSeatThresholdParties(records);
	ThresholdPartyOption const* selected = nullptr;
	for (auto const& option : options) {
		if (option.partyIndex == partyIndex) {
			selected = &option;
			break;
		}
	}

	std::optional<PartyColour> colour;
	if (selected) {
		colour = selected->colour;
	}
	else {
		auto const parties = collectSeatPartySet(records);
		if (partyIndex == CoalitionPartnerIndex) {
			colour = coalitionSeatColour(parties.catalog);
		}
		else {
			auto const* party = findParty(parties.catalog, partyIndex);
			int independentShade = 0;
			colour = resolveSeatPartyColour(party, partyIndex, independentShade);
		}
	}

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (auto const& spec : SeatThresholds) {
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::SeatThreshold;
		column.outcome = spec.outcome;
		column.partyIndex = partyIndex;
		column.header = spec.header;
		column.colour = colour;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = nullptr;
		if (record.document.contains("simulation_report") &&
			record.document["simulation_report"].is_object()) {
			report = &record.document["simulation_report"];
		}
		int specIndex = 0;
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			cells.push_back(cellFromSeatCount(
				thresholdCellNumber(report, partyIndex, SeatThresholds[specIndex])));
			++specIndex;
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildTppView(std::vector<SnapshotRecord> const& records)
{
	ParliamentView view;
	auto const parties = collectSeatPartySet(records);
	auto const* party = findParty(parties.catalog, 0);
	int independentShade = 0;
	auto const colour = resolveSeatPartyColour(party, 0, independentShade);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (auto const& spec : SeatThresholds) {
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::TppThreshold;
		column.outcome = spec.outcome;
		column.header = spec.header;
		column.colour = colour;
		view.columns.push_back(std::move(column));
	}

	ParliamentColumn separator;
	separator.kind = ParliamentColumn::Kind::Separator;
	separator.includeInGraph = false;
	view.columns.push_back(std::move(separator));

	ParliamentColumn mean;
	mean.kind = ParliamentColumn::Kind::TppMean;
	mean.outcome = ParliamentColumn::Outcome::Majority;
	mean.header = "Mean";
	mean.colour = colour;
	mean.includeInGraph = false;
	view.columns.push_back(std::move(mean));

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = nullptr;
		if (record.document.contains("simulation_report") &&
			record.document["simulation_report"].is_object()) {
			report = &record.document["simulation_report"];
		}
		auto const frequency = tppFrequency(report);
		int specIndex = 0;
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::Separator) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			number.kind = NumberKind::Finite;
			if (column.kind == ParliamentColumn::Kind::TppMean) {
				number.value = double(tppSampleExpectation(frequency));
			}
			else {
				number.value = double(tppSamplePercentile(
					frequency, SeatThresholds[specIndex].percentile));
				++specIndex;
			}
			cells.push_back(cellFromNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildVoteShareView(std::vector<SnapshotRecord> const& records)
{
	ParliamentView view;
	auto const parties = collectVoteSharePartySet(records);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	int independentShade = 0;
	for (int partyIndex : parties.partyIndices) {
		auto const* party = findParty(parties.catalog, partyIndex);
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::PartyVoteShare;
		column.partyIndex = partyIndex;
		column.header = partyLabel(party, partyIndex);
		column.colour = resolveSeatPartyColour(party, partyIndex, independentShade);
		view.columns.push_back(std::move(column));
	}

	if (parties.hasCoalition) {
		ParliamentColumn separator;
		separator.kind = ParliamentColumn::Kind::Separator;
		separator.includeInGraph = false;
		view.columns.push_back(std::move(separator));

		ParliamentColumn coalition;
		coalition.kind = ParliamentColumn::Kind::CoalitionVoteShare;
		coalition.header = "Coalition";
		coalition.colour = coalitionSeatColour(parties.catalog);
		coalition.includeInGraph = false;
		view.columns.push_back(std::move(coalition));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = nullptr;
		if (record.document.contains("simulation_report") &&
			record.document["simulation_report"].is_object()) {
			report = &record.document["simulation_report"];
		}
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::Separator) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (report) {
				if (column.kind == ParliamentColumn::Kind::CoalitionVoteShare) {
					number = voteShareNumber(coalitionFpBins(*report));
				}
				else {
					number = voteShareNumber(
						partyPrimaryBins(*report, column.partyIndex));
				}
			}
			cells.push_back(cellFromNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

std::vector<ThresholdPartyOption> listNestedSeatParties(
	std::vector<SnapshotRecord> const& records,
	char const* key)
{
	auto const parties = collectNestedSeatPartySet(records, key);
	std::vector<ThresholdPartyOption> options;
	int independentShade = 0;
	for (int partyIndex : parties.partyIndices) {
		auto const* party = findParty(parties.catalog, partyIndex);
		ThresholdPartyOption option;
		option.partyIndex = partyIndex;
		option.label = partyLabel(party, partyIndex);
		option.colour = resolveSeatPartyColour(party, partyIndex, independentShade);
		options.push_back(std::move(option));
	}
	return options;
}

std::vector<ThresholdPartyOption> listSeatWinChanceParties(
	std::vector<SnapshotRecord> const& records)
{
	return listNestedSeatParties(records, "seat_party_win_percent");
}

std::vector<ThresholdPartyOption> listSeatFpParties(
	std::vector<SnapshotRecord> const& records)
{
	return listNestedSeatParties(records, "seat_party_mean_fp_share");
}

std::vector<std::string> listSeatWinChanceSeats(
	std::vector<SnapshotRecord> const& records)
{
	std::vector<std::string> names;
	for (auto const& selection : selectParliamentRows(records)) {
		auto const* report = simulationReport(records[selection.recordIndex]);
		if (!report) continue;
		for (auto const& name : seatNamesFromReport(*report)) {
			appendUniqueName(names, name);
		}
	}
	return names;
}

ThresholdPartyOption const* findPartyOption(
	std::vector<ThresholdPartyOption> const& options, int partyIndex)
{
	for (auto const& option : options) {
		if (option.partyIndex == partyIndex) return &option;
	}
	return nullptr;
}

std::optional<PartyColour> nestedSeatPartyColour(
	std::vector<SnapshotRecord> const& records,
	int partyIndex,
	char const* key)
{
	auto const options = listNestedSeatParties(records, key);
	if (auto const* selected = findPartyOption(options, partyIndex)) {
		return selected->colour;
	}
	auto const parties = collectNestedSeatPartySet(records, key);
	auto const* party = findParty(parties.catalog, partyIndex);
	int independentShade = 0;
	return resolveSeatPartyColour(party, partyIndex, independentShade);
}

ParliamentView buildSeatPartyTableView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex,
	ParliamentColumn::Kind kind,
	char const* valueKey)
{
	ParliamentView view;
	auto const seats = listSeatWinChanceSeats(records);
	auto const colour = nestedSeatPartyColour(records, partyIndex, valueKey);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (auto const& seatName : seats) {
		ParliamentColumn column;
		column.kind = kind;
		column.partyIndex = partyIndex;
		column.header = seatName;
		column.colour = colour;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = simulationReport(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (report) {
				number = nestedSeatPartyValue(
					*report, valueKey, column.header, column.partyIndex);
			}
			cells.push_back(cellFromNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildSeatPartyGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	std::optional<int> partyIndex,
	ParliamentColumn::Kind kind,
	char const* valueKey)
{
	ParliamentView view;
	auto const parties = listNestedSeatParties(records, valueKey);
	std::vector<ThresholdPartyOption> series;
	if (partyIndex) {
		if (auto const* selected = findPartyOption(parties, *partyIndex)) {
			series.push_back(*selected);
		}
		else {
			ThresholdPartyOption option;
			option.partyIndex = *partyIndex;
			auto const set = collectNestedSeatPartySet(records, valueKey);
			auto const* party = findParty(set.catalog, *partyIndex);
			option.label = partyLabel(party, *partyIndex);
			int independentShade = 0;
			option.colour = resolveSeatPartyColour(party, *partyIndex, independentShade);
			series.push_back(std::move(option));
		}
	}
	else {
		series = parties;
	}

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (auto const& option : series) {
		ParliamentColumn column;
		column.kind = kind;
		column.partyIndex = option.partyIndex;
		column.header = option.label;
		column.colour = option.colour;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = simulationReport(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (report && !seatName.empty()) {
				number = nestedSeatPartyValue(
					*report, valueKey, seatName, column.partyIndex);
			}
			cells.push_back(cellFromNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildSeatWinChanceView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex)
{
	return buildSeatPartyTableView(
		records, partyIndex,
		ParliamentColumn::Kind::SeatWinChance,
		"seat_party_win_percent");
}

ParliamentView buildSeatWinChanceGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	std::optional<int> partyIndex)
{
	return buildSeatPartyGraphView(
		records, seatName, partyIndex,
		ParliamentColumn::Kind::SeatWinChance,
		"seat_party_win_percent");
}

ParliamentView buildSeatFpView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex)
{
	return buildSeatPartyTableView(
		records, partyIndex,
		ParliamentColumn::Kind::SeatFp,
		"seat_party_mean_fp_share");
}

ParliamentView buildSeatFpGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	std::optional<int> partyIndex)
{
	return buildSeatPartyGraphView(
		records, seatName, partyIndex,
		ParliamentColumn::Kind::SeatFp,
		"seat_party_mean_fp_share");
}

namespace {

ParsedNumber seatNamedNumber(
	json const& report,
	char const* key,
	std::string_view seatName)
{
	auto const names = seatNamesFromReport(report);
	auto const found = std::find(names.begin(), names.end(), seatName);
	if (found == names.end()) return {};
	auto const seatIndex = std::size_t(found - names.begin());
	if (!report.contains(key) || !report[key].is_array()) return {};
	auto const& values = report[key];
	if (seatIndex >= values.size()) return {};
	return parseNumber(values[seatIndex]);
}

CellValue cellFromCompletion(ParsedNumber const& number)
{
	CellValue cell;
	if (number.kind == NumberKind::NonFinite) {
		cell.kind = CellValue::Kind::Diagnostic;
		cell.text = formatNonFiniteMarker(number.tag);
		return cell;
	}
	cell.kind = CellValue::Kind::Value;
	cell.percent = number.kind == NumberKind::Finite ? number.value : 0.0;
	cell.text = formatPercent(cell.percent * 100.0);
	return cell;
}

constexpr PartyColour CompletionSeriesColour{90, 90, 90};
constexpr double CompletionChangeThreshold = 0.0001;

}

ParliamentView buildSeatCompletionView(
	std::vector<SnapshotRecord> const& records,
	char const* valueKey)
{
	ParliamentView view;
	auto const seats = listSeatWinChanceSeats(records);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (auto const& seatName : seats) {
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::SeatCompletion;
		column.header = seatName;
		column.colour = CompletionSeriesColour;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = simulationReport(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (report) {
				number = seatNamedNumber(*report, valueKey, column.header);
			}
			cells.push_back(cellFromCompletion(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildSeatCompletionGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	char const* valueKey)
{
	ParliamentView view;

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	if (!seatName.empty()) {
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::SeatCompletion;
		column.header = seatName;
		column.colour = CompletionSeriesColour;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* report = simulationReport(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (report && !seatName.empty()) {
				number = seatNamedNumber(*report, valueKey, seatName);
			}
			cells.push_back(cellFromCompletion(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

CompletionCellStyle completionCellStyle(
	CellValue const& cell,
	std::optional<double> previousValue)
{
	CompletionCellStyle style;
	if (cell.kind != CellValue::Kind::Percent &&
		cell.kind != CellValue::Kind::Value) {
		return style;
	}
	double const t = std::clamp(cell.percent, 0.0, 1.0);
	int const channel = int(std::lround(255.0 * (1.0 - t) + 128.0 * t));
	style.background = {channel, channel, channel};
	if (previousValue && std::isfinite(*previousValue)) {
		double const delta = cell.percent - *previousValue;
		if (std::abs(delta) > CompletionChangeThreshold) {
			style.changeBorder = delta > 0.0 ?
				CompletionChangeBorder::Increase :
				CompletionChangeBorder::Decrease;
		}
	}
	return style;
}

namespace {

PartyColour categoryPaletteColour(int index)
{
	static PartyColour const colours[] = {
		{31, 119, 180},
		{255, 127, 14},
		{44, 160, 44},
		{214, 39, 40},
		{148, 103, 189},
		{140, 86, 75},
		{227, 119, 194},
		{127, 127, 127},
		{188, 189, 34},
		{23, 190, 207}
	};
	int const count = int(sizeof(colours) / sizeof(colours[0]));
	if (index < 0) index = 0;
	return colours[index % count];
}

std::string categoryStatHeader(
	std::string const& category,
	CategoryStatistic statistic,
	CategoryStatGrouping grouping)
{
	auto const* label = categoryStatisticLabel(statistic);
	if (grouping == CategoryStatGrouping::ByCategory) {
		return category + " " + label;
	}
	return std::string(label) + " " + category;
}

ParsedNumber categoryStatNumber(
	json const& summary,
	char const* arrayKey,
	std::string_view categoryName,
	char const* field)
{
	if (!summary.contains(arrayKey) || !summary[arrayKey].is_array()) return {};
	for (auto const& item : summary[arrayKey]) {
		if (!item.is_object() || !item.contains("category") ||
			!item["category"].is_string()) {
			continue;
		}
		if (item["category"].get<std::string>() != categoryName) continue;
		if (!item.contains(field)) return {};
		return parseNumber(item[field]);
	}
	return {};
}

CellValue cellFromCategoryStat(
	ParsedNumber const& number, CategoryStatistic statistic)
{
	if (categoryStatisticInteger(statistic)) {
		return cellFromSeatCount(number);
	}
	return cellFromSeatNumber(number);
}

ParliamentColumn categoryStatColumn(
	std::string const& categoryName,
	CategoryStatistic statistic,
	PartyColour colour,
	CategoryStatGrouping grouping,
	bool includeInGraph)
{
	ParliamentColumn column;
	column.kind = ParliamentColumn::Kind::CategoryStat;
	column.partyIndex = int(statistic);
	column.header = includeInGraph ?
		categoryName : categoryStatHeader(categoryName, statistic, grouping);
	column.category = categoryName;
	column.colour = colour;
	column.includeInGraph = includeInGraph;
	return column;
}

void appendCategorySeparator(ParliamentView& view)
{
	ParliamentColumn separator;
	separator.kind = ParliamentColumn::Kind::Separator;
	separator.includeInGraph = false;
	view.columns.push_back(std::move(separator));
}

}

std::vector<std::string> listCategoryNames(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey)
{
	std::vector<std::string> names;
	if (!arrayKey) return names;
	for (auto const& selection : selectParliamentRows(records)) {
		auto const* summary = liveAnalysisSummary(records[selection.recordIndex]);
		if (!summary || !summary->contains(arrayKey) ||
			!(*summary)[arrayKey].is_array()) {
			continue;
		}
		for (auto const& item : (*summary)[arrayKey]) {
			if (!item.is_object() || !item.contains("category") ||
				!item["category"].is_string()) {
				continue;
			}
			appendUniqueName(names, item["category"].get<std::string>());
		}
	}
	return names;
}

ParliamentView buildCategoryStatTableView(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey,
	CategoryStatGrouping grouping)
{
	ParliamentView view;
	auto const categories = listCategoryNames(records, arrayKey);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	if (grouping == CategoryStatGrouping::ByCategory) {
		for (int index = 0; index < int(categories.size()); ++index) {
			if (index > 0) appendCategorySeparator(view);
			auto const colour = categoryPaletteColour(index);
			for (int statistic = 0; statistic < CategoryStatisticCount; ++statistic) {
				view.columns.push_back(categoryStatColumn(
					categories[index],
					categoryStatisticAt(statistic),
					colour,
					grouping,
					false));
			}
		}
	}
	else {
		for (int statistic = 0; statistic < CategoryStatisticCount; ++statistic) {
			if (statistic > 0) appendCategorySeparator(view);
			for (int index = 0; index < int(categories.size()); ++index) {
				view.columns.push_back(categoryStatColumn(
					categories[index],
					categoryStatisticAt(statistic),
					categoryPaletteColour(index),
					grouping,
					false));
			}
		}
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* summary = liveAnalysisSummary(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			if (column.kind == ParliamentColumn::Kind::Separator) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cells.push_back(std::move(cell));
				continue;
			}
			auto const statistic = categoryStatisticAt(column.partyIndex);
			ParsedNumber number;
			if (summary && arrayKey) {
				number = categoryStatNumber(
					*summary, arrayKey, column.category,
					categoryStatisticKey(statistic));
			}
			cells.push_back(cellFromCategoryStat(number, statistic));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildCategoryStatGraphView(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey,
	CategoryStatistic statistic)
{
	ParliamentView view;
	auto const categories = listCategoryNames(records, arrayKey);

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	for (int index = 0; index < int(categories.size()); ++index) {
		view.columns.push_back(categoryStatColumn(
			categories[index],
			statistic,
			categoryPaletteColour(index),
			CategoryStatGrouping::ByCategory,
			true));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* summary = liveAnalysisSummary(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (summary && arrayKey) {
				number = categoryStatNumber(
					*summary, arrayKey, column.category,
					categoryStatisticKey(statistic));
			}
			cells.push_back(cellFromCategoryStat(number, statistic));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

ParliamentView buildInternal2ppView(std::vector<SnapshotRecord> const& records)
{
	ParliamentView view;

	ParliamentColumn snapshot;
	snapshot.kind = ParliamentColumn::Kind::Snapshot;
	snapshot.header = "Snapshot";
	snapshot.includeInGraph = false;
	view.columns.push_back(std::move(snapshot));

	struct Spec {
		char const* header;
		char const* key;
	};
	Spec const specs[] = {
		{"Internal projected 2PP", "projected_2pp"},
		{"Raw 2PP deviation", "raw_2pp_deviation"}
	};
	for (int index = 0; index < int(sizeof(specs) / sizeof(specs[0])); ++index) {
		ParliamentColumn column;
		column.kind = ParliamentColumn::Kind::Internal2pp;
		column.header = specs[index].header;
		column.category = specs[index].key;
		column.colour = categoryPaletteColour(index);
		column.includeInGraph = true;
		view.columns.push_back(std::move(column));
	}

	auto const selections = selectParliamentRows(records);
	view.rows.reserve(selections.size());
	view.cells.reserve(selections.size());
	for (auto const& selection : selections) {
		auto const& record = records[selection.recordIndex];
		ParliamentRow row = makeTableRow(record, selection);
		std::vector<CellValue> cells;
		cells.reserve(view.columns.size());
		json const* summary = liveAnalysisSummary(record);
		for (auto const& column : view.columns) {
			if (column.kind == ParliamentColumn::Kind::Snapshot) {
				CellValue cell;
				cell.kind = CellValue::Kind::Text;
				cell.text = row.timestampLabel;
				cells.push_back(std::move(cell));
				continue;
			}
			ParsedNumber number;
			if (summary && !column.category.empty() &&
				summary->contains(column.category)) {
				number = parseNumber((*summary)[column.category]);
			}
			cells.push_back(cellFromSeatNumber(number));
		}
		view.rows.push_back(std::move(row));
		view.cells.push_back(std::move(cells));
	}
	return view;
}

bool isCalledSeatWin(double percent)
{
	return std::isfinite(percent) && percent > 99.995;
}

bool updateSeatWinCalled(bool previouslyCalled, double percent)
{
	if (!std::isfinite(percent)) return previouslyCalled;
	if (previouslyCalled) return percent >= 99.0;
	return percent > 99.995;
}

bool seatWinCalledUpToRow(
	std::vector<std::vector<CellValue>> const& cells,
	int row,
	int column)
{
	if (row < 0 || column < 0) return false;
	bool called = false;
	int const last = std::min(row, int(cells.size()) - 1);
	for (int r = 0; r <= last; ++r) {
		if (column >= int(cells[r].size())) continue;
		auto const& cell = cells[r][column];
		if (cell.kind != CellValue::Kind::Percent &&
			cell.kind != CellValue::Kind::Value) {
			continue;
		}
		called = updateSeatWinCalled(called, cell.percent);
	}
	return called;
}

SeatWinCellStyle seatWinCellStyle(
	std::optional<PartyColour> const& partyColour,
	CellValue const& cell,
	std::optional<double> previousPercent,
	SeatWinShading shading,
	bool called,
	MetricShadeSettings const& settings)
{
	SeatWinCellStyle style;
	if (cell.kind != CellValue::Kind::Percent &&
		cell.kind != CellValue::Kind::Value) {
		return style;
	}

	PartyColour const party = partyColour.value_or(PartyColour{128, 128, 128});
	if (settings.useCalledStyle && called &&
		shading == SeatWinShading::CurrentChance) {
		style.background = mixRgbTowardBlack(party, 0.55);
		style.whiteText = true;
		return style;
	}

	if (shading == SeatWinShading::CurrentChance) {
		double const full = settings.levelFullPercent > 0.0 ?
			settings.levelFullPercent : 100.0;
		double const t = std::clamp(cell.percent, 0.0, full) / full;
		style.background = mixRgbTowardWhite(
			party, 1.0 - t * settings.levelSaturationCap);
		return style;
	}

	if (!previousPercent || !std::isfinite(*previousPercent)) {
		return style;
	}
	double const delta = cell.percent - *previousPercent;
	double const saturation = metricChangeSaturation(
		std::abs(delta), settings.changeFullPercent);
	if (saturation <= 0.0 || delta == 0.0) return style;
	PartyColour const base = delta > 0.0 ?
		PartyColour{0, 160, 0} : PartyColour{200, 0, 0};
	style.background = mixRgbTowardWhite(base, 1.0 - saturation);
	return style;
}

std::optional<NodeInspectorSummary> inspectorSummary(SnapshotRecord const& record)
{
	NodeInspectorSummary summary;
	summary.simulationName = record.simulationName;
	summary.termCode = record.termCode;
	summary.snapshotCode = record.snapshotCode;
	summary.completedAt = record.completedAt;
	summary.filename = record.filename;
	summary.boothCount = record.boothCount;
	summary.seatCount = record.analysisSeatCount;
	summary.firstBoothSeat = record.firstBoothSeat;
	summary.firstBoothName = record.firstBoothName;
	return summary;
}

}
