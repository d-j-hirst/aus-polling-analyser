#include "../LiveSnapshotData.h"
#include "../LiveSnapshotGraph.h"
#include "../LiveSnapshotTable.h"
#include "../SpecialPartyCodes.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>

using json = nlohmann::json;

namespace {

std::filesystem::path makeTempRoot()
{
	auto path = std::filesystem::temp_directory_path() /
		"polling-live-snapshot-tests";
	std::error_code error;
	std::filesystem::remove_all(path, error);
	std::filesystem::create_directories(path);
	return path;
}

json party(
	int index,
	std::string name,
	std::string abbreviation,
	int red,
	int green,
	int blue)
{
	return {
		{"party_index", index},
		{"name", std::move(name)},
		{"abbreviation", std::move(abbreviation)},
		{"colour", {{"r", red}, {"g", green}, {"b", blue}}}
	};
}

json outcome(int partyIndex, json value)
{
	return {{"party_index", partyIndex}, {"value", std::move(value)}};
}

json categoryEvidence(
	std::string category,
	double bias,
	double stdDev,
	double raw,
	double sourceCount,
	double voteCount)
{
	return {
		{"category", std::move(category)},
		{"bias", bias},
		{"std_dev", stdDev},
		{"raw", raw},
		{"source_count", sourceCount},
		{"vote_count", voteCount}
	};
}

json snapshotDocument(
	std::string snapshotCode,
	std::string completedAt,
	json majority,
	json minority,
	json mostSeats,
	json tied,
	json extraAnalysis = json::object())
{
	json analysis = {
		{"booths", json::array({
			{{"name", "Adelaide"}, {"seat_name", "Adelaide"}}
		})},
		{"seats", json::array({{{"name", "Adelaide"}}})}
	};
	analysis.update(extraAnalysis);
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
	return {
		{"format_version", 2},
		{"run", {
			{"simulation_name", "Live Simulation"},
			{"term_code", "2026sa"},
			{"snapshot_code", std::move(snapshotCode)},
			{"completed_at", std::move(completedAt)},
			{"iterations", 200},
			{"output_set", "sa2026-test1"}
		}},
		{"parties", json::array({
			party(0, "Labor", "ALP", 255, 0, 0),
			party(1, "Liberals", "LNP", 0, 0, 255),
			party(-4, "Coalition", "LNP", 0, 0, 180)
		})},
		{"simulation_report", {
			{"majority_percent", std::move(majority)},
			{"minority_percent", std::move(minority)},
			{"most_seats_percent", std::move(mostSeats)},
			{"tied_percent", std::move(tied)}
		}},
		{"live_analysis", std::move(analysis)},
		{"live_analysis_summary", std::move(summary)}
	};
}

void writeJson(std::filesystem::path const& path, json const& document)
{
	std::ofstream stream(path, std::ios::binary);
	stream << document.dump(2);
}

}

