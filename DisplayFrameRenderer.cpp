#include "DisplayFrameRenderer.h"

#include "General.h"
#include "Log.h"
#include "SpecialPartyCodes.h"

constexpr int GraphParty = 1;

// Note: The "Top" variables represent the height from the top of the drawing space, which is not
// the same as the number that is actually passed to the function due to the presence of the toolbar
// so all the compile-time stuff is done in draw space but needs to be converted to be inclusive of
// the toolbar before being passed to wx functions.

constexpr float BoxMargin = 10.0f;
constexpr float ProbabilityBoxLeft = BoxMargin;
constexpr float ProbabilityBoxTop = BoxMargin;
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

constexpr float ExpectationBoxLeft = BoxMargin;
constexpr float ExpectationBoxTop = ProbabilityBoxTop + ProbabilityBoxHeight + BoxMargin;
constexpr float ExpectationBoxWidth = ProbabilityBoxWidth;
constexpr float ExpectationBoxHeight = 200.0f;
constexpr float ExpectationBoxTitleHeight = 30.0f;
constexpr float ExpectationBoxTextHeight = 24.0f;

constexpr float RegionsBoxLeft = ProbabilityBoxLeft + ProbabilityBoxWidth + BoxMargin;
constexpr float RegionsBoxWidth = 700.0f;
constexpr float RegionsBoxHeight = 250.0f;
constexpr float RegionsBoxTitleHeight = 30.0f;
constexpr float RegionsBoxTextHeight = 24.0f;

constexpr float BoundsBoxLeft = RegionsBoxLeft;
constexpr float BoundsBoxWidth = RegionsBoxWidth;
constexpr float BoundsBoxHeight = 130.0f;
constexpr float BoundsBoxTitleHeight = 30.0f;
constexpr float BoundsBoxTextHeight = 24.0f;

constexpr float VoteShareBoxWidth = ProbabilityBoxWidth;
constexpr float VoteShareBoxTop = ExpectationBoxTop + ExpectationBoxHeight + BoxMargin;
constexpr float VoteShareBoxLeft = ProbabilityBoxLeft;
constexpr float VoteShareBoxTitleHeight = 30.0f;
constexpr float VoteShareBoxTextHeight = 24.0f;

constexpr float SeatsBoxLeft = RegionsBoxLeft + RegionsBoxWidth + BoxMargin;
constexpr float SeatsBoxTitleHeight = 30.0f;
constexpr float SeatsBoxTextHeight = 11.0f;

constexpr float GraphBoxLeft = VoteShareBoxLeft + VoteShareBoxWidth + BoxMargin;
constexpr float GraphBoxWidth = SeatsBoxLeft - GraphBoxLeft - BoxMargin;
constexpr float GraphBoxTop = VoteShareBoxTop;
constexpr float GraphAxisOffset = 20.0f;
constexpr float GraphAxisLabelWidth = 50.0f;
constexpr float GraphTopSpace = 10.0f;
constexpr int GraphAxisLabelInterval = 5;

constexpr float CornerRounding = 30.0f;
constexpr float TextBoxCornerRounding = 20.0f;

