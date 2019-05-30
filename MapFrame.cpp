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

void MapFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the poll data table to the entire client size.
	//pollData->SetSize(wxSize(this->GetClientSize().x,
	//	this->GetClientSize().y));
}

void MapFrame::OnSimulationSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedSeat = selectSeatComboBox->GetCurrentSelection();
	paint();
}

// Handles the movement of the mouse in the display frame.
void MapFrame::OnMouseMove(wxMouseEvent& WXUNUSED(event)) {
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
	int numBooths = 0;
	dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
	for (auto seat = project->getSeatBegin(); seat != project->getSeatEnd(); ++seat) {
		if (!seat->latestResults) continue;
		for (int boothId : seat->latestResults->booths) {
			auto const& booth = project->getBooth(boothId);
			Point2Df coords = { booth.coords.longitude , booth.coords.latitude };
			Point2Df mapCoords = coords.scale(dv.minCoords, dv.maxCoords).componentMultiplication(dv.dcSize());
			int winnerId = (booth.tcpVote[0] > booth.tcpVote[1] ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
			Party const* winnerParty = project->getPartyByCandidate(winnerId);
			wxColour winnerColour = wxColour(winnerParty->colour.r, winnerParty->colour.g, winnerParty->colour.b, 255);
			dc.SetBrush(winnerColour);
			dc.DrawCircle(wxPoint(int(mapCoords.x), int(mapCoords.y)), 5);
			++numBooths;
		}
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
	selectSeatComboBox->Select(0);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSeatComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}