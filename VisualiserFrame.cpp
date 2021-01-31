#include "VisualiserFrame.h"

#include "General.h"
#include "SpecialPartyCodes.h"

#include <algorithm>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	TogglePolls,
	ToggleModels,
	ToggleHouseEffects,
	ToggleProjections,
	SelectModel,
	SelectProjection,
	SelectParty,
	ModelDisplayMode,
};

// maximum distance from the mouse pointer to a poll point that will allow it to be selected.
const int SelectMousePollDistance = 4;
const int SelectMousePollDistanceSquared = SelectMousePollDistance * SelectMousePollDistance;

const wxColour BackgroundColour = wxColour(255, 255, 255);
const wxColour AxisColour = wxColour(0, 0, 0);
const wxColour ModelColour = wxColour(0, 0, 0);
const wxColour PollSelectionColour = wxColour(255, 255, 0);
const wxColour PollInfoBorderColour = wxColour(0, 0, 0);
const wxColour PollInfoBackgroundColour = wxColour(255, 255, 255);
const wxColour PollInfoTextColour = wxColour(0, 0, 0);

struct ModelRange {
	int lowerPercentile;
	int upperPercentile;
	float colourStrength;
};

constexpr std::array<ModelRange, 4> SpreadRanges = {{
	{1, 99, 0.25f},
	{5, 95, 0.4f},
	{10, 90, 0.55f},
	{25, 75, 0.7f}
}};

constexpr int MedianPercentile = 50;

constexpr int ModelMouseoverYTolerance = 100;

constexpr int PollInfoPadding = 3;
constexpr int PollInfoLineHeight = 20;

// frame constructor
VisualiserFrame::VisualiserFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Visualiser", wxPoint(333, 0), project),
	refresher(refresher)
{
	refreshToolbar();
	createDcPanel();
	bindEventHandlers();
	Layout();
	paint();
}

