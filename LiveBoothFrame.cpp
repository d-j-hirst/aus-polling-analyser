#include "LiveBoothFrame.h"

#include "General.h"

#include "wx/dcbuffer.h"
#include "wx/settings.h"

#include <algorithm>
#include <optional>

namespace {
constexpr int StatusLineHeight = 22;
constexpr int ScrollBarThickness = 16;
constexpr int ChromeMinHeight = 36;

bool isHorizontalArrow(int key)
{
	return key == WXK_LEFT || key == WXK_RIGHT ||
		key == WXK_NUMPAD_LEFT || key == WXK_NUMPAD_RIGHT;
}

bool isVerticalArrow(int key)
{
	return key == WXK_UP || key == WXK_DOWN ||
		key == WXK_NUMPAD_UP || key == WXK_NUMPAD_DOWN;
}

bool isArrowKey(int key)
{
	return isHorizontalArrow(key) || isVerticalArrow(key);
}
}

enum ControlId {
	Base = 820,
	Frame,
	DcPanel,
	PrimaryView,
	SecondaryView,
	ViewMode,
	ThresholdParty,
	SeatFocus,
	ShadingMode,
	CategoryLayout,
	FilterText,
	VerticalScroll,
	HorizontalScroll
};

LiveBoothFrame::LiveBoothFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Live Booths", wxPoint(333, 0), project),
	refresher(refresher)
{
	createChrome();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxDefaultPosition, wxDefaultSize,
		wxTAB_TRAVERSAL | wxWANTS_CHARS);
	dcPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	verticalScroll = new wxScrollBar(this, ControlId::VerticalScroll, wxDefaultPosition,
		wxDefaultSize, wxSB_VERTICAL);
	horizontalScroll = new wxScrollBar(this, ControlId::HorizontalScroll, wxDefaultPosition,
		wxDefaultSize, wxSB_HORIZONTAL);

	bindEventHandlers();
	refreshData();
}

