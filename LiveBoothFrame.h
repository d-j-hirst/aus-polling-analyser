#pragma once

#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "GenericChildFrame.h"
#include "LiveSnapshotData.h"
#include "LiveSnapshotGraph.h"
#include "LiveSnapshotTable.h"
#include "PollingProject.h"
#include "ProjectFrame.h"

class LiveBoothFrame : public GenericChildFrame
{
public:
	LiveBoothFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	void paint();

	void refreshData();

private:
	enum class Mode {
		NodeInspector,
		Parliament,
		SeatExpectations,
		SeatThresholds,
		Tpp,
		VoteShares,
		SeatWinChance,
		SeatFp,
		FpCounted,
		TppSwingBasis,
		TcpSwingBasis,
		BoothTypeBias,
		VoteTypeBias,
		Internal2pp
	};
	enum class SummaryDisplay { Table, Graph };

	void OnPaint(wxPaintEvent& event);
	void OnMouseMove(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnDcLeftDown(wxMouseEvent& event);
	void OnCharHook(wxKeyEvent& event);
	void OnResize(wxSizeEvent& event);
	void OnModeSelected(wxCommandEvent& event);
	void OnRunSelected(wxCommandEvent& event);
	void OnViewSelected(wxCommandEvent& event);
	void OnPartySelected(wxCommandEvent& event);
	void OnSeatSelected(wxCommandEvent& event);
	void OnShadingSelected(wxCommandEvent& event);
	void OnLayoutSelected(wxCommandEvent& event);
	void OnVerticalScroll(wxScrollEvent& event);
	void OnHorizontalScroll(wxScrollEvent& event);

	void bindEventHandlers();
	void createChrome();
	void updateToolbar();
	void layoutContents();
	void applyScrollbars();
	void render(wxDC& dc);
	void renderStatus(wxDC& dc) const;
	void renderNodeInspector(wxDC& dc, int y) const;
	void renderParliament(wxDC& dc, int originY) const;
	void renderSummaryTable(wxDC& dc, int originY) const;
	void renderSummaryGraph(wxDC& dc, int originY) const;
	void rememberRunSelection();
	void restoreRunSelection();
	void restorePartyCombo();
	void restoreSeatCombo();
	void restoreShadingCombo();
	void restoreLayoutCombo();
	void rebuildPresentation();
	void syncThresholdView();
	void syncSeatWinViews();
	void syncSeatFpViews();
	void syncCompletionViews();
	void syncCategoryStatViews();
	bool isSeatMetricMode() const;
	bool isCompletionMode() const;
	bool isCategoryStatMode() const;
	bool isWideSeatTableMode() const;
	char const* completionValueKey() const;
	char const* categoryStatArrayKey() const;
	LiveSnapshot::ParliamentView const& activeTableView() const;
	int chromeHeight() const;
	int statusBandHeight() const;
	bool showingSummaryMode() const;
	bool showingSummaryTable() const;
	bool showingSummaryGraph() const;
	wxColour headerColour(LiveSnapshot::ParliamentColumn const& column) const;
	wxColour bodyColour(LiveSnapshot::ParliamentColumn const& column, int row) const;
	LiveSnapshot::SeatWinCellStyle seatWinStyle(int row, int column) const;
	LiveSnapshot::CompletionCellStyle completionStyle(int row, int column) const;
	wxColour graphColour(LiveSnapshot::PartyColour const& colour) const;
	wxPen graphPen(LiveSnapshot::GraphSeries const& series) const;
	wxString parliamentTooltip(LiveSnapshot::TableLayout::Hit const& hit) const;
	wxString graphTooltip(LiveSnapshot::GraphLayout::Hit const& hit) const;
	LiveSnapshot::GraphLayout currentGraphLayout() const;

	ProjectFrame::Refresher refresher;

	wxPanel* chrome = nullptr;
	wxPanel* inspectorBar = nullptr;
	wxPanel* parliamentBar = nullptr;
	wxStaticText* modeLabel = nullptr;
	wxComboBox* modeComboBox = nullptr;
	wxStaticText* runLabel = nullptr;
	wxComboBox* runComboBox = nullptr;
	wxStaticText* filterLabel = nullptr;
	wxTextCtrl* filterTextCtrl = nullptr;
	wxStaticText* viewLabel = nullptr;
	wxComboBox* viewComboBox = nullptr;
	wxStaticText* partyLabel = nullptr;
	wxComboBox* partyComboBox = nullptr;
	wxStaticText* seatLabel = nullptr;
	wxComboBox* seatComboBox = nullptr;
	wxStaticText* shadingLabel = nullptr;
	wxComboBox* shadingComboBox = nullptr;
	wxStaticText* layoutLabel = nullptr;
	wxComboBox* layoutComboBox = nullptr;

	wxPanel* dcPanel = nullptr;
	wxScrollBar* verticalScroll = nullptr;
	wxScrollBar* horizontalScroll = nullptr;

	Mode mode = Mode::NodeInspector;
	SummaryDisplay summaryDisplay = SummaryDisplay::Table;
	std::string selectedFilename;
	LiveSnapshot::LoadResult loadResult;
	LiveSnapshot::ParliamentView parliamentView;
	LiveSnapshot::ParliamentView seatView;
	LiveSnapshot::ParliamentView thresholdView;
	LiveSnapshot::ParliamentView tppView;
	LiveSnapshot::ParliamentView voteShareView;
	LiveSnapshot::ParliamentView seatWinTableView;
	LiveSnapshot::ParliamentView seatWinGraphView;
	LiveSnapshot::ParliamentView seatFpTableView;
	LiveSnapshot::ParliamentView seatFpGraphView;
	LiveSnapshot::ParliamentView completionTableView;
	LiveSnapshot::ParliamentView completionGraphView;
	LiveSnapshot::ParliamentView categoryStatTableView;
	LiveSnapshot::ParliamentView categoryStatGraphView;
	LiveSnapshot::ParliamentView internal2ppView;
	std::vector<LiveSnapshot::ThresholdPartyOption> thresholdParties;
	std::vector<LiveSnapshot::ThresholdPartyOption> seatWinParties;
	std::vector<LiveSnapshot::ThresholdPartyOption> seatFpParties;
	std::vector<std::string> seatWinSeats;
	int selectedThresholdPartyIndex = 0;
	int selectedSeatWinTablePartyIndex = 0;
	int selectedSeatWinGraphPartyIndex = 0;
	int selectedSeatFpTablePartyIndex = 0;
	int selectedSeatFpGraphPartyIndex = 0;
	bool seatWinGraphAllParties = true;
	bool seatFpGraphAllParties = true;
	std::string selectedSeatWinSeatName;
	LiveSnapshot::SeatWinShading seatWinShading = LiveSnapshot::SeatWinShading::CurrentChance;
	bool categoryGroupByType = true;
	LiveSnapshot::CategoryStatistic categoryGraphStatistic =
		LiveSnapshot::CategoryStatistic::Bias;
	LiveSnapshot::GraphModel graphModel;
	LiveSnapshot::TableLayout tableLayout;
	int scrollX = 0;
	int scrollY = 0;
	int lastTooltipRow = -1;
	int lastTooltipColumn = -1;
	bool updatingControls = false;
};