void VisualiserFrame::paint(bool resetMouse) {
	if (resetMouse) this->resetMouseOver();
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void VisualiserFrame::refreshData() {
	refreshToolbar();
}

void VisualiserFrame::resetMouseOver() {
	mouseoverPoll = Poll::InvalidId;
}

void VisualiserFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the poll data table to the entire client size.
	//pollData->SetSize(wxSize(dcPanel->GetClientSize().x,
	//	dcPanel->GetClientSize().y));
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseMove(wxMouseEvent& event) {
	if (event.Dragging()) {
		if (panStart == -1) {
			beginPan(event.GetX());
		}
		else {
			continuePan(event.GetX());
		}
	}
	else {
		endPan();
	}
	mouseoverPoll = getPollFromMouse(event.GetPosition());
	modelMouseoverTimepoint = getModelTimePointFromMouse(event.GetPosition());
	projectionMouseoverTimepoint = getProjectionTimePointFromMouse(event.GetPosition());
	paint();
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseDown(wxMouseEvent& event) {
	if (panStart == -1) {
		beginPan(event.GetX());
	}
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseWheel(wxMouseEvent& event) {
	if (panStart != -1) return; // don't zoom while dragging
	zoom(float(event.GetWheelRotation()) / float(event.GetWheelDelta()), event.GetX());
	mouseoverPoll = getPollFromMouse(event.GetPosition());
	paint();
}

void VisualiserFrame::OnTogglePolls(wxCommandEvent& event) {
	displayPolls = event.IsChecked();
	this->Update();
	paint();
}

void VisualiserFrame::OnToggleModels(wxCommandEvent& event) {
	displayModels = event.IsChecked();
	this->Update();
	paint();
}

void VisualiserFrame::OnToggleHouseEffects(wxCommandEvent& event) {
	displayHouseEffects = event.IsChecked();
	this->Update();
	paint();
}

void VisualiserFrame::OnToggleProjections(wxCommandEvent& event) {
	displayProjections = event.IsChecked();
	this->Update();
	paint();
}

void VisualiserFrame::OnModelSelection(wxCommandEvent&) {
	selectedModel = selectModelComboBox->GetCurrentSelection();
	refreshPartyChoice();
	paint();
}

void VisualiserFrame::OnProjectionSelection(wxCommandEvent&) {
	selectedProjection = selectProjectionComboBox->GetCurrentSelection();
	paint();
}

void VisualiserFrame::OnPartySelection(wxCommandEvent&)
{
	selectedParty = selectPartyComboBox->GetCurrentSelection();
	paint();
}

void VisualiserFrame::OnModelDisplayMode(wxCommandEvent&)
{
	modelDisplayMode = ModelDisplayMode(modelDisplayModeComboBox->GetCurrentSelection());
	paint();
}

void VisualiserFrame::setVisualiserBounds(int startDay, int endDay) {
	startDay = std::max(project->getEarliestDate(), startDay);
	endDay = std::min(project->getLatestDate(), endDay);
	visStartDay = startDay; visEndDay = endDay;
}

void VisualiserFrame::beginPan(int mouseX)
{
	panStart = mouseX;
	originalStartDay = visStartDay;
	originalEndDay = visEndDay;
}

void VisualiserFrame::continuePan(int mouseX)
{
	int pixelsMoved = mouseX - panStart;
	float moveProportion = float(pixelsMoved) / gv.graphWidth;
	int daysMoved = -int(moveProportion * (visEndDay - visStartDay));
	int newEndDay = originalEndDay + daysMoved;
	int newStartDay = originalStartDay + daysMoved;
	int endAdjust = project->getLatestDate() - newEndDay;
	if (endAdjust < 0) {
		newEndDay = newEndDay + endAdjust;
		newStartDay = newStartDay + endAdjust;
	}
	int startAdjust = project->getEarliestDate() - newStartDay;
	if (startAdjust > 0) {
		newEndDay = newEndDay + startAdjust;
		newStartDay = newStartDay + startAdjust;
	}
	setVisualiserBounds(newStartDay, newEndDay);
}

void VisualiserFrame::zoom(float factor, int x)
{
	float currentViewLength = float(visEndDay - visStartDay);
	float zoomFactor = pow(2.0f, -factor);
	float effectiveX = std::max(std::min(float(x), gv.graphMargin + gv.graphWidth), gv.graphMargin);
	float normX = ((effectiveX - gv.graphMargin) / gv.graphWidth);
	int focusDate = std::max(std::min(int(getDateFromX(int(effectiveX)).GetMJD()), visEndDay), visStartDay);
	float newViewLength = std::max(16.0f, currentViewLength * zoomFactor);
	setVisualiserBounds(focusDate - int(newViewLength * normX), focusDate + int(newViewLength * (1.0f - normX)));
}

void VisualiserFrame::refreshPartyChoice() {
	std::string partyBoxString;

	// Prepare the list of parties being described by this model
	partyOrder.clear();
	if (selectedModel >= 0 && selectedModel < project->models().count()) {
		auto thisModel = project->models().viewByIndex(selectedModel);
		for (int partyIndex = 0; partyIndex < thisModel.rawSeriesCount(); ++partyIndex) {
			std::string thisPartyString = thisModel.rawPartyCodeByIndex(partyIndex);
			if (selectedParty == partyIndex) {
				partyBoxString = thisPartyString;
			}
			partyOrder.push_back(thisPartyString);
		}
		if (thisModel.viewTPPSeries().timePoint.size()) {
			partyOrder.push_back(project->parties().begin()->second.abbreviation + "TPP");
		}
	}

	wxArrayString arrayString;
	std::transform(partyOrder.begin(), partyOrder.end(), std::back_inserter(arrayString),
		[](std::string s) {return wxString(s); });

	selectPartyComboBox->Clear();
	selectPartyComboBox->Append(arrayString);
	selectPartyComboBox->SetValue(partyBoxString);
	toolBar->Realize();
}

void VisualiserFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\toggle_polls.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\toggle_models.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\toggle_house_effects.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\toggle_projections.png", wxBITMAP_TYPE_PNG);

	// Prepare the list of models
	wxArrayString modelArray;
	std::string modelBoxString;
	if (project->models().count()) {
		for (auto const& [key, model] : project->models()) {
			modelArray.push_back(model.getName());
		}
		if (selectedModel >= int(modelArray.size())) {
			selectedModel = int(modelArray.size()) - 1;
		}
		if (selectedModel >= 0) {
			modelBoxString = modelArray[selectedModel];
		}
	}

	// Prepare the list of projections
	wxArrayString projectionArray;
	std::string projectionBoxString;

	if (project->projections().count()) {
		for (auto const& [key, projection] : project->projections()) {
			projectionArray.push_back(projection.getSettings().name);
		}
		if (selectedProjection >= int(projectionArray.size())) {
			selectedProjection = int(projectionArray.size()) - 1;
		}
		if (selectedProjection >= 0) {
			projectionBoxString = projectionArray[selectedProjection];
		}
	}

	wxArrayString modelDisplayModeArray;
	modelDisplayModeArray.push_back("Raw Trend");
	modelDisplayModeArray.push_back("Adjusted Trend");
	std::string modelDisplayModeString = modelDisplayModeArray[int(modelDisplayMode)];

	selectModelComboBox = new wxComboBox(toolBar, ControlId::SelectModel, modelBoxString, wxPoint(0, 0), wxSize(150, 30), modelArray);

	selectProjectionComboBox = new wxComboBox(toolBar, ControlId::SelectProjection, projectionBoxString, wxPoint(0, 0), wxSize(150, 30), projectionArray);

	selectPartyComboBox = new wxComboBox(toolBar, ControlId::SelectParty, "", wxPoint(0, 0), wxSize(150, 30), wxArrayString());

	modelDisplayModeComboBox = new wxComboBox(toolBar, ControlId::ModelDisplayMode, modelDisplayModeString, wxPoint(0, 0), wxSize(150, 30), modelDisplayModeArray);

	refreshPartyChoice();

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::TogglePolls, "Toggle Poll Display", toolBarBitmaps[0], wxNullBitmap, wxITEM_CHECK, "Toggle Poll Display");
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::ToggleModels, "Toggle Model Display", toolBarBitmaps[1], wxNullBitmap, wxITEM_CHECK, "Toggle Model Display");
	toolBar->AddTool(ControlId::ToggleHouseEffects, "Toggle House Effect Display", toolBarBitmaps[2], wxNullBitmap, wxITEM_CHECK, "Toggle House Effect Display");
	toolBar->AddControl(selectModelComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::ToggleProjections, "Toggle Projection Display", toolBarBitmaps[3], wxNullBitmap, wxITEM_CHECK, "Toggle Projection Display");
	toolBar->AddControl(selectProjectionComboBox);
	toolBar->AddSeparator();
	toolBar->AddControl(selectPartyComboBox);
	toolBar->AddControl(modelDisplayModeComboBox);
	toolBar->ToggleTool(ControlId::TogglePolls, displayPolls);
	toolBar->ToggleTool(ControlId::ToggleModels, displayModels);
	toolBar->ToggleTool(ControlId::ToggleHouseEffects, displayHouseEffects);
	toolBar->ToggleTool(ControlId::ToggleProjections, displayProjections);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void VisualiserFrame::createDcPanel()
{
	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));
}

void VisualiserFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &VisualiserFrame::OnResize, this, PA_VisualiserFrame_FrameID);

	dcPanel->Bind(wxEVT_MOTION, &VisualiserFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_MOUSEWHEEL, &VisualiserFrame::OnMouseWheel, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_LEFT_DOWN, &VisualiserFrame::OnMouseDown, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &VisualiserFrame::OnPaint, this, ControlId::DcPanel);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnTogglePolls, this, ControlId::TogglePolls);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleModels, this, ControlId::ToggleModels);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleHouseEffects, this, ControlId::ToggleHouseEffects);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleProjections, this, ControlId::ToggleProjections);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnModelSelection, this, ControlId::SelectModel);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnProjectionSelection, this, ControlId::SelectProjection);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnPartySelection, this, ControlId::SelectParty);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnModelDisplayMode, this, ControlId::ModelDisplayMode);
}

void VisualiserFrame::updateInterface() {
}

void VisualiserFrame::render(wxDC& dc) {

	clearDC(dc);

	getStartAndEndDays();

	if (visStartDay < -100000.0) return; // if this is true there is nothing to draw.

	gv = GraphicsVariables();

	determineSelectedPartyIndex();
	determineGraphLimits();
	determineGraphVerticalScale();
	determineAxisTickInterval();
	determineFirstAxisTick();
	determineLaterAxisTicks();

	clearDC(dc);
	drawAxesLines(dc);
	drawAxisTickLines(dc);
	drawModels(dc);
	if (displayProjections) drawProjections(dc);

	if (displayPolls) {
		drawPollDots(dc);

		if (mouseoverPoll != Poll::InvalidId && project->polls().view(mouseoverPoll).date.IsValid()) {
			determineLabelRect();
			drawMouseoverLabelRect(dc);
			drawMouseoverPollText(dc);
		}
		else if (modelMouseoverTimepoint != -1) {
			determineLabelRect();
			drawMouseoverLabelRect(dc);
			drawMouseoverModelText(dc);
		}
		else if (projectionMouseoverTimepoint != -1) {
			determineLabelRect();
			drawMouseoverLabelRect(dc);
			drawMouseoverProjectionText(dc);
		}
	}
}

void VisualiserFrame::determineSelectedPartyIndex()
{
	selectedPartyIndex = -1;
	if (selectedParty < 0 || selectedParty >= int(partyOrder.size())) return;
	std::string code = partyOrder[selectedParty];
	if (code == OthersCode) {
		selectedPartyIndex = PartyCollection::MaxParties;
	}
	else {
		for (int thisPartyIndex = 0; thisPartyIndex < project->parties().count(); ++thisPartyIndex) {
			Party const& party = project->parties().viewByIndex(thisPartyIndex);
			if (std::find(party.officialCodes.begin(), party.officialCodes.end(), code)
				!= party.officialCodes.end())
			{
				selectedPartyIndex = thisPartyIndex;
				break;
			}
		}
	}
}

void VisualiserFrame::getStartAndEndDays() {
	if (project->polls().count() > 1) {
		if (visStartDay < -100000.0) setVisualiserBounds(-1000000, 1000000);
	}
}

void VisualiserFrame::determineGraphLimits() {
	gv.DCwidth = dcPanel->GetClientSize().GetWidth();
	gv.DCheight = dcPanel->GetClientSize().GetHeight();

	gv.graphMargin = 16.0f;
	gv.graphWidth = gv.DCwidth - gv.graphMargin * 2.0f;
	gv.graphRight = gv.graphMargin + gv.graphWidth;
	gv.graphBottom = gv.DCheight - gv.graphMargin;
	gv.graphTop = gv.graphMargin;
	gv.horzAxis = (gv.graphBottom + gv.graphTop) * 0.5f;
}

void VisualiserFrame::determineGraphVerticalScale()
{
	gv.minVote = 100.0f;
	gv.maxVote = 0.0f;
	for (auto const& [id, poll] : project->polls()) {
		int dateInt = dateToIntMjd(poll.date);
		if (dateInt >= visStartDay && dateInt <= visEndDay) {
			float val = getVoteFromPoll(poll);
			if (val < 0.0f) continue;
			gv.minVote = std::min(gv.minVote, val);
			gv.maxVote = std::max(gv.maxVote, val);
		}
	}

	if (displayModels && selectedModel >= 0 && selectedModel < project->models().count()) {
		StanModel const& model = project->models().viewByIndex(selectedModel);
		if (model.getLastUpdatedDate().IsValid()) {
			auto thisModel = project->models().viewByIndex(selectedModel);
			if (selectedParty >= 0) {
				auto series = viewSeriesFromModel(thisModel);
				if (series) {
					auto thisTimePoint = series->timePoint.begin();
					int modelStartDay = dateToIntMjd(model.getStartDate());
					for (int i = 0; i < int(series->timePoint.size()) - 1; ++i) {
						if (modelStartDay + i >= visStartDay && modelStartDay + i <= visEndDay) {
							float lowRange = series->timePoint[i].values[SpreadRanges[0].lowerPercentile];
							float highRange = series->timePoint[i].values[SpreadRanges[0].upperPercentile];
							gv.minVote = std::min(gv.minVote, lowRange);
							gv.maxVote = std::max(gv.maxVote, highRange);
						}
					}
				}
			}
		}
	}

	if (displayProjections && selectedProjection >= 0 && selectedProjection < project->projections().count()) {
		Projection const& projection = project->projections().viewByIndex(selectedProjection);
		if (projection.getLastUpdatedDate().IsValid()) {
			auto thisProjection = project->projections().viewByIndex(selectedProjection);
			if (selectedParty >= 0) {
				auto series = viewSeriesFromProjection(thisProjection);
				if (series) {
					auto thisTimePoint = series->timePoint.begin();
					int projectionStartDay = dateToIntMjd(getProjectionStartDate(thisProjection));
					for (int i = 0; i < int(series->timePoint.size()) - 1; ++i) {
						if (projectionStartDay + i >= visStartDay && projectionStartDay + i <= visEndDay) {
							float lowRange = series->timePoint[i].values[SpreadRanges[0].lowerPercentile];
							float highRange = series->timePoint[i].values[SpreadRanges[0].upperPercentile];
							gv.minVote = std::min(gv.minVote, lowRange);
							gv.maxVote = std::max(gv.maxVote, highRange);
						}
					}
				}
			}
		}
	}
}

