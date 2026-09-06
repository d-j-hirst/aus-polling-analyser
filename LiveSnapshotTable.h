#pragma once

#include "LiveSnapshotData.h"

#include <vector>

namespace LiveSnapshot {

struct TableMetrics {
	int headerHeight = 28;
	int rowHeight = 24;
	int snapshotColumnWidth = 150;
	int outcomeColumnWidth = 108;
	int exactTieColumnWidth = 90;
	int seatColumnWidth = 96;
	int seatNameColumnWidth = 96;
	int categoryStatColumnWidth = 120;
	int internal2ppColumnWidth = 168;
	int separatorColumnWidth = 18;
	int coalitionColumnWidth = 168;
};

struct TableRect {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

inline bool rectsIntersect(TableRect const& a, TableRect const& b)
{
	return a.x < b.x + b.width && b.x < a.x + a.width &&
		a.y < b.y + b.height && b.y < a.y + a.height;
}

struct TableLayout {
	TableMetrics metrics;
	std::vector<int> columnWidths;
	int rowCount = 0;
	int frozenColumns = 0;

	int columnCount() const { return int(columnWidths.size()); }
	int contentWidth() const;
	int bodyHeight() const;
	int totalHeight() const;
	int columnLeft(int column) const;
	int frozenWidth() const;
	int horizontalScrollStep(int viewportWidth) const;
	int verticalScrollStep(int bodyViewportHeight) const;

	int clampScrollX(int scrollX, int viewportWidth) const;
	int clampScrollY(int scrollY, int bodyViewportHeight) const;

	struct VisibleCells {
		int firstRow = 0;
		int lastRow = 0;
		int firstCol = 0;
		int lastCol = 0;
	};
	VisibleCells visibleCells(
		int viewportWidth,
		int viewportHeight,
		int scrollX,
		int scrollY) const;

	TableRect headerRect(int column, int scrollX) const;
	TableRect bodyRect(int row, int column, int scrollX, int scrollY) const;

	enum class HitKind { None, Header, Body };
	struct Hit {
		HitKind kind = HitKind::None;
		int row = -1;
		int column = -1;
	};
	Hit hitTest(
		int x,
		int y,
		int scrollX,
		int scrollY,
		int viewportWidth,
		int viewportHeight) const;
};

std::vector<int> parliamentColumnWidths(
	ParliamentView const& view,
	TableMetrics const& metrics = {});

TableLayout makeTableLayout(
	std::vector<int> columnWidths,
	int rowCount,
	TableMetrics const& metrics = {});

TableLayout makeParliamentLayout(
	ParliamentView const& view,
	TableMetrics const& metrics = {});

}
