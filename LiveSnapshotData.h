#pragma once

#include "json.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class PollingProject;

namespace LiveSnapshot {

struct PartyColour {
	int r = 128;
	int g = 128;
	int b = 128;
};

struct PartyIdentity {
	int partyIndex = 0;
	std::optional<std::string> name;
	std::optional<std::string> abbreviation;
	std::optional<PartyColour> colour;
};

struct SnapshotRecord {
	std::string filename;
	std::string snapshotCode;
	std::string completedAt;
	std::string simulationName;
	std::string termCode;
	std::string outputSet;
	std::vector<PartyIdentity> parties;
	int boothCount = 0;
	int analysisSeatCount = 0;
	std::optional<std::string> firstBoothSeat;
	std::optional<std::string> firstBoothName;
	nlohmann::json document;
};

struct LoadWarning {
	std::string filename;
	std::string message;
};

enum class LoadStatus {
	Ok,
	NoSimulation,
	InvalidFolder,
	MissingFolder,
	UnreadableFolder,
};

struct LoadResult {
	LoadStatus status = LoadStatus::NoSimulation;
	std::string simulationName;
	std::string outputSet;
	std::string errorMessage;
	std::vector<SnapshotRecord> records;
	std::vector<LoadWarning> warnings;
};

struct ParliamentRow {
	std::string snapshotCode;
	std::string timestampLabel;
	bool duplicate = false;
	std::string selectedFilename;
	std::string completedAt;
	int duplicateCount = 1;
	std::size_t recordIndex = 0;
};

struct ParliamentColumn {
	enum class Kind {
		Snapshot,
		PartyOutcome,
		ExactTie,
		PartySeats,
		Separator,
		CoalitionSeats,
		SeatThreshold,
		TppThreshold,
		TppMean,
		PartyVoteShare,
		CoalitionVoteShare,
		SeatWinChance,
		SeatFp,
		SeatCompletion,
		CategoryStat,
		Internal2pp
	};
	enum class Outcome { Majority, Minority, MostSeats };

	Kind kind = Kind::Snapshot;
	Outcome outcome = Outcome::Majority;
	int partyIndex = 0;
	std::string header;
	std::string category;
	std::optional<PartyColour> colour;
	bool includeInGraph = true;
};

struct CellValue {
	enum class Kind { Text, Percent, Value, Diagnostic };
	Kind kind = Kind::Text;
	std::string text;
	double percent = 0.0;
};

struct ParliamentView {
	std::vector<ParliamentColumn> columns;
	std::vector<ParliamentRow> rows;
	std::vector<std::vector<CellValue>> cells;
};

struct NodeInspectorSummary {
	std::string simulationName;
	std::string termCode;
	std::string snapshotCode;
	std::string completedAt;
	std::string filename;
	int boothCount = 0;
	int seatCount = 0;
	std::optional<std::string> firstBoothSeat;
	std::optional<std::string> firstBoothName;
};

struct ParliamentSelection {
	std::size_t recordIndex = 0;
	int duplicateCount = 1;
};

LoadResult loadDirectory(
	std::filesystem::path const& directory,
	std::string simulationName,
	std::string outputSet);

LoadResult loadFromProject(PollingProject const& project);

// Replace catalog colours for parties present in `colours`. Snapshot files keep
// the colours from the run that produced them; Live Booths should follow the
// current project palette instead.
void overlayPartyColours(
	std::vector<SnapshotRecord>& records,
	std::map<int, PartyColour> const& colours);

std::vector<ParliamentSelection> selectParliamentRows(
	std::vector<SnapshotRecord> const& records);

struct ThresholdPartyOption {
	int partyIndex = 0;
	std::string label;
	std::optional<PartyColour> colour;
};

enum class SeatWinShading { CurrentChance, Change };

struct SeatWinCellStyle {
	PartyColour background = {255, 255, 255};
	bool whiteText = false;
};

struct MetricShadeSettings {
	double levelFullPercent = 100.0;
	double changeFullPercent = 100.0;
	double levelSaturationCap = 0.7;
	bool useCalledStyle = true;
};

inline MetricShadeSettings seatWinShadeSettings()
{
	return {};
}

inline MetricShadeSettings seatFpShadeSettings()
{
	MetricShadeSettings settings;
	settings.levelFullPercent = 50.0;
	settings.changeFullPercent = 4.0;
	settings.useCalledStyle = false;
	return settings;
}

enum class CompletionChangeBorder { None, Increase, Decrease };

struct CompletionCellStyle {
	PartyColour background = {255, 255, 255};
	CompletionChangeBorder changeBorder = CompletionChangeBorder::None;
};

ParliamentView buildParliamentView(std::vector<SnapshotRecord> const& records);
ParliamentView buildSeatExpectationView(std::vector<SnapshotRecord> const& records);
std::vector<ThresholdPartyOption> listSeatThresholdParties(
	std::vector<SnapshotRecord> const& records);
ParliamentView buildSeatThresholdView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex);
ParliamentView buildTppView(std::vector<SnapshotRecord> const& records);
ParliamentView buildVoteShareView(std::vector<SnapshotRecord> const& records);
std::vector<ThresholdPartyOption> listSeatWinChanceParties(
	std::vector<SnapshotRecord> const& records);