inline wxFont font(int fontSize) {
	return wxFont(fontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");
}

DisplayFrameRenderer::DisplayFrameRenderer(wxDC& dc, Simulation::Report const& simulation, wxSize dimensions)
	: dc(dc), simulation(simulation)
{
	dv.DCwidth = dimensions.GetWidth();
	dv.DCheight = dimensions.GetHeight();
}

void DisplayFrameRenderer::clearDC(wxDC & dc)
{
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
}

void DisplayFrameRenderer::setBrushAndPen(wxColour currentColour) const {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void DisplayFrameRenderer::render() {

	drawBackground();

	drawProbabilityBox();

	drawExpectationsBox();

	drawRegionsBox();

	drawBoundsBox();

	drawVoteShareBox();

	drawGraphBox();

	drawSeatsBox();
}

void DisplayFrameRenderer::drawBackground() const
{
	wxRect backgroundRect = wxRect(0, 0, dv.DCwidth, dv.DCheight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRectangle(backgroundRect);
	const wxColour backgroundGrey = wxColour(210, 210, 210); // light grey
	setBrushAndPen(backgroundGrey);
	dc.DrawRoundedRectangle(backgroundRect, CornerRounding);
}

void DisplayFrameRenderer::drawProbabilityBox() const
{
	drawProbabilityBoxBackground();
	drawProbabilityBoxLabels();
	drawProbabilityBoxData();
	drawSumOfLeads();
}

wxColour DisplayFrameRenderer::lightenedPartyColour(Party::Id partyId) const
{
	auto col = simulation.partyColour.at(partyId);
	col = { col.r / 2 + 128, col.g / 2 + 128, col.b / 2 + 128 };
	return wxColour(col.r, col.g, col.b);
}

void DisplayFrameRenderer::drawProbabilityBoxBackground() const
{
	// top half of probability box is lightened version of party 0's colour
	wxRect redRect = wxRect(ProbabilityBoxLeft, ProbabilityBoxTop, ProbabilityBoxWidth, ProbabilityBoxHeight);
	setBrushAndPen(lightenedPartyColour(0));
	dc.DrawRoundedRectangle(redRect, CornerRounding);
	// bottom half of probability box is lightened version of party 1's colour
	// needs to be a rounded rectangle for the bottom corners
	wxRect blueRect = wxRect(BoxMargin, BoxMargin + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, ProbabilityBoxHeight * 0.5f);
	setBrushAndPen(lightenedPartyColour(1));
	dc.DrawRoundedRectangle(blueRect, CornerRounding);
	// this covers the upper rounded corners so the boundary between the two halves is a straight line
	wxRect blueCoverRect = wxRect(BoxMargin, BoxMargin + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, CornerRounding);
	dc.DrawRectangle(blueCoverRect);
}

void DisplayFrameRenderer::drawProbabilityBoxLabels() const
{
	wxRect probTextRect = wxRect(BoxMargin + ProbabilityBoxPadding, BoxMargin + ProbabilityBoxPadding,
		ProbabilityBoxTextWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	drawProbabilityBoxText(probTextRect, simulation.partyAbbr.at(0) + " majority", offset);
	drawProbabilityBoxText(probTextRect, simulation.partyAbbr.at(0) + " minority", offset);
	drawProbabilityBoxText(probTextRect, "Hung", offset);
	drawProbabilityBoxText(probTextRect, simulation.partyAbbr.at(1) + " minority", offset);
	drawProbabilityBoxText(probTextRect, simulation.partyAbbr.at(1) + " majority", offset);
}

void DisplayFrameRenderer::drawProbabilityBoxData() const
{
	wxRect probDataRect = wxRect(BoxMargin + ProbabilityBoxPadding * 2 + ProbabilityBoxTextWidth,
		BoxMargin + ProbabilityBoxPadding,
		ProbabilityBoxDataWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	using Mp = Simulation::MajorParty;

	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMajorityPercent(Mp::One), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMinorityPercent(Mp::One), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getHungPercent(), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMinorityPercent(Mp::Two), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMajorityPercent(Mp::Two), 2) + "%", offset);
}

void DisplayFrameRenderer::drawProbabilityBoxText(wxRect& rect, std::string const& text, wxPoint subsequentOffset) const
{
	dc.SetFont(font(13));
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(rect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK);
	dc.DrawLabel(text, rect, wxALIGN_CENTRE);
	rect.Offset(subsequentOffset);
}

void DisplayFrameRenderer::drawSumOfLeads() const
{
	using Mp = Simulation::MajorParty;
	wxRect probPartyRect = wxRect(BoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		BoxMargin + ProbabilityBoxHeight * 0.5f - ProbabilityBoxSumHeight - ProbabilityBoxTextPadding,
		ProbabilityBoxSumWidth, ProbabilityBoxSumHeight);
	std::string partyOneAnnotation = simulation.partyAbbr.at(0) + " wins:";
	std::string partyOneData = formatFloat(simulation.getPartyOverallWinPercent(Mp::One), 2) + "%";
	drawSumOfLeadsText(probPartyRect, partyOneAnnotation, partyOneData);
	probPartyRect.Offset(0, ProbabilityBoxSumHeight + ProbabilityBoxTextPadding * 2);
	std::string partyTwoAnnotation = simulation.partyAbbr.at(1) + " wins:";
	std::string partyTwoData = formatFloat(simulation.getPartyOverallWinPercent(Mp::Two), 2) + "%";
	drawSumOfLeadsText(probPartyRect, partyTwoAnnotation, partyTwoData);
}

void DisplayFrameRenderer::drawSumOfLeadsText(wxRect& outerRect, std::string const& annotationText, std::string const& dataText) const
{
	wxRect annotationRect = outerRect;
	annotationRect.SetBottom(annotationRect.GetTop() + 20);
	wxRect dataRect = outerRect;
	dataRect.SetTop(dataRect.GetTop() + 20);
	dataRect.SetBottom(outerRect.GetBottom());
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(outerRect, TextBoxCornerRounding);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(13));
	dc.DrawLabel(annotationText, annotationRect, wxALIGN_CENTRE);
	dc.SetFont(font(18));
	dc.DrawLabel(dataText, dataRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawExpectationsBox() const
{
	drawExpectationsBoxBackground();
	drawExpectationsBoxTitle();
	drawExpectationsBoxRows();
}

void DisplayFrameRenderer::drawExpectationsBoxBackground() const
{
	wxRect expBoxRect = wxRect(ExpectationBoxLeft, ExpectationBoxTop, ExpectationBoxWidth, ExpectationBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(expBoxRect, TextBoxCornerRounding);
}

void DisplayFrameRenderer::drawExpectationsBoxTitle() const
{
	wxRect expBoxTitleRect = wxRect(ExpectationBoxLeft, ExpectationBoxTop, ExpectationBoxWidth, ExpectationBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Seat mean/median:", expBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawExpectationsBoxRows() const
{
	int rowSize = std::min(22, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation.partySeatWinFrequency.size()));
	wxRect expBoxNameRect = wxRect(ExpectationBoxLeft, ExpectationBoxTop + ExpectationBoxTitleHeight,
		ExpectationBoxWidth * 0.5f, rowSize);
	dc.SetFont(font(rowSize - 8));
	
	// Split into two so that we have the major parties first and emerging parties last
	for (auto [partyIndex, x] : simulation.partyWinExpectation) {
		if (partyIndex < 0) continue;
		drawExpectationsBoxRow(expBoxNameRect, partyIndex);
	}
	for (auto [partyIndex, x] : simulation.partyWinExpectation) {
		if (partyIndex >= 0) continue;
		drawExpectationsBoxRow(expBoxNameRect, partyIndex);
	}
}

void DisplayFrameRenderer::drawExpectationsBoxRow(wxRect& nameRect, PartyCollection::Index partyIndex) const
{
	int rowSize = std::min(21, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation.partySeatWinFrequency.size()));
	int width = (ExpectationBoxWidth - nameRect.GetWidth()) / 2;
	wxRect expBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(),
		(ExpectationBoxWidth - nameRect.GetWidth()) / 2, rowSize);
	std::string name = (simulation.partyName.at(partyIndex).size() < 18 ?
		simulation.partyName.at(partyIndex) : simulation.partyAbbr.at(partyIndex));
	dc.DrawLabel(name, nameRect, wxALIGN_CENTRE);
	dc.DrawLabel(formatFloat(simulation.getPartyWinExpectation(partyIndex), 2), expBoxDataRect, wxALIGN_CENTRE);
	expBoxDataRect.Offset(width, 0);
	dc.DrawLabel(formatFloat(simulation.getPartyWinMedian(partyIndex), 0), expBoxDataRect, wxALIGN_CENTRE);
	nameRect.Offset(0, rowSize);
}

void DisplayFrameRenderer::drawRegionsBox() const
{
	drawRegionsBoxBackground();
	drawRegionsBoxTitle();
	drawRegionsBoxRows();
}

void DisplayFrameRenderer::drawRegionsBoxBackground() const
{
	wxRect boxRect = wxRect(RegionsBoxLeft, BoxMargin, RegionsBoxWidth, RegionsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(boxRect, CornerRounding);
}

void DisplayFrameRenderer::drawRegionsBoxTitle() const
{
	wxRect titleRect = wxRect(RegionsBoxLeft, BoxMargin, RegionsBoxWidth, RegionsBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Regional breakdown", titleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawRegionsBoxRows() const
{
	drawRegionsBoxRowTitles();
	drawRegionsBoxRowList();
}

void DisplayFrameRenderer::drawRegionsBoxRowTitles() const
{
	int regionsBoxRowTop = BoxMargin + RegionsBoxTitleHeight;
	int offset = int(RegionsBoxWidth * 0.25f);
	wxRect regionsBoxRect = wxRect(RegionsBoxLeft, regionsBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.DrawLabel("Region", regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(simulation.partyAbbr.at(0), regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(simulation.partyAbbr.at(1), regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel("Others", regionsBoxRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawRegionsBoxRowList() const
{
	int regionsBoxRowTop = BoxMargin + RegionsBoxTitleHeight;
	//int numRegions = simulation.regionPartyWinExpectation.size();
	int vertOffset = std::min(RegionsBoxTextHeight,
		(RegionsBoxHeight - RegionsBoxTitleHeight) / (simulation.internalRegionCount() + 1));
	wxRect rowNameRect = wxRect(RegionsBoxLeft, regionsBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.SetFont(font(std::min(13, vertOffset / 2 + 3)));
	for (int regionIndex = 0; regionIndex < int(simulation.regionName.size()); ++regionIndex) {
		rowNameRect.Offset(0, vertOffset); // if we don't do this first it'll overlap with the titles
		if (regionIndex >= simulation.internalRegionCount()) break;
		drawRegionsBoxRowListItem(regionIndex, rowNameRect);
	}
}

void DisplayFrameRenderer::drawRegionsBoxRowListItem(RegionCollection::Index regionIndex, wxRect rowNameRect) const
{
	int horzOffset = int(RegionsBoxWidth * 0.25f);
	float seats[3] = { simulation.getRegionPartyWinExpectation(regionIndex, 0) ,
		simulation.getRegionPartyWinExpectation(regionIndex, 1) ,
		simulation.getRegionOthersWinExpectation(regionIndex) };
	float change[3] = { seats[0] - simulation.regionPartyIncuments[regionIndex][0] ,
		seats[1] - simulation.regionPartyIncuments[regionIndex][1],
		seats[2] - simulation.getOthersLeading(regionIndex) };
	wxRect elementRect = rowNameRect;
	dc.DrawLabel(simulation.regionName[regionIndex], elementRect, wxALIGN_CENTRE);
	for (int group = 0; group < 3; ++group) {
		elementRect.Offset(horzOffset, 0);
		dc.DrawLabel(formatFloat(seats[group], 2) + " " + formatFloat(change[group], 2, true),
			elementRect, wxALIGN_CENTRE);
	}
}

void DisplayFrameRenderer::drawBoundsBox() const
{
	drawBoundsBoxBackground();
	drawBoundsBoxTitle();
	drawBoundsBoxColumnHeadings();
	drawBoundsBoxItems();
}

void DisplayFrameRenderer::drawBoundsBoxBackground() const
{
	float BoundsBoxTop = BoxMargin + BoxMargin + RegionsBoxHeight;
	wxRect boundsBoxRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(boundsBoxRect, CornerRounding);
}

void DisplayFrameRenderer::drawBoundsBoxTitle() const
{
	float BoundsBoxTop = BoxMargin + BoxMargin + RegionsBoxHeight;
	wxRect boundsBoxTitleRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Probability intervals", boundsBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawBoundsBoxColumnHeadings() const
{
	float boundsHeadingTop = BoxMargin + BoxMargin + RegionsBoxHeight + BoundsBoxTitleHeight;
	float horzOffset = RegionsBoxWidth * 0.2f;
	dc.SetFont(font(15));
	std::array<std::string, 5> headingLabels = { "Party", "50%", "80%", "95%", "99%" };
	wxRect boundsBoxBaseRect = wxRect(BoundsBoxLeft, boundsHeadingTop,
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	wxRect boundsBoxHeadingRect = boundsBoxBaseRect;
	for (int headingIndex = 0; headingIndex < 5; ++headingIndex) {
		dc.DrawLabel(headingLabels[headingIndex], boundsBoxHeadingRect, wxALIGN_CENTRE);
		boundsBoxHeadingRect.Offset(horzOffset, 0);
	}
}

void DisplayFrameRenderer::drawBoundsBoxItems() const
{
	float boundsHeadingTop = BoxMargin + BoxMargin + RegionsBoxHeight + BoundsBoxTitleHeight;
	float horzOffset = RegionsBoxWidth * 0.2f;
	wxRect boundsBoxBaseRect = wxRect(BoundsBoxLeft, boundsHeadingTop,
	RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	dc.SetFont(font(13));
	std::array<std::string, 3> groupNames = { simulation.partyName.at(0), simulation.partyName.at(1), "Others" };
	for (int group = 0; group < 3; ++group) {
		boundsBoxBaseRect.Offset(0, BoundsBoxTextHeight);
		wxRect boundsBoxItemRect = boundsBoxBaseRect;
		dc.DrawLabel(groupNames[group], boundsBoxItemRect, wxALIGN_CENTRE);
		for (int boundsType = 0; boundsType < 4; ++boundsType) {
			boundsBoxItemRect.Offset(horzOffset, 0);
			using Mp = Simulation::MajorParty;
			std::string itemText = std::to_string(simulation.getProbabilityBound(3 - boundsType, Mp(group)))
				+ "-" + std::to_string(simulation.getProbabilityBound(4 + boundsType, Mp(group)));
			dc.DrawLabel(itemText, boundsBoxItemRect, wxALIGN_CENTRE);
		}
	}
}

void DisplayFrameRenderer::drawGraphBox() const
{
	dc.SetFont(font(11));
	GraphVariables gv = calculateGraphVariables();
	drawGraphBoxBackground(gv);
	int lowestSeatFrequency = simulation.getMinimumSeatFrequency(GraphParty);
	int highestSeatFrequency = simulation.getMaximumSeatFrequency(GraphParty);
	int seatRange = highestSeatFrequency - lowestSeatFrequency;
	if (seatRange > 0) {
		drawGraphAxisLabels(gv);
		drawGraphColumns(gv);
		drawGraphAxis(gv);
	}
}

DisplayFrameRenderer::GraphVariables DisplayFrameRenderer::calculateGraphVariables() const
{
	GraphVariables gv;
	gv.lowestSeatFrequency = simulation.getMinimumSeatFrequency(GraphParty);
	gv.highestSeatFrequency = simulation.getMaximumSeatFrequency(GraphParty);
	gv.seatRange = gv.highestSeatFrequency - gv.lowestSeatFrequency;
	float BackgroundHeight = dv.DCheight;
	gv.GraphBoxHeight = BackgroundHeight - GraphBoxTop - BoxMargin;
	gv.axisY = GraphBoxTop + gv.GraphBoxHeight - GraphAxisOffset;
	gv.axisLeft = GraphBoxLeft + GraphAxisOffset;
	gv.axisRight = GraphBoxLeft + GraphBoxWidth - GraphAxisOffset;
	gv.axisMidpoint = (gv.axisLeft + gv.axisRight) * 0.5f;
	gv.axisLength = gv.axisRight - gv.axisLeft;
	gv.seatColumnWidth = int(floor(gv.axisLength)) / gv.seatRange;
	return gv;
}

void DisplayFrameRenderer::drawGraphBoxBackground(GraphVariables const& gv) const
{
	wxRect graphBoxRect = wxRect(GraphBoxLeft, GraphBoxTop, GraphBoxWidth, gv.GraphBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(graphBoxRect, CornerRounding);
}

void DisplayFrameRenderer::drawGraphAxisLabels(GraphVariables const& gv) const
{
	float columnRange = float(gv.seatColumnWidth * gv.seatRange); // if all columns have the same width then the total width of all columns
															// will usually be somewhat smaller than the actual width of the graph
	float columnStart = gv.axisMidpoint - columnRange * 0.5f;
	int lowestAxisLabel = ((gv.lowestSeatFrequency + GraphAxisLabelInterval - 1) / GraphAxisLabelInterval) * GraphAxisLabelInterval;
	float axisLabelStart = columnStart + float(gv.seatColumnWidth * (lowestAxisLabel - gv.lowestSeatFrequency));
	wxRect axisLabelRect = wxRect(axisLabelStart - GraphAxisLabelWidth * 0.5f, gv.axisY,
		GraphAxisLabelWidth, GraphAxisOffset);
	setBrushAndPen(*wxBLACK);
	for (int axisLabelNumber = lowestAxisLabel; axisLabelNumber <= gv.highestSeatFrequency; axisLabelNumber += GraphAxisLabelInterval) {
		dc.DrawLabel(std::to_string(axisLabelNumber), axisLabelRect, wxALIGN_CENTRE);
		axisLabelRect.Offset(float(gv.seatColumnWidth * GraphAxisLabelInterval), 0);
	}
}

void DisplayFrameRenderer::drawGraphColumns(GraphVariables const& gv) const
{
	setBrushAndPen(*wxBLACK);
	int modalSeatFrequency = simulation.getModalSeatFrequencyCount(GraphParty);
	float columnRange = float(gv.seatColumnWidth * gv.seatRange); // if all columns have the same width then the total width of all columns
															// will usually be somewhat smaller than the actual width of the graph
	float columnStart = gv.axisMidpoint - columnRange * 0.5f;
	const float GraphMaxColumnHeight = gv.GraphBoxHeight - GraphAxisOffset - GraphTopSpace;
	setBrushAndPen(*wxBLUE);
	for (int seatNum = gv.lowestSeatFrequency; seatNum <= gv.highestSeatFrequency; ++seatNum) {
		float proportionOfMax = float(simulation.getPartySeatWinFrequency(1, seatNum)) / float(modalSeatFrequency) * 0.9999f;
		float columnHeight = std::ceil(proportionOfMax * GraphMaxColumnHeight);
		float columnTop = gv.axisY - columnHeight;
		float columnWidth = float(gv.seatColumnWidth);
		float columnLeft = columnStart - columnWidth * 0.5f + float(gv.seatColumnWidth) * float(seatNum - gv.lowestSeatFrequency);
		wxRect graphColumnRect = wxRect(columnLeft, columnTop, columnWidth, columnHeight);
		dc.DrawRectangle(graphColumnRect);
	}
}

void DisplayFrameRenderer::drawGraphAxis(GraphVariables const& gv) const
{
	wxPoint axisLeftPoint = wxPoint(gv.axisLeft, gv.axisY);
	wxPoint axisRightPoint = wxPoint(gv.axisRight, gv.axisY);
	setBrushAndPen(*wxBLACK);
	dc.DrawLine(axisLeftPoint, axisRightPoint);
}

void DisplayFrameRenderer::drawVoteShareBox() const
{
	drawVoteShareBoxBackground();
	drawVoteShareBoxTitle();
	drawVoteShareBoxRows();
}

void DisplayFrameRenderer::drawVoteShareBoxBackground() const
{
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	wxRect voteBoxRect = wxRect(VoteShareBoxLeft, VoteShareBoxTop, VoteShareBoxWidth, voteBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(voteBoxRect, TextBoxCornerRounding);
}

void DisplayFrameRenderer::drawVoteShareBoxTitle() const
{
	wxRect voteBoxTitleRect = wxRect(VoteShareBoxLeft, VoteShareBoxTop, VoteShareBoxWidth, VoteShareBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Party vote share mean/median:", voteBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawVoteShareBoxRows() const
{
	if (!(simulation.partyPrimaryFrequency.size())) return;
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	int rowSize = std::min(22, int(voteBoxHeight - VoteShareBoxTitleHeight) / int(simulation.partyPrimaryFrequency.size()));
	wxRect voteBoxNameRect = wxRect(VoteShareBoxLeft, VoteShareBoxTop + VoteShareBoxTitleHeight,
		VoteShareBoxWidth * 0.5f, rowSize);
	dc.SetFont(font(rowSize - 8));


	for (auto [partyIndex, _] : simulation.partyPrimaryFrequency) {
		if (partyIndex == OthersIndex) continue;
		drawVoteShareBoxRow(voteBoxNameRect, partyIndex);
	}
	drawVoteShareBoxOthersRow(voteBoxNameRect);
	drawVoteShareBoxPartyOneTppRow(voteBoxNameRect);
	drawVoteShareBoxPartyTwoTppRow(voteBoxNameRect);
}

void DisplayFrameRenderer::drawVoteShareBoxRow(wxRect& nameRect, PartyCollection::Index partyIndex) const
{
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	int rowSize = std::min(21, int(voteBoxHeight - VoteShareBoxTitleHeight) / int(simulation.partyPrimaryFrequency.size() + 3));
	int width = (VoteShareBoxWidth - nameRect.GetWidth()) / 2;
	wxRect voteBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(), width, rowSize);
	if (simulation.partyPrimaryFrequency.contains(partyIndex)) {
		std::string name = (simulation.partyName.at(partyIndex).size() < 18 ?
			simulation.partyName.at(partyIndex) : simulation.partyAbbr.at(partyIndex));
		dc.DrawLabel(name, nameRect, wxALIGN_CENTRE);
		// Parties with a primary vote of exactly 0 are generally to be considered "not analysed"
		// than actually zero, so we don't print them with an actual value
		if (simulation.partyPrimaryFrequency.contains(partyIndex) && simulation.getFpSampleCount(partyIndex) &&
			simulation.getFpSampleExpectation(partyIndex) > 0.0f)
		{
			float expectation = simulation.getFpSampleExpectation(partyIndex);
			dc.DrawLabel(formatFloat(expectation, 1), voteBoxDataRect, wxALIGN_CENTRE);
			voteBoxDataRect.Offset(width, 0);
			float median = simulation.getFpSampleMedian(partyIndex);
			dc.DrawLabel(formatFloat(median, 1), voteBoxDataRect, wxALIGN_CENTRE);
		}
		else {
			dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
			voteBoxDataRect.Offset(width, 0);
			dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
		}
	}
	nameRect.Offset(0, rowSize);
}

void DisplayFrameRenderer::drawVoteShareBoxOthersRow(wxRect& nameRect) const
{
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	int rowSize = std::min(21, int(voteBoxHeight - VoteShareBoxTitleHeight) / int(simulation.partyPrimaryFrequency.size() + 3));
	int width = (VoteShareBoxWidth - nameRect.GetWidth()) / 2;
	wxRect voteBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(), width, rowSize);
	std::string name = "Others";
	dc.DrawLabel(name, nameRect, wxALIGN_CENTRE);
	if (simulation.partyPrimaryFrequency.contains(OthersIndex)) {
		float meanVote = simulation.getFpSampleExpectation(OthersIndex);
		dc.DrawLabel(formatFloat(meanVote, 1), voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		float medianVote = simulation.getFpSampleMedian(OthersIndex);
		dc.DrawLabel(formatFloat(medianVote, 1), voteBoxDataRect, wxALIGN_CENTRE);
	}
	else {
		float totalVote = 0.0f;
		for (auto const& [partyIndex, primaryFrequencies] : simulation.partyPrimaryFrequency) {
			totalVote += simulation.getFpSampleExpectation(partyIndex);
		}
		float othersVote = 100.0f - totalVote;
		dc.DrawLabel(formatFloat(othersVote, 1), voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
	}
	nameRect.Offset(0, rowSize);
}

void DisplayFrameRenderer::drawVoteShareBoxPartyOneTppRow(wxRect& nameRect) const
{
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	int rowSize = std::min(21, int(voteBoxHeight - VoteShareBoxTitleHeight) / int(simulation.partyPrimaryFrequency.size() + 3));
	int width = (VoteShareBoxWidth - nameRect.GetWidth()) / 2;
	wxRect voteBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(), width, rowSize);
	std::string name = simulation.partyAbbr.at(0) + " TPP";
	dc.DrawLabel(name, nameRect, wxALIGN_CENTRE);
	if (simulation.getTppSampleCount()) {
		float expectation = simulation.getTppSampleExpectation();
		dc.DrawLabel(formatFloat(expectation, 1), voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		float median = simulation.getTppSampleMedian();
		dc.DrawLabel(formatFloat(median, 1), voteBoxDataRect, wxALIGN_CENTRE);
	}
	else {
		dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
	}
	nameRect.Offset(0, rowSize);
}

void DisplayFrameRenderer::drawVoteShareBoxPartyTwoTppRow(wxRect& nameRect) const
{
	float voteBoxHeight = dv.DCheight - VoteShareBoxTop - BoxMargin;
	int rowSize = std::min(21, int(voteBoxHeight - VoteShareBoxTitleHeight) / int(simulation.partyPrimaryFrequency.size() + 3));
	int width = (VoteShareBoxWidth - nameRect.GetWidth()) / 2;
	wxRect voteBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(), width, rowSize);
	std::string name = simulation.partyAbbr.at(1) + " TPP";
	dc.DrawLabel(name, nameRect, wxALIGN_CENTRE);
	if (simulation.getTppSampleCount()) {
		float expectation = 100.0f - simulation.getTppSampleExpectation();
		dc.DrawLabel(formatFloat(expectation, 1), voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		float median = 100.0f - simulation.getTppSampleMedian();
		dc.DrawLabel(formatFloat(median, 1), voteBoxDataRect, wxALIGN_CENTRE);
	}
	else {
		dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
		voteBoxDataRect.Offset(width, 0);
		dc.DrawLabel("na", voteBoxDataRect, wxALIGN_CENTRE);
	}
	nameRect.Offset(0, rowSize);
}

void DisplayFrameRenderer::drawSeatsBox() const
{
	drawSeatsBoxBackground();
	drawSeatsBoxTitle();
	drawSeatsList();
}

void DisplayFrameRenderer::drawSeatsBoxBackground() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - BoxMargin;
	float SeatsBoxHeight = dv.DCheight - BoxMargin * 2.0f;
	wxRect seatsBoxRect = wxRect(SeatsBoxLeft, BoxMargin, SeatsBoxWidth, SeatsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(seatsBoxRect, CornerRounding);
}

void DisplayFrameRenderer::drawSeatsBoxTitle() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - BoxMargin;
	wxRect titleRect = wxRect(SeatsBoxLeft, BoxMargin, SeatsBoxWidth, SeatsBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Close Seats", titleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawSeatsList() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - BoxMargin;
	float SeatsBoxHeight = dv.DCheight - BoxMargin * 2.0f;
	float seatsListTop = BoxMargin + SeatsBoxTitleHeight;
	wxRect seatsBoxNameRect = wxRect(SeatsBoxLeft, seatsListTop,
		SeatsBoxWidth * 0.3f, SeatsBoxTextHeight);
	wxRect seatsBoxMarginRect = wxRect(seatsBoxNameRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.25f, SeatsBoxTextHeight);
	wxRect seatsBoxPartyOneWinRect = wxRect(seatsBoxMarginRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.12f, SeatsBoxTextHeight);
	wxRect seatsBoxPartyTwoWinRect = wxRect(seatsBoxPartyOneWinRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.12f, SeatsBoxTextHeight);
	wxRect seatsBoxOthersWinRect = wxRect(seatsBoxPartyTwoWinRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.21f, SeatsBoxTextHeight);
	int seatsFittingInBox = int((SeatsBoxHeight - SeatsBoxTitleHeight) / SeatsBoxTextHeight);

	struct SeatInfo {
		std::string name;
		std::string incumbentText;
		float partyOneWinRate = 0.0f;
		float partyTwoWinRate = 0.0f;
		float bestOthersWinRate = 0.0f;
		float sortingVal; // highest individual category win rate
		std::string bestOthersString;
	};

	std::vector<SeatInfo> seatInfos;
	for (int seatIndex = 0; seatIndex < int(simulation.seatName.size()); ++seatIndex) {
		SeatInfo seatInfo;
		seatInfo.name = simulation.seatName[seatIndex];
		int incumbent = simulation.seatIncumbents[seatIndex];
		seatInfo.incumbentText = simulation.partyAbbr.at(simulation.seatIncumbents[seatIndex]);
		if (simulation.seatIncumbentMargins.size()) {
			seatInfo.incumbentText += " (" + formatFloat(simulation.seatIncumbentMargins[seatIndex], 1) + "%)";
		}
		else {
			if (!incumbent) seatInfo.incumbentText += " (" + formatFloat(simulation.seatMargins[seatIndex], 1) + "%)";
			else if (incumbent == 1) seatInfo.incumbentText += " (" + formatFloat(-simulation.seatMargins[seatIndex], 1) + "%)";
		}
		if (simulation.seatPartyWinPercent[seatIndex].contains(0)) seatInfo.partyOneWinRate = simulation.seatPartyWinPercent[seatIndex].at(0);
		if (simulation.seatPartyWinPercent[seatIndex].contains(1)) seatInfo.partyTwoWinRate = simulation.seatPartyWinPercent[seatIndex].at(1);
		int bestOthersIndex = -1;
		float bestOthersWinRate = 0.0f;
		for (auto const& [partyIndex, vote] : simulation.seatPartyWinPercent[seatIndex]) {
			if (partyIndex == 0 || partyIndex == 1) continue;
			if (vote > bestOthersWinRate) {
				bestOthersIndex = partyIndex;
				bestOthersWinRate = vote;
			}
		}
		seatInfo.bestOthersWinRate = bestOthersWinRate;
		if (bestOthersIndex != -1) {
			seatInfo.bestOthersString = " (" + simulation.partyAbbr.at(bestOthersIndex) + ")";
		}
		seatInfo.sortingVal = std::max(std::max(seatInfo.partyOneWinRate, seatInfo.partyTwoWinRate), seatInfo.bestOthersWinRate);
		seatInfos.push_back(seatInfo);
	}

	std::sort(seatInfos.begin(), seatInfos.end(), [](SeatInfo const& lhs, SeatInfo const& rhs) {
		return lhs.sortingVal < rhs.sortingVal;
	});

	dc.SetFont(font(8));
	seatsFittingInBox = std::min(int(seatInfos.size()), seatsFittingInBox);
	for (int listPos = 0; listPos < seatsFittingInBox; ++listPos)
	{
		dc.DrawLabel(seatInfos[listPos].name, seatsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(seatInfos[listPos].incumbentText, seatsBoxMarginRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(seatInfos[listPos].partyOneWinRate, 2), seatsBoxPartyOneWinRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(seatInfos[listPos].partyTwoWinRate, 2), seatsBoxPartyTwoWinRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(seatInfos[listPos].bestOthersWinRate, 2) + seatInfos[listPos].bestOthersString, seatsBoxOthersWinRect, wxALIGN_CENTRE);
		seatsBoxNameRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxMarginRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxPartyOneWinRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxPartyTwoWinRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxOthersWinRect.Offset(0, SeatsBoxTextHeight);
	}
}