void LiveBoothFrame::paint()
{
	if (!dcPanel) return;
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void LiveBoothFrame::refreshData()
{
	loadResult = LiveSnapshot::loadFromProject(*project);
	parliamentView = LiveSnapshot::buildParliamentView(loadResult.records);
	seatView = LiveSnapshot::buildSeatExpectationView(loadResult.records);
	tppView = LiveSnapshot::buildTppView(loadResult.records);
	voteShareView = LiveSnapshot::buildVoteShareView(loadResult.records);
	syncThresholdView();
	syncSeatWinViews();
	syncSeatFpViews();
	syncCompletionViews();
	syncCategoryStatViews();
	internal2ppView = LiveSnapshot::buildInternal2ppView(loadResult.records);
	rebuildPresentation();
	lastTooltipRow = -1;
	lastTooltipColumn = -1;
	if (dcPanel) dcPanel->UnsetToolTip();
	updateToolbar();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
}

void LiveBoothFrame::OnMouseMove(wxMouseEvent& event)
{
	if (!showingSummaryMode()) {
		return;
	}
	int const originY = statusBandHeight();
	auto const size = dcPanel->GetClientSize();
	if (showingSummaryGraph()) {
		auto const layout = currentGraphLayout();
		auto const hit = layout.hitTest(graphModel, event.GetX(), event.GetY() - originY);
		if (hit.kind != LiveSnapshot::GraphLayout::Hit::Kind::Point) {
			if (lastTooltipRow != -1) {
				dcPanel->UnsetToolTip();
				lastTooltipRow = -1;
				lastTooltipColumn = -1;
			}
			return;
		}
		if (hit.series == lastTooltipRow && hit.point == lastTooltipColumn) return;
		lastTooltipRow = hit.series;
		lastTooltipColumn = hit.point;
		dcPanel->SetToolTip(graphTooltip(hit));
		return;
	}

	auto const hit = tableLayout.hitTest(
		event.GetX(),
		event.GetY() - originY,
		scrollX,
		scrollY,
		size.GetWidth(),
		std::max(0, size.GetHeight() - originY));
	if (hit.kind != LiveSnapshot::TableLayout::HitKind::Body) {
		if (lastTooltipRow != -1) {
			dcPanel->UnsetToolTip();
			lastTooltipRow = -1;
			lastTooltipColumn = -1;
		}
		return;
	}
	if (hit.row == lastTooltipRow && hit.column == lastTooltipColumn) return;
	lastTooltipRow = hit.row;
	lastTooltipColumn = hit.column;
	dcPanel->SetToolTip(parliamentTooltip(hit));
}

void LiveBoothFrame::OnMouseWheel(wxMouseEvent& event)
{
	if (!showingSummaryTable()) return;
	int const rotation = event.GetWheelRotation();
	if (rotation == 0) return;
	int const lines = std::max(1, event.GetLinesPerAction());
	int const step = tableLayout.metrics.rowHeight * lines;
	if (event.ShiftDown() || event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
		scrollX -= rotation > 0 ? step : -step;
	}
	else {
		scrollY -= rotation > 0 ? step : -step;
	}
	applyScrollbars();
	paint();
}

void LiveBoothFrame::OnDcLeftDown(wxMouseEvent& event)
{
	if (dcPanel) dcPanel->SetFocus();
	event.Skip();
}

void LiveBoothFrame::OnCharHook(wxKeyEvent& event)
{
	if (!isArrowKey(event.GetKeyCode()) ||
		!isWideSeatTableMode() || !showingSummaryTable() || !dcPanel) {
		event.Skip();
		return;
	}
	dcPanel->SetFocus();
	int const key = event.GetKeyCode();
	if (isHorizontalArrow(key)) {
		int const step = tableLayout.horizontalScrollStep(dcPanel->GetClientSize().GetWidth());
		if (key == WXK_LEFT || key == WXK_NUMPAD_LEFT) scrollX -= step;
		else scrollX += step;
	}
	else {
		int const originY = statusBandHeight();
		int const bodyViewportHeight = std::max(0,
			dcPanel->GetClientSize().GetHeight() - originY -
			tableLayout.metrics.headerHeight);
		int const step = tableLayout.verticalScrollStep(bodyViewportHeight);
		if (key == WXK_UP || key == WXK_NUMPAD_UP) scrollY -= step;
		else scrollY += step;
	}
	applyScrollbars();
	paint();
}

void LiveBoothFrame::OnResize(wxSizeEvent&)
{
	layoutContents();
	paint();
}

void LiveBoothFrame::OnModeSelected(wxCommandEvent&)
{
	if (updatingControls) return;
	int const selection = modeComboBox->GetSelection();
	if (selection == 13) mode = Mode::Internal2pp;
	else if (selection == 12) mode = Mode::VoteTypeBias;
	else if (selection == 11) mode = Mode::BoothTypeBias;
	else if (selection == 10) mode = Mode::TcpSwingBasis;
	else if (selection == 9) mode = Mode::TppSwingBasis;
	else if (selection == 8) mode = Mode::FpCounted;
	else if (selection == 7) mode = Mode::SeatFp;
	else if (selection == 6) mode = Mode::SeatWinChance;
	else if (selection == 5) mode = Mode::VoteShares;
	else if (selection == 4) mode = Mode::Tpp;
	else if (selection == 3) mode = Mode::SeatThresholds;
	else if (selection == 2) mode = Mode::SeatExpectations;
	else if (selection == 1) mode = Mode::Parliament;
	else mode = Mode::NodeInspector;
	if (isCompletionMode()) syncCompletionViews();
	else if (isCategoryStatMode()) syncCategoryStatViews();
	rebuildPresentation();
	updateToolbar();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnRunSelected(wxCommandEvent&)
{
	if (updatingControls) return;
	rememberRunSelection();
	paint();
}

void LiveBoothFrame::OnViewSelected(wxCommandEvent&)
{
	if (updatingControls) return;
	summaryDisplay = viewComboBox->GetSelection() == 1 ?
		SummaryDisplay::Graph : SummaryDisplay::Table;
	rebuildPresentation();
	updateToolbar();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnPartySelected(wxCommandEvent&)
{
	if (updatingControls) return;
	int const selection = partyComboBox->GetSelection();
	if (mode == Mode::SeatThresholds) {
		if (selection >= 0 && selection < int(thresholdParties.size())) {
			selectedThresholdPartyIndex = thresholdParties[selection].partyIndex;
		}
		syncThresholdView();
	}
	else if (mode == Mode::SeatWinChance || mode == Mode::SeatFp) {
		auto const& parties = mode == Mode::SeatFp ? seatFpParties : seatWinParties;
		bool& allParties = mode == Mode::SeatFp ?
			seatFpGraphAllParties : seatWinGraphAllParties;
		int& tableParty = mode == Mode::SeatFp ?
			selectedSeatFpTablePartyIndex : selectedSeatWinTablePartyIndex;
		int& graphParty = mode == Mode::SeatFp ?
			selectedSeatFpGraphPartyIndex : selectedSeatWinGraphPartyIndex;
		if (summaryDisplay == SummaryDisplay::Graph) {
			if (selection <= 0) {
				allParties = true;
			}
			else if (selection - 1 < int(parties.size())) {
				allParties = false;
				graphParty = parties[selection - 1].partyIndex;
			}
		}
		else if (selection >= 0 && selection < int(parties.size())) {
			tableParty = parties[selection].partyIndex;
		}
		if (mode == Mode::SeatFp) syncSeatFpViews();
		else syncSeatWinViews();
	}
	rebuildPresentation();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnSeatSelected(wxCommandEvent&)
{
	if (updatingControls) return;
	int const selection = seatComboBox->GetSelection();
	if (selection >= 0 && selection < int(seatWinSeats.size())) {
		selectedSeatWinSeatName = seatWinSeats[selection];
	}
	if (isCompletionMode()) syncCompletionViews();
	else if (mode == Mode::SeatFp) syncSeatFpViews();
	else if (mode == Mode::SeatWinChance) syncSeatWinViews();
	rebuildPresentation();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnShadingSelected(wxCommandEvent&)
{
	if (updatingControls) return;
	seatWinShading = shadingComboBox->GetSelection() == 1 ?
		LiveSnapshot::SeatWinShading::Change :
		LiveSnapshot::SeatWinShading::CurrentChance;
	paint();
}

void LiveBoothFrame::OnLayoutSelected(wxCommandEvent&)
{
	if (updatingControls || !isCategoryStatMode()) return;
	int const selection = layoutComboBox->GetSelection();
	if (summaryDisplay == SummaryDisplay::Graph) {
		categoryGraphStatistic = LiveSnapshot::categoryStatisticAt(selection);
	}
	else {
		categoryGroupByType = selection <= 0;
	}
	syncCategoryStatViews();
	rebuildPresentation();
	layoutContents();
	paint();
}

void LiveBoothFrame::OnVerticalScroll(wxScrollEvent& event)
{
	scrollY = event.GetPosition();
	paint();
}

void LiveBoothFrame::OnHorizontalScroll(wxScrollEvent& event)
{
	scrollX = event.GetPosition();
	paint();
}

void LiveBoothFrame::bindEventHandlers()
{
	Bind(wxEVT_SIZE, &LiveBoothFrame::OnResize, this, ControlId::Frame);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnModeSelected, this, ControlId::PrimaryView);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnRunSelected, this, ControlId::SecondaryView);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnViewSelected, this, ControlId::ViewMode);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnPartySelected, this, ControlId::ThresholdParty);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnSeatSelected, this, ControlId::SeatFocus);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnShadingSelected, this, ControlId::ShadingMode);
	Bind(wxEVT_COMBOBOX, &LiveBoothFrame::OnLayoutSelected, this, ControlId::CategoryLayout);
	auto const hookArrows = [this](wxWindow* window) {
		window->Bind(wxEVT_CHAR_HOOK, &LiveBoothFrame::OnCharHook, this);
	};
	hookArrows(this);
	hookArrows(chrome);
	hookArrows(parliamentBar);
	hookArrows(modeComboBox);
	hookArrows(viewComboBox);
	hookArrows(partyComboBox);
	hookArrows(seatComboBox);
	hookArrows(shadingComboBox);
	hookArrows(layoutComboBox);
	hookArrows(dcPanel);
	dcPanel->Bind(wxEVT_PAINT, &LiveBoothFrame::OnPaint, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_MOTION, &LiveBoothFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_MOUSEWHEEL, &LiveBoothFrame::OnMouseWheel, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_LEFT_DOWN, &LiveBoothFrame::OnDcLeftDown, this, ControlId::DcPanel);
	verticalScroll->Bind(wxEVT_SCROLL_THUMBTRACK, &LiveBoothFrame::OnVerticalScroll, this);
	verticalScroll->Bind(wxEVT_SCROLL_CHANGED, &LiveBoothFrame::OnVerticalScroll, this);
	verticalScroll->Bind(wxEVT_SCROLL_LINEUP, &LiveBoothFrame::OnVerticalScroll, this);
	verticalScroll->Bind(wxEVT_SCROLL_LINEDOWN, &LiveBoothFrame::OnVerticalScroll, this);
	verticalScroll->Bind(wxEVT_SCROLL_PAGEUP, &LiveBoothFrame::OnVerticalScroll, this);
	verticalScroll->Bind(wxEVT_SCROLL_PAGEDOWN, &LiveBoothFrame::OnVerticalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_THUMBTRACK, &LiveBoothFrame::OnHorizontalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_CHANGED, &LiveBoothFrame::OnHorizontalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_LINEUP, &LiveBoothFrame::OnHorizontalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_LINEDOWN, &LiveBoothFrame::OnHorizontalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_PAGEUP, &LiveBoothFrame::OnHorizontalScroll, this);
	horizontalScroll->Bind(wxEVT_SCROLL_PAGEDOWN, &LiveBoothFrame::OnHorizontalScroll, this);
}

