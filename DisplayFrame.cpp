#include "DisplayFrame.h"

#include "General.h"

#include "wx/dcbuffer.h"

#include <algorithm>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectSimulation
};

constexpr int GraphParty = 1;

constexpr float ProbabilityBoxMargin = 10.0f;
constexpr float ProbabilityBoxLeft = ProbabilityBoxMargin;
constexpr float ProbabilityBoxWidth = 340.0f;
constexpr float ProbabilityBoxHeight = 180.0f;
constexpr float ProbabilityBoxPadding = 10.0f;
constexpr float ProbabilityBoxTextWidth = 130.0f;
constexpr float ProbabilityBoxTextHeight = 26.0f;
constexpr float ProbabilityBoxTextPadding = 8.0f;
constexpr float ProbabilityBoxTextOffset = 5.0f;
constexpr float ProbabilityBoxDataWidth = 80.0f;
constexpr float ProbabilityBoxSumWidth = 90.0f;
constexpr float ProbabilityBoxSumHeight = 50.0f;
constexpr float ProbabilityBoxTextInnerPadding = 5.0f;

constexpr float ExpectationBoxLeft = ProbabilityBoxMargin;
constexpr float ExpectationBoxWidth = ProbabilityBoxWidth;
constexpr float ExpectationBoxHeight = 200.0f;
constexpr float ExpectationBoxTitleHeight = 30.0f;
constexpr float ExpectationBoxTextHeight = 24.0f;

constexpr float StatesBoxLeft = ProbabilityBoxLeft + ProbabilityBoxWidth + ProbabilityBoxMargin;
constexpr float StatesBoxWidth = 400.0f;
constexpr float StatesBoxHeight = 250.0f;
constexpr float StatesBoxTitleHeight = 30.0f;
constexpr float StatesBoxTextHeight = 24.0f;

constexpr float BoundsBoxLeft = StatesBoxLeft;
constexpr float BoundsBoxWidth = StatesBoxWidth;
constexpr float BoundsBoxHeight = 130.0f;
constexpr float BoundsBoxTitleHeight = 30.0f;
constexpr float BoundsBoxTextHeight = 24.0f;

constexpr float GraphBoxLeft = ProbabilityBoxLeft;
constexpr float GraphBoxWidth = ProbabilityBoxWidth + ProbabilityBoxMargin + StatesBoxWidth;
constexpr float GraphAxisOffset = 20.0f;
constexpr float GraphAxisLabelWidth = 50.0f;
constexpr float GraphTopSpace = 10.0f;
constexpr int GraphAxisLabelInterval = 5;

constexpr float SeatsBoxLeft = StatesBoxLeft + StatesBoxWidth + ProbabilityBoxMargin;
constexpr float SeatsBoxTitleHeight = 30.0f;
constexpr float SeatsBoxTextHeight = 11.0f;

constexpr float CornerRounding = 30.0f;
constexpr float TextBoxCornerRounding = 20.0f;

const wxFont font8 = wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
const wxFont font13 = wxFont(13, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
const wxFont font15 = wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
const wxFont font18 = wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");

// frame constructor
DisplayFrame::DisplayFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Display", wxPoint(333, 0), project),
	refresher(refresher)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();

	paint();

	bindEventHandlers();
}

void DisplayFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
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

void DisplayFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &DisplayFrame::OnResize, this, PA_DisplayFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &DisplayFrame::OnSimulationSelection, this, ControlId::SelectSimulation);
	dcPanel->Bind(wxEVT_MOTION, &DisplayFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &DisplayFrame::OnPaint, this, ControlId::DcPanel);
}

void DisplayFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void DisplayFrame::updateInterface() {
}

void DisplayFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) const {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void DisplayFrame::render(wxDC& dc) {

	clearDC(dc);

	if (selectedSimulation < 0 || selectedSimulation >= project->simulations().count()) return;

	if (!simulation().lastUpdated.IsValid()) return;

	defineGraphLimits();
	dc.SetFont(font13);

	drawBackground(dc);

	drawProbabilityBox(dc);

	drawSumOfLeads(dc);

	drawExpectationsBox(dc);

	drawStatesBox(dc);

	drawBoundsBox(dc);

	drawGraphBox(dc);

	drawSeatsBox(dc);
}

void DisplayFrame::drawBackground(wxDC& dc) const
{
	wxRect backgroundRect = wxRect(0, dv.displayTop, dv.DCwidth, backgroundHeight());
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRectangle(backgroundRect);
	const wxColour backgroundGrey = wxColour(210, 210, 210); // light grey
	setBrushAndPen(backgroundGrey, dc);
	dc.DrawRoundedRectangle(backgroundRect, CornerRounding);
}

void DisplayFrame::drawProbabilityBox(wxDC& dc) const
{
	// Probability Box
	wxRect redRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop(), ProbabilityBoxWidth, ProbabilityBoxHeight);
	const wxColour probBoxRed = wxColour(255, 180, 170); // light red
	setBrushAndPen(probBoxRed, dc);
	dc.DrawRoundedRectangle(redRect, CornerRounding);
	wxRect blueRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, ProbabilityBoxHeight * 0.5f);
	const wxColour probBoxBlue = wxColour(170, 170, 255); // light blue
	setBrushAndPen(probBoxBlue, dc);
	dc.DrawRoundedRectangle(blueRect, CornerRounding);
	wxRect blueCoverRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, CornerRounding);
	dc.DrawRectangle(blueCoverRect);

	// Probability Box Text Rectangles
	wxRect probTextRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding, probabilityBoxTop() + ProbabilityBoxPadding,
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
		probabilityBoxTop() + ProbabilityBoxPadding,
		ProbabilityBoxDataWidth, ProbabilityBoxTextHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(simulation().partyOneMajorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(simulation().partyOneMinorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(simulation().hungPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(simulation().partyTwoMinorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
	probDataRect.Offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(probDataRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(formatFloat(simulation().partyTwoMajorityPercent, 2) + "%", probDataRect, wxALIGN_CENTRE);
}

void DisplayFrame::drawSumOfLeads(wxDC& dc) const
{
	wxRect probPartyOneRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		probabilityBoxTop() + ProbabilityBoxHeight * 0.5f - ProbabilityBoxSumHeight - ProbabilityBoxTextPadding,
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
	dc.DrawLabel(formatFloat(simulation().getPartyOneWinPercent(), 2) + "%", probPartyOneNumberRect, wxALIGN_CENTRE);
	dc.SetFont(font13);
	// Second party
	wxRect probPartyTwoRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		probabilityBoxTop() + ProbabilityBoxHeight * 0.5f + ProbabilityBoxTextPadding,
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
	dc.DrawLabel(formatFloat(100 - simulation().getPartyOneWinPercent(), 2) + "%", probPartyTwoNumberRect, wxALIGN_CENTRE);
	dc.SetFont(font13);
}

void DisplayFrame::drawExpectationsBox(wxDC& dc) const
{
	wxRect expBoxRect = wxRect(ExpectationBoxLeft, expectationBoxTop(), ExpectationBoxWidth, ExpectationBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(expBoxRect, TextBoxCornerRounding);
	wxRect expBoxTitleRect = expBoxRect;
	expBoxTitleRect.SetBottom(expectationBoxTop() + ExpectationBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font15);
	dc.DrawLabel("Seat expectations:", expBoxTitleRect, wxALIGN_CENTRE);

	wxRect expBoxNameRect = wxRect(ExpectationBoxLeft, expBoxTitleRect.GetBottom(),
		ExpectationBoxWidth * 0.7f, ExpectationBoxTextHeight);
	wxRect expBoxDataRect = wxRect(expBoxNameRect.GetRight(), expBoxTitleRect.GetBottom(),
		ExpectationBoxWidth - expBoxNameRect.GetWidth(), ExpectationBoxTextHeight);
	dc.SetFont(font13);
	for (int partyIndex = 0; partyIndex < project->parties().count(); ++partyIndex) {
		if (partyIndex >= int(simulation().partyWinExpectation.size())) break;
		dc.DrawLabel(project->parties().viewByIndex(partyIndex).name, expBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(simulation().partyWinExpectation[partyIndex], 2), expBoxDataRect, wxALIGN_CENTRE);
		expBoxNameRect.Offset(0, ExpectationBoxTextHeight);
		expBoxDataRect.Offset(0, ExpectationBoxTextHeight);
	}
}

void DisplayFrame::drawStatesBox(wxDC & dc) const
{
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
	for (auto const& regionPair : project->regions()) {
		Region const& thisRegion = regionPair.second;
		int regionIndex = project->regions().idToIndex(regionPair.first);
		if (regionIndex >= int(simulation().regionPartyWinExpectation.size())) break;
		float alpSeats = simulation().regionPartyWinExpectation[regionIndex][0];
		float lnpSeats = simulation().regionPartyWinExpectation[regionIndex][1];
		float othSeats = simulation().getOthersWinExpectation(regionIndex);
		float alpChange = alpSeats - thisRegion.partyLeading[0];
		float lnpChange = lnpSeats - thisRegion.partyLeading[1];
		float othChange = othSeats - thisRegion.getOthersLeading();
		statesBoxNameRect.Offset(0, StatesBoxTextHeight);
		statesBoxALPRect.Offset(0, StatesBoxTextHeight);
		statesBoxLNPRect.Offset(0, StatesBoxTextHeight);
		statesBoxOtherRect.Offset(0, StatesBoxTextHeight);
		dc.DrawLabel(thisRegion.name, statesBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(alpSeats, 2) + " " + (alpChange >= 0 ? "+" : "") + formatFloat(alpChange, 2),
			statesBoxALPRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(lnpSeats, 2) + " " + (lnpChange >= 0 ? "+" : "") + formatFloat(lnpChange, 2),
			statesBoxLNPRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(othSeats, 2) + " " + (othChange >= 0 ? "+" : "") + formatFloat(othChange, 2),
			statesBoxOtherRect, wxALIGN_CENTRE);
	}
}

void DisplayFrame::drawBoundsBox(wxDC & dc) const
{
	using std::to_string;
	float StatesBoxTop = dv.displayTop + ProbabilityBoxMargin;
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
	if (simulation().lastUpdated.IsValid()) {
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel(project->parties().view(0).abbreviation, boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyOneProbabilityBounds[3]) + "-" + to_string(simulation().partyOneProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyOneProbabilityBounds[2]) + "-" + to_string(simulation().partyOneProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyOneProbabilityBounds[1]) + "-" + to_string(simulation().partyOneProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyOneProbabilityBounds[0]) + "-" + to_string(simulation().partyOneProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel(project->parties().view(1).abbreviation, boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyTwoProbabilityBounds[3]) + "-" + to_string(simulation().partyTwoProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyTwoProbabilityBounds[2]) + "-" + to_string(simulation().partyTwoProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyTwoProbabilityBounds[1]) + "-" + to_string(simulation().partyTwoProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().partyTwoProbabilityBounds[0]) + "-" + to_string(simulation().partyTwoProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
		boundsBoxNameRect.Offset(0, BoundsBoxTextHeight);
		boundsBox50Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox80Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox95Rect.Offset(0, BoundsBoxTextHeight);
		boundsBox99Rect.Offset(0, BoundsBoxTextHeight);
		dc.DrawLabel("Others", boundsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().othersProbabilityBounds[3]) + "-" + to_string(simulation().othersProbabilityBounds[4]), boundsBox50Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().othersProbabilityBounds[2]) + "-" + to_string(simulation().othersProbabilityBounds[5]), boundsBox80Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().othersProbabilityBounds[1]) + "-" + to_string(simulation().othersProbabilityBounds[6]), boundsBox95Rect, wxALIGN_CENTRE);
		dc.DrawLabel(to_string(simulation().othersProbabilityBounds[0]) + "-" + to_string(simulation().othersProbabilityBounds[7]), boundsBox99Rect, wxALIGN_CENTRE);
	}
}

void DisplayFrame::drawGraphBox(wxDC & dc) const
{
	float BackgroundHeight = dv.DCheight - dv.displayTop;
	float GraphBoxTop = ProbabilityBoxMargin + expectationBoxTop() + ExpectationBoxHeight;
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
	if (simulation().lastUpdated.IsValid()) {
		int lowestSeatFrequency = simulation().getMinimumSeatFrequency(GraphParty);
		int highestSeatFrequency = simulation().getMaximumSeatFrequency(GraphParty);
		int seatRange = highestSeatFrequency - lowestSeatFrequency;
		int modalSeatFrequency = simulation().getModalSeatFrequencyCount(GraphParty);
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
				float proportionOfMax = float(simulation().partySeatWinFrequency[1][seatNum]) / float(modalSeatFrequency) * 0.9999f;
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
}

void DisplayFrame::drawSeatsBox(wxDC & dc) const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - ProbabilityBoxMargin;
	float SeatsBoxHeight = backgroundHeight() - ProbabilityBoxMargin * 2.0f;
	wxRect seatsBoxRect = wxRect(SeatsBoxLeft, probabilityBoxTop(), SeatsBoxWidth, SeatsBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(seatsBoxRect, CornerRounding);
	wxRect seatsBoxTitleRect = seatsBoxRect;
	seatsBoxTitleRect.SetBottom(probabilityBoxTop() + SeatsBoxTitleHeight);
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
	int closeSeat = simulation().findBestSeatDisplayCenter(GraphParty, seatsFittingInBox, *project);
	int firstSeat = std::max(std::min(closeSeat - seatsFittingInBox / 2, int(simulation().classicSeatIds.size()) - seatsFittingInBox), 0);
	dc.SetFont(font8);
	for (int seatIndex = firstSeat; seatIndex < firstSeat + seatsFittingInBox && seatIndex < int(simulation().classicSeatIds.size()); ++seatIndex) {
		Seat::Id seatId = simulation().classicSeatIds[seatIndex];
		if (!project->seats().exists(seatId)) continue;
		Seat const& seat = project->seats().view(seatId);
		float lnpWinPercent = simulation().getClassicSeatMajorPartyWinRate(seatIndex, GraphParty, *project); // NEED TO CHANGE THIS
		dc.DrawLabel(seat.name, seatsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(project->parties().view(seat.incumbent).abbreviation + " (" + formatFloat(seat.margin, 1) + ")", seatsBoxMarginRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(lnpWinPercent, 2), seatsBoxLnpWinRect, wxALIGN_CENTRE);
		seatsBoxNameRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxMarginRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxLnpWinRect.Offset(0, SeatsBoxTextHeight);
	}
}

void DisplayFrame::clearDC(wxDC& dc) const {
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
	for (auto const& [key, simulation] : project->simulations()) {
		simulationArray.push_back(simulation.name);
	}
	std::string comboBoxString;
	if (selectedSimulation >= int(simulationArray.size())) {
		selectedSimulation = int(simulationArray.size()) - 1;
	}
	if (selectedSimulation >= 0) {
		comboBoxString = simulationArray[selectedSimulation];
	}

	selectSimulationComboBox = new wxComboBox(toolBar, ControlId::SelectSimulation, comboBoxString, wxPoint(0, 0), wxSize(150, 30), simulationArray);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSimulationComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

float DisplayFrame::backgroundHeight() const
{
	return dv.DCheight - dv.displayTop;
}

float DisplayFrame::probabilityBoxTop() const
{
	return dv.displayTop + ProbabilityBoxMargin;
}

float DisplayFrame::expectationBoxTop() const
{
	return probabilityBoxTop() + ProbabilityBoxMargin + ProbabilityBoxHeight;
}

Simulation const& DisplayFrame::simulation() const
{
	return project->simulations().viewByIndex(selectedSimulation);
}
