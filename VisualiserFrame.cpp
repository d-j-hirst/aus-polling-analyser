#include "VisualiserFrame.h"

#include "General.h"

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
	mouseOverPoll = Poll::InvalidId;
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
	mouseOverPoll = getPollFromMouse(event.GetPosition());
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
	mouseOverPoll = getPollFromMouse(event.GetPosition());
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

void VisualiserFrame::OnModelSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedModel = selectModelComboBox->GetCurrentSelection();
	paint();
}

void VisualiserFrame::OnProjectionSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedProjection = selectProjectionComboBox->GetCurrentSelection();
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
		for (auto it = project->models().cbegin(); it != project->models().cend(); ++it) {
			modelArray.push_back(it->second.name);
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
		for (auto projection : project->projections()) {
			projectionArray.push_back(projection.second.name);
		}
		if (selectedProjection >= int(projectionArray.size())) {
			selectedProjection = int(projectionArray.size()) - 1;
		}
		if (selectedProjection >= 0) {
			projectionBoxString = projectionArray[selectedProjection];
		}
	}

	selectModelComboBox = new wxComboBox(toolBar, ControlId::SelectModel, modelBoxString, wxPoint(0, 0), wxSize(150, 30), modelArray);

	selectProjectionComboBox = new wxComboBox(toolBar, ControlId::SelectProjection, projectionBoxString, wxPoint(0, 0), wxSize(150, 30), projectionArray);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::TogglePolls, "Toggle Poll Display", toolBarBitmaps[0], wxNullBitmap, wxITEM_CHECK, "Toggle Poll Display");
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::ToggleModels, "Toggle Model Display", toolBarBitmaps[1], wxNullBitmap, wxITEM_CHECK, "Toggle Model Display");
	toolBar->AddTool(ControlId::ToggleHouseEffects, "Toggle House Effect Display", toolBarBitmaps[2], wxNullBitmap, wxITEM_CHECK, "Toggle House Effect Display");
	toolBar->AddControl(selectModelComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::ToggleProjections, "Toggle Projection Display", toolBarBitmaps[3], wxNullBitmap, wxITEM_CHECK, "Toggle Projection Display");
	toolBar->AddControl(selectProjectionComboBox);
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
}

void VisualiserFrame::updateInterface() {
}

void VisualiserFrame::render(wxDC& dc) {

	clearDC(dc);

	getStartAndEndDays();

	if (visStartDay < -100000.0) return; // if this is true there is nothing to draw.

	gv = GraphicsVariables();

	determineGraphLimits();
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

		if (mouseOverPoll != Poll::InvalidId && project->polls().view(mouseOverPoll).date.IsValid()) {
			determineMouseOverPollRect();
			drawMouseOverPollRect(dc);
			drawMouseOverPollText(dc);
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
		Model const& model = project->models().viewByIndex(selectedModel);
		if (model.lastUpdated.IsValid()) {
			drawModel(model, dc);
		}
	}
}

void VisualiserFrame::drawModel(Model const& model, wxDC& dc) {
	if (!displayModels && !displayHouseEffects) return;
	ModelTimePoint const* thisTimePoint = &model.day[0];
	for (int i = 0; i < int(model.day.size()) - 1; ++i) {
		ModelTimePoint const* nextTimePoint = &model.day[i + 1];
		int x = getXFromDate(int(floor(model.effStartDate.GetMJD())) + i);
		int x2 = getXFromDate(int(floor(model.effStartDate.GetMJD())) + i + 1);
		if (displayModels) {
			int y = getYFrom2PP(thisTimePoint->trend2pp);
			int y2 = getYFrom2PP(nextTimePoint->trend2pp);
			dc.SetPen(wxPen(ModelColour));
			dc.DrawLine(x, y, x2, y2);
		}
		for (int pollsterIndex = 0; pollsterIndex < project->pollsters().count() && displayHouseEffects; ++pollsterIndex) {
			// House effects are increased by 50 so they are easily displayed at the same scale as the models themselves
			int y = getYFrom2PP(thisTimePoint->houseEffect[pollsterIndex] + 50.0f);
			int y2 = getYFrom2PP(nextTimePoint->houseEffect[pollsterIndex] + 50.0f);
			dc.SetPen(wxPen(project->pollsters().viewByIndex(pollsterIndex).colour));
			dc.DrawLine(x, y, x2, y2);
		}
		thisTimePoint = nextTimePoint;
	}
}

