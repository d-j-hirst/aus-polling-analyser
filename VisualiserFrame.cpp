#include "VisualiserFrame.h"
#include "General.h"
#include <algorithm>

// IDs for the controls and the menu commands
enum {
	PA_VisualiserFrame_Base = 450, // To avoid mixing events with other frames.
	PA_VisualiserFrame_FrameID,
	PA_VisualiserFrame_DcPanelID,
	PA_VisualiserFrame_TogglePollsID,
	PA_VisualiserFrame_ToggleModelsID,
	PA_VisualiserFrame_ToggleHouseEffectsID,
	PA_VisualiserFrame_ToggleProjectionsID,
	PA_VisualiserFrame_SelectModelID,
	PA_VisualiserFrame_SelectProjectionID,
};

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// square of the maximum distance from the mouse pointer to a poll point that will allow it to be selected.
const int PA_Poll_Select_Distance_Squared_Maximum = 16;

// frame constructor
VisualiserFrame::VisualiserFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), PA_VisualiserFrame_FrameID, "Visualiser", wxPoint(333, 0), project),
	refresher(refresher),
	mouseOverPoll(nullptr)
{
	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, PA_VisualiserFrame_DcPanelID, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();

	paint();

	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &VisualiserFrame::OnResize, this, PA_VisualiserFrame_FrameID);

	dcPanel->Bind(wxEVT_MOTION, &VisualiserFrame::OnMouseMove, this, PA_VisualiserFrame_DcPanelID);
	dcPanel->Bind(wxEVT_MOUSEWHEEL, &VisualiserFrame::OnMouseWheel, this, PA_VisualiserFrame_DcPanelID);
	dcPanel->Bind(wxEVT_LEFT_DOWN, &VisualiserFrame::OnMouseDown, this, PA_VisualiserFrame_DcPanelID);
	dcPanel->Bind(wxEVT_PAINT, &VisualiserFrame::OnPaint, this, PA_VisualiserFrame_DcPanelID);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnTogglePolls, this, PA_VisualiserFrame_TogglePollsID);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleModels, this, PA_VisualiserFrame_ToggleModelsID);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleHouseEffects, this, PA_VisualiserFrame_ToggleHouseEffectsID);
	Bind(wxEVT_TOOL, &VisualiserFrame::OnToggleProjections, this, PA_VisualiserFrame_ToggleProjectionsID);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnModelSelection, this, PA_VisualiserFrame_SelectModelID);
	Bind(wxEVT_COMBOBOX, &VisualiserFrame::OnProjectionSelection, this, PA_VisualiserFrame_SelectProjectionID);
}

void VisualiserFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void VisualiserFrame::resetMouseOver() {
	mouseOverPoll = nullptr;
}

void VisualiserFrame::refreshData() {
	refreshToolbar();
}

void VisualiserFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the poll data table to the entire client size.
	//pollData->SetSize(wxSize(dcPanel->GetClientSize().x,
	//	dcPanel->GetClientSize().y));
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseMove(wxMouseEvent& event) {
	if (event.Dragging()) {
		if (dragStart == -1) {
			dragStart = event.GetX();
			originalStartDay = project->getVisStartDay();
			originalEndDay = project->getVisEndDay();
		}
		else {
			int pixelsMoved = event.GetX() - dragStart;
			float moveProportion = float(pixelsMoved) / gv.graphWidth;
			int daysMoved = -int(moveProportion * (project->getVisEndDay() - project->getVisStartDay()));
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
			project->setVisualiserBounds(newStartDay, newEndDay);
		}
	}
	else {
		dragStart = -1;
	}
	mouseOverPoll = getPollFromMouse(event.GetPosition());
	paint();
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseDown(wxMouseEvent& event) {
	if (dragStart == -1) {
		dragStart = event.GetX();
		originalStartDay = project->getVisStartDay();
		originalEndDay = project->getVisEndDay();
	}
}

// Handles the movement of the mouse in the visualiser frame.
void VisualiserFrame::OnMouseWheel(wxMouseEvent& event) {
	if (dragStart != -1) return; // don't zoom while dragging
	float currentViewLength = float(project->getVisEndDay() - project->getVisStartDay());
	float zoomFactor = pow(2.0f, -float(event.GetWheelRotation()) / float(event.GetWheelDelta()));
	float effectiveX = std::max(std::min(float(event.GetX()), gv.graphMargin + gv.graphWidth), gv.graphMargin);
	float normX = ((effectiveX - gv.graphMargin) / gv.graphWidth);
	int focusDate = std::max(std::min(int(getDateFromX(int(effectiveX)).GetMJD()), project->getVisEndDay()), project->getVisStartDay());
	float newViewLength = std::max(16.0f, currentViewLength * zoomFactor);
	project->setVisualiserBounds(focusDate - int(newViewLength * normX), focusDate + int(newViewLength * (1.0f - normX)));
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

void VisualiserFrame::updateInterface() {
}

void VisualiserFrame::render(wxDC& dc) {

	clearDC(dc);

	getStartAndEndDays();

	if (project->getVisStartDay() < -100000.0) return; // if this is true there is nothing to draw.

	gv = GraphicsVariables();

	defineGraphLimits();
	determineAxisTickInterval();
	determineFirstAxisTick();
	getAxisTicks();

	clearDC(dc);
	drawAxesLines(dc);
	drawAxisTickLines(dc);
	drawModels(dc);
	if (displayProjections) drawProjections(dc);

	if (displayPolls) {
		drawPollDots(dc);

		if (mouseOverPoll && mouseOverPoll->date.IsValid()) {
			determineMouseOverPollRect();
			drawMouseOverPollRect(dc);
			drawMouseOverPollText(dc);
		}
	}
}

void VisualiserFrame::getStartAndEndDays() {
	if (project->getPollCount() > 1) {

		// this can be uncommented once the user has control over the visualiser, but for now we'll just show all polls
		if (project->getVisStartDay() < -100000.0)
			project->setVisualiserBounds(-1000000, 1000000);
	}
}

void VisualiserFrame::defineGraphLimits() {

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
	int days = project->getVisEndDay() - project->getVisStartDay();
	if (days < 12) gv.interval = AXISTICK_DAILY;
	else if (days < 24) gv.interval = AXISTICK_TWO_DAY;
	else if (days < 48) gv.interval = AXISTICK_FOUR_DAY;
	else if (days < 84) gv.interval = AXISTICK_WEEKLY;
	else if (days < 168) gv.interval = AXISTICK_FORTNIGHTLY;
	else if (days < 360) gv.interval = AXISTICK_MONTHLY;
	else if (days < 720) gv.interval = AXISTICK_BIMONTHLY;
	else if (days < 1440) gv.interval = AXISTICK_QUARTERLY;
	else if (days < 2190) gv.interval = AXISTICK_HALFYEARLY;
	else if (days < 4380) gv.interval = AXISTICK_YEARLY;
	else if (days < 8760) gv.interval = AXISTICK_TWO_YEAR;
	else if (days < 21900) gv.interval = AXISTICK_FIVE_YEAR;
	else gv.interval = AXISTICK_DECADE;
}

void VisualiserFrame::determineFirstAxisTick() {
	wxDateTime timeToAdd = project->MjdToDate(project->getVisStartDay());
	switch (gv.interval) {
	case AXISTICK_WEEKLY:
	case AXISTICK_FORTNIGHTLY:
		timeToAdd.Add(wxDateSpan(0, 0, 1, 0));
		setWeekDayToMonday(timeToAdd);
		break;
	case AXISTICK_MONTHLY:
		timeToAdd.SetDay(1);
		timeToAdd.Add(wxDateSpan(0, 1, 0, 0));
		break;
	case AXISTICK_BIMONTHLY:
		timeToAdd.SetDay(1);
		setToTwoMonth(timeToAdd);
		break;
	case AXISTICK_QUARTERLY:
		timeToAdd.SetDay(1);
		setToQuarter(timeToAdd);
		break;
	case AXISTICK_HALFYEARLY:
		timeToAdd.SetDay(1);
		setToHalfYear(timeToAdd);
		break;
	case AXISTICK_YEARLY:
		timeToAdd.SetDay(1);
		timeToAdd.SetMonth(wxDateTime::Jan);
		break;
	case AXISTICK_TWO_YEAR:
		timeToAdd.SetDay(1);
		timeToAdd.SetMonth(wxDateTime::Jan);
		timeToAdd.SetYear(timeToAdd.GetYear() / 2);
		break;
	case AXISTICK_FIVE_YEAR:
		timeToAdd.SetDay(1);
		timeToAdd.SetMonth(wxDateTime::Jan);
		timeToAdd.SetYear(timeToAdd.GetYear() / 5);
		break;
	case AXISTICK_DECADE:
		timeToAdd.SetDay(1);
		timeToAdd.SetMonth(wxDateTime::Jan);
		timeToAdd.SetYear(timeToAdd.GetYear() / 10);
		break;
	}
	gv.AxisTick.push_back(timeToAdd);
}

void VisualiserFrame::getAxisTicks() {
	wxDateTime timeToAdd = gv.AxisTick[0];
	for (int i = 0; timeToAdd.GetModifiedJulianDayNumber() < project->getVisEndDay(); i++) {
		if (i) gv.AxisTick.push_back(timeToAdd);
		switch (gv.interval) {
		case AXISTICK_DAILY:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 1)); break;
		case AXISTICK_TWO_DAY:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 2)); break;
		case AXISTICK_FOUR_DAY:
			timeToAdd.Add(wxDateSpan(0, 0, 0, 4)); break;
		case AXISTICK_WEEKLY:
			timeToAdd.Add(wxDateSpan(0, 0, 1, 0)); break;
		case AXISTICK_FORTNIGHTLY:
			timeToAdd.Add(wxDateSpan(0, 0, 2, 0)); break;
		case AXISTICK_MONTHLY:
			timeToAdd.Add(wxDateSpan(0, 1, 0, 0)); break;
		case AXISTICK_BIMONTHLY:
			timeToAdd.Add(wxDateSpan(0, 2, 0, 0)); break;
		case AXISTICK_QUARTERLY:
			timeToAdd.Add(wxDateSpan(0, 3, 0, 0)); break;
		case AXISTICK_HALFYEARLY:
			timeToAdd.Add(wxDateSpan(0, 6, 0, 0)); break;
		case AXISTICK_YEARLY:
			timeToAdd.Add(wxDateSpan(1, 0, 0, 0)); break;
		case AXISTICK_TWO_YEAR:
			timeToAdd.Add(wxDateSpan(2, 0, 0, 0)); break;
		case AXISTICK_FIVE_YEAR:
			timeToAdd.Add(wxDateSpan(5, 0, 0, 0)); break;
		case AXISTICK_DECADE:
			timeToAdd.Add(wxDateSpan(10, 0, 0, 0)); break;
		}
	}
}