std::vector<std::string> listSeatWinChanceSeats(
	std::vector<SnapshotRecord> const& records);
ParliamentView buildSeatWinChanceView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex);
ParliamentView buildSeatWinChanceGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	std::optional<int> partyIndex);
std::vector<ThresholdPartyOption> listSeatFpParties(
	std::vector<SnapshotRecord> const& records);
ParliamentView buildSeatFpView(
	std::vector<SnapshotRecord> const& records,
	int partyIndex);
ParliamentView buildSeatFpGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	std::optional<int> partyIndex);
ParliamentView buildSeatCompletionView(
	std::vector<SnapshotRecord> const& records,
	char const* valueKey);
ParliamentView buildSeatCompletionGraphView(
	std::vector<SnapshotRecord> const& records,
	std::string const& seatName,
	char const* valueKey);
CompletionCellStyle completionCellStyle(
	CellValue const& cell,
	std::optional<double> previousValue);

enum class CategoryStatGrouping { ByCategory, ByStatistic };

enum class CategoryStatistic {
	Bias,
	StdDev,
	Raw,
	SourceCount,
	VoteCount
};

constexpr int CategoryStatisticCount = 5;

inline CategoryStatistic categoryStatisticAt(int index)
{
	if (index <= 0) return CategoryStatistic::Bias;
	if (index == 1) return CategoryStatistic::StdDev;
	if (index == 2) return CategoryStatistic::Raw;
	if (index == 3) return CategoryStatistic::SourceCount;
	return CategoryStatistic::VoteCount;
}

inline char const* categoryStatisticKey(CategoryStatistic statistic)
{
	switch (statistic) {
	case CategoryStatistic::StdDev: return "std_dev";
	case CategoryStatistic::Raw: return "raw";
	case CategoryStatistic::SourceCount: return "source_count";
	case CategoryStatistic::VoteCount: return "vote_count";
	case CategoryStatistic::Bias:
	default: return "bias";
	}
}

inline char const* categoryStatisticLabel(CategoryStatistic statistic)
{
	switch (statistic) {
	case CategoryStatistic::StdDev: return "StdDev";
	case CategoryStatistic::Raw: return "Raw";
	case CategoryStatistic::SourceCount: return "Source";
	case CategoryStatistic::VoteCount: return "Votes";
	case CategoryStatistic::Bias:
	default: return "Bias";
	}
}

inline char const* categoryStatisticTitle(CategoryStatistic statistic)
{
	switch (statistic) {
	case CategoryStatistic::StdDev: return "StdDev";
	case CategoryStatistic::Raw: return "Raw";
	case CategoryStatistic::SourceCount: return "Source count";
	case CategoryStatistic::VoteCount: return "Vote count";
	case CategoryStatistic::Bias:
	default: return "Bias";
	}
}

inline bool categoryStatisticInteger(CategoryStatistic statistic)
{
	return statistic == CategoryStatistic::SourceCount ||
		statistic == CategoryStatistic::VoteCount;
}

std::vector<std::string> listCategoryNames(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey);
ParliamentView buildCategoryStatTableView(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey,
	CategoryStatGrouping grouping);
ParliamentView buildCategoryStatGraphView(
	std::vector<SnapshotRecord> const& records,
	char const* arrayKey,
	CategoryStatistic statistic);
ParliamentView buildInternal2ppView(std::vector<SnapshotRecord> const& records);

bool isCalledSeatWin(double percent);
bool updateSeatWinCalled(bool previouslyCalled, double percent);
bool seatWinCalledUpToRow(
	std::vector<std::vector<CellValue>> const& cells,
	int row,
	int column);
SeatWinCellStyle seatWinCellStyle(
	std::optional<PartyColour> const& partyColour,
	CellValue const& cell,
	std::optional<double> previousPercent,
	SeatWinShading shading,
	bool called = false,
	MetricShadeSettings const& settings = {});

std::string formatSnapshotTimestamp(std::string_view snapshotCode);
std::optional<std::int64_t> snapshotCodeTimeSeconds(std::string_view snapshotCode);
std::string formatCompletedAt(std::string_view completedAt);
std::string formatRunSelectorLabel(SnapshotRecord const& record);
std::string formatStatusLine(LoadResult const& result);
std::string formatPercent(double value);
std::string formatSeatExpectation(double value);
std::string formatSeatCount(double value);
std::string formatNonFiniteMarker(std::string_view tag);

std::optional<NodeInspectorSummary> inspectorSummary(
	SnapshotRecord const& record);

int compareCompletedAt(std::string_view left, std::string_view right);

bool isMajorParliamentParty(int partyIndex);

PartyColour fadeParliamentHeaderColour(
	PartyColour colour,
	ParliamentColumn::Outcome outcome);

}
