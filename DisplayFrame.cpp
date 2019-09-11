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

constexpr float RegionsBoxLeft = ProbabilityBoxLeft + ProbabilityBoxWidth + ProbabilityBoxMargin;
constexpr float RegionsBoxWidth = 400.0f;
constexpr float RegionsBoxHeight = 250.0f;
constexpr float RegionsBoxTitleHeight = 30.0f;
constexpr float RegionsBoxTextHeight = 24.0f;

constexpr float BoundsBoxLeft = RegionsBoxLeft;
constexpr float BoundsBoxWidth = RegionsBoxWidth;
constexpr float BoundsBoxHeight = 130.0f;
constexpr float BoundsBoxTitleHeight = 30.0f;
constexpr float BoundsBoxTextHeight = 24.0f;

constexpr float GraphBoxLeft = ProbabilityBoxLeft;
constexpr float GraphBoxWidth = ProbabilityBoxWidth + ProbabilityBoxMargin + RegionsBoxWidth;
constexpr float GraphAxisOffset = 20.0f;
constexpr float GraphAxisLabelWidth = 50.0f;
constexpr float GraphTopSpace = 10.0f;
constexpr int GraphAxisLabelInterval = 5;

constexpr float SeatsBoxLeft = RegionsBoxLeft + RegionsBoxWidth + ProbabilityBoxMargin;
constexpr float SeatsBoxTitleHeight = 30.0f;
constexpr float SeatsBoxTextHeight = 11.0f;

constexpr float CornerRounding = 30.0f;
constexpr float TextBoxCornerRounding = 20.0f;

wxFont font(int fontSize) {
	return wxFont(fontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
}

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

	drawBackground(dc);

	drawProbabilityBox(dc);

	drawExpectationsBox(dc);

	drawRegionsBox(dc);

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
	drawProbabilityBoxBackground(dc);
	drawProbabilityBoxLabels(dc);
	drawProbabilityBoxData(dc);
	drawSumOfLeads(dc);
}

wxColour DisplayFrame::lightenedPartyColour(Party::Id partyId) const
{
	auto col = project->parties().view(partyId).colour;
	col = { col.r / 2 + 128, col.g / 2 + 128, col.b / 2 + 128 };
	return wxColour(col.r, col.g, col.b);
}

void DisplayFrame::drawProbabilityBoxBackground(wxDC& dc) const
{
	// top half of probability box is lightened version of party 0's colour
	wxRect redRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop(), ProbabilityBoxWidth, ProbabilityBoxHeight);
	setBrushAndPen(lightenedPartyColour(0), dc);
	dc.DrawRoundedRectangle(redRect, CornerRounding);
	// bottom half of probability box is lightened version of party 1's colour
	// needs to be a rounded rectangle for the bottom corners
	wxRect blueRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, ProbabilityBoxHeight * 0.5f);
	setBrushAndPen(lightenedPartyColour(1), dc);
	dc.DrawRoundedRectangle(blueRect, CornerRounding);
	// this covers the upper rounded corners so the boundary between the two halves is a straight line
	wxRect blueCoverRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, CornerRounding);
	dc.DrawRectangle(blueCoverRect);
}

void DisplayFrame::drawProbabilityBoxLabels(wxDC& dc) const
{
	wxRect probTextRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding, probabilityBoxTop() + ProbabilityBoxPadding,
		ProbabilityBoxTextWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	drawProbabilityBoxText(dc, probTextRect, project->parties().view(0).abbreviation + " majority", offset);
	drawProbabilityBoxText(dc, probTextRect, project->parties().view(0).abbreviation + " minority", offset);
	drawProbabilityBoxText(dc, probTextRect, "Hung", offset);
	drawProbabilityBoxText(dc, probTextRect, project->parties().view(1).abbreviation + " minority", offset);
	drawProbabilityBoxText(dc, probTextRect, project->parties().view(1).abbreviation + " majority", offset);
}

void DisplayFrame::drawProbabilityBoxData(wxDC& dc) const
{
	wxRect probDataRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 2 + ProbabilityBoxTextWidth,
		probabilityBoxTop() + ProbabilityBoxPadding,
		ProbabilityBoxDataWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	drawProbabilityBoxText(dc, probDataRect, formatFloat(simulation().partyOneMajorityPercent, 2) + "%", offset);
	drawProbabilityBoxText(dc, probDataRect, formatFloat(simulation().partyOneMinorityPercent, 2) + "%", offset);
	drawProbabilityBoxText(dc, probDataRect, formatFloat(simulation().hungPercent, 2) + "%", offset);
	drawProbabilityBoxText(dc, probDataRect, formatFloat(simulation().partyTwoMinorityPercent, 2) + "%", offset);
	drawProbabilityBoxText(dc, probDataRect, formatFloat(simulation().partyTwoMajorityPercent, 2) + "%", offset);
}