void VisualiserFrame::setWeekDayToMonday(wxDateTime& dt) {
	switch (dt.GetWeekDay()) {
	case wxDateTime::Tue:
		dt.Subtract(wxDateSpan(0, 0, 0, 1));
	case wxDateTime::Wed:
		dt.Subtract(wxDateSpan(0, 0, 0, 2));
	case wxDateTime::Thu:
		dt.Subtract(wxDateSpan(0, 0, 0, 3));
	case wxDateTime::Fri:
		dt.Subtract(wxDateSpan(0, 0, 0, 4));
	case wxDateTime::Sat:
		dt.Subtract(wxDateSpan(0, 0, 0, 5));
	case wxDateTime::Sun:
		dt.Subtract(wxDateSpan(0, 0, 0, 6));
	}
}

void VisualiserFrame::setToTwoMonth(wxDateTime& dt) {
	switch (dt.GetMonth()) {
	case wxDateTime::Feb:
		dt.SetMonth(wxDateTime::Jan);
		break;
	case wxDateTime::Apr:
		dt.SetMonth(wxDateTime::Mar);
		break;
	case wxDateTime::Jun:
		dt.SetMonth(wxDateTime::May);
		break;
	case wxDateTime::Aug:
		dt.SetMonth(wxDateTime::Jul);
		break;
	case wxDateTime::Oct:
		dt.SetMonth(wxDateTime::Sep);
		break;
	case wxDateTime::Dec:
		dt.SetMonth(wxDateTime::Nov);
		break;
	}
}

void VisualiserFrame::setToQuarter(wxDateTime& dt) {
	switch (dt.GetMonth()) {
	case wxDateTime::Feb:
	case wxDateTime::Mar:
		dt.SetMonth(wxDateTime::Jan);
		break;
	case wxDateTime::May:
	case wxDateTime::Jun:
		dt.SetMonth(wxDateTime::Apr);
		break;
	case wxDateTime::Aug:
	case wxDateTime::Sep:
		dt.SetMonth(wxDateTime::Jul);
		break;
	case wxDateTime::Oct:
	case wxDateTime::Dec:
		dt.SetMonth(wxDateTime::Oct);
		break;
	}
}

