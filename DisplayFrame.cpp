#include "DisplayFrame.h"
#include "General.h"
#include <algorithm>

// IDs for the controls and the menu commands
enum {
	PA_DisplayFrame_Base = 450, // To avoid mixing events with other frames.
	PA_DisplayFrame_FrameID,
	PA_DisplayFrame_DcPanelID,
	PA_DisplayFrame_SelectSimulationID
};

const int GraphParty = 1;

const float ProbabilityBoxMargin = 10.0f;
const float ProbabilityBoxLeft = ProbabilityBoxMargin;
const float ProbabilityBoxWidth = 340.0f;
const float ProbabilityBoxHeight = 180.0f;
const float ProbabilityBoxPadding = 10.0f;
const float ProbabilityBoxTextWidth = 130.0f;
const float ProbabilityBoxTextHeight = 26.0f;
const float ProbabilityBoxTextPadding = 8.0f;
const float ProbabilityBoxTextOffset = 5.0f;
const float ProbabilityBoxDataWidth = 80.0f;
const float ProbabilityBoxSumWidth = 90.0f;
const float ProbabilityBoxSumHeight = 50.0f;
const float ProbabilityBoxTextInnerPadding = 5.0f;

const float ExpectationBoxLeft = ProbabilityBoxMargin;
const float ExpectationBoxWidth = ProbabilityBoxWidth;
const float ExpectationBoxHeight = 200.0f;
const float ExpectationBoxTitleHeight = 30.0f;
const float ExpectationBoxTextHeight = 24.0f;

const float StatesBoxLeft = ProbabilityBoxLeft + ProbabilityBoxWidth + ProbabilityBoxMargin;
const float StatesBoxWidth = 400.0f;
const float StatesBoxHeight = 250.0f;
const float StatesBoxTitleHeight = 30.0f;
const float StatesBoxTextHeight = 24.0f;

const float BoundsBoxLeft = StatesBoxLeft;
const float BoundsBoxWidth = StatesBoxWidth;
const float BoundsBoxHeight = 130.0f;
const float BoundsBoxTitleHeight = 30.0f;
const float BoundsBoxTextHeight = 24.0f;

const float GraphBoxLeft = ProbabilityBoxLeft;
const float GraphBoxWidth = ProbabilityBoxWidth + ProbabilityBoxMargin + StatesBoxWidth;
const float GraphAxisOffset = 20.0f;
const float GraphAxisLabelWidth = 50.0f;
const float GraphTopSpace = 10.0f;
const int GraphAxisLabelInterval = 5;

const float SeatsBoxLeft = StatesBoxLeft + StatesBoxWidth + ProbabilityBoxMargin;
const float SeatsBoxTitleHeight = 30.0f;
const float SeatsBoxTextHeight = 11.0f;

const float CornerRounding = 30.0f;
const float TextBoxCornerRounding = 20.0f;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// square of the maximum distance from the mouse pointer to a poll point that will allow it to be selected.
const int PA_Poll_Select_Distance_Squared_Maximum = 16;

// frame constructor
DisplayFrame::DisplayFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), PA_DisplayFrame_FrameID, "Display", wxPoint(333, 0), project),
	refresher(refresher)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, PA_DisplayFrame_DcPanelID, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();

	paint();

	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &DisplayFrame::OnResize, this, PA_DisplayFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &DisplayFrame::OnSimulationSelection, this, PA_DisplayFrame_SelectSimulationID);
	dcPanel->Bind(wxEVT_MOTION, &DisplayFrame::OnMouseMove, this, PA_DisplayFrame_DcPanelID);
	dcPanel->Bind(wxEVT_PAINT, &DisplayFrame::OnPaint, this, PA_DisplayFrame_DcPanelID);
}

void DisplayFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void DisplayFrame::resetMouseOver() {

}

void DisplayFrame::refreshData() {
	refreshToolbar();
}

void DisplayFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the poll data table to the entire client size.
	//pollData->SetSize(wxSize(this->GetClientSize().x,
	//	this->GetClientSize().y));
}