void VisualiserFrame::determineAxisTickInterval() {
	int days = visEndDay - visStartDay;
	if (days < 12) gv.interval = GraphicsVariables::AxisTickInterval::Day;
	else if (days < 24) gv.interval = GraphicsVariables::AxisTickInterval::TwoDay;
	else if (days < 48) gv.interval = GraphicsVariables::AxisTickInterval::FourDay;
	else if (days < 84) gv.interval = GraphicsVariables::AxisTickInterval::Week;
	else if (days < 168) gv.interval = GraphicsVariables::AxisTickInterval::Fortnight;
	else if (days < 360) gv.interval = GraphicsVariables::AxisTickInterval::Month;
	else if (days < 720) gv.interval = GraphicsVariables::AxisTickInterval::TwoMonth;
	else if (days < 1440) gv.interval = GraphicsVariables::AxisTickInterval::Quarter;
	else if (days < 2190) gv.interval = GraphicsVariables::AxisTickInterval::HalfYear;
	else if (days < 4380) gv.interval = GraphicsVariables::AxisTickInterval::Year;
	else if (days < 8760) gv.interval = GraphicsVariables::AxisTickInterval::TwoYear;
	else if (days < 21900) gv.interval = GraphicsVariables::AxisTickInterval::FiveYear;
	else gv.interval = GraphicsVariables::AxisTickInterval::Decade;
}

void VisualiserFrame::determineFirstAxisTick() {
	wxDateTime tickDate = mjdToDate(visStartDay);
	switch (gv.interval) {
	case GraphicsVariables::AxisTickInterval::Week:
	case GraphicsVariables::AxisTickInterval::Fortnight:
		tickDate.Add(wxDateSpan(0, 0, 1, 0));
		setWeekDayToMonday(tickDate);
		break;
	case GraphicsVariables::AxisTickInterval::Month:
		tickDate.SetDay(1);
		tickDate.Add(wxDateSpan(0, 1, 0, 0));
		break;
	case GraphicsVariables::AxisTickInterval::TwoMonth:
		tickDate.SetDay(1);
		setToTwoMonth(tickDate);
		break;
	case GraphicsVariables::AxisTickInterval::Quarter:
		tickDate.SetDay(1);
		setToQuarter(tickDate);
		break;
	case GraphicsVariables::AxisTickInterval::HalfYear:
		tickDate.SetDay(1);
		setToHalfYear(tickDate);
		break;
	case GraphicsVariables::AxisTickInterval::Year:
		tickDate.SetDay(1);
		tickDate.SetMonth(wxDateTime::Jan);
		break;
	case GraphicsVariables::AxisTickInterval::TwoYear:
		tickDate.SetDay(1);
		tickDate.SetMonth(wxDateTime::Jan);
		tickDate.SetYear(tickDate.GetYear() / 2);
		break;
	case GraphicsVariables::AxisTickInterval::FiveYear:
		tickDate.SetDay(1);
		tickDate.SetMonth(wxDateTime::Jan);
		tickDate.SetYear(tickDate.GetYear() / 5);
		break;
	case GraphicsVariables::AxisTickInterval::Decade:
		tickDate.SetDay(1);
		tickDate.SetMonth(wxDateTime::Jan);
		tickDate.SetYear(tickDate.GetYear() / 10);
		break;
	}
	gv.AxisTick.push_back(tickDate);
}

