#pragma once

#include "LiveSnapshotData.h"
#include "LiveSnapshotTable.h"

#include <optional>
#include <string>
#include <vector>

namespace LiveSnapshot {

enum class GraphLineStyle { Solid, Dash, Dot };

struct GraphSeries {
	std::string label;
	PartyColour colour;
	GraphLineStyle style = GraphLineStyle::Solid;
	int columnIndex = 0;
	std::vector<std::optional<double>> values;
};

struct GraphYTick {
	double value = 0.0;
	std::string label;
};

struct GraphModel {
	std::vector<std::string> xLabels;
	std::vector<double> xTimes;
	std::vector<GraphSeries> series;
	double yMin = 0.0;
	double yMax = 100.0;
	bool yIsPercent = true;
	bool yFitToData = false;
	bool yIntegerLabels = false;
};

GraphModel makeParliamentGraph(ParliamentView const& view);
GraphModel makeSeatExpectationGraph(ParliamentView const& view);
GraphModel makeSeatThresholdGraph(ParliamentView const& view);
GraphModel makeTppGraph(ParliamentView const& view);
GraphModel makeVoteShareGraph(ParliamentView const& view);
GraphModel makeSeatWinChanceGraph(ParliamentView const& view, bool fitToPartyRange);
GraphModel makeSeatFpGraph(ParliamentView const& view);
GraphModel makeCompletionGraph(ParliamentView const& view);
GraphModel makeCategoryStatGraph(ParliamentView const& view, bool integerLabels);
GraphModel makeInternal2ppGraph(ParliamentView const& view);

PartyColour parliamentGraphColour(ParliamentColumn const& column);
GraphLineStyle parliamentGraphStyle(ParliamentColumn const& column);

struct GraphMetrics {
	int leftAxisWidth = 54;
	int bottomAxisHeight = 34;
	int topPadding = 8;
	int plotInset = 8;
	int plotRightPadding = 10;
	int legendWidth = 170;
	int legendItemHeight = 18;
	int pointHitRadius = 8;
};

struct GraphLayout {
	GraphMetrics metrics;
	TableRect plot;
	TableRect legend;
	std::vector<int> xPixels;
	std::vector<int> xLabelIndices;
	std::optional<int> xBreakpointPixel;
	std::vector<GraphYTick> yTicks;
	double yMin = 0.0;
	double yMax = 100.0;

	int yForValue(double value) const;
	int yForPercent(double percent) const;

	struct Hit {
		enum class Kind { None, Point };
		Kind kind = Kind::None;
		int series = -1;
		int point = -1;
	};
	Hit hitTest(GraphModel const& model, int x, int y) const;
};

GraphLayout layoutParliamentGraph(
	GraphModel const& model,
	int viewportWidth,
	int viewportHeight,
	GraphMetrics const& metrics = {});

}
