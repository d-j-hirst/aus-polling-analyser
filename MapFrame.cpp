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
	float BackgroundHeight = dv.DCheight - dv.displayTop;
	wxRect backgroundRect = wxRect(0, dv.displayTop, dv.DCwidth, BackgroundHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRectangle(backgroundRect);
	const wxColour backgroundGrey = wxColour(210, 210, 210); // light grey
	setBrushAndPen(backgroundGrey, dc);
	dc.DrawRectangle(backgroundRect);

	auto longitudeRange = project->boothLongitudeRange();
	auto latitudeRange = project->boothLatitudeRange();
	int numBooths = 0;
	setBrushAndPen(wxColour(0, 0, 0), dc);
	for (auto seat = project->getSeatBegin(); seat != project->getSeatEnd(); ++seat) {
		if (!seat->latestResults) continue;
		for (int boothId : seat->latestResults->booths) {
			float longitude = project->getBooth(boothId).coords.longitude;
			float latitude = project->getBooth(boothId).coords.latitude;
			float mapX = (longitude - longitudeRange.first) / (longitudeRange.second - longitudeRange.first) * dv.DCwidth;
			float mapY = dv.DCheight - (latitude - latitudeRange.first) / (latitudeRange.second - latitudeRange.first) * dv.DCheight;
			dc.DrawCircle(wxPoint(int(mapX), int(mapY)), 5);
			++numBooths;
		}
	}

}

void MapFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
}

void MapFrame::defineGraphLimits() {
	dv.DCwidth = dcPanel->GetClientSize().GetWidth();
	dv.DCheight = dcPanel->GetClientSize().GetHeight();

	dv.displayBottom = dv.DCheight;
	//dv.displayTop = toolBar->GetSize().GetHeight();
	dv.displayTop = 0;
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