void VisualiserFrame::setToHalfYear(wxDateTime& dt) {
	switch (dt.GetMonth()) {
	case wxDateTime::Feb:
	case wxDateTime::Mar:
	case wxDateTime::Apr:
	case wxDateTime::May:
	case wxDateTime::Jun:
		dt.SetMonth(wxDateTime::Jan);
		break;
	case wxDateTime::Aug:
	case wxDateTime::Sep:
	case wxDateTime::Oct:
	case wxDateTime::Nov:
	case wxDateTime::Dec:
		dt.SetMonth(wxDateTime::Jul);
		break;
	}
}

void VisualiserFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(wxColour(100, 200, 255)));
	dc.Clear();
}

void VisualiserFrame::drawAxesLines(wxDC& dc) {
	wxColour currentColour = wxColour(0, 0, 0);
	dc.SetPen(wxPen(currentColour));

	dc.DrawLine(gv.graphMargin, gv.graphTop, gv.graphMargin, gv.graphBottom);
	dc.DrawLine(gv.graphMargin, gv.horzAxis, gv.graphRight, gv.horzAxis);
	dc.DrawLine(gv.graphRight, gv.graphTop, gv.graphRight, gv.graphBottom);
}

void VisualiserFrame::drawAxisTickLines(wxDC& dc) {
	wxColour currentColour = wxColour(0, 0, 0);
	dc.SetPen(wxPen(currentColour));

	for (int i = 0; i<int(gv.AxisTick.size()); ++i) {
		int x = getXFromDate(gv.AxisTick[i]);
		int y1 = gv.horzAxis;
		int y2 = gv.horzAxis + 10;
		dc.DrawLine(x, y1, x, y2);
		x -= 28;
		y1 += 15;
		dc.DrawText(gv.AxisTick[i].FormatISODate(), wxPoint(x, y1));
	}
}

void VisualiserFrame::drawModels(wxDC& dc) {
	if (selectedModel != -1) {
		Model const* model = project->getModelPtr(selectedModel);
		if (model->lastUpdated.IsValid()) {
			drawModel(model, dc);
		}
	}
}

void VisualiserFrame::drawModel(Model const* model, wxDC& dc) {
	if (!displayModels && !displayHouseEffects) return;
	ModelTimePoint const* thisTimePoint = &model->day[0];
	for (int i = 0; i < int(model->day.size()) - 1; ++i) {
		ModelTimePoint const* nextTimePoint = &model->day[i + 1];
		int x = getXFromDate(int(floor(model->effStartDate.GetMJD())) + i);
		int x2 = getXFromDate(int(floor(model->effStartDate.GetMJD())) + i + 1);
		if (displayModels) {
			int y = getYFrom2PP(thisTimePoint->trend2pp);
			int y2 = getYFrom2PP(nextTimePoint->trend2pp);
			dc.SetPen(wxPen(wxColour(0, 0, 0)));
			dc.DrawLine(x, y, x2, y2);
		}
		for (int pollsterIndex = 0; pollsterIndex < project->getPollsterCount() && displayHouseEffects; ++pollsterIndex) {
			int y = getYFrom2PP(thisTimePoint->houseEffect[pollsterIndex] + 50.0f);
			int y2 = getYFrom2PP(nextTimePoint->houseEffect[pollsterIndex] + 50.0f);
			dc.SetPen(wxPen(project->getPollster(pollsterIndex).colour));
			dc.DrawLine(x, y, x2, y2);
		}
		thisTimePoint = nextTimePoint;
	}
}

// draws lines for the vote projections.
void VisualiserFrame::drawProjections(wxDC& dc) {
	if (selectedProjection != -1) {
		Projection const* projection = project->getProjectionPtr(selectedProjection);
		if (projection->lastUpdated.IsValid()) {
			drawProjection(projection, dc);
		}
	}
}

// draws lines for the given projection.
void VisualiserFrame::drawProjection(Projection const* projection, wxDC& dc) {
	for (int i = 0; i < int(projection->meanProjection.size()) - 1; ++i) {
		int x = getXFromDate(int(floor(projection->baseModel->effEndDate.GetMJD())) + i);
		int x2 = getXFromDate(int(floor(projection->baseModel->effEndDate.GetMJD())) + i + 1);
		for (int sigma = -2; sigma <= 2; ++sigma) {
			int y = getYFrom2PP(projection->meanProjection[i] + projection->sdProjection[i] * sigma);
			int y2 = getYFrom2PP(projection->meanProjection[i + 1] + projection->sdProjection[i + 1] * sigma);
			int greyLevel = 50 + abs(sigma) * 50;
			dc.SetPen(wxPen(wxColour(greyLevel, greyLevel, greyLevel)));
			dc.DrawLine(x, y, x2, y2);
		}
	}
}

