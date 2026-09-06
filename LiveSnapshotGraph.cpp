#include "LiveSnapshotGraph.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace LiveSnapshot {
namespace {

PartyColour mixTowardWhite(PartyColour colour, double whiteAmount)
{
	auto const mix = [whiteAmount](int channel) {
		return int(std::lround(channel * (1.0 - whiteAmount) + 255.0 * whiteAmount));
	};
	colour.r = std::clamp(mix(colour.r), 0, 255);
	colour.g = std::clamp(mix(colour.g), 0, 255);
	colour.b = std::clamp(mix(colour.b), 0, 255);
	return colour;
}

int distanceSquared(int x1, int y1, int x2, int y2)
{
	int const dx = x1 - x2;
	int const dy = y1 - y2;
	return dx * dx + dy * dy;
}

double niceTickStep(double span)
{
	if (!(span > 0.0) || !std::isfinite(span)) return 1.0;
	double const raw = span / 4.0;
	double const exponent = std::floor(std::log10(raw));
	double const base = std::pow(10.0, exponent);
	double const mantissa = raw / base;
	double nice = 10.0;
	if (mantissa <= 1.0) nice = 1.0;
	else if (mantissa <= 2.0) nice = 2.0;
	else if (mantissa <= 5.0) nice = 5.0;
	return nice * base;
}

double niceFloor(double value, double step)
{
	if (!(step > 0.0)) return value;
	return std::floor(value / step + 1e-9) * step;
}

double niceCeil(double value, double step)
{
	if (!(step > 0.0)) return value;
	return std::ceil(value / step - 1e-9) * step;
}

void expandPercentSpan(double& low, double& high, double minSpan)
{
	if (!(minSpan > 0.0) || high - low >= minSpan) return;
	double const extra = minSpan - (high - low);
	low -= extra / 2.0;
	high += extra / 2.0;
	if (low < 0.0) {
		high -= low;
		low = 0.0;
	}
	if (high > 100.0) {
		low -= high - 100.0;
		high = 100.0;
	}
	low = std::max(0.0, low);
	high = std::min(100.0, high);
}

std::pair<double, double> fittedPercentBounds(
	double minValue, double maxValue, double minSpan = 0.0)
{
	if (!std::isfinite(minValue) || !std::isfinite(maxValue)) return {0.0, 100.0};
	if (maxValue < minValue) std::swap(minValue, maxValue);
	double const span = maxValue - minValue;
	double const pad = span < 0.05 ? 0.5 : std::max(0.05, span * 0.1);
	double low = std::max(0.0, minValue - pad);
	double high = std::min(100.0, maxValue + pad);
	if (high <= low) return {0.0, 100.0};
	expandPercentSpan(low, high, minSpan);
	double const step = niceTickStep(high - low);
	low = std::max(0.0, niceFloor(low, step));
	high = std::min(100.0, niceCeil(high, step));
	if (high <= low) high = std::min(100.0, low + step);
	expandPercentSpan(low, high, minSpan);
	return {low, high};
}

std::string formatPercentTick(double value, double step)
{
	int decimals = 0;
	if (step < 0.95) decimals = 1;
	if (step < 0.095) decimals = 2;
	double const threshold = 0.5 * std::pow(10.0, -decimals);
	if (std::abs(value) < threshold) value = 0.0;
	std::ostringstream out;
	out << std::fixed << std::setprecision(decimals) << value << '%';
	return out.str();
}

constexpr double PostBreakpointRealHours = 24.0;
constexpr double PostBreakpointAxisHours = 1.0;
constexpr double PostBreakpointScale =
	PostBreakpointAxisHours / PostBreakpointRealHours;

std::optional<double> threeAmAfter(double timeSeconds)
{
	if (!std::isfinite(timeSeconds)) return std::nullopt;
	using namespace std::chrono;
	sys_seconds const first{seconds{std::int64_t(std::llround(timeSeconds))}};
	sys_seconds breakpoint = floor<days>(first) + hours{3};
	if (breakpoint <= first) breakpoint += days{1};
	return double(duration_cast<seconds>(breakpoint.time_since_epoch()).count());
}

double timeAxisOffset(double time, double origin, double breakpoint)
{
	if (!std::isfinite(time) || !std::isfinite(origin)) return 0.0;
	if (!std::isfinite(breakpoint) || !(breakpoint > origin) || time <= breakpoint) {
		return time - origin;
	}
	return (breakpoint - origin) + (time - breakpoint) * PostBreakpointScale;
}

std::string formatFittedValueTick(double value, bool integerLabels)
{
	return integerLabels ? formatSeatCount(value) : formatSeatExpectation(value);
}

void appendFittedValueTicks(
	std::vector<GraphYTick>& ticks,
	double yMin,
	double yMax,
	bool integerLabels)
{
	auto const add = [&](double value) {
		if (!ticks.empty() && std::abs(ticks.back().value - value) <= 1e-9) return;
		ticks.push_back({value, formatFittedValueTick(value, integerLabels)});
	};
	add(yMin);
	double const step = niceTickStep(yMax - yMin);
	if (!(step > 0.0) || !std::isfinite(step)) {
		add(yMax);
		return;
	}
	double tick = niceFloor(yMin, step);
	if (tick <= yMin + step * 1e-9) tick += step;
	for (; tick < yMax - step * 1e-9; tick += step) {
		if (tick > yMin + 1e-9) add(tick);
	}
	add(yMax);
}

}