void LiveBoothFrame::createChrome()
{
	chrome = new wxPanel(this, wxID_ANY);
	chrome->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

	wxArrayString modeChoices;
	modeChoices.push_back("Node Inspector");
	modeChoices.push_back("Parliament");
	modeChoices.push_back("Seat Expectations");
	modeChoices.push_back("Seat Thresholds");
	modeChoices.push_back("TPP");
	modeChoices.push_back("Vote Shares");
	modeChoices.push_back("Seat Win Chance");
	modeChoices.push_back("Seat FP");
	modeChoices.push_back("FP Percent Counted");
	modeChoices.push_back("TPP Swing Basis");
	modeChoices.push_back("TCP Swing Basis");
	modeChoices.push_back("Booth Type Bias");
	modeChoices.push_back("Vote Type Bias");
	modeChoices.push_back("Internal 2PP Metrics");

	modeLabel = new wxStaticText(chrome, wxID_ANY, "Mode:");
	modeComboBox = new wxComboBox(chrome, ControlId::PrimaryView, "Node Inspector",
		wxDefaultPosition, wxSize(200, 30), modeChoices, wxCB_READONLY);

	inspectorBar = new wxPanel(chrome, wxID_ANY);
	inspectorBar->SetBackgroundColour(chrome->GetBackgroundColour());
	runLabel = new wxStaticText(inspectorBar, wxID_ANY, "Run:");
	runComboBox = new wxComboBox(inspectorBar, ControlId::SecondaryView, "",
		wxDefaultPosition, wxSize(280, 30), wxArrayString(), wxCB_READONLY);
	filterLabel = new wxStaticText(inspectorBar, wxID_ANY, "Filter:");
	filterTextCtrl = new wxTextCtrl(inspectorBar, ControlId::FilterText, "",
		wxDefaultPosition, wxSize(180, 24));

	auto* inspectorSizer = new wxBoxSizer(wxHORIZONTAL);
	inspectorSizer->Add(runLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	inspectorSizer->Add(runComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	inspectorSizer->Add(filterLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
	inspectorSizer->Add(filterTextCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	inspectorBar->SetSizer(inspectorSizer);

	parliamentBar = new wxPanel(chrome, wxID_ANY);
	parliamentBar->SetBackgroundColour(chrome->GetBackgroundColour());
	wxArrayString viewChoices;
	viewChoices.push_back("Table View");
	viewChoices.push_back("Graph View");
	viewLabel = new wxStaticText(parliamentBar, wxID_ANY, "Display:");
	viewComboBox = new wxComboBox(parliamentBar, ControlId::ViewMode, "Table View",
		wxDefaultPosition, wxSize(130, 30), viewChoices, wxCB_READONLY);
	partyLabel = new wxStaticText(parliamentBar, wxID_ANY, "Party:");
	partyComboBox = new wxComboBox(parliamentBar, ControlId::ThresholdParty, "",
		wxDefaultPosition, wxSize(140, 30), wxArrayString(), wxCB_READONLY);
	seatLabel = new wxStaticText(parliamentBar, wxID_ANY, "Seat:");
	seatComboBox = new wxComboBox(parliamentBar, ControlId::SeatFocus, "",
		wxDefaultPosition, wxSize(160, 30), wxArrayString(), wxCB_READONLY);
	shadingLabel = new wxStaticText(parliamentBar, wxID_ANY, "Shading:");
	wxArrayString shadingChoices;
	shadingChoices.push_back("Current chance");
	shadingChoices.push_back("Change");
	shadingComboBox = new wxComboBox(parliamentBar, ControlId::ShadingMode, "Current chance",
		wxDefaultPosition, wxSize(150, 30), shadingChoices, wxCB_READONLY);
	layoutLabel = new wxStaticText(parliamentBar, wxID_ANY, "Group:");
	wxArrayString layoutChoices;
	layoutChoices.push_back("By booth type");
	layoutChoices.push_back("By statistic");
	layoutComboBox = new wxComboBox(parliamentBar, ControlId::CategoryLayout, "By booth type",
		wxDefaultPosition, wxSize(150, 30), layoutChoices, wxCB_READONLY);

	auto* parliamentSizer = new wxBoxSizer(wxHORIZONTAL);
	parliamentSizer->Add(viewLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	parliamentSizer->Add(viewComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	parliamentSizer->Add(partyLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
	parliamentSizer->Add(partyComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	parliamentSizer->Add(seatLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
	parliamentSizer->Add(seatComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	parliamentSizer->Add(shadingLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
	parliamentSizer->Add(shadingComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	parliamentSizer->Add(layoutLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
	parliamentSizer->Add(layoutComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
	parliamentBar->SetSizer(parliamentSizer);

	auto* chromeSizer = new wxBoxSizer(wxHORIZONTAL);
	chromeSizer->Add(modeLabel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	chromeSizer->Add(modeComboBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
	chromeSizer->Add(inspectorBar, 0, wxALIGN_CENTER_VERTICAL);
	chromeSizer->Add(parliamentBar, 0, wxALIGN_CENTER_VERTICAL);
	chromeSizer->AddStretchSpacer(1);
	chrome->SetSizer(chromeSizer);
}

void LiveBoothFrame::updateToolbar()
{
	updatingControls = true;
	if (mode == Mode::Internal2pp) modeComboBox->SetSelection(13);
	else if (mode == Mode::VoteTypeBias) modeComboBox->SetSelection(12);
	else if (mode == Mode::BoothTypeBias) modeComboBox->SetSelection(11);
	else if (mode == Mode::TcpSwingBasis) modeComboBox->SetSelection(10);
	else if (mode == Mode::TppSwingBasis) modeComboBox->SetSelection(9);
	else if (mode == Mode::FpCounted) modeComboBox->SetSelection(8);
	else if (mode == Mode::SeatFp) modeComboBox->SetSelection(7);
	else if (mode == Mode::SeatWinChance) modeComboBox->SetSelection(6);
	else if (mode == Mode::VoteShares) modeComboBox->SetSelection(5);
	else if (mode == Mode::Tpp) modeComboBox->SetSelection(4);
	else if (mode == Mode::SeatThresholds) modeComboBox->SetSelection(3);
	else if (mode == Mode::SeatExpectations) modeComboBox->SetSelection(2);
	else if (mode == Mode::Parliament) modeComboBox->SetSelection(1);
	else modeComboBox->SetSelection(0);
	viewComboBox->SetSelection(summaryDisplay == SummaryDisplay::Graph ? 1 : 0);

	runComboBox->Clear();
	for (auto const& record : loadResult.records) {
		runComboBox->Append(wxString::FromUTF8(LiveSnapshot::formatRunSelectorLabel(record)));
	}
	restoreRunSelection();
	restorePartyCombo();
	restoreSeatCombo();
	restoreShadingCombo();
	restoreLayoutCombo();

	bool const inspector = mode == Mode::NodeInspector;
	bool const showParty = mode == Mode::SeatThresholds || isSeatMetricMode();
	bool const showSeat = (isSeatMetricMode() || isCompletionMode()) &&
		summaryDisplay == SummaryDisplay::Graph;
	bool const showShading = isSeatMetricMode() &&
		summaryDisplay == SummaryDisplay::Table;
	bool const showLayout = isCategoryStatMode();
	auto* sizer = chrome->GetSizer();
	sizer->Show(inspectorBar, inspector);
	sizer->Show(parliamentBar, !inspector);
	auto* parliamentSizer = parliamentBar->GetSizer();
	parliamentSizer->Show(partyLabel, showParty);
	parliamentSizer->Show(partyComboBox, showParty);
	parliamentSizer->Show(seatLabel, showSeat);
	parliamentSizer->Show(seatComboBox, showSeat);
	parliamentSizer->Show(shadingLabel, showShading);
	parliamentSizer->Show(shadingComboBox, showShading);
	parliamentSizer->Show(layoutLabel, showLayout);
	parliamentSizer->Show(layoutComboBox, showLayout);
	parliamentBar->Layout();
	chrome->Layout();
	updatingControls = false;
}

void LiveBoothFrame::layoutContents()
{
	wxSize const client = GetClientSize();
	int const top = chromeHeight();
	if (chrome) chrome->SetSize(0, 0, client.GetWidth(), top);

	bool const table = showingSummaryTable();
	int viewportWidth = client.GetWidth();
	int viewportHeight = std::max(0, client.GetHeight() - top);

	bool needVertical = false;
	bool needHorizontal = false;
	int bodyViewportHeight = 0;
	if (table) {
		int const tableOriginY = statusBandHeight();
		auto const tableBodyViewport = [&]() {
			int const tableViewportHeight = std::max(0, viewportHeight - tableOriginY);
			return std::max(0, tableViewportHeight - tableLayout.metrics.headerHeight);
		};
		bodyViewportHeight = tableBodyViewport();
		needVertical = tableLayout.bodyHeight() > bodyViewportHeight;
		needHorizontal = tableLayout.contentWidth() > viewportWidth;
		if (needVertical) viewportWidth = std::max(0, viewportWidth - ScrollBarThickness);
		if (needHorizontal) {
			viewportHeight = std::max(0, viewportHeight - ScrollBarThickness);
			bodyViewportHeight = tableBodyViewport();
		}
		needVertical = tableLayout.bodyHeight() > bodyViewportHeight;
		needHorizontal = tableLayout.contentWidth() > viewportWidth;
		if (needVertical) viewportWidth = std::max(0, client.GetWidth() - ScrollBarThickness);
		if (needHorizontal) {
			viewportHeight = std::max(0, client.GetHeight() - top - ScrollBarThickness);
			bodyViewportHeight = tableBodyViewport();
		}

		scrollX = tableLayout.clampScrollX(scrollX, std::max(1, viewportWidth));
		scrollY = tableLayout.clampScrollY(scrollY, std::max(1, bodyViewportHeight));
	}

	dcPanel->SetSize(0, top, viewportWidth, viewportHeight);
	if (needVertical) {
		verticalScroll->SetSize(viewportWidth, top, ScrollBarThickness, viewportHeight);
		verticalScroll->Show();
	}
	else {
		verticalScroll->Hide();
	}
	if (needHorizontal) {
		horizontalScroll->SetSize(0, top + viewportHeight, viewportWidth, ScrollBarThickness);
		horizontalScroll->Show();
	}
	else {
		horizontalScroll->Hide();
	}

	if (needVertical) {
		int const thumb = std::max(1, bodyViewportHeight);
		int const range = std::max(thumb, tableLayout.bodyHeight());
		verticalScroll->SetScrollbar(scrollY, thumb, range, thumb);
	}
	if (needHorizontal) {
		int const thumb = std::max(1, viewportWidth);
		int const range = std::max(thumb, tableLayout.contentWidth());
		horizontalScroll->SetScrollbar(scrollX, thumb, range, thumb);
	}
}

void LiveBoothFrame::applyScrollbars()
{
	layoutContents();
}

void LiveBoothFrame::render(wxDC& dc)
{
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
	dc.SetTextForeground(*wxBLACK);

	renderStatus(dc);
	if (loadResult.status != LiveSnapshot::LoadStatus::Ok) return;

	if (mode == Mode::NodeInspector) {
		renderNodeInspector(dc, statusBandHeight() + 8);
	}
	else {
		renderParliament(dc, statusBandHeight());
	}
}

void LiveBoothFrame::renderStatus(wxDC& dc) const
{
	int y = 4;
	dc.DrawText(wxString::FromUTF8(LiveSnapshot::formatStatusLine(loadResult)), wxPoint(8, y));
	y += StatusLineHeight;
	if (!loadResult.warnings.empty()) {
		auto const& warning = loadResult.warnings.front();
		std::string text = warning.filename.empty() ? warning.message :
			warning.filename + ": " + warning.message;
		if (loadResult.warnings.size() > 1) {
			text += "  (+" + std::to_string(loadResult.warnings.size() - 1) + " more)";
		}
		dc.SetTextForeground(wxColour(120, 40, 40));
		dc.DrawText(wxString::FromUTF8(text), wxPoint(8, y - 2));
		dc.SetTextForeground(*wxBLACK);
	}
}

void LiveBoothFrame::renderNodeInspector(wxDC& dc, int y) const
{
	if (loadResult.records.empty()) {
		dc.DrawText("No live snapshot files in this output folder.", wxPoint(8, y));
		return;
	}

	LiveSnapshot::SnapshotRecord const* selected = nullptr;
	for (auto const& record : loadResult.records) {
		if (record.filename == selectedFilename) {
			selected = &record;
			break;
		}
	}
	if (!selected) selected = &loadResult.records.back();
	auto const summary = LiveSnapshot::inspectorSummary(*selected);
	if (!summary) {
		dc.DrawText("Selected snapshot could not be summarised.", wxPoint(8, y));
		return;
	}

	dc.DrawText("Live booth inspector proof of concept", wxPoint(8, y));
	y += 28;
	dc.DrawText(wxString::FromUTF8("File: " + summary->filename), wxPoint(8, y));
	y += 22;
	dc.DrawText(wxString::FromUTF8("Snapshot: " +
		LiveSnapshot::formatSnapshotTimestamp(summary->snapshotCode)), wxPoint(8, y));
	y += 22;
	dc.DrawText(wxString::FromUTF8("Completed: " +
		LiveSnapshot::formatCompletedAt(summary->completedAt)), wxPoint(8, y));
	y += 22;
	dc.DrawText(wxString::FromUTF8("Seats: " + std::to_string(summary->seatCount) +
		"    Booths: " + std::to_string(summary->boothCount)), wxPoint(8, y));
	y += 28;
	if (!summary->firstBoothName && !summary->firstBoothSeat) {
		dc.DrawText("No booth snapshots in live_analysis.", wxPoint(8, y));
		return;
	}
	dc.DrawText("First booth snapshot:", wxPoint(8, y));
	y += 22;
	dc.DrawText(wxString::FromUTF8("Seat: " + summary->firstBoothSeat.value_or("")), wxPoint(8, y));
	y += 22;
	dc.DrawText(wxString::FromUTF8("Booth: " + summary->firstBoothName.value_or("")), wxPoint(8, y));
}

void LiveBoothFrame::renderParliament(wxDC& dc, int originY) const
{
	auto const& view = activeTableView();
	if (view.rows.empty()) {
		dc.DrawText("No snapshot rows to display.", wxPoint(8, originY + 8));
		return;
	}
	if (showingSummaryGraph()) {
		renderSummaryGraph(dc, originY);
		return;
	}
	renderSummaryTable(dc, originY);
}

void LiveBoothFrame::renderSummaryTable(wxDC& dc, int originY) const
{
	auto const& view = activeTableView();
	auto const size = dcPanel->GetClientSize();
	int const viewportWidth = size.GetWidth();
	int const viewportHeight = std::max(0, size.GetHeight() - originY);
	int const bodyViewportHeight = std::max(0, viewportHeight - tableLayout.metrics.headerHeight);
	int const clampedX = tableLayout.clampScrollX(scrollX, viewportWidth);
	int const clampedY = tableLayout.clampScrollY(scrollY, bodyViewportHeight);
	auto const visible = tableLayout.visibleCells(
		viewportWidth, viewportHeight, clampedX, clampedY);

	wxPen const defaultBorder(wxColour(200, 200, 200), 1);
	wxPen const increaseBorder(wxColour(0, 255, 255), 3);
	wxPen const decreaseBorder(wxColour(255, 0, 0), 3);

	auto const drawCell = [&](LiveSnapshot::TableRect const& rect, std::string const& text,
		wxColour const& background, wxColour const& textColour, wxPen const& borderPen) {
		wxRect wxrect(rect.x, rect.y + originY, rect.width, rect.height);
		dc.SetPen(borderPen);
		dc.SetBrush(wxBrush(background));
		dc.DrawRectangle(wxrect);
		wxDCClipper const cellClip(dc, wxrect);
		dc.SetTextForeground(textColour);
		dc.DrawText(wxString::FromUTF8(text), wxPoint(wxrect.x + 6, wxrect.y + 4));
	};

	auto const drawColumnRange = [&](int firstCol, int lastCol) {
		for (int column = firstCol; column < lastCol; ++column) {
			auto const rect = tableLayout.headerRect(column, clampedX);
			auto const background = headerColour(view.columns[column]);
			drawCell(rect, view.columns[column].header, background, *wxBLACK, defaultBorder);
		}
		for (int row = visible.firstRow; row < visible.lastRow; ++row) {
			for (int column = firstCol; column < lastCol; ++column) {
				auto const rect = tableLayout.bodyRect(row, column, clampedX, clampedY);
				std::string text;
				if (row < int(view.cells.size()) &&
					column < int(view.cells[row].size())) {
					text = view.cells[row][column].text;
				}
				wxColour background = bodyColour(view.columns[column], row);
				wxColour textColour = *wxBLACK;
				wxPen const* border = &defaultBorder;
				if (view.columns[column].kind ==
					LiveSnapshot::ParliamentColumn::Kind::SeatWinChance ||
					view.columns[column].kind ==
					LiveSnapshot::ParliamentColumn::Kind::SeatFp) {
					auto const style = seatWinStyle(row, column);
					background = wxColour(
						style.background.r, style.background.g, style.background.b);
					if (style.whiteText) textColour = *wxWHITE;
				}
				else if (view.columns[column].kind ==
					LiveSnapshot::ParliamentColumn::Kind::SeatCompletion) {
					auto const style = completionStyle(row, column);
					background = wxColour(
						style.background.r, style.background.g, style.background.b);
					if (style.changeBorder ==
						LiveSnapshot::CompletionChangeBorder::Increase) {
						border = &increaseBorder;
					}
					else if (style.changeBorder ==
						LiveSnapshot::CompletionChangeBorder::Decrease) {
						border = &decreaseBorder;
					}
				}
				drawCell(rect, text, background, textColour, *border);
			}
		}
	};

	wxDCClipper const tableClip(dc, wxRect(0, originY, viewportWidth, viewportHeight));
	int const freezeCount = std::clamp(
		tableLayout.frozenColumns, 0, tableLayout.columnCount());
	int const freezeWidth = tableLayout.frozenWidth();
	{
		wxDCClipper const scrollClip(dc, wxRect(
			freezeWidth,
			originY,
			std::max(0, viewportWidth - freezeWidth),
			viewportHeight));
		drawColumnRange(visible.firstCol, visible.lastCol);
	}
	if (freezeCount > 0) {
		drawColumnRange(0, freezeCount);
	}
	dc.SetTextForeground(*wxBLACK);
}

void LiveBoothFrame::renderSummaryGraph(wxDC& dc, int originY) const
{
	auto const layout = currentGraphLayout();
	auto const plot = layout.plot;
	wxRect const plotRect(plot.x, plot.y + originY, plot.width, plot.height);
	wxDCClipper const graphClip(dc,
		wxRect(0, originY, dcPanel->GetClientSize().GetWidth(),
			std::max(0, dcPanel->GetClientSize().GetHeight() - originY)));

	dc.SetPen(wxPen(wxColour(210, 210, 210)));
	dc.SetBrush(*wxWHITE_BRUSH);
	dc.DrawRectangle(plotRect);

	dc.SetTextForeground(wxColour(80, 80, 80));
	for (auto const& tick : layout.yTicks) {
		int const y = layout.yForValue(tick.value) + originY;
		dc.SetPen(wxPen(wxColour(230, 230, 230)));
		dc.DrawLine(plotRect.GetLeft(), y, plotRect.GetRight(), y);
		wxString const label = wxString::FromUTF8(tick.label);
		wxSize const textSize = dc.GetTextExtent(label);
		dc.DrawText(label, wxPoint(plot.x - textSize.GetWidth() - 6, y - textSize.GetHeight() / 2));
	}

	dc.SetPen(wxPen(wxColour(160, 160, 160)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(plotRect);

	if (layout.xBreakpointPixel) {
		int const x = *layout.xBreakpointPixel;
		dc.SetPen(wxPen(wxColour(170, 170, 180), 1, wxPENSTYLE_SHORT_DASH));
		dc.DrawLine(x, plotRect.GetTop(), x, plotRect.GetBottom());
	}

	for (auto const& series : graphModel.series) {
		dc.SetPen(graphPen(series));
		int prevX = 0;
		int prevY = 0;
		bool havePrev = false;
		int const count = std::min(int(series.values.size()), int(layout.xPixels.size()));
		for (int point = 0; point < count; ++point) {
			if (!series.values[point]) {
				havePrev = false;
				continue;
			}
			int const x = layout.xPixels[point];
			int const y = layout.yForValue(*series.values[point]) + originY;
			if (havePrev) dc.DrawLine(prevX, prevY, x, y);
			prevX = x;
			prevY = y;
			havePrev = true;
		}
		dc.SetBrush(wxBrush(graphColour(series.colour)));
		dc.SetPen(wxPen(graphColour(series.colour)));
		for (int point = 0; point < count; ++point) {
			if (!series.values[point]) continue;
			dc.DrawCircle(
				layout.xPixels[point],
				layout.yForValue(*series.values[point]) + originY,
				3);
		}
	}

	dc.SetTextForeground(wxColour(60, 60, 60));
	for (int index : layout.xLabelIndices) {
		if (index < 0 || index >= int(graphModel.xLabels.size())) continue;
		wxString const label = wxString::FromUTF8(graphModel.xLabels[index]);
		wxSize const textSize = dc.GetTextExtent(label);
		int const x = layout.xPixels[index] - textSize.GetWidth() / 2;
		int const y = plotRect.GetBottom() + 6;
		dc.DrawText(label, wxPoint(std::max(plotRect.GetLeft(), x), y));
	}

	int const legendLeft = layout.legend.x + 8;
	int legendY = layout.legend.y + originY;
	for (auto const& series : graphModel.series) {
		dc.SetPen(graphPen(series));
		int const mid = legendY + 8;
		dc.DrawLine(legendLeft, mid, legendLeft + 22, mid);
		dc.SetBrush(wxBrush(graphColour(series.colour)));
		dc.DrawCircle(legendLeft + 11, mid, 3);
		dc.SetTextForeground(*wxBLACK);
		dc.DrawText(wxString::FromUTF8(series.label), wxPoint(legendLeft + 28, legendY));
		legendY += layout.metrics.legendItemHeight;
	}
}

void LiveBoothFrame::rememberRunSelection()
{
	int const selection = runComboBox->GetSelection();
	if (selection >= 0 && selection < int(loadResult.records.size())) {
		selectedFilename = loadResult.records[selection].filename;
	}
}

void LiveBoothFrame::restoreRunSelection()
{
	if (loadResult.records.empty()) {
		selectedFilename.clear();
		return;
	}
	int found = -1;
	for (int index = 0; index < int(loadResult.records.size()); ++index) {
		if (loadResult.records[index].filename == selectedFilename) {
			found = index;
			break;
		}
	}
	if (found < 0) {
		found = int(loadResult.records.size()) - 1;
		selectedFilename = loadResult.records[found].filename;
	}
	runComboBox->SetSelection(found);
}

void LiveBoothFrame::restorePartyCombo()
{
	partyComboBox->Clear();
	if (mode == Mode::SeatThresholds) {
		int selected = 0;
		for (int index = 0; index < int(thresholdParties.size()); ++index) {
			partyComboBox->Append(wxString::FromUTF8(thresholdParties[index].label));
			if (thresholdParties[index].partyIndex == selectedThresholdPartyIndex) {
				selected = index;
			}
		}
		if (!thresholdParties.empty()) {
			partyComboBox->SetSelection(selected);
		}
		return;
	}
	if (mode != Mode::SeatWinChance && mode != Mode::SeatFp) return;

	auto const& parties = mode == Mode::SeatFp ? seatFpParties : seatWinParties;
	bool const allParties = mode == Mode::SeatFp ?
		seatFpGraphAllParties : seatWinGraphAllParties;
	int const tableParty = mode == Mode::SeatFp ?
		selectedSeatFpTablePartyIndex : selectedSeatWinTablePartyIndex;
	int const graphParty = mode == Mode::SeatFp ?
		selectedSeatFpGraphPartyIndex : selectedSeatWinGraphPartyIndex;

	int selected = 0;
	if (summaryDisplay == SummaryDisplay::Graph) {
		partyComboBox->Append("All parties");
		for (int index = 0; index < int(parties.size()); ++index) {
			partyComboBox->Append(wxString::FromUTF8(parties[index].label));
			if (!allParties && parties[index].partyIndex == graphParty) {
				selected = index + 1;
			}
		}
		partyComboBox->SetSelection(selected);
		return;
	}

	for (int index = 0; index < int(parties.size()); ++index) {
		partyComboBox->Append(wxString::FromUTF8(parties[index].label));
		if (parties[index].partyIndex == tableParty) {
			selected = index;
		}
	}
	if (!parties.empty()) {
		partyComboBox->SetSelection(selected);
	}
}

void LiveBoothFrame::restoreSeatCombo()
{
	seatComboBox->Clear();
	int selected = 0;
	for (int index = 0; index < int(seatWinSeats.size()); ++index) {
		seatComboBox->Append(wxString::FromUTF8(seatWinSeats[index]));
		if (seatWinSeats[index] == selectedSeatWinSeatName) {
			selected = index;
		}
	}
	if (!seatWinSeats.empty()) {
		seatComboBox->SetSelection(selected);
	}
}

void LiveBoothFrame::restoreShadingCombo()
{
	shadingComboBox->Clear();
	shadingComboBox->Append(mode == Mode::SeatFp ? "Projected FP" : "Current chance");
	shadingComboBox->Append("Change");
	shadingComboBox->SetSelection(
		seatWinShading == LiveSnapshot::SeatWinShading::Change ? 1 : 0);
}

void LiveBoothFrame::restoreLayoutCombo()
{
	if (!layoutComboBox) return;
	layoutComboBox->Clear();
	if (!isCategoryStatMode()) return;
	if (summaryDisplay == SummaryDisplay::Graph) {
		layoutLabel->SetLabel("Statistic:");
		for (int index = 0; index < LiveSnapshot::CategoryStatisticCount; ++index) {
			layoutComboBox->Append(LiveSnapshot::categoryStatisticTitle(
				LiveSnapshot::categoryStatisticAt(index)));
		}
		layoutComboBox->SetSelection(int(categoryGraphStatistic));
		return;
	}
	layoutLabel->SetLabel("Group:");
	layoutComboBox->Append(mode == Mode::VoteTypeBias ? "By vote type" : "By booth type");
	layoutComboBox->Append("By statistic");
	layoutComboBox->SetSelection(categoryGroupByType ? 0 : 1);
}

int LiveBoothFrame::chromeHeight() const
{
	if (!chrome) return ChromeMinHeight;
	wxSize const best = chrome->GetBestSize();
	return std::max(ChromeMinHeight, best.GetHeight());
}

void LiveBoothFrame::rebuildPresentation()
{
	auto const& view = activeTableView();
	tableLayout = LiveSnapshot::makeParliamentLayout(view);
	if (isCompletionMode()) {
		tableLayout.frozenColumns = 1;
		graphModel = LiveSnapshot::makeCompletionGraph(completionGraphView);
	}
	else if (isCategoryStatMode()) {
		tableLayout.frozenColumns = 1;
		graphModel = LiveSnapshot::makeCategoryStatGraph(
			categoryStatGraphView,
			LiveSnapshot::categoryStatisticInteger(categoryGraphStatistic));
	}
	else if (mode == Mode::Internal2pp) {
		graphModel = LiveSnapshot::makeInternal2ppGraph(view);
	}
	else if (mode == Mode::SeatWinChance || mode == Mode::SeatFp) {
		tableLayout.frozenColumns = 1;
		if (mode == Mode::SeatFp) {
			graphModel = LiveSnapshot::makeSeatFpGraph(seatFpGraphView);
		}
		else {
			graphModel = LiveSnapshot::makeSeatWinChanceGraph(
				seatWinGraphView, !seatWinGraphAllParties);
		}
	}
	else if (mode == Mode::VoteShares) {
		graphModel = LiveSnapshot::makeVoteShareGraph(view);
	}
	else if (mode == Mode::Tpp) {
		graphModel = LiveSnapshot::makeTppGraph(view);
	}
	else if (mode == Mode::SeatThresholds) {
		graphModel = LiveSnapshot::makeSeatThresholdGraph(view);
	}
	else if (mode == Mode::SeatExpectations) {
		graphModel = LiveSnapshot::makeSeatExpectationGraph(view);
	}
	else {
		graphModel = LiveSnapshot::makeParliamentGraph(view);
	}
}

void LiveBoothFrame::syncThresholdView()
{
	thresholdParties = LiveSnapshot::listSeatThresholdParties(loadResult.records);
	bool found = false;
	for (auto const& option : thresholdParties) {
		if (option.partyIndex == selectedThresholdPartyIndex) {
			found = true;
			break;
		}
	}
	if (!found) {
		selectedThresholdPartyIndex = 0;
		found = false;
		for (auto const& option : thresholdParties) {
			if (option.partyIndex == 0) {
				found = true;
				break;
			}
		}
		if (!found && !thresholdParties.empty()) {
			selectedThresholdPartyIndex = thresholdParties.front().partyIndex;
		}
	}
	thresholdView = LiveSnapshot::buildSeatThresholdView(
		loadResult.records, selectedThresholdPartyIndex);
}

void LiveBoothFrame::syncSeatWinViews()
{
	seatWinParties = LiveSnapshot::listSeatWinChanceParties(loadResult.records);
	seatWinSeats = LiveSnapshot::listSeatWinChanceSeats(loadResult.records);

	auto const partyExists = [](
		std::vector<LiveSnapshot::ThresholdPartyOption> const& options, int partyIndex) {
		for (auto const& option : options) {
			if (option.partyIndex == partyIndex) return true;
		}
		return false;
	};

	if (!partyExists(seatWinParties, selectedSeatWinTablePartyIndex)) {
		selectedSeatWinTablePartyIndex = 0;
		if (!partyExists(seatWinParties, 0) && !seatWinParties.empty()) {
			selectedSeatWinTablePartyIndex = seatWinParties.front().partyIndex;
		}
	}
	if (!seatWinGraphAllParties &&
		!partyExists(seatWinParties, selectedSeatWinGraphPartyIndex)) {
		seatWinGraphAllParties = true;
	}

	bool seatFound = false;
	for (auto const& name : seatWinSeats) {
		if (name == selectedSeatWinSeatName) {
			seatFound = true;
			break;
		}
	}
	if (!seatFound) {
		selectedSeatWinSeatName = seatWinSeats.empty() ? "" : seatWinSeats.front();
	}

	seatWinTableView = LiveSnapshot::buildSeatWinChanceView(
		loadResult.records, selectedSeatWinTablePartyIndex);
	std::optional<int> graphParty;
	if (!seatWinGraphAllParties) graphParty = selectedSeatWinGraphPartyIndex;
	seatWinGraphView = LiveSnapshot::buildSeatWinChanceGraphView(
		loadResult.records, selectedSeatWinSeatName, graphParty);
}

void LiveBoothFrame::syncSeatFpViews()
{
	seatFpParties = LiveSnapshot::listSeatFpParties(loadResult.records);
	seatWinSeats = LiveSnapshot::listSeatWinChanceSeats(loadResult.records);

	auto const partyExists = [](
		std::vector<LiveSnapshot::ThresholdPartyOption> const& options, int partyIndex) {
		for (auto const& option : options) {
			if (option.partyIndex == partyIndex) return true;
		}
		return false;
	};

	if (!partyExists(seatFpParties, selectedSeatFpTablePartyIndex)) {
		selectedSeatFpTablePartyIndex = 0;
		if (!partyExists(seatFpParties, 0) && !seatFpParties.empty()) {
			selectedSeatFpTablePartyIndex = seatFpParties.front().partyIndex;
		}
	}
	if (!seatFpGraphAllParties &&
		!partyExists(seatFpParties, selectedSeatFpGraphPartyIndex)) {
		seatFpGraphAllParties = true;
	}

	bool seatFound = false;
	for (auto const& name : seatWinSeats) {
		if (name == selectedSeatWinSeatName) {
			seatFound = true;
			break;
		}
	}
	if (!seatFound) {
		selectedSeatWinSeatName = seatWinSeats.empty() ? "" : seatWinSeats.front();
	}

	seatFpTableView = LiveSnapshot::buildSeatFpView(
		loadResult.records, selectedSeatFpTablePartyIndex);
	std::optional<int> graphParty;
	if (!seatFpGraphAllParties) graphParty = selectedSeatFpGraphPartyIndex;
	seatFpGraphView = LiveSnapshot::buildSeatFpGraphView(
		loadResult.records, selectedSeatWinSeatName, graphParty);
}

void LiveBoothFrame::syncCompletionViews()
{
	seatWinSeats = LiveSnapshot::listSeatWinChanceSeats(loadResult.records);

	bool seatFound = false;
	for (auto const& name : seatWinSeats) {
		if (name == selectedSeatWinSeatName) {
			seatFound = true;
			break;
		}
	}
	if (!seatFound) {
		selectedSeatWinSeatName = seatWinSeats.empty() ? "" : seatWinSeats.front();
	}

	char const* const key = completionValueKey();
	completionTableView = LiveSnapshot::buildSeatCompletionView(loadResult.records, key);
	completionGraphView = LiveSnapshot::buildSeatCompletionGraphView(
		loadResult.records, selectedSeatWinSeatName, key);
}

void LiveBoothFrame::syncCategoryStatViews()
{
	char const* const key = categoryStatArrayKey();
	auto const grouping = categoryGroupByType ?
		LiveSnapshot::CategoryStatGrouping::ByCategory :
		LiveSnapshot::CategoryStatGrouping::ByStatistic;
	categoryStatTableView = LiveSnapshot::buildCategoryStatTableView(
		loadResult.records, key, grouping);
	categoryStatGraphView = LiveSnapshot::buildCategoryStatGraphView(
		loadResult.records, key, categoryGraphStatistic);
}

bool LiveBoothFrame::isSeatMetricMode() const
{
	return mode == Mode::SeatWinChance || mode == Mode::SeatFp;
}

bool LiveBoothFrame::isCompletionMode() const
{
	return mode == Mode::FpCounted ||
		mode == Mode::TppSwingBasis ||
		mode == Mode::TcpSwingBasis;
}

bool LiveBoothFrame::isCategoryStatMode() const
{
	return mode == Mode::BoothTypeBias || mode == Mode::VoteTypeBias;
}

bool LiveBoothFrame::isWideSeatTableMode() const
{
	return isSeatMetricMode() || isCompletionMode() || isCategoryStatMode();
}

char const* LiveBoothFrame::completionValueKey() const
{
	if (mode == Mode::TppSwingBasis) return "seat_tpp_completion";
	if (mode == Mode::TcpSwingBasis) return "seat_tcp_completion";
	return "seat_fp_completion";
}

char const* LiveBoothFrame::categoryStatArrayKey() const
{
	return mode == Mode::VoteTypeBias ? "vote_type" : "booth_type";
}

LiveSnapshot::ParliamentView const& LiveBoothFrame::activeTableView() const
{
	if (mode == Mode::Internal2pp) return internal2ppView;
	if (isCategoryStatMode()) return categoryStatTableView;
	if (isCompletionMode()) return completionTableView;
	if (mode == Mode::SeatFp) return seatFpTableView;
	if (mode == Mode::SeatWinChance) return seatWinTableView;
	if (mode == Mode::VoteShares) return voteShareView;
	if (mode == Mode::Tpp) return tppView;
	if (mode == Mode::SeatThresholds) return thresholdView;
	if (mode == Mode::SeatExpectations) return seatView;
	return parliamentView;
}

int LiveBoothFrame::statusBandHeight() const
{
	int height = StatusLineHeight;
	if (!loadResult.warnings.empty() ||
		loadResult.status != LiveSnapshot::LoadStatus::Ok) {
		height += StatusLineHeight;
	}
	return height;
}

bool LiveBoothFrame::showingSummaryMode() const
{
	return mode == Mode::Parliament ||
		mode == Mode::SeatExpectations ||
		mode == Mode::SeatThresholds ||
		mode == Mode::Tpp ||
		mode == Mode::VoteShares ||
		mode == Mode::SeatWinChance ||
		mode == Mode::SeatFp ||
		isCompletionMode() ||
		isCategoryStatMode() ||
		mode == Mode::Internal2pp;
}

bool LiveBoothFrame::showingSummaryTable() const
{
	return showingSummaryMode() &&
		summaryDisplay == SummaryDisplay::Table &&
		loadResult.status == LiveSnapshot::LoadStatus::Ok &&
		!activeTableView().rows.empty();
}

bool LiveBoothFrame::showingSummaryGraph() const
{
	return showingSummaryMode() &&
		summaryDisplay == SummaryDisplay::Graph &&
		loadResult.status == LiveSnapshot::LoadStatus::Ok &&
		!activeTableView().rows.empty();
}

wxColour LiveBoothFrame::headerColour(
	LiveSnapshot::ParliamentColumn const& column) const
{
	if (column.kind == LiveSnapshot::ParliamentColumn::Kind::Separator) {
		return wxColour(226, 226, 232);
	}
	if (!column.colour) return wxColour(236, 236, 236);
	auto const outcome =
		column.kind == LiveSnapshot::ParliamentColumn::Kind::PartyOutcome ||
		column.kind == LiveSnapshot::ParliamentColumn::Kind::SeatThreshold ||
		column.kind == LiveSnapshot::ParliamentColumn::Kind::TppThreshold ?
			column.outcome : LiveSnapshot::ParliamentColumn::Outcome::Majority;
	auto const faded = LiveSnapshot::fadeParliamentHeaderColour(*column.colour, outcome);
	return wxColour(faded.r, faded.g, faded.b);
}

wxColour LiveBoothFrame::bodyColour(
	LiveSnapshot::ParliamentColumn const& column, int row) const
{
	if (column.kind == LiveSnapshot::ParliamentColumn::Kind::Separator) {
		return wxColour(226, 226, 232);
	}
	if (column.kind == LiveSnapshot::ParliamentColumn::Kind::CoalitionSeats ||
		column.kind == LiveSnapshot::ParliamentColumn::Kind::TppMean ||
		column.kind == LiveSnapshot::ParliamentColumn::Kind::CoalitionVoteShare) {
		return row % 2 == 1 ? wxColour(236, 242, 250) : wxColour(244, 248, 252);
	}
	return row % 2 == 1 ? wxColour(248, 248, 248) : *wxWHITE;
}

LiveSnapshot::SeatWinCellStyle LiveBoothFrame::seatWinStyle(int row, int column) const
{
	auto const& view = activeTableView();
	LiveSnapshot::CellValue cell;
	std::optional<LiveSnapshot::PartyColour> colour;
	if (column >= 0 && column < int(view.columns.size())) {
		colour = view.columns[column].colour;
	}
	if (row >= 0 && row < int(view.cells.size()) &&
		column >= 0 && column < int(view.cells[row].size())) {
		cell = view.cells[row][column];
	}
	std::optional<double> previous;
	if (row > 0 && row - 1 < int(view.cells.size()) &&
		column >= 0 && column < int(view.cells[row - 1].size())) {
		auto const& previousCell = view.cells[row - 1][column];
		if (previousCell.kind == LiveSnapshot::CellValue::Kind::Percent ||
			previousCell.kind == LiveSnapshot::CellValue::Kind::Value) {
			previous = previousCell.percent;
		}
	}
	bool const called = mode == Mode::SeatWinChance &&
		LiveSnapshot::seatWinCalledUpToRow(view.cells, row, column);
	auto const settings = mode == Mode::SeatFp ?
		LiveSnapshot::seatFpShadeSettings() :
		LiveSnapshot::seatWinShadeSettings();
	return LiveSnapshot::seatWinCellStyle(
		colour, cell, previous, seatWinShading, called, settings);
}

LiveSnapshot::CompletionCellStyle LiveBoothFrame::completionStyle(int row, int column) const
{
	auto const& view = activeTableView();
	LiveSnapshot::CellValue cell;
	if (row >= 0 && row < int(view.cells.size()) &&
		column >= 0 && column < int(view.cells[row].size())) {
		cell = view.cells[row][column];
	}
	std::optional<double> previous;
	if (row > 0 && row - 1 < int(view.cells.size()) &&
		column >= 0 && column < int(view.cells[row - 1].size())) {
		auto const& previousCell = view.cells[row - 1][column];
		if (previousCell.kind == LiveSnapshot::CellValue::Kind::Percent ||
			previousCell.kind == LiveSnapshot::CellValue::Kind::Value) {
			previous = previousCell.percent;
		}
	}
	return LiveSnapshot::completionCellStyle(cell, previous);
}

wxColour LiveBoothFrame::graphColour(LiveSnapshot::PartyColour const& colour) const
{
	return wxColour(colour.r, colour.g, colour.b);
}

wxPen LiveBoothFrame::graphPen(LiveSnapshot::GraphSeries const& series) const
{
	wxPenStyle style = wxPENSTYLE_SOLID;
	if (series.style == LiveSnapshot::GraphLineStyle::Dash) style = wxPENSTYLE_SHORT_DASH;
	else if (series.style == LiveSnapshot::GraphLineStyle::Dot) style = wxPENSTYLE_DOT;
	return wxPen(graphColour(series.colour), 2, style);
}

wxString LiveBoothFrame::parliamentTooltip(LiveSnapshot::TableLayout::Hit const& hit) const
{
	if (hit.kind != LiveSnapshot::TableLayout::HitKind::Body) return {};
	auto const& view = activeTableView();
	if (hit.row < 0 || hit.row >= int(view.rows.size())) return {};
	if (hit.column >= 0 && hit.column < int(view.columns.size()) &&
		view.columns[hit.column].kind == LiveSnapshot::ParliamentColumn::Kind::Separator) {
		return {};
	}
	auto const& row = view.rows[hit.row];
	std::string text = row.selectedFilename + "\nCompleted: " +
		LiveSnapshot::formatCompletedAt(row.completedAt);
	if (row.duplicateCount > 1) {
		text += "\n" + std::to_string(row.duplicateCount) + " runs for this snapshot";
	}
	return wxString::FromUTF8(text);
}

wxString LiveBoothFrame::graphTooltip(LiveSnapshot::GraphLayout::Hit const& hit) const
{
	if (hit.kind != LiveSnapshot::GraphLayout::Hit::Kind::Point) return {};
	if (hit.series < 0 || hit.series >= int(graphModel.series.size())) return {};
	if (hit.point < 0 || hit.point >= int(graphModel.xLabels.size())) return {};
	auto const& series = graphModel.series[hit.series];
	if (hit.point >= int(series.values.size()) || !series.values[hit.point]) return {};
	std::string const value = graphModel.yIsPercent ?
		LiveSnapshot::formatPercent(*series.values[hit.point]) :
		(graphModel.yIntegerLabels ?
			LiveSnapshot::formatSeatCount(*series.values[hit.point]) :
			LiveSnapshot::formatSeatExpectation(*series.values[hit.point]));
	std::string text = series.label + "\n" + graphModel.xLabels[hit.point] + "\n" + value;
	return wxString::FromUTF8(text);
}

LiveSnapshot::GraphLayout LiveBoothFrame::currentGraphLayout() const
{
	auto const size = dcPanel->GetClientSize();
	int const originY = statusBandHeight();
	return LiveSnapshot::layoutParliamentGraph(
		graphModel,
		size.GetWidth(),
		std::max(0, size.GetHeight() - originY));
}