void VisualiserFrame::determineLaterAxisTicks() {
	wxDateTime timeToAdd = gv.AxisTick[0];
	for (int i = 0; timeToAdd.GetModifiedJulianDayNumber() < visEndDay; i++) {
		if (i) gv.AxisTick.push_back(timeToAdd);
		switch (gv.interval) {
		case GraphicsVariables::AxisTickInterval::Day:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 1)); break;
		case GraphicsVariables::AxisTickInterval::TwoDay:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 2)); break;
		case GraphicsVariables::AxisTickInterval::FourDay:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 4)); break;
		case GraphicsVariables::AxisTickInterval::Week:
			timeToAdd.Add(wxDateSpan(0, 0, 1, 0)); break;
		case GraphicsVariables::AxisTickInterval::Fortnight:
			timeToAdd.Add(wxDateSpan(0, 0, 2, 0)); break;
		case GraphicsVariables::AxisTickInterval::Month:
			timeToAdd.Add(wxDateSpan(0, 1, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::TwoMonth:
			timeToAdd.Add(wxDateSpan(0, 2, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::Quarter:
			timeToAdd.Add(wxDateSpan(0, 3, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::HalfYear:
			timeToAdd.Add(wxDateSpan(0, 6, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::Year:
			timeToAdd.Add(wxDateSpan(1, 0, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::TwoYear:
			timeToAdd.Add(wxDateSpan(2, 0, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::FiveYear:
			timeToAdd.Add(wxDateSpan(5, 0, 0, 0)); break;
		case GraphicsVariables::AxisTickInterval::Decade:
			timeToAdd.Add(wxDateSpan(10, 0, 0, 0)); break;
		}
	}
}

void VisualiserFrame::setWeekDayToMonday(wxDateTime& dt) {
	dt.Subtract(wxDateSpan(0, 0, 0, int(dt.GetWeekDay() - 1)));
}

void VisualiserFrame::setToTwoMonth(wxDateTime& dt) {
	dt.SetMonth(wxDateTime::Month((int(dt.GetMonth()) / 2) * 2));
}

void VisualiserFrame::setToQuarter(wxDateTime& dt) {
	dt.SetMonth(wxDateTime::Month((int(dt.GetMonth()) / 3) * 3));
}

void VisualiserFrame::setToHalfYear(wxDateTime& dt) {
	dt.SetMonth(wxDateTime::Month((int(dt.GetMonth()) / 6) * 6));
}

void VisualiserFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(BackgroundColour));
	dc.Clear();
}

void VisualiserFrame::drawAxesLines(wxDC& dc) {
	dc.SetPen(wxPen(AxisColour));

	dc.DrawLine(gv.graphMargin, gv.graphTop, gv.graphMargin, gv.graphBottom);
	dc.DrawLine(gv.graphMargin, gv.horzAxis, gv.graphRight, gv.horzAxis);
	dc.DrawLine(gv.graphRight, gv.graphTop, gv.graphRight, gv.graphBottom);
}

void VisualiserFrame::drawAxisTickLines(wxDC& dc) {
	dc.SetPen(wxPen(AxisColour));

	for (int i = 0; i<int(gv.AxisTick.size()); ++i) {
		constexpr int AxisTickHeight = 10;
		const wxPoint DateTextOffset = wxPoint(-28, 15);
		int x = getXFromDate(gv.AxisTick[i]);
		int y1 = gv.horzAxis;
		int y2 = gv.horzAxis + AxisTickHeight;
		dc.DrawLine(x, y1, x, y2);
		dc.DrawText(gv.AxisTick[i].FormatISODate(), wxPoint(x, y1) + DateTextOffset);
	}
}

void VisualiserFrame::drawModels(wxDC& dc) {
	if (selectedModel != -1) {
		StanModel const& model = project->models().viewByIndex(selectedModel);
		if (model.getLastUpdatedDate().IsValid()) {
			drawModel(model, dc);
		}
	}
}

void VisualiserFrame::drawModel(StanModel const& model, wxDC& dc) {
	if (!displayModels && !displayHouseEffects) return;
	if (selectedParty < 0) return;
	auto series = viewSeriesFromModel(model);
	if (!series) return;
	auto thisTimePoint = series->timePoint.begin();
	for (int i = 0; i < int(series->timePoint.size()) - 1; ++i) {
		auto nextTimePoint = std::next(thisTimePoint);
		int x = getXFromDate(int(floor(model.getStartDate().GetMJD())) + i);
		int x2 = getXFromDate(int(floor(model.getStartDate().GetMJD())) + i + 1);
		if (displayModels) {
			for (auto range : SpreadRanges) {
				int y_tl = getYFromVote((*thisTimePoint).values[range.upperPercentile]);
				int y_tr = getYFromVote((*nextTimePoint).values[range.upperPercentile]);
				int y_bl = getYFromVote((*thisTimePoint).values[range.lowerPercentile]);
				int y_br = getYFromVote((*nextTimePoint).values[range.lowerPercentile]);
				dc.SetPen(*wxTRANSPARENT_PEN);
				int colourVal = 255 - int(range.colourStrength * 255.0f);
				dc.SetBrush(wxColour(colourVal, colourVal, colourVal));
				wxPointList pointList;
				std::unique_ptr<wxPoint> p_tl = std::make_unique<wxPoint>(x, y_tl);
				std::unique_ptr<wxPoint> p_tr = std::make_unique<wxPoint>(x2, y_tr);
				std::unique_ptr<wxPoint> p_br = std::make_unique<wxPoint>(x2, y_br);
				std::unique_ptr<wxPoint> p_bl = std::make_unique<wxPoint>(x, y_bl);
				pointList.Append(p_tl.get());
				pointList.Append(p_tr.get());
				pointList.Append(p_br.get());
				pointList.Append(p_bl.get());
				dc.DrawPolygon(&pointList);
			}

			int y = getYFromVote((*thisTimePoint).values[StanModel::Spread::Size / 2]);
			int y2 = getYFromVote((*nextTimePoint).values[StanModel::Spread::Size / 2]);
			dc.SetPen(wxPen(ModelColour));
			dc.DrawLine(x, y, x2, y2);
		}
		thisTimePoint = nextTimePoint;
	}
}

void VisualiserFrame::drawProjections(wxDC& dc) {
	if (selectedProjection != -1) {
		Projection const& projection = project->projections().viewByIndex(selectedProjection);
		if (projection.getLastUpdatedDate().IsValid()) {
			drawProjection(projection, dc);
		}
	}
}

void VisualiserFrame::drawProjection(Projection const& projection, wxDC& dc) {
	if (!displayProjections) return;
	if (selectedParty < 0) return;
	constexpr int NumSigmaLevels = 2;
	constexpr int SigmaBrightnessChange = 50;
	auto series = viewSeriesFromProjection(projection);
	if (!series) return;
	int projStartDay = int(floor(getProjectionStartDate(projection).GetMJD()));
	auto thisTimePoint = series->timePoint.begin();
	for (int i = 0; i < int(series->timePoint.size()) - 1; ++i) {
		auto nextTimePoint = std::next(thisTimePoint);
		int x = getXFromDate(projStartDay + i);
		int x2 = getXFromDate(projStartDay + i + 1);
		for (auto range : SpreadRanges) {
			int y_tl = getYFromVote((*thisTimePoint).values[range.upperPercentile]);
			int y_tr = getYFromVote((*nextTimePoint).values[range.upperPercentile]);
			int y_bl = getYFromVote((*thisTimePoint).values[range.lowerPercentile]);
			int y_br = getYFromVote((*nextTimePoint).values[range.lowerPercentile]);
			dc.SetPen(*wxTRANSPARENT_PEN);
			int colourVal = 255 - int(range.colourStrength * 255.0f);
			dc.SetBrush(wxColour(colourVal, colourVal, colourVal));
			wxPointList pointList;
			std::unique_ptr<wxPoint> p_tl = std::make_unique<wxPoint>(x, y_tl);
			std::unique_ptr<wxPoint> p_tr = std::make_unique<wxPoint>(x2, y_tr);
			std::unique_ptr<wxPoint> p_br = std::make_unique<wxPoint>(x2, y_br);
			std::unique_ptr<wxPoint> p_bl = std::make_unique<wxPoint>(x, y_bl);
			pointList.Append(p_tl.get());
			pointList.Append(p_tr.get());
			pointList.Append(p_br.get());
			pointList.Append(p_bl.get());
			dc.DrawPolygon(&pointList);
		}

		int y = getYFromVote((*thisTimePoint).values[StanModel::Spread::Size / 2]);
		int y2 = getYFromVote((*nextTimePoint).values[StanModel::Spread::Size / 2]);
		dc.SetPen(wxPen(ModelColour));
		dc.DrawLine(x, y, x2, y2);
		thisTimePoint = nextTimePoint;
	}
}

void VisualiserFrame::drawPollDots(wxDC& dc) {
	for (auto const& [id, poll] : project->polls()) {
		float vote = getVoteFromPoll(poll);
		if (vote < 0.0f) continue;
		int x = getXFromDate(poll.date);
		int y = getYFromVote(vote);
		// first draw the yellow outline for selected poll, if appropriate
		if (id == mouseoverPoll) {
			setBrushAndPen(PollSelectionColour, dc);
			dc.DrawCircle(x, y, 5);
		}
		// now draw the actual poll dot
		setBrushAndPen(project->pollsters().view(poll.pollster).colour, dc);
		dc.DrawCircle(x, y, 3);
	}
}

void VisualiserFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

int VisualiserFrame::getXFromDate(wxDateTime const& date) {
	int dateNum = dateToIntMjd(date);
	return int(float(dateNum - visStartDay) / float(visEndDay - visStartDay) * gv.graphWidth + gv.graphMargin);
}

int VisualiserFrame::getXFromDate(int date) {
	return int(float(date - visStartDay) / float(visEndDay - visStartDay) * gv.graphWidth + gv.graphMargin);
}

wxDateTime VisualiserFrame::getDateFromX(int x) {
	if (x < gv.graphMargin || x > gv.graphMargin + gv.graphWidth) return wxInvalidDateTime;
	int dateNum = int((float(x) - gv.graphMargin) / gv.graphWidth * float(visEndDay - visStartDay)) + visStartDay;
	wxDateTime date = mjdToDate(dateNum);
	return date;
}

int VisualiserFrame::getYFromVote(float thisVote) {
	return int((gv.maxVote - thisVote) / (gv.maxVote - gv.minVote) 
		* (gv.graphBottom - gv.graphTop) + gv.graphTop);
}

float VisualiserFrame::getVoteFromPoll(Poll const& poll)
{
	if (selectedPartyIndex == -1 || selectedPartyIndex > PartyCollection::MaxParties) {
		return poll.getBest2pp();
	}
	else if (selectedPartyIndex == PartyCollection::MaxParties) {
		float sumOfOthers = poll.primary[PartyCollection::MaxParties];
		for (int partyIndex = 0; partyIndex < project->parties().count(); ++partyIndex) {
			if (poll.primary[partyIndex] >= 0 && project->parties().viewByIndex(partyIndex).includeInOthers) {
				sumOfOthers += poll.primary[partyIndex];
			}
		}
		return sumOfOthers;
	}
	else {
		return poll.primary[selectedPartyIndex];
	}
}

Poll::Id VisualiserFrame::getPollFromMouse(wxPoint point) {
	Poll::Id bestPoll = Poll::InvalidId;
	long long closest = 10000000;
	for (auto const& [id, poll] : project->polls()) {
		// Normal ints would cause these calculations to overflow if used here
		int64_t xDist = (abs(getXFromDate(poll.date) - point.x));
		int64_t yDist = (abs(getYFromVote(getVoteFromPoll(poll)) - point.y));
		int64_t totalDist = xDist * xDist + yDist * yDist;
		if (totalDist < closest && totalDist < SelectMousePollDistanceSquared) {
			closest = totalDist;
			bestPoll = id;
		}
	}
	return bestPoll;
}

int VisualiserFrame::getModelTimePointFromMouse(wxPoint point)
{
	if (selectedModel != -1 && selectedParty != -1) {
		StanModel const& model = project->models().viewByIndex(selectedModel);
		auto series = viewSeriesFromModel(model);
		if (!series) return -1;
		int xLeft = getXFromDate(int(floor(model.getStartDate().GetMJD())));
		int xRight = getXFromDate(int(floor(model.getStartDate().GetMJD())) + int(series->timePoint.size()));
		float proportion = float(point.x - xLeft) / float(xRight - xLeft);
		int timePoint = int(std::floor(proportion * float(series->timePoint.size())));
		if (timePoint < 0) return -1;
		if (timePoint >= int(series->timePoint.size())) return -1;
		int expectedY = getYFromVote(series->timePoint[timePoint].values[MedianPercentile]);
		if (std::abs(expectedY - point.y) > ModelMouseoverYTolerance) return -1;
		return timePoint;
	}
	return -1;
}

int VisualiserFrame::getProjectionTimePointFromMouse(wxPoint point)
{
	if (selectedProjection != -1 && selectedParty != -1) {
		Projection const& projection = project->projections().viewByIndex(selectedProjection);
		auto const& series = viewSeriesFromProjection(projection);
		if (!series) return -1;
		int xLeft = getXFromDate(int(floor(getProjectionStartDate(projection).GetMJD())));
		int xRight = getXFromDate(int(floor(getProjectionStartDate(projection).GetMJD())) + int(series->timePoint.size()));
		float proportion = float(point.x - xLeft) / float(xRight - xLeft);
		int timePoint = int(std::floor(proportion * float(series->timePoint.size())));
		if (timePoint < 0) return -1;
		if (timePoint >= int(series->timePoint.size())) return -1;
		int expectedY = getYFromVote(series->timePoint[timePoint].values[MedianPercentile]);
		if (std::abs(expectedY - point.y) > ModelMouseoverYTolerance) return -1;
		return timePoint;
	}
	return -1;
}

void VisualiserFrame::determineLabelRect() {
	if (mouseoverPoll != Poll::InvalidId) {
		determineLabelRectFromPoll();
	}
	else if (modelMouseoverTimepoint > 0) {
		determineLabelRectFromModel();
	}
	else if (projectionMouseoverTimepoint > 0) {
		determineLabelRectFromProjection();
	}
}

void VisualiserFrame::determineLabelRectFromPoll()
{
	constexpr int MinimumLines = 2;
	constexpr int VerticalPaddingTotal = PollInfoPadding * 2 - 1;
	constexpr int DefaultWidth = 200;
	constexpr int MousePointerHorzSpacing = 10;
	auto const& thisPoll = project->polls().view(mouseoverPoll);
	int nLines = MinimumLines;
	if (thisPoll.calc2pp >= 0) nLines++;
	if (thisPoll.respondent2pp >= 0) nLines++;
	if (thisPoll.reported2pp >= 0) nLines++;
	for (int i = 0; i <= PartyCollection::MaxParties; ++i) {
		if (thisPoll.primary[i] >= 0) nLines++;
	}
	int height = nLines * PollInfoLineHeight + VerticalPaddingTotal;
	int width = DefaultWidth;
	int left = getXFromDate(thisPoll.date) - width;
	int top = getYFromVote(getVoteFromPoll(thisPoll));
	if (left < 0)
		left += width + MousePointerHorzSpacing;
	if ((top + height) > gv.graphBottom + gv.graphMargin)
		top = gv.graphBottom + gv.graphMargin - height;
	mouseOverLabelRect = wxRect(left, top, width, height);
}

void VisualiserFrame::determineLabelRectFromModel()
{
	constexpr int MinimumLines = 2;
	constexpr int VerticalPaddingTotal = PollInfoPadding * 2 - 1;
	constexpr int DefaultWidth = 200;
	constexpr int MousePointerHorzSpacing = 10;
	auto const& thisModel = project->models().view(selectedModel);
	int nLines = SpreadRanges.size() * 2 + 2;
	int height = nLines * PollInfoLineHeight + VerticalPaddingTotal;
	int width = DefaultWidth;
	auto const& series = viewSeriesFromModel(thisModel);
	if (!series) return;
	int left = getXFromDate(int(floor(thisModel.getStartDate().GetMJD())) + modelMouseoverTimepoint) - width;
	int top = getYFromVote(series->timePoint[modelMouseoverTimepoint].values[50]);
	if (left < 0)
		left += width + MousePointerHorzSpacing;
	if ((top + height) > gv.graphBottom + gv.graphMargin)
		top = gv.graphBottom + gv.graphMargin - height;
	mouseOverLabelRect = wxRect(left, top, width, height);
}

void VisualiserFrame::determineLabelRectFromProjection()
{
	constexpr int MinimumLines = 2;
	constexpr int VerticalPaddingTotal = PollInfoPadding * 2 - 1;
	constexpr int DefaultWidth = 200;
	constexpr int MousePointerHorzSpacing = 10;
	auto const& thisProjection = project->projections().view(selectedProjection);
	int nLines = SpreadRanges.size() * 2 + 2;
	int height = nLines * PollInfoLineHeight + VerticalPaddingTotal;
	int width = DefaultWidth;
	auto const& series = viewSeriesFromProjection(thisProjection);
	if (!series) return;
	int left = getXFromDate(int(floor(getProjectionStartDate(thisProjection).GetMJD())) + projectionMouseoverTimepoint) - width;
	int top = getYFromVote(series->timePoint[projectionMouseoverTimepoint].values[50]);
	if (left < 0)
		left += width + MousePointerHorzSpacing;
	if ((top + height) > gv.graphBottom + gv.graphMargin)
		top = gv.graphBottom + gv.graphMargin - height;
	mouseOverLabelRect = wxRect(left, top, width, height);
}

void VisualiserFrame::drawMouseoverLabelRect(wxDC& dc) {
	if (mouseoverPoll == Poll::InvalidId && modelMouseoverTimepoint == -1 && projectionMouseoverTimepoint == -1) return;
	dc.SetPen(wxPen(PollInfoBorderColour)); // black border
	dc.SetBrush(wxBrush(PollInfoBackgroundColour)); // white interior
	dc.DrawRoundedRectangle(mouseOverLabelRect, PollInfoPadding);
}

void VisualiserFrame::drawMouseoverPollText(wxDC& dc) {
	if (mouseoverPoll == Poll::InvalidId) return;
	auto const& thisPoll = project->polls().view(mouseoverPoll);
	wxPoint currentPoint = mouseOverLabelRect.GetTopLeft() += wxPoint(PollInfoPadding, PollInfoPadding);
	dc.SetPen(wxPen(PollInfoTextColour)); // black text
	std::string pollsterName;
	if (thisPoll.pollster != Pollster::InvalidId) pollsterName = project->pollsters().view(thisPoll.pollster).name;
	else pollsterName = "Invalid";
	dc.DrawText(pollsterName, currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	dc.DrawText(thisPoll.date.FormatISODate(), currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	if (thisPoll.reported2pp >= 0) {
		dc.DrawText("Previous-Election 2PP: " + thisPoll.getReported2ppString(), currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
	if (thisPoll.respondent2pp >= 0) {
		dc.DrawText("Respondent-allocated 2PP: " + thisPoll.getRespondent2ppString(), currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
	if (thisPoll.calc2pp >= 0) {
		dc.DrawText("Calculated 2PP: " + thisPoll.getCalc2ppString(), currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
	for (int i = 0; i < PartyCollection::MaxParties; ++i) {
		if (thisPoll.primary[i] >= 0) {
			dc.DrawText(project->parties().viewByIndex(i).abbreviation + " primary: " + thisPoll.getPrimaryString(i), currentPoint);
			currentPoint += wxPoint(0, PollInfoLineHeight);
		}
	}
	if (thisPoll.primary[PartyCollection::MaxParties] >= 0) {
		dc.DrawText("Others primary: " + thisPoll.getPrimaryString(PartyCollection::MaxParties), currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
}

void VisualiserFrame::drawMouseoverModelText(wxDC& dc)
{
	if (selectedModel == -1 || selectedParty == -1 || modelMouseoverTimepoint == -1) return;
	auto const& thisModel = project->models().viewByIndex(selectedModel);
	auto const& thisSeries = viewSeriesFromModel(thisModel);
	if (!thisSeries) return;
	auto const& thisSpread = thisSeries->timePoint[modelMouseoverTimepoint];
	wxPoint currentPoint = mouseOverLabelRect.GetTopLeft() += wxPoint(PollInfoPadding, PollInfoPadding);
	dc.SetPen(wxPen(PollInfoTextColour)); // black text
	dc.DrawText(thisModel.getStartDate().Add(wxDateSpan(0, 0, 0, modelMouseoverTimepoint)).FormatISODate(), currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	for (auto it = SpreadRanges.begin(); it != SpreadRanges.end(); ++it) {
		std::string lowerText = std::to_string(it->lowerPercentile) + "%: " + std::to_string(thisSpread.values[it->lowerPercentile]);
		dc.DrawText(lowerText, currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
	std::string meanText = "50%: " + std::to_string(thisSpread.values[MedianPercentile]);
	dc.DrawText(meanText, currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	for (auto it = SpreadRanges.rbegin(); it != SpreadRanges.rend(); ++it) {
		std::string upperText = std::to_string(it->upperPercentile) + "%: " + std::to_string(thisSpread.values[it->upperPercentile]);
		dc.DrawText(upperText, currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}

}

void VisualiserFrame::drawMouseoverProjectionText(wxDC& dc)
{
	if (selectedProjection == -1 || selectedParty == -1 || projectionMouseoverTimepoint == -1) return;
	auto const& thisProjection = project->projections().viewByIndex(selectedProjection);
	auto const& thisSeries = viewSeriesFromProjection(thisProjection);
	if (!thisSeries) return;
	auto const& thisSpread = thisSeries->timePoint[projectionMouseoverTimepoint];
	wxPoint currentPoint = mouseOverLabelRect.GetTopLeft() += wxPoint(PollInfoPadding, PollInfoPadding);
	dc.SetPen(wxPen(PollInfoTextColour)); // black text
	dc.DrawText(getProjectionStartDate(thisProjection).Add(wxDateSpan(0, 0, 0, projectionMouseoverTimepoint)).FormatISODate(), currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	for (auto it = SpreadRanges.begin(); it != SpreadRanges.end(); ++it) {
		std::string lowerText = std::to_string(it->lowerPercentile) + "%: " + std::to_string(thisSpread.values[it->lowerPercentile]);
		dc.DrawText(lowerText, currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}
	std::string meanText = "50%: " + std::to_string(thisSpread.values[MedianPercentile]);
	dc.DrawText(meanText, currentPoint);
	currentPoint += wxPoint(0, PollInfoLineHeight);
	for (auto it = SpreadRanges.rbegin(); it != SpreadRanges.rend(); ++it) {
		std::string upperText = std::to_string(it->upperPercentile) + "%: " + std::to_string(thisSpread.values[it->upperPercentile]);
		dc.DrawText(upperText, currentPoint);
		currentPoint += wxPoint(0, PollInfoLineHeight);
	}

}

StanModel::SeriesOutput VisualiserFrame::viewSeriesFromModel(StanModel const& model) const
{
	if (selectedParty < 0) return nullptr;
	std::string partyCode = partyOrder[selectedParty];
	if (modelDisplayMode == ModelDisplayMode::Adjusted) {
		// default to raw series if the adjusted series haven'y been generated
		if (selectedParty < int(partyOrder.size()) - 1) {
			return model.viewAdjustedSeries(partyCode);
		}
		else if (selectedParty == int(partyOrder.size() - 1)) {
			return &model.viewTPPSeries();
		}
	}
	else if (modelDisplayMode == ModelDisplayMode::Raw) {
		return model.viewRawSeries(partyCode);
	}
	return nullptr;
}

StanModel::SeriesOutput VisualiserFrame::viewSeriesFromProjection(Projection const& projection) const
{
	std::string partyCode = partyOrder[selectedParty];
	if (selectedParty < int(partyOrder.size() - 1)) {
		return projection.viewPrimarySeries(partyCode);
	}
	else if (selectedParty == int(partyOrder.size() - 1)) {
		return &projection.viewTPPSeries();
	}
	return nullptr;
}

wxDateTime VisualiserFrame::getProjectionStartDate(Projection const& projection)
{
	StanModel const& model = project->models().view(projection.getSettings().baseModel);
	return model.getEndDate() + wxDateSpan(0, 0, 0, 1);
}

void VisualiserFrame::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}