PartyColour parliamentGraphColour(ParliamentColumn const& column)
{
	if (column.kind == ParliamentColumn::Kind::ExactTie || !column.colour) {
		return {70, 70, 70};
	}
	if (column.outcome == ParliamentColumn::Outcome::Minority) {
		return mixTowardWhite(*column.colour, 0.28);
	}
	if (column.outcome == ParliamentColumn::Outcome::MostSeats) {
		return mixTowardWhite(*column.colour, 0.5);
	}
	return *column.colour;
}

GraphLineStyle parliamentGraphStyle(ParliamentColumn const& column)
{
	if (column.kind == ParliamentColumn::Kind::ExactTie) return GraphLineStyle::Solid;
	if (column.outcome == ParliamentColumn::Outcome::Minority) return GraphLineStyle::Dash;
	if (column.outcome == ParliamentColumn::Outcome::MostSeats) return GraphLineStyle::Dot;
	return GraphLineStyle::Solid;
}

GraphModel makeGraphFromView(
	ParliamentView const& view,
	bool yIsPercent,
	bool fadeByOutcome,
	bool outcomeLineStyles,
	bool fitPercentRange,
	double minPercentSpan = 0.0)
{
	GraphModel model;
	model.yIsPercent = yIsPercent;
	model.xLabels.reserve(view.rows.size());
	model.xTimes.reserve(view.rows.size());
	for (auto const& row : view.rows) {
		model.xLabels.push_back(formatSnapshotTimestamp(row.snapshotCode));
		auto const time = snapshotCodeTimeSeconds(row.snapshotCode);
		model.xTimes.push_back(time ? double(*time) : std::numeric_limits<double>::quiet_NaN());
	}

	double minValue = 0.0;
	double maxValue = 0.0;
	bool haveValue = false;
	for (int column = 0; column < int(view.columns.size()); ++column) {
		auto const& header = view.columns[column];
		if (header.kind == ParliamentColumn::Kind::Snapshot ||
			header.kind == ParliamentColumn::Kind::Separator ||
			!header.includeInGraph) {
			continue;
		}

		GraphSeries series;
		series.label = header.header;
		series.colour = fadeByOutcome ?
			parliamentGraphColour(header) : header.colour.value_or(PartyColour{70, 70, 70});
		series.style = outcomeLineStyles ?
			parliamentGraphStyle(header) : GraphLineStyle::Solid;
		series.columnIndex = column;
		series.values.reserve(view.rows.size());
		for (int row = 0; row < int(view.rows.size()); ++row) {
			if (row >= int(view.cells.size()) ||
				column >= int(view.cells[row].size())) {
				series.values.push_back(std::nullopt);
				continue;
			}
			auto const& cell = view.cells[row][column];
			if (cell.kind != CellValue::Kind::Percent &&
				cell.kind != CellValue::Kind::Value) {
				series.values.push_back(std::nullopt);
				continue;
			}
			series.values.push_back(cell.percent);
			if (!haveValue) {
				minValue = cell.percent;
				maxValue = cell.percent;
				haveValue = true;
			}
			else {
				minValue = std::min(minValue, cell.percent);
				maxValue = std::max(maxValue, cell.percent);
			}
		}
		model.series.push_back(std::move(series));
	}
	if (yIsPercent && fitPercentRange && haveValue) {
		auto const bounds = fittedPercentBounds(minValue, maxValue, minPercentSpan);
		model.yMin = bounds.first;
		model.yMax = bounds.second;
		model.yFitToData = true;
	}
	else if (yIsPercent) {
		model.yMin = 0.0;
		model.yMax = 100.0;
	}
	else if (!haveValue || maxValue <= 0.0) {
		model.yMin = 0.0;
		model.yMax = 1.0;
	}
	else {
		model.yMin = 0.0;
		double const padded = maxValue * 1.05;
		if (padded <= 5.0) model.yMax = std::ceil(padded);
		else model.yMax = std::ceil(padded / 5.0) * 5.0;
	}
	return model;
}