void VisualiserFrame::drawPollDots(wxDC& dc) {
	for (int i = 0; i < project->getPollCount(); ++i) {
		Poll const* poll = project->getPollPtr(i);
		int x = getXFromDate(poll->date);
		int y = getYFrom2PP(poll->getBest2pp());
		// first draw the yellow outline for selected poll, if appropriate
		if (poll == mouseOverPoll) {
			setBrushAndPen(wxColourDatabase().Find("YELLOW"), dc);
			dc.DrawCircle(x, y, 5);
		}
		// now draw the actual poll dot
		setBrushAndPen(poll->pollster->colour, dc);
		dc.DrawCircle(x, y, 3);
	}
}

void VisualiserFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

int VisualiserFrame::getXFromDate(wxDateTime const& date) {
	int dateNum = int(floor(date.GetModifiedJulianDayNumber()));
	return int(float(dateNum - project->getVisStartDay()) / float(project->getVisEndDay() - project->getVisStartDay()) * gv.graphWidth + gv.graphMargin);
}

int VisualiserFrame::getXFromDate(int date) {
	return int(float(date - project->getVisStartDay()) / float(project->getVisEndDay() - project->getVisStartDay()) * gv.graphWidth + gv.graphMargin);
}

wxDateTime VisualiserFrame::getDateFromX(int x) {
	if (x < gv.graphMargin || x > gv.graphMargin + gv.graphWidth) return wxInvalidDateTime;
	int dateNum = int((float(x) - gv.graphMargin) / gv.graphWidth * float(project->getVisEndDay() - project->getVisStartDay())) + project->getVisStartDay();
	wxDateTime date = project->MjdToDate(dateNum);
	return date;
}

int VisualiserFrame::getYFrom2PP(float this2pp) {
	return int((this2pp - 50.0f) * -15.0f + gv.horzAxis);
}

Poll const* VisualiserFrame::getPollFromMouse(wxPoint point) {
	Poll const* bestPoll = nullptr;
	if (!project->getPollCount()) return nullptr;
	int closest = 10000000;
	for (int i = 0; i < project->getPollCount(); ++i) {
		Poll const* tempPoll = project->getPollPtr(i);
		int xDist = abs(getXFromDate(tempPoll->date) - point.x);
		int yDist = abs(getYFrom2PP(tempPoll->getBest2pp()) - point.y);
		int totalDist = xDist * xDist + yDist * yDist;
		if (totalDist < closest && totalDist < PA_Poll_Select_Distance_Squared_Maximum) {
			closest = totalDist;
			bestPoll = tempPoll;
		}
	}
	return bestPoll;
}

void VisualiserFrame::determineMouseOverPollRect() {
	if (!mouseOverPoll) return;
	int nLines = 2;
	if (mouseOverPoll->calc2pp >= 0) nLines++;
	if (mouseOverPoll->respondent2pp >= 0) nLines++;
	if (mouseOverPoll->reported2pp >= 0) nLines++;
	for (int i = 0; i < 16; ++i) {
		if (mouseOverPoll->primary[i] >= 0) nLines++;
	}
	int height = nLines * 20 + 7;
	int width = 200;
	int left = getXFromDate(mouseOverPoll->date) - width;
	int top = getYFrom2PP(mouseOverPoll->getBest2pp());
	if (left < 0)
		left += width + 10;
	if ((top + height) > gv.graphBottom + gv.graphMargin)
		top = gv.graphBottom + gv.graphMargin - height;
	mouseOverPollRect = wxRect(left, top, width, height);
}

void VisualiserFrame::drawMouseOverPollRect(wxDC& dc) {
	if (!mouseOverPoll) return;
	dc.SetPen(wxPen(wxColour(0, 0, 0))); // black border
	dc.SetBrush(wxBrush(wxColour(255, 255, 255))); // white interior
	dc.DrawRoundedRectangle(mouseOverPollRect, 3);
}

