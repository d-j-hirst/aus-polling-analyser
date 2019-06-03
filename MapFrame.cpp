#include "MapFrame.h"
#include "General.h"
#include <algorithm>

// IDs for the controls and the menu commands
enum {
	PA_MapFrame_Base = 450, // To avoid mixing events with other frames.
	PA_MapFrame_FrameID,
	PA_MapFrame_DcPanelID,
	PA_MapFrame_SelectSeatID
};


// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// square of the maximum distance from the mouse pointer to a poll point that will allow it to be selected.
const int PA_Poll_Select_Distance_Squared_Maximum = 16;

// frame constructor
MapFrame::MapFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_MapFrame_FrameID, "Display", wxPoint(333, 0), project),
	parent(parent)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, PA_MapFrame_DcPanelID, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();

	paint();

	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &MapFrame::OnResize, this, PA_MapFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &MapFrame::OnSimulationSelection, this, PA_MapFrame_SelectSeatID);
	dcPanel->Bind(wxEVT_MOTION, &MapFrame::OnMouseMove, this, PA_MapFrame_DcPanelID);
	dcPanel->Bind(wxEVT_PAINT, &MapFrame::OnPaint, this, PA_MapFrame_DcPanelID);
	dcPanel->Bind(wxEVT_MOUSEWHEEL, &MapFrame::OnMouseWheel, this, PA_MapFrame_DcPanelID);
}

void MapFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void MapFrame::resetMouseOver() {

}

void MapFrame::refreshData() {
	refreshToolbar();
}