GraphModel makeParliamentGraph(ParliamentView const& view)
{
	return makeGraphFromView(view, true, true, true, false);
}

GraphModel makeSeatExpectationGraph(ParliamentView const& view)
{
	return makeGraphFromView(view, false, false, false, false);
}

GraphModel makeSeatThresholdGraph(ParliamentView const& view)
{
	auto model = makeGraphFromView(view, false, true, false, false);
	model.yIntegerLabels = true;
	return model;
}

GraphModel makeTppGraph(ParliamentView const& view)
{
	return makeGraphFromView(view, true, true, false, true);
}

GraphModel makeVoteShareGraph(ParliamentView const& view)
{
	auto model = makeGraphFromView(view, false, false, false, false);
	model.yIsPercent = true;
	model.yFitToData = true;
	return model;
}

GraphModel makeSeatWinChanceGraph(ParliamentView const& view, bool fitToPartyRange)
{
	return makeGraphFromView(
		view, true, false, false, fitToPartyRange, fitToPartyRange ? 10.0 : 0.0);
}

GraphModel makeSeatFpGraph(ParliamentView const& view)
{
	return makeGraphFromView(view, true, false, false, true, 5.0);
}

GraphModel makeCompletionGraph(ParliamentView const& view)
{
	auto model = makeGraphFromView(view, true, false, false, false);
	for (auto& series : model.series) {
		for (auto& value : series.values) {
			if (value) *value *= 100.0;
		}
	}
	model.yMin = 0.0;
	model.yMax = 100.0;
	model.yIsPercent = true;
	model.yFitToData = false;
	return model;
}