void VisualiserFrame::drawMouseOverPollText(wxDC& dc) {
	if (!mouseOverPoll) return;
	wxPoint currentPoint = mouseOverPollRect.GetTopLeft() += wxPoint(3, 3);
	dc.SetPen(wxPen(wxColour(0, 0, 0))); // black text
	dc.DrawText(mouseOverPoll->pollster->name, currentPoint);
	currentPoint += wxPoint(0, 20);
	dc.DrawText(mouseOverPoll->date.FormatISODate(), currentPoint);
	currentPoint += wxPoint(0, 20);
	if (mouseOverPoll->reported2pp >= 0) {
		dc.DrawText("Previous-Election 2PP: " + mouseOverPoll->getReported2ppString(), currentPoint);
		currentPoint += wxPoint(0, 20);
	}
	if (mouseOverPoll->respondent2pp >= 0) {
		dc.DrawText("Respondent-allocated 2PP: " + mouseOverPoll->getRespondent2ppString(), currentPoint);
		currentPoint += wxPoint(0, 20);
	}
	if (mouseOverPoll->calc2pp >= 0) {
		dc.DrawText("Calculated 2PP: " + mouseOverPoll->getCalc2ppString(), currentPoint);
		currentPoint += wxPoint(0, 20);
	}
	for (int i = 0; i < 15; ++i) {
		if (mouseOverPoll->primary[i] >= 0) {
			dc.DrawText(project->parties().view(i).abbreviation + " primary: " + mouseOverPoll->getPrimaryString(i), currentPoint);
			currentPoint += wxPoint(0, 20);
		}
	}
	if (mouseOverPoll->primary[15] >= 0) {
		dc.DrawText("Others primary: " + mouseOverPoll->getPrimaryString(15), currentPoint);
		currentPoint += wxPoint(0, 20);
	}
}

void VisualiserFrame::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
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
	for (auto it = project->getModelBegin(); it != project->getModelEnd(); ++it) {
		modelArray.push_back(it->name);
	}
	std::string modelBoxString;
	if (selectedModel >= int(modelArray.size())) {
		selectedModel = int(modelArray.size()) - 1;
	}
	if (selectedModel >= 0) {
		modelBoxString = modelArray[selectedModel];
	}

	// Prepare the list of projections
	wxArrayString projectionArray;
	std::string projectionBoxString = "";

	if (project->getProjectionCount()) {
		for (auto it = project->getProjectionBegin(); it != project->getProjectionEnd(); ++it) {
			projectionArray.push_back(it->name);
		}
		if (selectedProjection >= int(projectionArray.size())) {
			selectedProjection = int(projectionArray.size()) - 1;
		}
		if (selectedModel >= 0) {
			projectionBoxString = projectionArray[selectedModel];
		}
	}

	selectModelComboBox = new wxComboBox(toolBar, PA_VisualiserFrame_SelectModelID, modelBoxString, wxPoint(0, 0), wxSize(150, 30), modelArray);

	selectProjectionComboBox = new wxComboBox(toolBar, PA_VisualiserFrame_SelectProjectionID, projectionBoxString, wxPoint(0, 0), wxSize(150, 30), projectionArray);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_VisualiserFrame_TogglePollsID, "Toggle Poll Display", toolBarBitmaps[0], wxNullBitmap, wxITEM_CHECK, "Toggle Poll Display");
	toolBar->AddSeparator();
	toolBar->AddTool(PA_VisualiserFrame_ToggleModelsID, "Toggle Model Display", toolBarBitmaps[1], wxNullBitmap, wxITEM_CHECK, "Toggle Model Display");
	toolBar->AddTool(PA_VisualiserFrame_ToggleHouseEffectsID, "Toggle House Effect Display", toolBarBitmaps[2], wxNullBitmap, wxITEM_CHECK, "Toggle House Effect Display");
	toolBar->AddControl(selectModelComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(PA_VisualiserFrame_ToggleProjectionsID, "Toggle Projection Display", toolBarBitmaps[3], wxNullBitmap, wxITEM_CHECK, "Toggle Projection Display");
	toolBar->AddControl(selectProjectionComboBox);
	toolBar->ToggleTool(PA_VisualiserFrame_TogglePollsID, displayPolls);
	toolBar->ToggleTool(PA_VisualiserFrame_ToggleModelsID, displayModels);
	toolBar->ToggleTool(PA_VisualiserFrame_ToggleHouseEffectsID, displayHouseEffects);
	toolBar->ToggleTool(PA_VisualiserFrame_ToggleProjectionsID, displayProjections);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}