wxColour mixColour(wxColour const& colour1, wxColour const& colour2, float colour1Percent) {
	unsigned char red = unsigned char(std::clamp(float(colour1.Red()) * colour1Percent + float(colour2.Red()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	unsigned char green = unsigned char(std::clamp(float(colour1.Green()) * colour1Percent + float(colour2.Green()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	unsigned char blue = unsigned char(std::clamp(float(colour1.Blue()) * colour1Percent + float(colour2.Blue()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	return wxColour(red, green, blue);
}

Point2Di MapFrame::calculateScreenPosFromCoords(Point2Df coords)
{
	Point2Df mapCoords = coords.scale(dv.minCoords, dv.maxCoords).componentMultiplication(dv.dcSize());
	return Point2Di(int(std::floor(mapCoords.x)), int(std::floor(mapCoords.y)));
}

int MapFrame::calculateCircleSizeFromBooth(Results::Booth const & booth)
{
	return std::clamp(int(std::log(booth.totalNewTcpVotes()) - 2.5f), 2, 6);
}

void MapFrame::drawBoothsForSeat(Seat const& seat, wxDC& dc)
{
	if (!seat.latestResults) return;
	for (int boothId : seat.latestResults->booths) {
		auto const& booth = project->getBooth(boothId);
		if (!booth.totalNewTcpVotes()) continue;
		Point2Di mapCoords = calculateScreenPosFromCoords(Point2Df(booth.coords.longitude, booth.coords.latitude));
		int winnerId = (booth.newTcpVote[0] > booth.newTcpVote[1] ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
		Party const* winnerParty = project->getPartyByCandidate(winnerId);
		wxColour winnerColour = wxColour(winnerParty->colour.r, winnerParty->colour.g, winnerParty->colour.b, 255);
		float tcpMargin = float(std::max(booth.newTcpVote[0], booth.newTcpVote[1])) / float(booth.totalNewTcpVotes()) - 0.5f;
		float colourFactor = std::clamp(tcpMargin * 4.0f, 0.0f, 1.0f);
		wxColour finalColour = mixColour(winnerColour, wxColour(255, 255, 255), colourFactor);
		int circleSize = calculateCircleSizeFromBooth(booth);
		dc.SetBrush(finalColour);
		dc.DrawCircle(wxPoint(mapCoords.x, mapCoords.y), circleSize);
	}
}

void MapFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
}

void MapFrame::OnSimulationSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedSeat = selectSeatComboBox->GetCurrentSelection();
	paint();
}

// Handles the movement of the mouse in the display frame.
void MapFrame::OnMouseMove(wxMouseEvent& event) {
	if (event.Dragging()) {
		if (dragStart.x == -1) {
			dragStart = Point2Di(event.GetX(), event.GetY());
		}
		else {
			Point2Di mousePos = Point2Di(event.GetX(), event.GetY());
			Point2Di pixelsMoved = Point2Di(event.GetX(), event.GetY()) - dragStart;
			Point2Df mapSize = dv.maxCoords - dv.minCoords;
			Point2Df scaledPixelsMoved = Point2Df(pixelsMoved).scale(dv.dcTopLeft, dv.dcBottomRight);
			Point2Df degreesMoved = -scaledPixelsMoved.componentMultiplication(mapSize);
			Point2Df newTopLeft = dv.minCoords + degreesMoved;
			auto currentLatitudeRange = project->boothLatitudeRange();
			auto currentLongitudeRange = project->boothLongitudeRange();
			Point2Df minCoords = { currentLongitudeRange.x, currentLatitudeRange.x };
			Point2Df maxCoords = { currentLongitudeRange.y, currentLatitudeRange.y };
			newTopLeft = newTopLeft.max(minCoords);
			newTopLeft = newTopLeft.min(maxCoords - mapSize);
			dv.minCoords = newTopLeft;
			dv.maxCoords = newTopLeft + mapSize;
			dragStart = mousePos;
		}
	}
	else {
		dragStart = Point2Di(-1, -1);
	}
	//mouseOverPoll = getPollFromMouse(event.GetPosition());
	paint();
}

void MapFrame::OnMouseWheel(wxMouseEvent& event)
{
	float zoomFactor = pow(2.0f, -float(event.GetWheelRotation()) / float(event.GetWheelDelta()));
	Point2Df mapSize = dv.maxCoords - dv.minCoords;
	Point2Di scrollPos = Point2Di(event.GetX(), event.GetY());
	Point2Df scaledScrollPos = Point2Df(scrollPos).scale(dv.dcTopLeft, dv.dcBottomRight);
	Point2Df scrollCoords = scaledScrollPos.componentMultiplication(mapSize) + dv.minCoords;
	Point2Df newMapSize = mapSize * zoomFactor;
	Point2Df newTopLeft = scrollCoords - newMapSize * 0.5f;
	auto currentLatitudeRange = project->boothLatitudeRange();
	auto currentLongitudeRange = project->boothLongitudeRange();
	Point2Df minCoords = { currentLongitudeRange.x, currentLatitudeRange.x };
	Point2Df maxCoords = { currentLongitudeRange.y, currentLatitudeRange.y };
	Point2Df maxSize = maxCoords - minCoords;
	newMapSize = newMapSize.min(maxSize);
	newTopLeft = newTopLeft.max(minCoords);
	newTopLeft = newTopLeft.min(maxCoords - newMapSize);
	dv.minCoords = newTopLeft;
	dv.maxCoords = newTopLeft + newMapSize;
	paint();
}

void MapFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void MapFrame::updateInterface() {
}

void MapFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void MapFrame::render(wxDC& dc) {

	using std::to_string;

	clearDC(dc);

	defineGraphLimits();
	wxFont font8 = wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
	wxFont font13 = wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
	wxFont font15 = wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
	wxFont font18 = wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
	dc.SetFont(font13);

	// Background
	wxRect backgroundRect = wxRect(dv.dcTopLeft.x, dv.dcTopLeft.y, dv.dcSize().x, dv.dcSize().y);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRectangle(backgroundRect);
	const wxColour backgroundGrey = wxColour(210, 210, 210); // light grey
	setBrushAndPen(backgroundGrey, dc);
	dc.DrawRectangle(backgroundRect);

	if (dv.minCoords.isZero()) {
		auto currentLatitudeRange = project->boothLatitudeRange();
		auto currentLongitudeRange = project->boothLongitudeRange();
		dv.minCoords = { currentLongitudeRange.x, currentLatitudeRange.x };
		dv.maxCoords = { currentLongitudeRange.y, currentLatitudeRange.y };
	}

	dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
	if (selectedSeat <= 0) {
		for (auto seat = project->getSeatBegin(); seat != project->getSeatEnd(); ++seat) {
			drawBoothsForSeat(*seat, dc);
		}
	}
	else {
		drawBoothsForSeat(project->getSeat(selectedSeat - 1), dc);
	}

}

void MapFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
}

void MapFrame::defineGraphLimits() {
	dv.dcTopLeft = { 0.0f, 0.0f };
	dv.dcBottomRight = {float(dcPanel->GetClientSize().GetWidth()), float(dcPanel->GetClientSize().GetHeight())};
}

void MapFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// *** Simulation Combo Box *** //

	// Create the choices for the combo box.
	// By default "no seat" is the selected seat
	// Set the selected seat to be the first seat
	wxArrayString seatArray;
	seatArray.push_back("None");
	for (auto it = project->getSeatBegin(); it != project->getSeatEnd(); ++it) {
		seatArray.push_back(it->name);
	}
	std::string comboBoxString;
	if (selectedSeat >= int(seatArray.size())) {
		selectedSeat = int(seatArray.size()) - 1;
	}
	if (selectedSeat >= 0) {
		comboBoxString = seatArray[selectedSeat];
	}

	selectSeatComboBox = new wxComboBox(toolBar, PA_MapFrame_SelectSeatID, comboBoxString, wxPoint(0, 0), wxSize(150, 30), seatArray);
	selectedSeat = 0;
	selectSeatComboBox->Select(0);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSeatComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}