GraphModel makeCategoryStatGraph(ParliamentView const& view, bool integerLabels)
{
	auto model = makeGraphFromView(view, false, false, false, false);
	model.yIsPercent = false;
	model.yIntegerLabels = integerLabels;
	model.yFitToData = true;

	double minValue = 0.0;
	double maxValue = 0.0;
	bool haveValue = false;
	for (auto const& series : model.series) {
		for (auto const& value : series.values) {
			if (!value || !std::isfinite(*value)) continue;
			if (!haveValue) {
				minValue = *value;
				maxValue = *value;
				haveValue = true;
			}
			else {
				minValue = std::min(minValue, *value);
				maxValue = std::max(maxValue, *value);
			}
		}
	}
	if (!haveValue) {
		model.yMin = 0.0;
		model.yMax = 1.0;
		return model;
	}
	if (minValue >= 0.0) {
		model.yMin = 0.0;
		double const padded = maxValue * 1.05;
		if (!(padded > 0.0)) model.yMax = 1.0;
		else if (integerLabels) model.yMax = std::max(1.0, std::ceil(padded));
		else model.yMax = padded;
	}
	else {
		double span = maxValue - minValue;
		if (!(span > 0.0)) span = std::max(std::abs(minValue), 1.0);
		double const pad = span * 0.05;
		model.yMin = minValue - pad;
		model.yMax = std::max(0.0, maxValue + pad);
	}
	return model;
}

GraphModel makeInternal2ppGraph(ParliamentView const& view)
{
	return makeCategoryStatGraph(view, false);
}

int GraphLayout::yForValue(double value) const
{
	if (plot.height <= 0) return plot.y;
	double const minValue = yMin;
	double const maxValue = yMax > minValue ? yMax : minValue + 1.0;
	double const clamped = std::clamp(value, minValue, maxValue);
	double const t = (clamped - minValue) / (maxValue - minValue);
	return plot.y + plot.height - int(std::lround(t * plot.height));
}

int GraphLayout::yForPercent(double percent) const
{
	return yForValue(percent);
}

GraphLayout::Hit GraphLayout::hitTest(GraphModel const& model, int x, int y) const
{
	Hit hit;
	int const radius = std::max(1, metrics.pointHitRadius);
	int const limit = radius * radius;
	int best = limit + 1;
	for (int series = 0; series < int(model.series.size()); ++series) {
		auto const& values = model.series[series].values;
		int const count = std::min(int(values.size()), int(xPixels.size()));
		for (int point = 0; point < count; ++point) {
			if (!values[point]) continue;
			int const px = xPixels[point];
			int const py = yForValue(*values[point]);
			int const dist = distanceSquared(x, y, px, py);
			if (dist < best && dist <= limit) {
				best = dist;
				hit.kind = Hit::Kind::Point;
				hit.series = series;
				hit.point = point;
			}
		}
	}
	return hit;
}

