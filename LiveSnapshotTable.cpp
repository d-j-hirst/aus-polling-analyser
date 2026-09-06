#include "LiveSnapshotTable.h"

#include <algorithm>

namespace LiveSnapshot {

int TableLayout::contentWidth() const
{
	int width = 0;
	for (int columnWidth : columnWidths) width += columnWidth;
	return width;
}

int TableLayout::bodyHeight() const
{
	return rowCount * metrics.rowHeight;
}

int TableLayout::totalHeight() const
{
	return metrics.headerHeight + bodyHeight();
}

int TableLayout::columnLeft(int column) const
{
	int left = 0;
	int const count = columnCount();
	for (int index = 0; index < column && index < count; ++index) {
		left += columnWidths[index];
	}
	return left;
}

int TableLayout::frozenWidth() const
{
	int width = 0;
	int const freeze = std::clamp(frozenColumns, 0, columnCount());
	for (int index = 0; index < freeze; ++index) {
		width += columnWidths[index];
	}
	return width;
}

int TableLayout::horizontalScrollStep(int viewportWidth) const
{
	int const usable = std::max(1, viewportWidth - frozenWidth());
	return std::max(1, usable / 4);
}

int TableLayout::verticalScrollStep(int bodyViewportHeight) const
{
	int const usable = std::max(1, bodyViewportHeight);
	return std::max(metrics.rowHeight, usable / 4);
}

int TableLayout::clampScrollX(int scrollX, int viewportWidth) const
{
	int const maxScroll = std::max(0, contentWidth() - viewportWidth);
	return std::clamp(scrollX, 0, maxScroll);
}

int TableLayout::clampScrollY(int scrollY, int bodyViewportHeight) const
{
	int const maxScroll = std::max(0, bodyHeight() - bodyViewportHeight);
	return std::clamp(scrollY, 0, maxScroll);
}

TableLayout::VisibleCells TableLayout::visibleCells(
	int viewportWidth,
	int viewportHeight,
	int scrollX,
	int scrollY) const
{
	VisibleCells visible;
	int const columns = columnCount();
	if (columns == 0) return visible;

	int const bodyViewportHeight = std::max(0, viewportHeight - metrics.headerHeight);
	int const clampedX = clampScrollX(scrollX, viewportWidth);
	int const clampedY = clampScrollY(scrollY, bodyViewportHeight);

	int const freeze = std::clamp(frozenColumns, 0, columns);
	visible.firstCol = freeze;
	int const hiddenUntil = clampedX + frozenWidth();
	while (visible.firstCol < columns &&
		columnLeft(visible.firstCol) + columnWidths[visible.firstCol] <= hiddenUntil) {
		++visible.firstCol;
	}
	visible.lastCol = visible.firstCol;
	while (visible.lastCol < columns &&
		columnLeft(visible.lastCol) < clampedX + viewportWidth) {
		++visible.lastCol;
	}

	if (rowCount <= 0 || bodyViewportHeight <= 0) {
		visible.firstRow = 0;
		visible.lastRow = 0;
		return visible;
	}

	visible.firstRow = std::min(rowCount, clampedY / metrics.rowHeight);
	int const lastPixel = clampedY + bodyViewportHeight - 1;
	visible.lastRow = std::min(rowCount, lastPixel / metrics.rowHeight + 1);
	return visible;
}

int columnScreenX(TableLayout const& layout, int column, int scrollX)
{
	int const left = layout.columnLeft(column);
	int const freeze = std::clamp(layout.frozenColumns, 0, layout.columnCount());
	if (column >= 0 && column < freeze) return left;
	return left - scrollX;
}

TableRect TableLayout::headerRect(int column, int scrollX) const
{
	if (column < 0 || column >= columnCount()) return {};
	return {
		columnScreenX(*this, column, scrollX),
		0,
		columnWidths[column],
		metrics.headerHeight
	};
}

TableRect TableLayout::bodyRect(
	int row, int column, int scrollX, int scrollY) const
{
	if (row < 0 || row >= rowCount || column < 0 || column >= columnCount()) {
		return {};
	}
	return {
		columnScreenX(*this, column, scrollX),
		metrics.headerHeight + row * metrics.rowHeight - scrollY,
		columnWidths[column],
		metrics.rowHeight
	};
}

TableLayout::Hit TableLayout::hitTest(
	int x,
	int y,
	int scrollX,
	int scrollY,
	int viewportWidth,
	int viewportHeight) const
{
	Hit hit;
	if (x < 0 || y < 0 || x >= viewportWidth || y >= viewportHeight) {
		return hit;
	}
	int const bodyViewportHeight = std::max(0, viewportHeight - metrics.headerHeight);
	int const clampedX = clampScrollX(scrollX, viewportWidth);
	int const clampedY = clampScrollY(scrollY, bodyViewportHeight);
	int const freeze = std::clamp(frozenColumns, 0, columnCount());
	int const freezeWidth = frozenWidth();

	int column = -1;
	if (freeze > 0 && x < freezeWidth) {
		int left = 0;
		for (int index = 0; index < freeze; ++index) {
			int const right = left + columnWidths[index];
			if (x < right) {
				column = index;
				break;
			}
			left = right;
		}
		if (column < 0) return hit;
	}
	else {
		int const contentX = x + clampedX;
		if (contentX < 0 || contentX >= contentWidth()) return hit;
		int left = 0;
		column = 0;
		for (; column < columnCount(); ++column) {
			int const right = left + columnWidths[column];
			if (contentX < right) break;
			left = right;
		}
		if (column >= columnCount() || column < freeze) return hit;
	}

	if (y < metrics.headerHeight) {
		hit.kind = HitKind::Header;
		hit.column = column;
		return hit;
	}

	int const contentY = y - metrics.headerHeight + clampedY;
	if (contentY < 0 || contentY >= bodyHeight()) return hit;
	hit.kind = HitKind::Body;
	hit.column = column;
	hit.row = contentY / metrics.rowHeight;
	return hit;
}

std::vector<int> parliamentColumnWidths(
	ParliamentView const& view,
	TableMetrics const& metrics)
{
	std::vector<int> widths;
	widths.reserve(view.columns.size());
	for (auto const& column : view.columns) {
		if (column.kind == ParliamentColumn::Kind::Snapshot) {
			widths.push_back(metrics.snapshotColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::ExactTie) {
			widths.push_back(metrics.exactTieColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::PartySeats ||
			column.kind == ParliamentColumn::Kind::SeatThreshold ||
			column.kind == ParliamentColumn::Kind::TppThreshold ||
			column.kind == ParliamentColumn::Kind::TppMean ||
			column.kind == ParliamentColumn::Kind::PartyVoteShare ||
			column.kind == ParliamentColumn::Kind::CoalitionVoteShare) {
			widths.push_back(metrics.seatColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::SeatWinChance ||
			column.kind == ParliamentColumn::Kind::SeatFp ||
			column.kind == ParliamentColumn::Kind::SeatCompletion) {
			widths.push_back(metrics.seatNameColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::CategoryStat) {
			widths.push_back(metrics.categoryStatColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::Internal2pp) {
			widths.push_back(metrics.internal2ppColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::Separator) {
			widths.push_back(metrics.separatorColumnWidth);
		}
		else if (column.kind == ParliamentColumn::Kind::CoalitionSeats) {
			widths.push_back(metrics.coalitionColumnWidth);
		}
		else {
			widths.push_back(metrics.outcomeColumnWidth);
		}
	}
	return widths;
}

TableLayout makeTableLayout(
	std::vector<int> columnWidths,
	int rowCount,
	TableMetrics const& metrics)
{
	TableLayout layout;
	layout.metrics = metrics;
	layout.columnWidths = std::move(columnWidths);
	layout.rowCount = std::max(0, rowCount);
	return layout;
}

TableLayout makeParliamentLayout(
	ParliamentView const& view,
	TableMetrics const& metrics)
{
	return makeTableLayout(
		parliamentColumnWidths(view, metrics),
		int(view.rows.size()),
		metrics);
}

}