void DisplayFrame::OnSimulationSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedSimulation = selectSimulationComboBox->GetCurrentSelection();
	paint();
}

// Handles the movement of the mouse in the display frame.
void DisplayFrame::OnMouseMove(wxMouseEvent& WXUNUSED(event)) {
	paint();
}

void DisplayFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void DisplayFrame::updateInterface() {
}

void DisplayFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void DisplayFrame::render(wxDC& dc) {

	using std::to_string;

	clearDC(dc);

	if (!project->getSimulationCount()) return;

	Simulation* sim = project->getSimulationPtr(selectedSimulation);

	if (!sim->lastUpdated.IsValid()) return;

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
	dc.DrawRoundedRectangle(backgroundRect, CornerRounding);

	// Probability Box
	float ProbabilityBoxTop = dv.displayTop + ProbabilityBoxMargin;
	wxRect redRect = wxRect(ProbabilityBoxMargin, ProbabilityBoxTop, ProbabilityBoxWidth, ProbabilityBoxHeight);
	const wxColour probBoxRed = wxColour(255, 180, 170); // light red
	setBrushAndPen(probBoxRed, dc);
	dc.DrawRoundedRectangle(redRect, CornerRounding);
	wxRect blueRect = wxRect(ProbabilityBoxMargin, ProbabilityBoxTop + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, ProbabilityBoxHeight * 0.5f);
	const wxColour probBoxBlue = wxColour(170, 170, 255); // light blue
	setBrushAndPen(probBoxBlue, dc);
	dc.DrawRoundedRectangle(blueRect, CornerRounding);
	wxRect blueCoverRect = wxRect(ProbabilityBoxMargin, ProbabilityBoxTop + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, CornerRounding);
	dc.DrawRectangle(blueCoverRect);

	// Probability Box Text Rectangles
	wxRect probTextRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding, ProbabilityBoxTop + ProbabilityBoxPadding,
		ProbabilityBoxTextWidth, ProbabilityBoxTextHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probTextRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("ALP majority", probTextRect, wxALIGN_CENTRE);
	probTextRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probTextRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("ALP minority", probTextRect, wxALIGN_CENTRE);
	probTextRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probTextRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("Hung", probTextRect, wxALIGN_CENTRE);
	probTextRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probTextRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("LNP minority", probTextRect, wxALIGN_CENTRE);
	probTextRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probTextRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("LNP majority", probTextRect, wxALIGN_CENTRE);

	// Probability Box Data Rectangles
	wxRect probDataRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 2 + ProbabilityBoxTextWidth,
		ProbabilityBoxTop + ProbabilityBoxPadding,
		ProbabilityBoxDataWidth, ProbabilityBoxTextHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(sim->partyOneMajorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(sim->partyOneMinorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(sim->hungPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(sim->partyTwoMinorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(sim->partyTwoMajorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);

	// Sum of leads
	wxRect probPartyOneRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		ProbabilityBoxTop + ProbabilityBoxHeight * 0.5f - ProbabilityBoxSumHeight - ProbabilityBoxTextPadding,
		ProbabilityBoxSumWidth, ProbabilityBoxSumHeight);
	wxRect probPartyOneAnnotationRect = probPartyOneRect;
	probPartyOneAnnotationRect.SetBottom(probPartyOneAnnotationRect.GetTop() + 20);
	wxRect probPartyOneNumberRect = probPartyOneRect;
	probPartyOneNumberRect.SetTop(probPartyOneNumberRect.GetTop() + 20);
	probPartyOneNumberRect.SetBottom(probPartyOneRect.GetBottom());
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probPartyOneRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("ALP win:", probPartyOneAnnotationRect, wxALIGN_CENTRE);
	dc.SetFont(font18);
	dc.DrawLabel(formatFloat(sim->getPartyOneWinPercent(), 2) + "%", probPartyOneNumberRect, wxALIGN_CENTRE);
	dc.SetFont(font13);
	// Second party
	wxRect probPartyTwoRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		ProbabilityBoxTop + ProbabilityBoxHeight * 0.5f + ProbabilityBoxTextPadding,
		ProbabilityBoxSumWidth, ProbabilityBoxSumHeight);
	wxRect probPartyTwoAnnotationRect = probPartyTwoRect;
	probPartyTwoAnnotationRect.SetBottom(probPartyTwoAnnotationRect.GetTop() + 20);
	wxRect probPartyTwoNumberRect = probPartyTwoRect;
	probPartyTwoNumberRect.SetTop(probPartyTwoNumberRect.GetTop() + 20);
	probPartyTwoNumberRect.SetBottom(probPartyTwoRect.GetBottom());
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probPartyTwoRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel("LNP win:", probPartyTwoAnnotationRect, wxALIGN_CENTRE);
	dc.SetFont(font18);
	dc.DrawLabel(formatFloat(100 - sim->getPartyOneWinPercent(), 2) + "%", probPartyTwoNumberRect, wxALIGN_CENTRE);
	dc.SetFont(font13);

	// Expectation box
	float ExpectationBoxTop = ProbabilityBoxTop + ProbabilityBoxMargin + ProbabilityBoxHeight;
	wxRect expBoxRect = wxRect(ExpectationBoxLeft, ExpectationBoxTop, ExpectationBoxWidth, ExpectationBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(expBoxRect, TextBoxCornerRounding);
	wxRect expBoxTitleRect = expBoxRect;
	expBoxTitleRect.SetBottom(ExpectationBoxTop + ExpectationBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font15);
	dc.DrawLabel("Seat expectations:", expBoxTitleRect, wxALIGN_CENTRE);

	wxRect expBoxNameRect = wxRect(ExpectationBoxLeft, expBoxTitleRect.GetBottom(),
		ExpectationBoxWidth * 0.7f, ExpectationBoxTextHeight);
	wxRect expBoxDataRect = wxRect(expBoxNameRect.GetRight(), expBoxTitleRect.GetBottom(),
		ExpectationBoxWidth - expBoxNameRect.GetWidth(), ExpectationBoxTextHeight);
	dc.SetFont(font13);
	for (int partyIndex = 0; partyIndex < project->parties().count(); ++partyIndex) {
		if (partyIndex >= int(sim->partyWinExpectation.size())) break;
		dc.DrawLabel(project->parties().view(partyIndex).name, expBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(sim->partyWinExpectation[partyIndex], 2), expBoxDataRect, wxALIGN_CENTRE);
		expBoxNameRect.Offset(0, ExpectationBoxTextHeight);
		expBoxDataRect.Offset(0, ExpectationBoxTextHeight);
	}

	float StatesBoxTop = dv.displayTop + ProbabilityBoxMargin;
	wxRect statesBoxRect = wxRect(StatesBoxLeft, StatesBoxTop, StatesBoxWidth, StatesBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(statesBoxRect, CornerRounding);
	wxRect statesBoxTitleRect = statesBoxRect;
	statesBoxTitleRect.SetBottom(StatesBoxTop + StatesBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font15);
	dc.DrawLabel("State-by-state breakdown", statesBoxTitleRect, wxALIGN_CENTRE);

	wxRect statesBoxNameRect = wxRect(StatesBoxLeft, statesBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.25f, StatesBoxTextHeight);
	wxRect statesBoxALPRect = wxRect(statesBoxNameRect.GetRight(), statesBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.25f, StatesBoxTextHeight);
	wxRect statesBoxLNPRect = wxRect(statesBoxALPRect.GetRight(), statesBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.25f, StatesBoxTextHeight);
	wxRect statesBoxOtherRect = wxRect(statesBoxLNPRect.GetRight(), statesBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.25f, StatesBoxTextHeight);
	dc.DrawLabel("State", statesBoxNameRect, wxALIGN_CENTRE);
	dc.DrawLabel("ALP", statesBoxALPRect, wxALIGN_CENTRE);
	dc.DrawLabel("LNP", statesBoxLNPRect, wxALIGN_CENTRE);
	dc.DrawLabel("Others", statesBoxOtherRect, wxALIGN_CENTRE);
	dc.SetFont(font13);
	for (int regionIndex = 0; regionIndex < project->getRegionCount(); ++regionIndex) {
		if (regionIndex >= int(sim->regionPartyWinExpectation.size())) break;
		Region const* thisRegion = project->getRegionPtr(regionIndex);
		float alpSeats = sim->regionPartyWinExpectation[regionIndex][0];
		float lnpSeats = sim->regionPartyWinExpectation[regionIndex][1];
		float othSeats = sim->getOthersWinExpectation(regionIndex);
		float alpChange = alpSeats - thisRegion->partyLeading[0];
		float lnpChange = lnpSeats - thisRegion->partyLeading[1];
		float othChange = othSeats - thisRegion->getOthersLeading();
		statesBoxNameRect.Offset(0, StatesBoxTextHeight);
		statesBoxALPRect.Offset(0, StatesBoxTextHeight);
		statesBoxLNPRect.Offset(0, StatesBoxTextHeight);
		statesBoxOtherRect.Offset(0, StatesBoxTextHeight);
		dc.DrawLabel(thisRegion->name, statesBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(alpSeats, 2) + " " + (alpChange >= 0 ? "+" : "") + formatFloat(alpChange, 2),
			statesBoxALPRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(lnpSeats, 2) + " " + (lnpChange >= 0 ? "+" : "") + formatFloat(lnpChange, 2),
			statesBoxLNPRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(othSeats, 2) + " " + (othChange >= 0 ? "+" : "") + formatFloat(othChange, 2),
			statesBoxOtherRect, wxALIGN_CENTRE);
	}

	float BoundsBoxTop = ProbabilityBoxMargin + StatesBoxTop + StatesBoxHeight;
	wxRect boundsBoxRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(boundsBoxRect, CornerRounding);
	wxRect boundsBoxTitleRect = boundsBoxRect;
	boundsBoxTitleRect.SetBottom(BoundsBoxTop + BoundsBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font15);
	dc.DrawLabel("Probability intervals", boundsBoxTitleRect, wxALIGN_CENTRE);

	wxRect boundsBoxNameRect = wxRect(BoundsBoxLeft, boundsBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox50Rect = wxRect(boundsBoxNameRect.GetRight(), boundsBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox80Rect = wxRect(boundsBox50Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox95Rect = wxRect(boundsBox80Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox99Rect = wxRect(boundsBox95Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		StatesBoxWidth * 0.2f, BoundsBoxTextHeight);
	dc.DrawLabel("Party", boundsBoxNameRect, wxALIGN_CENTRE);
	dc.DrawLabel("50%", boundsBox50Rect, wxALIGN_CENTRE);
	dc.DrawLabel("80%", boundsBox80Rect, wxALIGN_CENTRE);
	dc.DrawLabel("95%", boundsBox95Rect, wxALIGN_CENTRE);
	dc.DrawLabel("99%", boundsBox99Rect, wxALIGN_CENTRE);
	dc.SetFont(font13);
	if (sim->lastUpdated.IsValid()) {
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel(project->parties().view(0).abbreviation, boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyOneProbabilityBounds[3]) + "-" + to_string(sim->partyOneProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyOneProbabilityBounds[2]) + "-" + to_string(sim->partyOneProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyOneProbabilityBounds[1]) + "-" + to_string(sim->partyOneProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyOneProbabilityBounds[0]) + "-" + to_string(sim->partyOneProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel(project->parties().view(1).abbreviation, boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyTwoProbabilityBounds[3]) + "-" + to_string(sim->partyTwoProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyTwoProbabilityBounds[2]) + "-" + to_string(sim->partyTwoProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyTwoProbabilityBounds[1]) + "-" + to_string(sim->partyTwoProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->partyTwoProbabilityBounds[0]) + "-" + to_string(sim->partyTwoProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel("Others", boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->othersProbabilityBounds[3]) + "-" + to_string(sim->othersProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->othersProbabilityBounds[2]) + "-" + to_string(sim->othersProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->othersProbabilityBounds[1]) + "-" + to_string(sim->othersProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(sim->othersProbabilityBounds[0]) + "-" + to_string(sim->othersProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
	}

	float GraphBoxTop = ProbabilityBoxMargin + ExpectationBoxTop + ExpectationBoxHeight;
	float GraphBoxHeight = BackgroundHeight - GraphBoxTop - ProbabilityBoxMargin + dv.displayTop;
	wxRect graphBoxRect = wxRect(GraphBoxLeft, GraphBoxTop, GraphBoxWidth, GraphBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(graphBoxRect, CornerRounding);
	float axisY = GraphBoxTop + GraphBoxHeight - GraphAxisOffset;
	float axisLeft = GraphBoxLeft + GraphAxisOffset;
	float axisRight = GraphBoxLeft + GraphBoxWidth - GraphAxisOffset;
	float axisMidpoint = (axisLeft + axisRight) * 0.5f;
	float axisLength = axisRight - axisLeft;
	wxPoint axisLeftPoint = wxPoint(axisLeft, axisY);
	wxPoint axisRightPoint = wxPoint(axisRight, axisY);
	setBrushAndPen(*wxBLACK, dc);
	if (sim->lastUpdated.IsValid()) {
		int lowestSeatFrequency = sim->getMinimumSeatFrequency(GraphParty);
		int highestSeatFrequency = sim->getMaximumSeatFrequency(GraphParty);
		int seatRange = highestSeatFrequency - lowestSeatFrequency;
		int modalSeatFrequency = sim->getModalSeatFrequencyCount(GraphParty);
		if (seatRange > 0) {
			int seatColumnWidth = int(floor(axisLength)) / seatRange;
			float columnRange = float(seatColumnWidth * seatRange); // if all columns have the same width then the total width of all columns
			// will usually be somewhat smaller than the actual width of the graph
			float columnStart = axisMidpoint - columnRange * 0.5f;
			int lowestAxisLabel = ((lowestSeatFrequency + GraphAxisLabelInterval - 1) / GraphAxisLabelInterval) * GraphAxisLabelInterval;
			float axisLabelStart = columnStart + float(seatColumnWidth * (lowestAxisLabel - lowestSeatFrequency));
			wxRect axisLabelRect = wxRect(axisLabelStart - GraphAxisLabelWidth * 0.5f, axisY,
				GraphAxisLabelWidth, GraphAxisOffset);
			for (int axisLabelNumber = lowestAxisLabel; axisLabelNumber <= highestSeatFrequency; axisLabelNumber += GraphAxisLabelInterval) {
				dc.DrawLabel(std::to_string(axisLabelNumber), axisLabelRect, wxALIGN_CENTRE);
				axisLabelRect.Offset(float(seatColumnWidth * GraphAxisLabelInterval), 0);
			}
			const float GraphMaxColumnHeight = GraphBoxHeight - GraphAxisOffset - GraphTopSpace;
			setBrushAndPen(*wxBLUE, dc);
			for (int seatNum = lowestSeatFrequency; seatNum <= highestSeatFrequency; ++seatNum) {
				float proportionOfMax = float(sim->partySeatWinFrequency[1][seatNum]) / float(modalSeatFrequency) * 0.9999f;
				float columnHeight = std::ceil(proportionOfMax * GraphMaxColumnHeight);
				float columnTop = axisY - columnHeight;
				float columnWidth = float(seatColumnWidth);
				float columnLeft = columnStart - columnWidth * 0.5f + float(seatColumnWidth) * float(seatNum - lowestSeatFrequency);
				wxRect graphColumnRect = wxRect(columnLeft, columnTop, columnWidth, columnHeight);
				dc.DrawRectangle(graphColumnRect);
				axisLabelRect.Offset(float(seatColumnWidth), 0);
			}
			setBrushAndPen(*wxBLACK, dc);
			dc.DrawLine(axisLeftPoint, axisRightPoint);
		}
	}

	float SeatsBoxTop = ProbabilityBoxTop;
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - ProbabilityBoxMargin;
	float SeatsBoxHeight = BackgroundHeight - ProbabilityBoxMargin * 2.0f;
	wxRect seatsBoxRect = wxRect(SeatsBoxLeft, SeatsBoxTop, SeatsBoxWidth, SeatsBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(seatsBoxRect, CornerRounding);
	wxRect seatsBoxTitleRect = seatsBoxRect;
	seatsBoxTitleRect.SetBottom(SeatsBoxTop + SeatsBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font15);
	dc.DrawLabel("Close Seats", seatsBoxTitleRect, wxALIGN_CENTRE);
	wxRect seatsBoxNameRect = wxRect(SeatsBoxLeft, seatsBoxTitleRect.GetBottom(),
		SeatsBoxWidth * 0.4f, SeatsBoxTextHeight);
	wxRect seatsBoxMarginRect = wxRect(seatsBoxNameRect.GetRight(), seatsBoxTitleRect.GetBottom(),
		SeatsBoxWidth * 0.3f, SeatsBoxTextHeight);
	wxRect seatsBoxLnpWinRect = wxRect(seatsBoxMarginRect.GetRight(), seatsBoxTitleRect.GetBottom(),
		SeatsBoxWidth * 0.3f, SeatsBoxTextHeight);
	int seatsFittingInBox = int((SeatsBoxHeight - SeatsBoxTitleHeight) / SeatsBoxTextHeight);
	int closeSeat = sim->findBestSeatDisplayCenter(project->parties().getPartyPtr(GraphParty), seatsFittingInBox);
	int firstSeat = std::max(std::min(closeSeat - seatsFittingInBox / 2, int(sim->classicSeatList.size()) - seatsFittingInBox), 0);
	dc.SetFont(font8);
	for (int seatIndex = firstSeat; seatIndex < firstSeat + seatsFittingInBox && seatIndex < int(sim->classicSeatList.size()); ++seatIndex) {
		ClassicSeat seat = sim->classicSeatList[seatIndex];
		float lnpWinPercent = sim->getClassicSeatMajorPartyWinRate(seatIndex, project->parties().getPartyPtr(GraphParty)); // NEED TO CHANGE THIS
		dc.DrawLabel(seat.seat->name, seatsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(seat.seat->incumbent->abbreviation + " (" + formatFloat(seat.seat->margin, 1) + ")", seatsBoxMarginRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(lnpWinPercent, 2), seatsBoxLnpWinRect, wxALIGN_CENTRE);
		seatsBoxNameRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxMarginRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxLnpWinRect.Offset(0, SeatsBoxTextHeight);
	}
}

void DisplayFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
}

void DisplayFrame::defineGraphLimits() {
	dv.DCwidth = dcPanel->GetClientSize().GetWidth();
	dv.DCheight = dcPanel->GetClientSize().GetHeight();

	dv.displayBottom = dv.DCheight;
	//dv.displayTop = toolBar->GetSize().GetHeight();
	dv.displayTop = 0;
}

void DisplayFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// *** Simulation Combo Box *** //

	// Create the choices for the combo box.
	// Set the selected simulation to be the first simulation
	wxArrayString simulationArray;
	int count = 0;
	for (auto it = project->getSimulationBegin(); it != project->getSimulationEnd(); ++it, ++count) {
		simulationArray.push_back(it->name);
	}
	std::string comboBoxString;
	if (selectedSimulation >= int(simulationArray.size())) {
		selectedSimulation = int(simulationArray.size()) - 1;
	}
	if (selectedSimulation >= 0) {
		comboBoxString = simulationArray[selectedSimulation];
	}

	selectSimulationComboBox = new wxComboBox(toolBar, PA_DisplayFrame_SelectSimulationID, comboBoxString, wxPoint(0, 0), wxSize(150, 30), simulationArray);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSimulationComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}