GraphLayout layoutParliamentGraph(
	GraphModel const& model,
	int viewportWidth,
	int viewportHeight,
	GraphMetrics const& metrics)
{
	GraphLayout layout;
	layout.metrics = metrics;
	layout.yMin = model.yMin;
	layout.yMax = model.yMax > model.yMin ? model.yMax : model.yMin + 1.0;
	viewportWidth = std::max(0, viewportWidth);
	viewportHeight = std::max(0, viewportHeight);

	if (model.yIsPercent && !model.yFitToData) {
		int const ticks[] = {0, 25, 50, 75, 100};
		for (int tick : ticks) {
			layout.yTicks.push_back({double(tick), std::to_string(tick) + "%"});
		}
	}
	else if (model.yIsPercent) {
		double const step = niceTickStep(layout.yMax - layout.yMin);
		for (double tick = layout.yMin; tick <= layout.yMax + step * 0.5; tick += step) {
			double const value = std::min(tick, layout.yMax);
			layout.yTicks.push_back({value, formatPercentTick(value, step)});
			if (tick >= layout.yMax) break;
		}
		if (layout.yTicks.empty() ||
			layout.yTicks.back().value < layout.yMax - step * 0.25) {
			layout.yTicks.push_back({layout.yMax, formatPercentTick(layout.yMax, step)});
		}
	}
	else if (model.yFitToData) {
		appendFittedValueTicks(
			layout.yTicks, layout.yMin, layout.yMax, model.yIntegerLabels);
	}
	else {
		for (int index = 0; index <= 4; ++index) {
			double const value = layout.yMax * double(index) / 4.0;
			layout.yTicks.push_back({
				value,
				model.yIntegerLabels ? formatSeatCount(value) : formatSeatExpectation(value)
			});
		}
	}

	int const legendWidth = model.series.empty() ? 0 : metrics.legendWidth;
	layout.legend = {
		std::max(0, viewportWidth - legendWidth),
		metrics.topPadding,
		legendWidth,
		std::max(0, viewportHeight - metrics.topPadding - metrics.bottomAxisHeight)
	};
	layout.plot = {
		metrics.leftAxisWidth,
		metrics.topPadding,
		std::max(0, layout.legend.x - metrics.leftAxisWidth - metrics.plotRightPadding),
		std::max(0, viewportHeight - metrics.topPadding - metrics.bottomAxisHeight)
	};

	int const count = int(model.xLabels.size());
	layout.xPixels.resize(count);
	int const innerLeft = layout.plot.x + metrics.plotInset;
	int const innerRight = layout.plot.x + std::max(0, layout.plot.width - metrics.plotInset);
	int const span = std::max(0, innerRight - innerLeft);

	bool timesValid = int(model.xTimes.size()) == count && count > 0;
	double timeMin = 0.0;
	double timeMax = 0.0;
	if (timesValid) {
		timeMin = model.xTimes.front();
		timeMax = model.xTimes.front();
		for (double time : model.xTimes) {
			if (!std::isfinite(time)) {
				timesValid = false;
				break;
			}
			timeMin = std::min(timeMin, time);
			timeMax = std::max(timeMax, time);
		}
	}
	bool const useTimeScale = timesValid && count > 1 && timeMax > timeMin;
	std::optional<double> breakpoint;
	if (useTimeScale) breakpoint = threeAmAfter(timeMin);
	double axisMin = 0.0;
	double axisMax = 1.0;
	if (useTimeScale) {
		double const breakAt = breakpoint.value_or(timeMax);
		axisMin = timeAxisOffset(timeMin, timeMin, breakAt);
		axisMax = timeAxisOffset(timeMax, timeMin, breakAt);
		if (!(axisMax > axisMin)) axisMax = axisMin + 1.0;
		if (breakpoint && *breakpoint > timeMin && *breakpoint < timeMax) {
			double const axisBreak = timeAxisOffset(*breakpoint, timeMin, *breakpoint);
			double const t = (axisBreak - axisMin) / (axisMax - axisMin);
			layout.xBreakpointPixel =
				innerLeft + int(std::lround(double(span) * t));
		}
	}

	for (int index = 0; index < count; ++index) {
		if (count == 1) {
			layout.xPixels[index] = innerLeft + span / 2;
		}
		else if (useTimeScale) {
			double const axis = timeAxisOffset(
				model.xTimes[index], timeMin, breakpoint.value_or(timeMax));
			double const t = (axis - axisMin) / (axisMax - axisMin);
			layout.xPixels[index] = innerLeft + int(std::lround(double(span) * t));
		}
		else {
			layout.xPixels[index] = innerLeft +
				int(std::lround(double(span) * double(index) / double(count - 1)));
		}
	}

	if (count > 0 && layout.plot.width > 0) {
		int const minLabelGap = 88;
		layout.xLabelIndices.push_back(0);
		for (int index = 1; index < count - 1; ++index) {
			if (layout.xPixels[index] - layout.xPixels[layout.xLabelIndices.back()] >=
				minLabelGap) {
				layout.xLabelIndices.push_back(index);
			}
		}
		if (count > 1) {
			if (layout.xPixels[count - 1] -
					layout.xPixels[layout.xLabelIndices.back()] < minLabelGap &&
				layout.xLabelIndices.size() > 1) {
				layout.xLabelIndices.pop_back();
			}
			if (layout.xLabelIndices.back() != count - 1) {
				layout.xLabelIndices.push_back(count - 1);
			}
		}
	}
	return layout;
}

}