void DisplayFrame::drawProbabilityBoxText(wxDC& dc, wxRect& rect, std::string const& text, wxPoint subsequentOffset) const
{
	dc.SetFont(font(13));
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(rect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.DrawLabel(text, rect, wxALIGN_CENTRE);
	rect.Offset(subsequentOffset);
}

void DisplayFrame::drawSumOfLeads(wxDC& dc) const
{
	wxRect probPartyRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		probabilityBoxTop() + ProbabilityBoxHeight * 0.5f - ProbabilityBoxSumHeight - ProbabilityBoxTextPadding,
		ProbabilityBoxSumWidth, ProbabilityBoxSumHeight);
	std::string partyOneAnnotation = project->parties().view(0).abbreviation + " wins:";
	std::string partyOneData = formatFloat(simulation().getPartyOneWinPercent(), 2) + "%";
	drawSumOfLeadsText(dc, probPartyRect, partyOneAnnotation, partyOneData);
	probPartyRect.Offset(0, ProbabilityBoxSumHeight + ProbabilityBoxTextPadding * 2);
	std::string partyTwoAnnotation = project->parties().view(1).abbreviation + " wins:";
	std::string partyTwoData = formatFloat(simulation().getPartyTwoWinPercent(), 2) + "%";
	drawSumOfLeadsText(dc, probPartyRect, partyTwoAnnotation, partyTwoData);
}

void DisplayFrame::drawSumOfLeadsText(wxDC& dc, wxRect& outerRect, std::string const& annotationText, std::string const& dataText) const
{
	wxRect annotationRect = outerRect;
	annotationRect.SetBottom(annotationRect.GetTop() + 20);
	wxRect dataRect = outerRect;
	dataRect.SetTop(dataRect.GetTop() + 20);
	dataRect.SetBottom(outerRect.GetBottom());
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(outerRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font(13));
	dc.DrawLabel(annotationText, annotationRect, wxALIGN_CENTRE);
	dc.SetFont(font(18));
	dc.DrawLabel(dataText, dataRect, wxALIGN_CENTRE);
}

void DisplayFrame::drawExpectationsBox(wxDC& dc) const
{
	drawExpectationsBoxBackground(dc);
	drawExpectationsBoxTitle(dc);
	drawExpectationsBoxRows(dc);
}

void DisplayFrame::drawExpectationsBoxBackground(wxDC& dc) const
{
	wxRect expBoxRect = wxRect(ExpectationBoxLeft, expectationBoxTop(), ExpectationBoxWidth, ExpectationBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(expBoxRect, TextBoxCornerRounding);
}

void DisplayFrame::drawExpectationsBoxTitle(wxDC& dc) const
{
	wxRect expBoxTitleRect = wxRect(ExpectationBoxLeft, expectationBoxTop(), ExpectationBoxWidth, ExpectationBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font(15));
	dc.DrawLabel("Seat expectations:", expBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrame::drawExpectationsBoxRows(wxDC& dc) const
{
	int rowSize = std::min(22, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation().partyWinExpectation.size()));
	wxRect expBoxNameRect = wxRect(ExpectationBoxLeft, expectationBoxTop() + ExpectationBoxTitleHeight,
		ExpectationBoxWidth * 0.7f, rowSize);
	dc.SetFont(font(rowSize - 8));
	for (int partyIndex = 0; partyIndex < project->parties().count(); ++partyIndex) {
		drawExpectationsBoxRow(dc, expBoxNameRect, partyIndex);
	}
}

void DisplayFrame::drawExpectationsBoxRow(wxDC& dc, wxRect& nameRect, PartyCollection::Index partyIndex) const
{
	int rowSize = std::min(21, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation().partyWinExpectation.size()));
	wxRect expBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(),
		ExpectationBoxWidth - nameRect.GetWidth(), rowSize);
	if (partyIndex >= int(simulation().partyWinExpectation.size())) return;
	dc.DrawLabel(project->parties().viewByIndex(partyIndex).name, nameRect, wxALIGN_CENTRE);
	dc.DrawLabel(formatFloat(simulation().partyWinExpectation[partyIndex], 2), expBoxDataRect, wxALIGN_CENTRE);
	nameRect.Offset(0, rowSize);
}

void DisplayFrame::drawRegionsBox(wxDC& dc) const
{
	drawRegionsBoxBackground(dc);
	drawRegionsBoxTitle(dc);
	drawRegionsBoxRows(dc);
}