int main()
{
	using namespace LiveSnapshot;
	auto const root = makeTempRoot();

	assert(formatSnapshotTimestamp("20260402143211") == "02-04-26 14:32:11");
	assert(formatCompletedAt("2026-09-04T19:48:52+10:00") == "04-09-26 19:48:52");
	assert(formatPercent(34.019) == "34.02%");
	assert(formatNonFiniteMarker("nan") == "nan");
	assert(formatNonFiniteMarker("positive_infinity") == "+inf");
	assert(formatNonFiniteMarker("negative_infinity") == "-inf");
	assert(compareCompletedAt(
		"2026-09-04T19:48:52+10:00",
		"2026-09-04T09:48:52Z") == 0);
	assert(compareCompletedAt(
		"2026-09-04T20:00:00+10:00",
		"2026-09-04T09:48:52Z") > 0);

	assert(isMajorParliamentParty(0));
	assert(isMajorParliamentParty(1));
	assert(!isMajorParliamentParty(2));
	assert(!isMajorParliamentParty(-4));

	PartyColour const sample{180, 40, 40};
	auto const majorityHeader = fadeParliamentHeaderColour(
		sample, ParliamentColumn::Outcome::Majority);
	auto const minorityHeader = fadeParliamentHeaderColour(
		sample, ParliamentColumn::Outcome::Minority);
	auto const mostSeatsHeader = fadeParliamentHeaderColour(
		sample, ParliamentColumn::Outcome::MostSeats);
	assert(majorityHeader.r == (180 + 255 * 2) / 3);
	assert(majorityHeader.r < minorityHeader.r);
	assert(minorityHeader.r < mostSeatsHeader.r);
	assert(mostSeatsHeader.r < 255);

	auto missing = loadDirectory(root / "missing", "Live Simulation", "gone");
	assert(missing.status == LoadStatus::MissingFolder);
	assert(missing.errorMessage.find("Live Simulation") != std::string::npos);
	assert(missing.errorMessage.find("gone") != std::string::npos);

	auto const emptyDir = root / "empty";
	std::filesystem::create_directories(emptyDir);
	auto empty = loadDirectory(emptyDir, "Live Simulation", "empty-set");
	assert(empty.status == LoadStatus::Ok);
	assert(empty.records.empty());
	assert(formatStatusLine(empty).find("0 runs") != std::string::npos);

	auto const dir = root / "sa2026-test1";
	std::filesystem::create_directories(dir);

	writeJson(dir / "snapshot_20260401100000__run_20260904T100000.json",
		snapshotDocument(
			"20260401100000",
			"2026-09-04T10:00:00+10:00",
			json::array({outcome(0, 100.0)}),
			json::array({outcome(0, 0.0)}),
			json::array({outcome(0, 100.0)}),
			0.0));
	writeJson(dir / "snapshot_20260402143211__run_20260904T120000.json",
		snapshotDocument(
			"20260402143211",
			"2026-09-04T12:00:00+10:00",
			json::array({outcome(0, 90.0), outcome(1, 0.0)}),
			json::array({outcome(0, 10.0), outcome(1, 0.0)}),
			json::array({outcome(0, 95.0), outcome(1, 5.0)}),
			1.5));
	writeJson(dir / "snapshot_20260402143211__run_20260904T194852.json",
		snapshotDocument(
			"20260402143211",
			"2026-09-04T19:48:52+10:00",
			json::array({outcome(0, 100.0), outcome(1, 0.0)}),
			json::array({outcome(0, 0.0), outcome(1, 0.0)}),
			json::array({outcome(0, 100.0), outcome(1, 0.0)}),
			json{{"non_finite", "nan"}}));
	writeJson(dir / "snapshot_20260301000000__run_late.json",
		snapshotDocument(
			"20260301000000",
			"2026-09-05T08:00:00+10:00",
			json::array({outcome(1, 12.5)}),
			json::array({outcome(1, 3.0)}),
			json::array({outcome(1, 8.0)}),
			0.0));

	writeJson(dir / "snapshot_bad_version.json", json{{"format_version", 1}});
	{
		std::ofstream tmp(dir / "snapshot_writing.json.tmp", std::ios::binary);
		tmp << "{}";
	}
	{
		std::ofstream malformed(dir / "snapshot_broken.json", std::ios::binary);
		malformed << "{not json";
	}
	writeJson(dir / "snapshot_20260402143211__run_20260904T194852.analysis.json",
		json{{"should_not", "load"}});

	auto loaded = loadDirectory(dir, "Live Simulation", "sa2026-test1");
	assert(loaded.status == LoadStatus::Ok);
	assert(loaded.records.size() == 4);
	assert(loaded.warnings.size() == 2);
	assert(!loaded.records[3].document.contains("live_analysis"));
	assert(!loaded.records[3].document.contains("live_baseline_report"));
	assert(loaded.records[3].document.contains("live_analysis_summary"));
	assert(loaded.records[3].boothCount == 1);
	assert(loaded.records[3].analysisSeatCount == 1);
	assert(loaded.records[3].firstBoothName == "Adelaide");
	assert(loaded.records[0].snapshotCode == "20260301000000");
	assert(loaded.records[1].snapshotCode == "20260401100000");
	assert(loaded.records[2].snapshotCode == "20260402143211");
	assert(loaded.records[3].snapshotCode == "20260402143211");
	assert(loaded.records[2].completedAt < loaded.records[3].completedAt ||
		compareCompletedAt(loaded.records[2].completedAt, loaded.records[3].completedAt) < 0);
	assert(loaded.records[3].filename.find("194852") != std::string::npos);

	auto const selections = selectParliamentRows(loaded.records);
	assert(selections.size() == 3);
	assert(loaded.records[selections[2].recordIndex].filename.find("194852") !=
		std::string::npos);
	assert(selections[2].duplicateCount == 2);

	auto sameTimeDir = root / "tie";
	std::filesystem::create_directories(sameTimeDir);
	auto const tiedDocA = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 1.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 1.0)}),
		0.0);
	auto tiedDocB = tiedDocA;
	tiedDocB["simulation_report"]["majority_percent"][0]["value"] = 2.0;
	writeJson(sameTimeDir / "snapshot_20260402143211__run_a.json", tiedDocA);
	writeJson(sameTimeDir / "snapshot_20260402143211__run_b.json", tiedDocB);
	auto tiedLoad = loadDirectory(sameTimeDir, "Live Simulation", "tie");
	auto tiedRows = selectParliamentRows(tiedLoad.records);
	assert(tiedRows.size() == 1);
	assert(tiedLoad.records[tiedRows[0].recordIndex].filename.ends_with("run_b.json"));

	auto view = buildParliamentView(loaded.records);
	assert(view.rows.size() == 3);
	assert(view.rows[0].snapshotCode == "20260301000000");
	assert(view.rows[1].duplicate == false);
	assert(view.rows[2].duplicate);
	assert(view.rows[2].timestampLabel.ends_with("*"));
	assert(view.rows[2].timestampLabel.find("02-04-26 14:32:11") == 0);
	assert(view.rows[2].selectedFilename.find("194852") != std::string::npos);

	assert(view.columns.size() == 1 + 3 * 2 + 1);
	assert(view.columns[0].header == "Snapshot");
	assert(view.columns[1].header == "ALP Majority");
	assert(view.columns[2].header == "ALP Minority");
	assert(view.columns[3].header == "ALP Most Seats");
	assert(view.columns[4].header == "LNP Majority");
	assert(view.columns[5].header == "LNP Minority");
	assert(view.columns[6].header == "LNP Most Seats");
	assert(view.columns.back().header == "Exact Tie");

	auto const& first = view.cells[0];
	assert(first[1].kind == CellValue::Kind::Percent);
	assert(first[1].text == "0.00%");
	assert(first[4].text == "12.50%");

	auto const& latest = view.cells[2];
	assert(latest[1].text == "100.00%");
	assert(latest.back().kind == CellValue::Kind::Diagnostic);
	assert(latest.back().text == "nan");

	auto greensDir = root / "greens";
	std::filesystem::create_directories(greensDir);
	auto greensDoc = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({
			outcome(0, 80.0),
			outcome(1, 5.0),
			outcome(2, 4.0)
		}),
		json::array({
			outcome(0, 10.0),
			outcome(1, 2.0),
			outcome(2, 0.0)
		}),
		json::array({
			outcome(0, 85.0),
			outcome(1, 8.0),
			outcome(2, 6.0)
		}),
		0.0);
	greensDoc["parties"].push_back(party(2, "Greens", "GRN", 0, 160, 0));
	writeJson(greensDir / "snapshot_20260402143211__run_a.json", greensDoc);
	auto greensView = buildParliamentView(
		loadDirectory(greensDir, "Live Simulation", "greens").records);
	std::vector<std::string> greensHeaders;
	for (auto const& column : greensView.columns) {
		greensHeaders.push_back(column.header);
	}
	assert(greensHeaders == std::vector<std::string>({
		"Snapshot",
		"ALP Majority",
		"ALP Minority",
		"ALP Most Seats",
		"LNP Majority",
		"LNP Minority",
		"LNP Most Seats",
		"GRN Majority",
		"GRN Most Seats",
		"Exact Tie"
	}));

	auto overlayRecords = loadDirectory(greensDir, "Live Simulation", "greens").records;
	assert(overlayRecords[0].parties[0].colour && overlayRecords[0].parties[0].colour->r == 255);
	overlayPartyColours(overlayRecords, {{0, {12, 34, 56}}});
	assert(overlayRecords[0].parties[0].colour &&
		overlayRecords[0].parties[0].colour->r == 12 &&
		overlayRecords[0].parties[0].colour->g == 34 &&
		overlayRecords[0].parties[0].colour->b == 56);
	assert(overlayRecords[0].parties[1].colour && overlayRecords[0].parties[1].colour->b == 255);
	auto overlayView = buildParliamentView(overlayRecords);
	assert(overlayView.columns[1].colour && overlayView.columns[1].colour->r == 12);
	auto overlayGraph = makeParliamentGraph(overlayView);
	assert(overlayGraph.series[0].colour.r == 12);
	assert(overlayGraph.series[0].colour.g == 34);

	auto summary = inspectorSummary(loaded.records[3]);
	assert(summary);
	assert(summary->firstBoothName == "Adelaide");
	assert(summary->boothCount == 1);
	assert(summary->seatCount == 1);

	TableMetrics metrics;
	metrics.headerHeight = 20;
	metrics.rowHeight = 10;
	metrics.snapshotColumnWidth = 100;
	metrics.outcomeColumnWidth = 50;
	metrics.exactTieColumnWidth = 40;
	auto layout = makeParliamentLayout(view, metrics);
	assert(layout.rowCount == 3);
	assert(layout.columnCount() == int(view.columns.size()));
	assert(layout.contentWidth() == 100 + 50 * 6 + 40);
	assert(layout.bodyHeight() == 30);
	assert(layout.clampScrollX(1000, 120) == layout.contentWidth() - 120);
	assert(layout.clampScrollY(1000, 15) == 15);
	assert(layout.clampScrollX(-4, 400) == 0);

	auto visible = layout.visibleCells(120, 35, 100, 10);
	assert(visible.firstRow == 1);
	assert(visible.lastRow == 3);
	assert(visible.firstCol >= 1);

	auto headerHit = layout.hitTest(10, 5, 0, 0, 400, 35);
	assert(headerHit.kind == TableLayout::HitKind::Header);
	assert(headerHit.column == 0);

	auto bodyHit = layout.hitTest(10, 25, 0, 0, 400, 35);
	assert(bodyHit.kind == TableLayout::HitKind::Body);
	assert(bodyHit.row == 0);
	auto scrolledHit = layout.hitTest(10, 25, 0, 10, 400, 35);
	assert(scrolledHit.kind == TableLayout::HitKind::Body);
	assert(scrolledHit.row == 1);

	auto outside = layout.hitTest(-1, 10, 0, 0, 400, 80);
	assert(outside.kind == TableLayout::HitKind::None);

	layout.frozenColumns = 1;
	assert(layout.frozenWidth() == 100);
	assert(layout.headerRect(0, 80).x == 0);
	assert(layout.bodyRect(0, 0, 80, 0).x == 0);
	assert(layout.headerRect(1, 80).x == layout.columnLeft(1) - 80);
	assert(layout.horizontalScrollStep(400) == (400 - layout.frozenWidth()) / 4);
	assert(layout.verticalScrollStep(40) == 10);
	auto frozenHeader = layout.hitTest(10, 5, 80, 0, 400, 35);
	assert(frozenHeader.kind == TableLayout::HitKind::Header);
	assert(frozenHeader.column == 0);
	auto frozenBody = layout.hitTest(10, 25, 80, 0, 400, 35);
	assert(frozenBody.kind == TableLayout::HitKind::Body);
	assert(frozenBody.column == 0);
	assert(frozenBody.row == 0);
	auto frozenVisible = layout.visibleCells(200, 35, 0, 0);
	assert(frozenVisible.firstCol >= 1);
	layout.frozenColumns = 0;

	auto graph = makeParliamentGraph(view);
	assert(graph.xLabels.size() == 3);
	assert(graph.series.size() == view.columns.size() - 1);
	assert(graph.series[0].label == "ALP Majority");
	assert(graph.series[0].style == GraphLineStyle::Solid);
	assert(graph.series[1].style == GraphLineStyle::Dash);
	assert(graph.series[2].style == GraphLineStyle::Dot);
	assert(graph.series[0].values[0] == 0.0);
	assert(graph.series[0].values[2] == 100.0);
	assert(!graph.series.back().values[2].has_value());
	assert(graph.series[0].colour.r == 255);
	assert(parliamentGraphColour(view.columns[2]).g >
		parliamentGraphColour(view.columns[1]).g);
	assert(parliamentGraphColour(view.columns[3]).g >
		parliamentGraphColour(view.columns[2]).g);

	auto graphLayout = layoutParliamentGraph(graph, 800, 400);
	assert(graphLayout.xPixels.size() == 3);
	assert(graphLayout.xPixels[0] < graphLayout.xPixels[2]);
	int const earlyGap = graphLayout.xPixels[1] - graphLayout.xPixels[0];
	int const lateGap = graphLayout.xPixels[2] - graphLayout.xPixels[1];
	assert(earlyGap > lateGap * 10);
	assert(graph.xTimes.size() == 3);
	assert(graph.xTimes[0] < graph.xTimes[1]);
	assert(graph.xTimes[1] < graph.xTimes[2]);
	assert(graphLayout.yForPercent(100) < graphLayout.yForPercent(0));
	assert(graphLayout.plot.width > 200);
	assert(graphLayout.legend.width > 0);
	auto graphHit = graphLayout.hitTest(
		graph,
		graphLayout.xPixels[2],
		graphLayout.yForPercent(100));
	assert(graphHit.kind == GraphLayout::Hit::Kind::Point);
	assert(graphHit.point == 2);

	GraphModel nightOnly;
	nightOnly.xLabels = {"18:00", "20:00", "22:00"};
	nightOnly.xTimes = {
		double(*snapshotCodeTimeSeconds("20260404180000")),
		double(*snapshotCodeTimeSeconds("20260404200000")),
		double(*snapshotCodeTimeSeconds("20260404220000"))
	};
	auto nightLayout = layoutParliamentGraph(nightOnly, 800, 400);
	int const twoHourA = nightLayout.xPixels[1] - nightLayout.xPixels[0];
	int const twoHourB = nightLayout.xPixels[2] - nightLayout.xPixels[1];
	assert(std::abs(twoHourA - twoHourB) <= 1);
	assert(!nightLayout.xBreakpointPixel);

	GraphModel withPost;
	withPost.xLabels = {"18:00", "22:00", "03:00", "next"};
	withPost.xTimes = {
		double(*snapshotCodeTimeSeconds("20260404180000")),
		double(*snapshotCodeTimeSeconds("20260404220000")),
		double(*snapshotCodeTimeSeconds("20260405030000")),
		double(*snapshotCodeTimeSeconds("20260406030000"))
	};
	auto postLayout = layoutParliamentGraph(withPost, 800, 400);
	int const fourHours = postLayout.xPixels[1] - postLayout.xPixels[0];
	int const twentyFourAfter = postLayout.xPixels[3] - postLayout.xPixels[2];
	assert(postLayout.xBreakpointPixel);
	assert(*postLayout.xBreakpointPixel == postLayout.xPixels[2]);
	assert(std::abs(fourHours - 4 * twentyFourAfter) <= 3);

	assert(formatSeatExpectation(33.9016) == "33.90");
	assert(formatSeatCount(7.0) == "7");
	assert(formatSeatCount(7.4) == "7");

	auto seatDoc = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 100.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 100.0)}),
		0.0);
	seatDoc["parties"].push_back(party(7, "One Nation", "ONP", 255, 165, 0));
	seatDoc["parties"].push_back(json{
		{"party_index", -3},
		{"name", "Emerging Party"},
		{"abbreviation", "OTH"},
		{"colour", nullptr}
	});
	seatDoc["parties"].push_back(json{
		{"party_index", 6},
		{"name", "Independent"},
		{"abbreviation", "IND"},
		{"colour", nullptr}
	});
	seatDoc["simulation_report"]["party_win_expectation"] = json::array({
		outcome(0, 33.9016),
		outcome(1, 5.03),
		outcome(7, 3.19),
		outcome(6, 4.3),
		outcome(-3, 0.01)
	});
	seatDoc["simulation_report"]["coalition_win_expectation"] = 5.03;
	auto seatOnlyDir = root / "seats-no-nat";
	std::filesystem::create_directories(seatOnlyDir);
	writeJson(seatOnlyDir / "snapshot_20260402143211__run_a.json", seatDoc);
	auto seatOnly = buildSeatExpectationView(
		loadDirectory(seatOnlyDir, "Live Simulation", "seats-no-nat").records);
	std::vector<std::string> seatHeaders;
	for (auto const& column : seatOnly.columns) {
		seatHeaders.push_back(column.header);
	}
	assert(seatHeaders == std::vector<std::string>({
		"Snapshot", "ALP", "LNP", "ONP", "OTH", "IND"
	}));
	assert(seatOnly.cells[0][1].kind == CellValue::Kind::Value);
	assert(seatOnly.cells[0][1].text == "33.90");
	assert(seatOnly.columns[4].colour && seatOnly.columns[4].colour->r == 0 &&
		seatOnly.columns[4].colour->g == 0);
	assert(seatOnly.columns[5].colour && seatOnly.columns[5].colour->r ==
		seatOnly.columns[5].colour->g);

	auto noNatGraph = makeSeatExpectationGraph(seatOnly);
	assert(noNatGraph.yIsPercent == false);
	assert(noNatGraph.yMax >= 33.90);
	for (auto const& series : noNatGraph.series) {
		assert(series.label != "Coalition Win Expectation");
	}

	auto natDoc = seatDoc;
	natDoc["parties"].push_back(party(3, "Nationals", "NAT", 0, 100, 0));
	natDoc["simulation_report"]["party_win_expectation"].push_back(outcome(3, 2.0));
	natDoc["simulation_report"]["coalition_win_expectation"] = 7.03;
	auto natDir = root / "seats-nat";
	std::filesystem::create_directories(natDir);
	writeJson(natDir / "snapshot_20260402143211__run_a.json", natDoc);
	auto seatNat = buildSeatExpectationView(
		loadDirectory(natDir, "Live Simulation", "seats-nat").records);
	assert(seatNat.columns[seatNat.columns.size() - 2].kind ==
		ParliamentColumn::Kind::Separator);
	assert(seatNat.columns.back().header == "Coalition Win Expectation");
	assert(seatNat.cells[0].back().text == "7.03");
	auto natGraph = makeSeatExpectationGraph(seatNat);
	assert(natGraph.series.size() == seatNat.columns.size() - 3);
	for (auto const& series : natGraph.series) {
		assert(series.label != "Coalition Win Expectation");
		assert(!series.label.empty());
	}

	seatDoc["simulation_report"]["party_win_median"] = json::array({
		outcome(0, 7),
		outcome(1, 2)
	});
	seatDoc["simulation_report"]["coalition_win_median"] = 8;
	seatDoc["simulation_report"]["party_seat_win_frequency"] = json::array({
		{{"party_index", 0}, {"values", json::array({1, 0, 2, 5, 2})}},
		{{"party_index", 1}, {"values", json::array({0, 3, 7})}}
	});
	seatDoc["simulation_report"]["coalition_seat_win_frequency"] = json::array({0, 0, 1, 0, 9});
	writeJson(seatOnlyDir / "snapshot_20260402143211__run_a.json", seatDoc);
	auto thresholdRecords = loadDirectory(
		seatOnlyDir, "Live Simulation", "seats-no-nat").records;
	auto thresholdParties = listSeatThresholdParties(thresholdRecords);
	std::vector<std::string> thresholdPartyLabels;
	for (auto const& option : thresholdParties) {
		thresholdPartyLabels.push_back(option.label);
	}
	assert(thresholdPartyLabels == std::vector<std::string>({
		"ALP", "LNP", "ONP", "OTH", "IND"
	}));
	for (auto const& option : thresholdParties) {
		assert(option.partyIndex != CoalitionPartnerIndex);
	}

	auto alpThresholds = buildSeatThresholdView(thresholdRecords, 0);
	std::vector<std::string> thresholdHeaders;
	for (auto const& column : alpThresholds.columns) {
		thresholdHeaders.push_back(column.header);
	}
	assert(thresholdHeaders == std::vector<std::string>({
		"Snapshot", "0.1%", "5%", "Median", "95%", "99.9%"
	}));
	assert(alpThresholds.columns[1].kind == ParliamentColumn::Kind::SeatThreshold);
	assert(alpThresholds.columns[1].outcome == ParliamentColumn::Outcome::MostSeats);
	assert(alpThresholds.columns[2].outcome == ParliamentColumn::Outcome::Minority);
	assert(alpThresholds.columns[3].outcome == ParliamentColumn::Outcome::Majority);
	assert(alpThresholds.columns[4].outcome == ParliamentColumn::Outcome::Minority);
	assert(alpThresholds.columns[5].outcome == ParliamentColumn::Outcome::MostSeats);
	assert(alpThresholds.cells[0][1].text == "0");
	assert(alpThresholds.cells[0][2].text == "0");
	assert(alpThresholds.cells[0][3].text == "7");
	assert(alpThresholds.cells[0][4].text == "4");
	assert(alpThresholds.cells[0][5].text == "4");

	auto alpGraph = makeSeatThresholdGraph(alpThresholds);
	assert(alpGraph.yIsPercent == false);
	assert(alpGraph.yIntegerLabels == true);
	assert(alpGraph.series.size() == 5);
	assert(alpGraph.series[0].label == "0.1%");
	assert(alpGraph.series[2].label == "Median");
	assert(alpGraph.series[2].colour.r == 255 && alpGraph.series[2].colour.g == 0);
	assert(alpGraph.series[1].colour.g > alpGraph.series[2].colour.g);
	assert(alpGraph.series[0].colour.g > alpGraph.series[1].colour.g);
	assert(alpGraph.series[0].colour.g == alpGraph.series[4].colour.g);
	assert(alpGraph.series[1].colour.g == alpGraph.series[3].colour.g);
	for (auto const& series : alpGraph.series) {
		assert(series.style == GraphLineStyle::Solid);
	}
	auto thresholdLayout = makeParliamentLayout(alpThresholds);
	assert(thresholdLayout.columnWidths[1] == TableMetrics{}.seatColumnWidth);
	auto thresholdGraphLayout = layoutParliamentGraph(alpGraph, 800, 400);
	assert(thresholdGraphLayout.yTicks.back().label.find('%') == std::string::npos);
	assert(thresholdGraphLayout.yTicks.back().label.find('.') == std::string::npos);

	auto lnpThresholds = buildSeatThresholdView(thresholdRecords, 1);
	assert(lnpThresholds.cells[0][1].text == "1");
	assert(lnpThresholds.cells[0][3].text == "2");

	natDoc["simulation_report"]["party_win_median"] =
		seatDoc["simulation_report"]["party_win_median"];
	natDoc["simulation_report"]["coalition_win_median"] = 8;
	natDoc["simulation_report"]["party_seat_win_frequency"] =
		seatDoc["simulation_report"]["party_seat_win_frequency"];
	natDoc["simulation_report"]["coalition_seat_win_frequency"] =
		seatDoc["simulation_report"]["coalition_seat_win_frequency"];
	writeJson(natDir / "snapshot_20260402143211__run_a.json", natDoc);
	auto natThresholdRecords = loadDirectory(
		natDir, "Live Simulation", "seats-nat").records;
	auto natThresholdParties = listSeatThresholdParties(natThresholdRecords);
	assert(natThresholdParties.back().partyIndex == CoalitionPartnerIndex);
	assert(natThresholdParties.back().label == "Coalition");
	auto coalitionThresholds = buildSeatThresholdView(
		natThresholdRecords, CoalitionPartnerIndex);
	assert(coalitionThresholds.cells[0][1].text == "2");
	assert(coalitionThresholds.cells[0][3].text == "8");
	assert(coalitionThresholds.cells[0][5].text == "4");

	auto missingMedian = seatDoc;
	missingMedian["simulation_report"].erase("party_win_median");
	auto missingMedianDir = root / "seats-missing-median";
	std::filesystem::create_directories(missingMedianDir);
	writeJson(missingMedianDir / "snapshot_20260402143211__run_a.json", missingMedian);
	auto fallbackMedian = buildSeatThresholdView(
		loadDirectory(missingMedianDir, "Live Simulation", "seats-missing-median").records,
		0);
	assert(fallbackMedian.cells[0][3].text == "3");

	auto tppDoc = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 100.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 100.0)}),
		0.0);
	tppDoc["simulation_report"]["tpp_frequency"] = json::array({
		{{"bin", 500}, {"count", 10}},
		{{"bin", 600}, {"count", 10}}
	});
	auto tppDir = root / "tpp";
	std::filesystem::create_directories(tppDir);
	writeJson(tppDir / "snapshot_20260402143211__run_a.json", tppDoc);
	auto tppView = buildTppView(
		loadDirectory(tppDir, "Live Simulation", "tpp").records);
	std::vector<std::string> tppHeaders;
	for (auto const& column : tppView.columns) {
		tppHeaders.push_back(column.header);
	}
	assert(tppHeaders == std::vector<std::string>({
		"Snapshot", "0.1%", "5%", "Median", "95%", "99.9%", "", "Mean"
	}));
	assert(tppView.columns[1].kind == ParliamentColumn::Kind::TppThreshold);
	assert(tppView.columns[1].outcome == ParliamentColumn::Outcome::MostSeats);
	assert(tppView.columns[3].outcome == ParliamentColumn::Outcome::Majority);
	assert(tppView.columns[6].kind == ParliamentColumn::Kind::Separator);
	assert(tppView.columns[7].kind == ParliamentColumn::Kind::TppMean);
	assert(tppView.columns[7].includeInGraph == false);
	assert(tppView.cells[0][1].kind == CellValue::Kind::Percent);
	assert(tppView.cells[0][1].text == formatPercent(50.0));
	assert(tppView.cells[0][2].text == formatPercent(50.01f));
	assert(tppView.cells[0][3].text == formatPercent(60.0));
	assert(tppView.cells[0][4].text == formatPercent(60.09f));
	assert(tppView.cells[0][5].text == formatPercent(60.09f));
	assert(tppView.cells[0][7].text == formatPercent(55.05f));
	assert(tppView.columns[1].colour && tppView.columns[1].colour->r == 255);

	auto tppGraph = makeTppGraph(tppView);
	assert(tppGraph.yIsPercent == true);
	assert(tppGraph.yFitToData == true);
	assert(tppGraph.yMin < 50.0);
	assert(tppGraph.yMax > 60.09);
	assert(tppGraph.yMin > 0.0);
	assert(tppGraph.yMax < 100.0);
	assert(tppGraph.series.size() == 5);
	assert(tppGraph.series[0].label == "0.1%");
	assert(tppGraph.series[2].label == "Median");
	assert(tppGraph.series[2].colour.r == 255 && tppGraph.series[2].colour.g == 0);
	assert(tppGraph.series[0].colour.g > tppGraph.series[1].colour.g);
	assert(tppGraph.series[1].colour.g > tppGraph.series[2].colour.g);
	for (auto const& series : tppGraph.series) {
		assert(series.label != "Mean");
		assert(series.style == GraphLineStyle::Solid);
	}
	auto tppGraphLayout = layoutParliamentGraph(tppGraph, 800, 400);
	assert(tppGraphLayout.yMin == tppGraph.yMin);
	assert(tppGraphLayout.yMax == tppGraph.yMax);
	assert(tppGraphLayout.yForValue(60.09) < tppGraphLayout.yForValue(50.0));
	assert(tppGraphLayout.yForValue(50.0) - tppGraphLayout.yForValue(60.09) >
		tppGraphLayout.plot.height * 2 / 5);
	assert(!tppGraphLayout.yTicks.empty());
	assert(tppGraphLayout.yTicks.front().label.find('%') != std::string::npos);
	assert(tppGraphLayout.yTicks.front().label != "0%");
	auto tppLayout = makeParliamentLayout(tppView);
	assert(tppLayout.columnWidths[6] == TableMetrics{}.separatorColumnWidth);
	assert(tppLayout.columnWidths[7] == TableMetrics{}.seatColumnWidth);

	auto voteDoc = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 100.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 100.0)}),
		0.0);
	voteDoc["simulation_report"]["party_primary_frequency"] = json::array({
		{{"party_index", 0}, {"bins", json::array({
			{{"bin", 400}, {"count", 10}}
		})}},
		{{"party_index", 1}, {"bins", json::array({
			{{"bin", 350}, {"count", 10}}
		})}}
	});
	auto voteDir = root / "votes";
	std::filesystem::create_directories(voteDir);
	writeJson(voteDir / "snapshot_20260402143211__run_a.json", voteDoc);
	auto voteView = buildVoteShareView(
		loadDirectory(voteDir, "Live Simulation", "votes").records);
	std::vector<std::string> voteHeaders;
	for (auto const& column : voteView.columns) {
		voteHeaders.push_back(column.header);
	}
	assert(voteHeaders == std::vector<std::string>({"Snapshot", "ALP", "LNP"}));
	assert(voteView.columns[1].kind == ParliamentColumn::Kind::PartyVoteShare);
	assert(voteView.cells[0][1].kind == CellValue::Kind::Percent);
	assert(voteView.cells[0][1].text == formatPercent(40.05f));
	assert(voteView.cells[0][2].text == formatPercent(35.05f));
	auto voteGraph = makeVoteShareGraph(voteView);
	assert(voteGraph.yIsPercent == true);
	assert(voteGraph.yFitToData == true);
	assert(voteGraph.yMin == 0.0);
	assert(voteGraph.yMax >= 40.05);
	assert(voteGraph.yMax < 100.0);
	assert(voteGraph.series.size() == 2);
	assert(voteGraph.series[0].label == "ALP");
	assert(voteGraph.series[0].style == GraphLineStyle::Solid);
	assert(voteGraph.series[0].colour.r == 255);

	auto voteNatDoc = voteDoc;
	voteNatDoc["parties"].push_back(party(3, "Nationals", "NAT", 0, 100, 0));
	voteNatDoc["simulation_report"]["party_primary_frequency"].push_back({
		{"party_index", 3},
		{"bins", json::array({{{"bin", 80}, {"count", 10}}})}
	});
	voteNatDoc["simulation_report"]["coalition_fp_frequency"] = json::array({
		{{"bin", 430}, {"count", 10}}
	});
	auto voteNatDir = root / "votes-nat";
	std::filesystem::create_directories(voteNatDir);
	writeJson(voteNatDir / "snapshot_20260402143211__run_a.json", voteNatDoc);
	auto voteNat = buildVoteShareView(
		loadDirectory(voteNatDir, "Live Simulation", "votes-nat").records);
	assert(voteNat.columns[voteNat.columns.size() - 2].kind ==
		ParliamentColumn::Kind::Separator);
	assert(voteNat.columns.back().kind == ParliamentColumn::Kind::CoalitionVoteShare);
	assert(voteNat.columns.back().header == "Coalition");
	assert(voteNat.columns.back().includeInGraph == false);
	assert(voteNat.cells[0].back().text == formatPercent(43.05f));
	auto voteNatGraph = makeVoteShareGraph(voteNat);
	assert(voteNatGraph.series.size() == 3);
	for (auto const& series : voteNatGraph.series) {
		assert(series.label != "Coalition");
	}

	assert(isCalledSeatWin(99.996));
	assert(isCalledSeatWin(100.0));
	assert(!isCalledSeatWin(99.995));
	assert(!isCalledSeatWin(50.0));
	assert(updateSeatWinCalled(false, 99.996));
	assert(!updateSeatWinCalled(false, 99.5));
	assert(updateSeatWinCalled(true, 99.5));
	assert(updateSeatWinCalled(true, 99.0));
	assert(!updateSeatWinCalled(true, 98.9));
	assert(!updateSeatWinCalled(true, 50.0));
	assert(updateSeatWinCalled(true, 99.996));

	PartyColour const alpRed{255, 0, 0};
	CellValue chanceCell;
	chanceCell.kind = CellValue::Kind::Percent;
	chanceCell.percent = 0.0;
	auto const whiteChance = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance);
	assert(whiteChance.background.r == 255);
	assert(whiteChance.background.g == 255);
	assert(!whiteChance.whiteText);

	chanceCell.percent = 50.0;
	auto const halfChance = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance);
	assert(halfChance.background.r == 255);
	assert(std::abs(halfChance.background.g - 166) <= 1);
	assert(!halfChance.whiteText);

	chanceCell.percent = 99.99;
	auto const almostCalled = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance);
	assert(!almostCalled.whiteText);
	assert(almostCalled.background.r > 250);
	assert(almostCalled.background.g > 60);
	assert(almostCalled.background.g < 90);

	chanceCell.percent = 99.996;
	auto const called = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance, true);
	assert(called.whiteText);
	assert(called.background.r < 130);
	assert(called.background.r < alpRed.r);

	chanceCell.percent = 99.5;
	auto const stillCalled = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance, true);
	assert(stillCalled.whiteText);
	auto const notYetCalled = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance, false);
	assert(!notYetCalled.whiteText);

	chanceCell.percent = 98.9;
	auto const uncalled = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance, true);
	assert(uncalled.whiteText);
	auto const uncalledAfterDrop = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		updateSeatWinCalled(true, 98.9));
	assert(!uncalledAfterDrop.whiteText);

	std::vector<std::vector<CellValue>> callSeries(4, std::vector<CellValue>(2));
	for (auto& row : callSeries) {
		row[0].kind = CellValue::Kind::Text;
		row[1].kind = CellValue::Kind::Percent;
	}
	callSeries[0][1].percent = 80.0;
	callSeries[1][1].percent = 99.996;
	callSeries[2][1].percent = 99.4;
	callSeries[3][1].percent = 98.5;
	assert(!seatWinCalledUpToRow(callSeries, 0, 1));
	assert(seatWinCalledUpToRow(callSeries, 1, 1));
	assert(seatWinCalledUpToRow(callSeries, 2, 1));
	assert(!seatWinCalledUpToRow(callSeries, 3, 1));

	chanceCell.percent = 60.0;
	auto const firstChange = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::Change);
	assert(firstChange.background.r == 255);
	assert(firstChange.background.g == 255);
	assert(!firstChange.whiteText);

	auto const upTen = seatWinCellStyle(
		alpRed, chanceCell, 50.0, SeatWinShading::Change);
	assert(std::abs(upTen.background.r - 128) <= 1);
	assert(std::abs(upTen.background.g - 208) <= 1);
	assert(upTen.background.g > upTen.background.r);

	CellValue lowCell;
	lowCell.kind = CellValue::Kind::Percent;
	lowCell.percent = 15.0;
	CellValue midCell = lowCell;
	midCell.percent = 55.0;
	CellValue highCell = lowCell;
	highCell.percent = 95.0;
	auto const fromFive = seatWinCellStyle(
		alpRed, lowCell, 5.0, SeatWinShading::Change);
	auto const fromFortyFive = seatWinCellStyle(
		alpRed, midCell, 45.0, SeatWinShading::Change);
	auto const fromEightyFive = seatWinCellStyle(
		alpRed, highCell, 85.0, SeatWinShading::Change);
	assert(fromFive.background.r == fromFortyFive.background.r);
	assert(fromFive.background.g == fromFortyFive.background.g);
	assert(fromFive.background.b == fromFortyFive.background.b);
	assert(fromFive.background.r == fromEightyFive.background.r);
	assert(fromFive.background.g == fromEightyFive.background.g);
	assert(fromFive.background.b == fromEightyFive.background.b);

	CellValue doubled;
	doubled.kind = CellValue::Kind::Percent;
	doubled.percent = 18.0;
	auto const nineToEighteen = seatWinCellStyle(
		alpRed, doubled, 9.0, SeatWinShading::Change);
	CellValue plusNine;
	plusNine.kind = CellValue::Kind::Percent;
	plusNine.percent = 59.0;
	auto const fiftyToFiftyNine = seatWinCellStyle(
		alpRed, plusNine, 50.0, SeatWinShading::Change);
	assert(nineToEighteen.background.r == fiftyToFiftyNine.background.r);
	assert(nineToEighteen.background.g == fiftyToFiftyNine.background.g);
	assert(nineToEighteen.background.r != 0 || nineToEighteen.background.g != 160);

	auto const downTen = seatWinCellStyle(
		alpRed, chanceCell, 70.0, SeatWinShading::Change);
	assert(downTen.background.r > downTen.background.g);
	assert(std::abs(downTen.background.r - 228) <= 1);

	auto const upFull = seatWinCellStyle(
		alpRed, chanceCell, -40.0, SeatWinShading::Change);
	assert(upFull.background.r == 0);
	assert(upFull.background.g == 160);

	chanceCell.percent = 99.996;
	auto const calledOnChange = seatWinCellStyle(
		alpRed, chanceCell, 90.0, SeatWinShading::Change, true);
	assert(!calledOnChange.whiteText);
	assert(calledOnChange.background.g > calledOnChange.background.r);
	auto const calledChangeFirst = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::Change, true);
	assert(!calledChangeFirst.whiteText);
	assert(calledChangeFirst.background.r == 255);
	assert(calledChangeFirst.background.g == 255);

	auto const fpSettings = seatFpShadeSettings();
	chanceCell.percent = 0.0;
	auto const fpZero = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		false, fpSettings);
	assert(fpZero.background.r == 255);
	assert(fpZero.background.g == 255);
	assert(!fpZero.whiteText);

	chanceCell.percent = 25.0;
	auto const fpHalf = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		false, fpSettings);
	assert(fpHalf.background.r == 255);
	assert(std::abs(fpHalf.background.g - 166) <= 1);

	chanceCell.percent = 50.0;
	auto const fpFull = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		false, fpSettings);
	chanceCell.percent = 80.0;
	auto const fpCapped = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		false, fpSettings);
	assert(fpFull.background.r == fpCapped.background.r);
	assert(fpFull.background.g == fpCapped.background.g);
	assert(std::abs(fpFull.background.g - 76) <= 1);

	chanceCell.percent = 99.996;
	auto const fpIgnoresCalled = seatWinCellStyle(
		alpRed, chanceCell, std::nullopt, SeatWinShading::CurrentChance,
		true, fpSettings);
	assert(!fpIgnoresCalled.whiteText);

	CellValue fpLow;
	fpLow.kind = CellValue::Kind::Percent;
	fpLow.percent = 7.0;
	CellValue fpMid = fpLow;
	fpMid.percent = 47.0;
	auto const fpFromFive = seatWinCellStyle(
		alpRed, fpLow, 5.0, SeatWinShading::Change, false, fpSettings);
	auto const fpFromFortyFive = seatWinCellStyle(
		alpRed, fpMid, 45.0, SeatWinShading::Change, false, fpSettings);
	assert(fpFromFive.background.r == fpFromFortyFive.background.r);
	assert(fpFromFive.background.g == fpFromFortyFive.background.g);
	chanceCell.percent = 46.0;
	auto const fpFourPoint = seatWinCellStyle(
		alpRed, chanceCell, 42.0, SeatWinShading::Change, false, fpSettings);
	assert(fpFourPoint.background.r == 0);
	assert(fpFourPoint.background.g == 160);

	auto winEarly = snapshotDocument(
		"20260401100000",
		"2026-09-04T10:00:00+10:00",
		json::array({outcome(0, 80.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 80.0)}),
		0.0);
	winEarly["parties"].push_back(party(-2, "Emerging Ind", "IND", 128, 128, 128));
	winEarly["parties"].push_back(party(6, "Independent", "IND", 160, 160, 160));
	winEarly["simulation_report"]["seat_name"] = json::array({"Adelaide", "Frome"});
	winEarly["simulation_report"]["seat_party_win_percent"] = json::array({
		json::array({
			outcome(0, 80.0), outcome(1, 20.0),
			outcome(-2, 1.0), outcome(6, 2.0)
		}),
		json::array({outcome(0, 40.0), outcome(1, 60.0)})
	});
	auto winLate = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 90.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 90.0)}),
		0.0);
	winLate["parties"].push_back(party(-2, "Emerging Ind", "IND", 128, 128, 128));
	winLate["parties"].push_back(party(6, "Independent", "IND", 160, 160, 160));
	winLate["simulation_report"]["seat_name"] = json::array({"Adelaide", "Frome", "Florey"});
	winLate["simulation_report"]["seat_party_win_percent"] = json::array({
		json::array({outcome(0, 90.0), outcome(1, 10.0)}),
		json::array({outcome(0, 40.0), outcome(1, 60.0)}),
		json::array({outcome(0, 99.996), outcome(1, 0.004)})
	});
	winEarly["simulation_report"]["seat_party_mean_fp_share"] = json::array({
		json::array({
			outcome(0, 42.0), outcome(1, 38.0),
			outcome(-2, 4.0), outcome(6, 3.0)
		}),
		json::array({outcome(0, 35.0), outcome(1, 45.0)})
	});
	winLate["simulation_report"]["seat_party_mean_fp_share"] = json::array({
		json::array({outcome(0, 44.0), outcome(1, 36.0)}),
		json::array({outcome(0, 35.0), outcome(1, 45.0)}),
		json::array({outcome(0, 51.0), outcome(1, 22.0)})
	});
	winEarly["simulation_report"]["seat_fp_completion"] = json::array({0.0, 0.2});
	winLate["simulation_report"]["seat_fp_completion"] = json::array({0.15, 0.2, 0.5});
	winEarly["simulation_report"]["seat_tpp_completion"] = json::array({0.0, 0.00005});
	winLate["simulation_report"]["seat_tpp_completion"] = json::array({0.0002, 0.00005, 1.0});
	winLate["simulation_report"]["seat_tcp_completion"] = json::array({1.0, 0.5, 0.25});
	auto winDir = root / "seat-win";
	std::filesystem::create_directories(winDir);
	writeJson(winDir / "snapshot_20260401100000__run_a.json", winEarly);
	writeJson(winDir / "snapshot_20260402143211__run_b.json", winLate);
	auto winRecords = loadDirectory(winDir, "Live Simulation", "seat-win").records;
	auto winParties = listSeatWinChanceParties(winRecords);
	std::vector<std::string> winPartyLabels;
	for (auto const& option : winParties) {
		winPartyLabels.push_back(option.label);
	}
	assert(winPartyLabels == std::vector<std::string>({"ALP", "LNP", "IND*", "IND"}));
	auto winSeats = listSeatWinChanceSeats(winRecords);
	assert(winSeats == std::vector<std::string>({"Adelaide", "Frome", "Florey"}));

	auto alpWins = buildSeatWinChanceView(winRecords, 0);
	std::vector<std::string> winHeaders;
	for (auto const& column : alpWins.columns) {
		winHeaders.push_back(column.header);
	}
	assert(winHeaders == std::vector<std::string>({
		"Snapshot", "Adelaide", "Frome", "Florey"
	}));
	assert(alpWins.columns[1].kind == ParliamentColumn::Kind::SeatWinChance);
	assert(alpWins.columns[1].colour && alpWins.columns[1].colour->r == 255);
	assert(alpWins.cells[0][1].text == formatPercent(80.0));
	assert(alpWins.cells[1][1].text == formatPercent(90.0));
	assert(alpWins.cells[0][3].text == formatPercent(0.0));
	assert(alpWins.cells[1][3].text == formatPercent(99.996));
	auto winLayout = makeParliamentLayout(alpWins);
	winLayout.frozenColumns = 1;
	assert(winLayout.columnWidths[1] == TableMetrics{}.seatNameColumnWidth);
	assert(winLayout.headerRect(0, 120).x == 0);
	assert(winLayout.headerRect(1, 120).x == winLayout.columnLeft(1) - 120);

	auto adelaideAll = buildSeatWinChanceGraphView(winRecords, "Adelaide", std::nullopt);
	assert(adelaideAll.columns.size() == 5);
	assert(adelaideAll.columns[1].header == "ALP");
	assert(adelaideAll.columns[2].header == "LNP");
	assert(adelaideAll.columns[3].header == "IND*");
	assert(adelaideAll.columns[4].header == "IND");
	assert(adelaideAll.cells[0][1].percent == 80.0);
	assert(adelaideAll.cells[0][2].percent == 20.0);
	auto allGraph = makeSeatWinChanceGraph(adelaideAll, false);
	assert(allGraph.yIsPercent);
	assert(!allGraph.yFitToData);
	assert(allGraph.yMin == 0.0);
	assert(allGraph.yMax == 100.0);
	assert(allGraph.series.size() == 4);
	assert(allGraph.series[0].label == "ALP");
	assert(allGraph.series[2].label == "IND*");
	assert(allGraph.series[0].style == GraphLineStyle::Solid);

	auto adelaideAlp = buildSeatWinChanceGraphView(winRecords, "Adelaide", 0);
	assert(adelaideAlp.columns.size() == 2);
	assert(adelaideAlp.cells[0][1].percent == 80.0);
	assert(adelaideAlp.cells[1][1].percent == 90.0);
	auto alpGraphFit = makeSeatWinChanceGraph(adelaideAlp, true);
	assert(alpGraphFit.yFitToData);
	assert(alpGraphFit.series.size() == 1);
	assert(alpGraphFit.yMax - alpGraphFit.yMin >= 10.0);
	assert(alpGraphFit.yMin > 0.0);
	assert(alpGraphFit.yMax < 100.0);
	assert(alpGraphFit.yMin <= 80.0);
	assert(alpGraphFit.yMax >= 90.0);

	auto fromeLnp = buildSeatWinChanceGraphView(winRecords, "Frome", 1);
	assert(fromeLnp.cells[0][1].percent == 60.0);
	assert(fromeLnp.cells[1][1].percent == 60.0);
	auto fromeGraph = makeSeatWinChanceGraph(fromeLnp, true);
	assert(fromeGraph.yMax - fromeGraph.yMin >= 10.0);

	auto fpParties = listSeatFpParties(winRecords);
	std::vector<std::string> fpPartyLabels;
	for (auto const& option : fpParties) {
		fpPartyLabels.push_back(option.label);
	}
	assert(fpPartyLabels == std::vector<std::string>({"ALP", "LNP", "IND*", "IND"}));

	auto alpFp = buildSeatFpView(winRecords, 0);
	assert(alpFp.columns[1].kind == ParliamentColumn::Kind::SeatFp);
	assert(alpFp.cells[0][1].text == formatPercent(42.0));
	assert(alpFp.cells[1][1].text == formatPercent(44.0));
	assert(alpFp.cells[1][3].text == formatPercent(51.0));
	auto fpLayout = makeParliamentLayout(alpFp);
	fpLayout.frozenColumns = 1;
	assert(fpLayout.columnWidths[1] == TableMetrics{}.seatNameColumnWidth);

	auto adelaideFpAll = buildSeatFpGraphView(winRecords, "Adelaide", std::nullopt);
	assert(adelaideFpAll.columns[3].header == "IND*");
	assert(adelaideFpAll.cells[0][1].percent == 42.0);
	auto fpAllGraph = makeSeatFpGraph(adelaideFpAll);
	assert(fpAllGraph.yIsPercent);
	assert(fpAllGraph.yFitToData);
	assert(fpAllGraph.yMax - fpAllGraph.yMin >= 5.0);
	assert(fpAllGraph.yMin <= 36.0);
	assert(fpAllGraph.yMax >= 44.0);

	auto adelaideAlpFp = buildSeatFpGraphView(winRecords, "Adelaide", 0);
	assert(adelaideAlpFp.cells[0][1].percent == 42.0);
	assert(adelaideAlpFp.cells[1][1].percent == 44.0);
	auto alpFpGraph = makeSeatFpGraph(adelaideAlpFp);
	assert(alpFpGraph.yFitToData);
	assert(alpFpGraph.series.size() == 1);
	assert(alpFpGraph.yMax - alpFpGraph.yMin >= 5.0);
	assert(alpFpGraph.yMin <= 42.0);
	assert(alpFpGraph.yMax >= 44.0);

	LiveSnapshot::CellValue completionZero;
	completionZero.kind = LiveSnapshot::CellValue::Kind::Value;
	completionZero.percent = 0.0;
	auto styleZero = completionCellStyle(completionZero, std::nullopt);
	assert(styleZero.background.r == 255);
	assert(styleZero.background.g == 255);
	assert(styleZero.background.b == 255);
	assert(styleZero.changeBorder == CompletionChangeBorder::None);

	LiveSnapshot::CellValue completionOne;
	completionOne.kind = LiveSnapshot::CellValue::Kind::Value;
	completionOne.percent = 1.0;
	auto styleOne = completionCellStyle(completionOne, std::nullopt);
	assert(styleOne.background.r == 128);
	assert(styleOne.background.g == 128);
	assert(styleOne.background.b == 128);
	assert(styleOne.changeBorder == CompletionChangeBorder::None);

	LiveSnapshot::CellValue completionLater;
	completionLater.kind = LiveSnapshot::CellValue::Kind::Value;
	completionLater.percent = 0.0002;
	assert(completionCellStyle(completionLater, 0.0).changeBorder ==
		CompletionChangeBorder::Increase);
	assert(completionCellStyle(completionLater, 0.00015).changeBorder ==
		CompletionChangeBorder::None);
	assert(completionCellStyle(completionLater, 0.0001).changeBorder ==
		CompletionChangeBorder::None);
	assert(completionCellStyle(completionZero, 0.15).changeBorder ==
		CompletionChangeBorder::Decrease);

	auto fpCounted = buildSeatCompletionView(winRecords, "seat_fp_completion");
	assert(fpCounted.columns.size() == 4);
	assert(fpCounted.columns[1].kind == ParliamentColumn::Kind::SeatCompletion);
	assert(fpCounted.columns[1].header == "Adelaide");
	assert(fpCounted.cells[0][1].percent == 0.0);
	assert(fpCounted.cells[0][1].text == formatPercent(0.0));
	assert(fpCounted.cells[1][1].percent == 0.15);
	assert(fpCounted.cells[1][1].text == formatPercent(15.0));
	assert(fpCounted.cells[0][2].percent == 0.2);
	assert(fpCounted.cells[1][2].percent == 0.2);
	assert(fpCounted.cells[1][3].percent == 0.5);
	auto fpCountedLayout = makeParliamentLayout(fpCounted);
	fpCountedLayout.frozenColumns = 1;
	assert(fpCountedLayout.columnWidths[1] == TableMetrics{}.seatNameColumnWidth);
	assert(completionCellStyle(fpCounted.cells[1][1], fpCounted.cells[0][1].percent).changeBorder ==
		CompletionChangeBorder::Increase);
	assert(completionCellStyle(fpCounted.cells[1][2], fpCounted.cells[0][2].percent).changeBorder ==
		CompletionChangeBorder::None);

	auto tppBasis = buildSeatCompletionView(winRecords, "seat_tpp_completion");
	assert(tppBasis.cells[0][1].percent == 0.0);
	assert(tppBasis.cells[1][1].percent == 0.0002);
	assert(completionCellStyle(tppBasis.cells[1][1], tppBasis.cells[0][1].percent).changeBorder ==
		CompletionChangeBorder::Increase);
	assert(completionCellStyle(tppBasis.cells[1][2], tppBasis.cells[0][2].percent).changeBorder ==
		CompletionChangeBorder::None);
	assert(tppBasis.cells[1][3].percent == 1.0);
	assert(completionCellStyle(tppBasis.cells[1][3], std::nullopt).background.r == 128);
	assert(completionCellStyle(tppBasis.cells[1][3], tppBasis.cells[0][3].percent).changeBorder ==
		CompletionChangeBorder::Increase);

	auto tcpBasis = buildSeatCompletionView(winRecords, "seat_tcp_completion");
	assert(tcpBasis.cells[0][1].percent == 0.0);
	assert(tcpBasis.cells[1][1].percent == 1.0);
	assert(tcpBasis.cells[0][1].text == formatPercent(0.0));
	assert(tcpBasis.cells[1][1].text == formatPercent(100.0));

	auto adelaideFpGraphView = buildSeatCompletionGraphView(
		winRecords, "Adelaide", "seat_fp_completion");
	assert(adelaideFpGraphView.columns.size() == 2);
	assert(adelaideFpGraphView.cells[0][1].percent == 0.0);
	assert(adelaideFpGraphView.cells[1][1].percent == 0.15);
	auto completionGraph = makeCompletionGraph(adelaideFpGraphView);
	assert(completionGraph.yIsPercent);
	assert(!completionGraph.yFitToData);
	assert(completionGraph.yMin == 0.0);
	assert(completionGraph.yMax == 100.0);
	assert(completionGraph.series.size() == 1);
	assert(completionGraph.series[0].values[0] == 0.0);
	assert(completionGraph.series[0].values[1] == 15.0);

	auto biasEarly = snapshotDocument(
		"20260402140000",
		"2026-09-04T19:40:00+10:00",
		json::array({outcome(0, 80.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 80.0)}),
		0.0);
	biasEarly["live_analysis_summary"]["booth_type"] = json::array();
	biasEarly["live_analysis_summary"]["vote_type"] = json::array({
		categoryEvidence("Absent", 0.01, 0.04, 0.02, 2.0, 100.0),
		categoryEvidence("Postal", -0.50, 0.10, -0.40, 3.0, 200.0)
	});
	biasEarly["live_analysis_summary"]["projected_2pp"] = 54.5;
	biasEarly["live_analysis_summary"]["raw_2pp_deviation"] = -1.8;
	auto biasLate = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 80.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 80.0)}),
		0.0);
	biasLate["live_analysis_summary"]["booth_type"] = json::array({
		categoryEvidence("PPVC", 0.03, 0.05, 0.04, 8.0, 12000.0)
	});
	biasLate["live_analysis_summary"]["vote_type"] = json::array({
		categoryEvidence("Absent", 0.02, 0.04, 0.03, 4.0, 150.0),
		categoryEvidence("Postal", -0.20, 0.08, -0.10, 5.0, 250.0),
		categoryEvidence("Early", 0.07, 0.06, 0.01, 1.0, 50.0)
	});
	biasLate["live_analysis_summary"]["projected_2pp"] = 56.25;
	biasLate["live_analysis_summary"]["raw_2pp_deviation"] = -0.5;
	auto biasDir = root / "category-bias";
	std::filesystem::create_directories(biasDir);
	writeJson(biasDir / "snapshot_20260402140000__run_a.json", biasEarly);
	writeJson(biasDir / "snapshot_20260402143211__run_b.json", biasLate);
	auto biasRecords = loadDirectory(
		biasDir, "Live Simulation", "category-bias").records;

	auto boothNames = listCategoryNames(biasRecords, "booth_type");
	assert(boothNames == std::vector<std::string>({"PPVC"}));
	auto voteNames = listCategoryNames(biasRecords, "vote_type");
	assert(voteNames == std::vector<std::string>({"Absent", "Postal", "Early"}));

	auto boothByType = buildCategoryStatTableView(
		biasRecords, "booth_type", CategoryStatGrouping::ByCategory);
	assert(boothByType.columns.size() == 6);
	assert(boothByType.columns[1].kind == ParliamentColumn::Kind::CategoryStat);
	assert(boothByType.columns[1].header == "PPVC Bias");
	assert(boothByType.columns[1].category == "PPVC");
	assert(boothByType.columns[5].header == "PPVC Votes");
	assert(boothByType.cells[0][1].percent == 0.0);
	assert(boothByType.cells[1][1].percent == 0.03);
	assert(boothByType.cells[1][1].text == formatSeatExpectation(0.03));
	assert(boothByType.cells[1][4].text == formatSeatCount(8.0));
	assert(boothByType.cells[1][5].text == formatSeatCount(12000.0));
	auto boothLayout = makeParliamentLayout(boothByType);
	assert(boothLayout.columnWidths[1] == TableMetrics{}.categoryStatColumnWidth);

	auto voteByType = buildCategoryStatTableView(
		biasRecords, "vote_type", CategoryStatGrouping::ByCategory);
	assert(voteByType.columns[1].header == "Absent Bias");
	assert(voteByType.columns[6].kind == ParliamentColumn::Kind::Separator);
	assert(voteByType.columns[7].header == "Postal Bias");
	assert(voteByType.columns[12].kind == ParliamentColumn::Kind::Separator);
	assert(voteByType.columns[13].header == "Early Bias");
	assert(voteByType.cells[0][1].percent == 0.01);
	assert(voteByType.cells[0][7].percent == -0.50);
	assert(voteByType.cells[0][13].percent == 0.0);
	assert(voteByType.cells[1][13].percent == 0.07);
	assert(voteByType.cells[1][7].percent == -0.20);
	auto voteLayout = makeParliamentLayout(voteByType);
	assert(voteLayout.columnWidths[6] == TableMetrics{}.separatorColumnWidth);
	voteLayout.frozenColumns = 1;
	assert(voteLayout.frozenWidth() == TableMetrics{}.snapshotColumnWidth);

	auto voteByStat = buildCategoryStatTableView(
		biasRecords, "vote_type", CategoryStatGrouping::ByStatistic);
	assert(voteByStat.columns[1].header == "Bias Absent");
	assert(voteByStat.columns[2].header == "Bias Postal");
	assert(voteByStat.columns[3].header == "Bias Early");
	assert(voteByStat.columns[4].kind == ParliamentColumn::Kind::Separator);
	assert(voteByStat.columns[5].header == "StdDev Absent");
	assert(voteByStat.cells[0][1].percent == 0.01);
	assert(voteByStat.cells[0][3].percent == 0.0);
	assert(voteByStat.cells[1][3].percent == 0.07);

	auto voteBiasGraphView = buildCategoryStatGraphView(
		biasRecords, "vote_type", CategoryStatistic::Bias);
	assert(voteBiasGraphView.columns.size() == 4);
	assert(voteBiasGraphView.columns[1].header == "Absent");
	assert(voteBiasGraphView.columns[2].header == "Postal");
	assert(voteBiasGraphView.columns[3].header == "Early");
	assert(voteBiasGraphView.columns[1].includeInGraph);
	auto voteBiasGraph = makeCategoryStatGraph(voteBiasGraphView, false);
	assert(!voteBiasGraph.yIsPercent);
	assert(voteBiasGraph.yFitToData);
	assert(voteBiasGraph.series.size() == 3);
	assert(voteBiasGraph.series[0].label == "Absent");
	assert(voteBiasGraph.series[1].values[0] == -0.50);
	assert(voteBiasGraph.yMin < 0.0);
	assert(voteBiasGraph.series[0].colour.r != voteBiasGraph.series[1].colour.r ||
		voteBiasGraph.series[0].colour.g != voteBiasGraph.series[1].colour.g ||
		voteBiasGraph.series[0].colour.b != voteBiasGraph.series[1].colour.b);
	auto voteBiasGraphLayout = layoutParliamentGraph(voteBiasGraph, 800, 400);
	assert(voteBiasGraphLayout.yMin == voteBiasGraph.yMin);
	assert(!voteBiasGraphLayout.yTicks.empty());
	assert(voteBiasGraphLayout.yTicks.front().value == voteBiasGraph.yMin);

	GraphModel rawAxis;
	rawAxis.yIsPercent = false;
	rawAxis.yFitToData = true;
	rawAxis.yMin = -6.079;
	rawAxis.yMax = 4.7;
	rawAxis.xLabels = {"a", "b"};
	auto rawAxisLayout = layoutParliamentGraph(rawAxis, 800, 400);
	assert(rawAxisLayout.yTicks.front().value == rawAxis.yMin);
	assert(rawAxisLayout.yTicks.front().value > -7.0);
	assert(rawAxisLayout.yTicks.front().label != "-10.00");
	assert(rawAxisLayout.yTicks.front().label.find("-6.") == 0);
	assert(rawAxisLayout.yTicks.back().value == rawAxis.yMax);
	bool hasMinusFive = false;
	for (auto const& tick : rawAxisLayout.yTicks) {
		if (std::abs(tick.value + 5.0) < 1e-9) hasMinusFive = true;
		assert(tick.value + 1e-9 >= rawAxis.yMin);
		assert(tick.value - 1e-9 <= rawAxis.yMax);
	}
	assert(hasMinusFive);

	auto voteCountGraphView = buildCategoryStatGraphView(
		biasRecords, "vote_type", CategoryStatistic::VoteCount);
	auto voteCountGraph = makeCategoryStatGraph(voteCountGraphView, true);
	assert(voteCountGraph.yIntegerLabels);
	assert(voteCountGraph.yMin == 0.0);
	assert(voteCountGraph.yMax >= 250.0);
	assert(voteCountGraph.series[0].values[0] == 100.0);
	assert(voteCountGraph.series[2].values[0] == 0.0);
	assert(voteCountGraph.series[2].values[1] == 50.0);

	auto internal2pp = buildInternal2ppView(biasRecords);
	assert(internal2pp.columns.size() == 3);
	assert(internal2pp.columns[0].kind == ParliamentColumn::Kind::Snapshot);
	assert(internal2pp.columns[1].kind == ParliamentColumn::Kind::Internal2pp);
	assert(internal2pp.columns[1].header == "Internal projected 2PP");
	assert(internal2pp.columns[2].header == "Raw 2PP deviation");
	assert(internal2pp.columns[1].includeInGraph);
	assert(internal2pp.columns[2].includeInGraph);
	assert(internal2pp.rows.size() == 2);
	assert(internal2pp.cells[0][1].percent == 54.5);
	assert(internal2pp.cells[0][1].text == formatSeatExpectation(54.5));
	assert(internal2pp.cells[0][2].percent == -1.8);
	assert(internal2pp.cells[1][1].percent == 56.25);
	assert(internal2pp.cells[1][2].percent == -0.5);
	assert(internal2pp.columns[1].colour);
	assert(internal2pp.columns[2].colour);
	assert(internal2pp.columns[1].colour->r != internal2pp.columns[2].colour->r ||
		internal2pp.columns[1].colour->g != internal2pp.columns[2].colour->g ||
		internal2pp.columns[1].colour->b != internal2pp.columns[2].colour->b);
	auto internalLayout = makeParliamentLayout(internal2pp);
	assert(internalLayout.columnWidths[1] == TableMetrics{}.internal2ppColumnWidth);
	assert(internalLayout.columnWidths[2] == TableMetrics{}.internal2ppColumnWidth);
	auto internalGraph = makeInternal2ppGraph(internal2pp);
	assert(!internalGraph.yIsPercent);
	assert(internalGraph.yFitToData);
	assert(internalGraph.series.size() == 2);
	assert(internalGraph.series[0].label == "Internal projected 2PP");
	assert(internalGraph.series[1].label == "Raw 2PP deviation");
	assert(internalGraph.series[0].values[0] == 54.5);
	assert(internalGraph.series[1].values[0] == -1.8);
	assert(internalGraph.series[0].values[1] == 56.25);
	assert(internalGraph.series[1].values[1] == -0.5);
	assert(internalGraph.yMin < 0.0);
	assert(internalGraph.series[0].colour.r != internalGraph.series[1].colour.r ||
		internalGraph.series[0].colour.g != internalGraph.series[1].colour.g ||
		internalGraph.series[0].colour.b != internalGraph.series[1].colour.b);

	auto compactDir = root / "compact-summary";
	std::filesystem::create_directories(compactDir);
	auto compactDoc = snapshotDocument(
		"20260402143211",
		"2026-09-04T19:48:52+10:00",
		json::array({outcome(0, 80.0)}),
		json::array({outcome(0, 0.0)}),
		json::array({outcome(0, 80.0)}),
		0.0);
	compactDoc.erase("live_analysis");
	compactDoc["simulation_report"]["seat_name"] = json::array({"Adelaide"});
	writeJson(compactDir / "snapshot_20260402143211__run_a.json", compactDoc);
	auto compactLoaded = loadDirectory(
		compactDir, "Live Simulation", "compact-summary");
	assert(compactLoaded.records.size() == 1);
	assert(!compactLoaded.records[0].document.contains("live_analysis"));
	auto compactSummary = inspectorSummary(compactLoaded.records[0]);
	assert(compactSummary);
	assert(compactSummary->boothCount == 1);
	assert(compactSummary->firstBoothName == "Adelaide");

	auto omittedCompletion = buildSeatCompletionView(
		compactLoaded.records, "seat_fp_completion");
	assert(omittedCompletion.columns.size() == 2);
	assert(omittedCompletion.cells[0][1].percent == 0.0);
	assert(omittedCompletion.cells[0][1].text == formatPercent(0.0));
	assert(completionCellStyle(omittedCompletion.cells[0][1], std::nullopt).changeBorder ==
		CompletionChangeBorder::None);

	auto omittedInternal = buildInternal2ppView(compactLoaded.records);
	assert(omittedInternal.columns.size() == 3);
	assert(omittedInternal.cells[0][1].percent == 0.0);
	assert(omittedInternal.cells[0][1].text == formatSeatExpectation(0.0));
	assert(omittedInternal.cells[0][2].percent == 0.0);

	std::error_code cleanupError;
	std::filesystem::remove_all(root, cleanupError);
	std::cout << "Live snapshot data tests passed\n";
}