void VisualiserFrame::drawProjections(wxDC& dc) {
	if (selectedProjection != -1) {
		Projection const& projection = project->projections().viewByIndex(selectedProjection);
		if (projection.lastUpdated.IsValid()) {
			drawProjection(projection, dc);
		}
	}
}

void VisualiserFrame::drawProjection(Projection const& projection, wxDC& dc) {
	constexpr int NumSigmaLevels = 2;
	constexpr int SigmaBrightnessChange = 50;
	Model const& model = project->models().view(projection.baseModel);
	for (int i = 0; i < int(projection.meanProjection.size()) - 1; ++i) {
		int x = getXFromDate(int(floor(model.effEndDate.GetMJD())) + i);
		int x2 = getXFromDate(int(floor(model.effEndDate.GetMJD())) + i + 1);
		for (int sigma = -NumSigmaLevels; sigma <= NumSigmaLevels; ++sigma) {
			int y = getYFrom2PP(projection.meanProjection[i] + projection.sdProjection[i] * sigma);
			int y2 = getYFrom2PP(projection.meanProjection[i + 1] + projection.sdProjection[i + 1] * sigma);
			int greyLevel = (abs(sigma) + 1) * SigmaBrightnessChange;
			dc.SetPen(wxPen(wxColour(greyLevel, greyLevel, greyLevel)));
			dc.DrawLine(x, y, x2, y2);
		}
	}
}

void VisualiserFrame::drawPollDots(wxDC& dc) {
	for (auto const& poll : project->polls()) {
		int x = getXFromDate(poll.second.date);
		int y = getYFrom2PP(poll.second.getBest2pp());
		// first draw the yellow outline for selected poll, if appropriate
		if (poll.first == mouseOverPoll) {
			setBrushAndPen(PollSelectionColour, dc);
			dc.DrawCircle(x, y, 5);
		}
		// now draw the actual poll dot
		setBrushAndPen(project->pollsters().view(poll.second.pollster).colour, dc);
		dc.DrawCircle(x, y, 3);
	}
}

void VisualiserFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

int VisualiserFrame::getXFromDate(wxDateTime const& date) {
	int dateNum = int(floor(date.GetModifiedJulianDayNumber()));
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

int VisualiserFrame::getYFrom2PP(float this2pp) {
	return int((this2pp - 50.0f) * -15.0f + gv.horzAxis);
}

Poll::Id VisualiserFrame::getPollFromMouse(wxPoint point) {
	Poll::Id bestPoll = Poll::InvalidId;
	int closest = 10000000;
	for (auto const& poll : project->polls()) {
		int xDist = abs(getXFromDate(poll.second.date) - point.x);
		int yDist = abs(getYFrom2PP(poll.second.getBest2pp()) - point.y);
		int totalDist = xDist * xDist + yDist * yDist;
		if (totalDist < closest && totalDist < SelectMousePollDistanceSquared) {
			closest = totalDist;
			bestPoll = poll.first;
		}
	}
	return bestPoll;
}

void VisualiserFrame::determineMouseOverPollRect() {
	constexpr int MinimumLines = 2;
	constexpr int VerticalPaddingTotal = PollInfoPadding * 2 - 1;
	constexpr int DefaultWidth = 200;
	constexpr int MousePointerHorzSpacing = 10;
	if (mouseOverPoll == Poll::InvalidId) return;
	auto const& thisPoll = project->polls().view(mouseOverPoll);
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
	int top = getYFrom2PP(thisPoll.getBest2pp());
	if (left < 0)
		left += width + MousePointerHorzSpacing;
	if ((top + height) > gv.graphBottom + gv.graphMargin)
		top = gv.graphBottom + gv.graphMargin - height;
	mouseOverPollRect = wxRect(left, top, width, height);
}

void VisualiserFrame::drawMouseOverPollRect(wxDC& dc) {
	if (mouseOverPoll == Poll::InvalidId) return;
	dc.SetPen(wxPen(PollInfoBorderColour)); // black border
	dc.SetBrush(wxBrush(PollInfoBackgroundColour)); // white interior
	dc.DrawRoundedRectangle(mouseOverPollRect, PollInfoPadding);
}

void VisualiserFrame::drawMouseOverPollText(wxDC& dc) {
	if (mouseOverPoll == Poll::InvalidId) return;
	auto const& thisPoll = project->polls().view(mouseOverPoll);
	wxPoint currentPoint = mouseOverPollRect.GetTopLeft() += wxPoint(PollInfoPadding, PollInfoPadding);
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

void VisualiserFrame::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}