void DisplayFrame::drawRegionsBoxBackground(wxDC& dc) const
{
	wxRect boxRect = wxRect(RegionsBoxLeft, dv.displayTop + ProbabilityBoxMargin, RegionsBoxWidth, RegionsBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(boxRect, CornerRounding);
}

void DisplayFrame::drawRegionsBoxTitle(wxDC& dc) const
{
	wxRect titleRect = wxRect(RegionsBoxLeft, dv.displayTop + ProbabilityBoxMargin, RegionsBoxWidth, RegionsBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font(15));
	dc.DrawLabel("Regional breakdown", titleRect, wxALIGN_CENTRE);
}

void DisplayFrame::drawRegionsBoxRows(wxDC& dc) const
{
	drawRegionsBoxRowTitles(dc);
	drawRegionsBoxRowList(dc);
}

void DisplayFrame::drawRegionsBoxRowTitles(wxDC& dc) const
{
	int regionsBoxRowTop = dv.displayTop + ProbabilityBoxMargin + RegionsBoxTitleHeight;
	int offset = int(RegionsBoxWidth * 0.25f);
	wxRect regionsBoxRect = wxRect(RegionsBoxLeft, regionsBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.DrawLabel("Region", regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(project->parties().view(0).abbreviation, regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(project->parties().view(1).abbreviation, regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel("Others", regionsBoxRect, wxALIGN_CENTRE);
}

void DisplayFrame::drawRegionsBoxRowList(wxDC& dc) const
{
	int statesBoxRowTop = dv.displayTop + ProbabilityBoxMargin + RegionsBoxTitleHeight;
	int horzOffset = int(RegionsBoxWidth * 0.25f);
	//int numRegions = simulation().regionPartyWinExpectation.size();
	int vertOffset = std::min(RegionsBoxTextHeight, 
		(RegionsBoxHeight - RegionsBoxTitleHeight) / (simulation().regionPartyWinExpectation.size() + 1));
	wxRect rowNameRect = wxRect(RegionsBoxLeft, statesBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.SetFont(font(std::min(13, vertOffset / 2 + 3)));
	for (auto const& regionPair : project->regions()) {
		rowNameRect.Offset(0, vertOffset); // if we don't do this first it'll overlap with the titles
		Region const& thisRegion = regionPair.second;
		int regionIndex = project->regions().idToIndex(regionPair.first);
		if (regionIndex >= int(simulation().regionPartyWinExpectation.size())) break;
		float seats[3] = { simulation().regionPartyWinExpectation[regionIndex][0] ,
			simulation().regionPartyWinExpectation[regionIndex][1] ,
			simulation().getOthersWinExpectation(regionIndex) };
		float change[3] = { seats[0] - thisRegion.partyLeading[0] ,
			seats[1] - thisRegion.partyLeading[1],
			seats[2] - thisRegion.getOthersLeading() };
		wxRect elementRect = rowNameRect;
		dc.DrawLabel(thisRegion.name, elementRect, wxALIGN_CENTRE);
		for (int group = 0; group < 3; ++group) {
			elementRect.Offset(horzOffset, 0);
			dc.DrawLabel(formatFloat(seats[group], 2) + " " + formatFloat(change[group], 2, true),
				elementRect, wxALIGN_CENTRE);
		}
	}
}

void DisplayFrame::drawBoundsBox(wxDC & dc) const
{
	using std::to_string;
	float StatesBoxTop = dv.displayTop + ProbabilityBoxMargin;
	float BoundsBoxTop = ProbabilityBoxMargin + StatesBoxTop + RegionsBoxHeight;
	wxRect boundsBoxRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxHeight);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRoundedRectangle(boundsBoxRect, CornerRounding);
	wxRect boundsBoxTitleRect = boundsBoxRect;
	boundsBoxTitleRect.SetBottom(BoundsBoxTop + BoundsBoxTitleHeight);
	setBrushAndPen(*wxBLACK, dc);
	dc.SetFont(font(15));
	dc.DrawLabel("Probability intervals", boundsBoxTitleRect, wxALIGN_CENTRE);

	wxRect boundsBoxNameRect = wxRect(BoundsBoxLeft, boundsBoxTitleRect.GetBottom(),
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox50Rect = wxRect(boundsBoxNameRect.GetRight(), boundsBoxTitleRect.GetBottom(),
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox80Rect = wxRect(boundsBox50Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox95Rect = wxRect(boundsBox80Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBox99Rect = wxRect(boundsBox95Rect.GetRight(), boundsBoxTitleRect.GetBottom(),
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	dc.DrawLabel("Party", boundsBoxNameRect, wxALIGN_CENTRE);
	dc.DrawLabel("50%", boundsBox50Rect, wxALIGN_CENTRE);
	dc.DrawLabel("80%", boundsBox80Rect, wxALIGN_CENTRE);
	dc.DrawLabel("95%", boundsBox95Rect, wxALIGN_CENTRE);
	dc.DrawLabel("99%", boundsBox99Rect, wxALIGN_CENTRE);
	dc.SetFont(font(13));
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
	dc.SetFont(font(15));
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
	dc.SetFont(font(8));
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
