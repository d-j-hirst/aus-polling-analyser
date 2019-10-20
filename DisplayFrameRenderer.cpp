#include "DisplayFrameRenderer.h"

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

DisplayFrameRenderer::DisplayFrameRenderer(PollingProject const& project, wxDC& dc, Simulation const& simulation, wxSize dimensions)
	: project(project), dc(dc), simulation(simulation)
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

	defineGraphLimits();

	drawBackground();

	drawProbabilityBox();

	drawExpectationsBox();

	drawRegionsBox();

	drawBoundsBox();

	drawGraphBox();

	drawSeatsBox();
}

void DisplayFrameRenderer::drawBackground() const
{
	wxRect backgroundRect = wxRect(0, dv.displayTop, dv.DCwidth, backgroundHeight());
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
	auto col = project.parties().view(partyId).colour;
	col = { col.r / 2 + 128, col.g / 2 + 128, col.b / 2 + 128 };
	return wxColour(col.r, col.g, col.b);
}

void DisplayFrameRenderer::drawProbabilityBoxBackground() const
{
	// top half of probability box is lightened version of party 0's colour
	wxRect redRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop(), ProbabilityBoxWidth, ProbabilityBoxHeight);
	setBrushAndPen(lightenedPartyColour(0));
	dc.DrawRoundedRectangle(redRect, CornerRounding);
	// bottom half of probability box is lightened version of party 1's colour
	// needs to be a rounded rectangle for the bottom corners
	wxRect blueRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, ProbabilityBoxHeight * 0.5f);
	setBrushAndPen(lightenedPartyColour(1));
	dc.DrawRoundedRectangle(blueRect, CornerRounding);
	// this covers the upper rounded corners so the boundary between the two halves is a straight line
	wxRect blueCoverRect = wxRect(ProbabilityBoxMargin, probabilityBoxTop() + ProbabilityBoxHeight * 0.5f,
		ProbabilityBoxWidth, CornerRounding);
	dc.DrawRectangle(blueCoverRect);
}

void DisplayFrameRenderer::drawProbabilityBoxLabels() const
{
	wxRect probTextRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding, probabilityBoxTop() + ProbabilityBoxPadding,
		ProbabilityBoxTextWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	drawProbabilityBoxText(probTextRect, project.parties().view(0).abbreviation + " majority", offset);
	drawProbabilityBoxText(probTextRect, project.parties().view(0).abbreviation + " minority", offset);
	drawProbabilityBoxText(probTextRect, "Hung", offset);
	drawProbabilityBoxText(probTextRect, project.parties().view(1).abbreviation + " minority", offset);
	drawProbabilityBoxText(probTextRect, project.parties().view(1).abbreviation + " majority", offset);
}

void DisplayFrameRenderer::drawProbabilityBoxData() const
{
	wxRect probDataRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 2 + ProbabilityBoxTextWidth,
		probabilityBoxTop() + ProbabilityBoxPadding,
		ProbabilityBoxDataWidth, ProbabilityBoxTextHeight);
	wxPoint offset(0, ProbabilityBoxTextHeight + ProbabilityBoxTextPadding);

	using Mp = Simulation::MajorParty;

	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMajorityPercent(Mp::One), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMinorityPercent(Mp::One), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getHungPercent(), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMajorityPercent(Mp::Two), 2) + "%", offset);
	drawProbabilityBoxText(probDataRect, formatFloat(simulation.getPartyMinorityPercent(Mp::Two), 2) + "%", offset);
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
	wxRect probPartyRect = wxRect(ProbabilityBoxMargin + ProbabilityBoxPadding * 3 + ProbabilityBoxTextWidth + ProbabilityBoxDataWidth,
		probabilityBoxTop() + ProbabilityBoxHeight * 0.5f - ProbabilityBoxSumHeight - ProbabilityBoxTextPadding,
		ProbabilityBoxSumWidth, ProbabilityBoxSumHeight);
	std::string partyOneAnnotation = project.parties().view(0).abbreviation + " wins:";
	std::string partyOneData = formatFloat(simulation.getPartyWinPercent(Mp::One), 2) + "%";
	drawSumOfLeadsText(probPartyRect, partyOneAnnotation, partyOneData);
	probPartyRect.Offset(0, ProbabilityBoxSumHeight + ProbabilityBoxTextPadding * 2);
	std::string partyTwoAnnotation = project.parties().view(1).abbreviation + " wins:";
	std::string partyTwoData = formatFloat(simulation.getPartyWinPercent(Mp::Two), 2) + "%";
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
	wxRect expBoxRect = wxRect(ExpectationBoxLeft, expectationBoxTop(), ExpectationBoxWidth, ExpectationBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(expBoxRect, TextBoxCornerRounding);
}

void DisplayFrameRenderer::drawExpectationsBoxTitle() const
{
	wxRect expBoxTitleRect = wxRect(ExpectationBoxLeft, expectationBoxTop(), ExpectationBoxWidth, ExpectationBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Seat expectations:", expBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawExpectationsBoxRows() const
{
	int rowSize = std::min(22, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation.internalPartyCount()));
	wxRect expBoxNameRect = wxRect(ExpectationBoxLeft, expectationBoxTop() + ExpectationBoxTitleHeight,
		ExpectationBoxWidth * 0.7f, rowSize);
	dc.SetFont(font(rowSize - 8));

	// don't want to try drawing individual parties if the project's parties and the simulated parties don't match
	if (simulation.internalPartyCount() != project.parties().count()) return;

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		drawExpectationsBoxRow(expBoxNameRect, partyIndex);
	}
}

void DisplayFrameRenderer::drawExpectationsBoxRow(wxRect& nameRect, PartyCollection::Index partyIndex) const
{
	int rowSize = std::min(21, int(ExpectationBoxHeight - ExpectationBoxTitleHeight) / int(simulation.internalPartyCount()));
	wxRect expBoxDataRect = wxRect(nameRect.GetRight(), nameRect.GetTop(),
		ExpectationBoxWidth - nameRect.GetWidth(), rowSize);
	if (partyIndex >= int(simulation.internalPartyCount())) return;
	dc.DrawLabel(project.parties().viewByIndex(partyIndex).name, nameRect, wxALIGN_CENTRE);
	dc.DrawLabel(formatFloat(simulation.getPartyWinExpectation(partyIndex), 2), expBoxDataRect, wxALIGN_CENTRE);
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
	wxRect boxRect = wxRect(RegionsBoxLeft, dv.displayTop + ProbabilityBoxMargin, RegionsBoxWidth, RegionsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(boxRect, CornerRounding);
}

void DisplayFrameRenderer::drawRegionsBoxTitle() const
{
	wxRect titleRect = wxRect(RegionsBoxLeft, dv.displayTop + ProbabilityBoxMargin, RegionsBoxWidth, RegionsBoxTitleHeight);
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
	int regionsBoxRowTop = dv.displayTop + ProbabilityBoxMargin + RegionsBoxTitleHeight;
	int offset = int(RegionsBoxWidth * 0.25f);
	wxRect regionsBoxRect = wxRect(RegionsBoxLeft, regionsBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.DrawLabel("Region", regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(project.parties().view(0).abbreviation, regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel(project.parties().view(1).abbreviation, regionsBoxRect, wxALIGN_CENTRE);
	regionsBoxRect.Offset(offset, 0);
	dc.DrawLabel("Others", regionsBoxRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawRegionsBoxRowList() const
{
	int regionsBoxRowTop = dv.displayTop + ProbabilityBoxMargin + RegionsBoxTitleHeight;
	//int numRegions = simulation.regionPartyWinExpectation.size();
	int vertOffset = std::min(RegionsBoxTextHeight,
		(RegionsBoxHeight - RegionsBoxTitleHeight) / (simulation.internalRegionCount() + 1));
	wxRect rowNameRect = wxRect(RegionsBoxLeft, regionsBoxRowTop,
		RegionsBoxWidth * 0.25f, RegionsBoxTextHeight);
	dc.SetFont(font(std::min(13, vertOffset / 2 + 3)));
	if (simulation.internalRegionCount() != project.regions().count()) return;
	for (auto const&[key, thisRegion] : project.regions()) {
		rowNameRect.Offset(0, vertOffset); // if we don't do this first it'll overlap with the titles
		int regionIndex = project.regions().idToIndex(key);
		if (regionIndex >= simulation.internalRegionCount()) break;
		drawRegionsBoxRowListItem(thisRegion, regionIndex, rowNameRect);
	}
}

void DisplayFrameRenderer::drawRegionsBoxRowListItem(Region const & thisRegion, RegionCollection::Index regionIndex, wxRect rowNameRect) const
{
	int horzOffset = int(RegionsBoxWidth * 0.25f);
	float seats[3] = { simulation.getRegionPartyWinExpectation(regionIndex, 0) ,
		simulation.getRegionPartyWinExpectation(regionIndex, 1) ,
		simulation.getRegionOthersWinExpectation(regionIndex) };
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

void DisplayFrameRenderer::drawBoundsBox() const
{
	drawBoundsBoxBackground();
	drawBoundsBoxTitle();
	drawBoundsBoxColumnHeadings();
	drawBoundsBoxItems();
}

void DisplayFrameRenderer::drawBoundsBoxBackground() const
{
	float BoundsBoxTop = ProbabilityBoxMargin + dv.displayTop + ProbabilityBoxMargin + RegionsBoxHeight;
	wxRect boundsBoxRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(boundsBoxRect, CornerRounding);
}

void DisplayFrameRenderer::drawBoundsBoxTitle() const
{
	float BoundsBoxTop = ProbabilityBoxMargin + dv.displayTop + ProbabilityBoxMargin + RegionsBoxHeight;
	wxRect boundsBoxTitleRect = wxRect(BoundsBoxLeft, BoundsBoxTop, BoundsBoxWidth, BoundsBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Probability intervals", boundsBoxTitleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawBoundsBoxColumnHeadings() const
{
	float boundsHeadingTop = ProbabilityBoxMargin + dv.displayTop + ProbabilityBoxMargin + RegionsBoxHeight + BoundsBoxTitleHeight;
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
	float boundsHeadingTop = ProbabilityBoxMargin + dv.displayTop + ProbabilityBoxMargin + RegionsBoxHeight + BoundsBoxTitleHeight;
	float horzOffset = RegionsBoxWidth * 0.2f;
	wxRect boundsBoxBaseRect = wxRect(BoundsBoxLeft, boundsHeadingTop,
		RegionsBoxWidth * 0.2f, BoundsBoxTextHeight);
	if (simulation.isValid()) {
		dc.SetFont(font(13));
		std::array<std::string, 3> groupNames = { project.parties().view(0).abbreviation, project.parties().view(1).abbreviation, "Others" };
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
}

void DisplayFrameRenderer::drawGraphBox() const
{
	dc.SetFont(font(13));
	GraphVariables gv = calculateGraphVariables();
	drawGraphBoxBackground(gv);
	if (simulation.isValid()) {
		int lowestSeatFrequency = simulation.getMinimumSeatFrequency(GraphParty);
		int highestSeatFrequency = simulation.getMaximumSeatFrequency(GraphParty);
		int seatRange = highestSeatFrequency - lowestSeatFrequency;
		if (seatRange > 0) {
			drawGraphAxisLabels(gv);
			drawGraphColumns(gv);
			drawGraphAxis(gv);
		}
	}
}

DisplayFrameRenderer::GraphVariables DisplayFrameRenderer::calculateGraphVariables() const
{
	GraphVariables gv;
	gv.lowestSeatFrequency = simulation.getMinimumSeatFrequency(GraphParty);
	gv.highestSeatFrequency = simulation.getMaximumSeatFrequency(GraphParty);
	gv.seatRange = gv.highestSeatFrequency - gv.lowestSeatFrequency;
	float BackgroundHeight = dv.DCheight - dv.displayTop;
	gv.GraphBoxTop = ProbabilityBoxMargin + expectationBoxTop() + ExpectationBoxHeight;
	gv.GraphBoxHeight = BackgroundHeight - gv.GraphBoxTop - ProbabilityBoxMargin + dv.displayTop;
	gv.axisY = gv.GraphBoxTop + gv.GraphBoxHeight - GraphAxisOffset;
	gv.axisLeft = GraphBoxLeft + GraphAxisOffset;
	gv.axisRight = GraphBoxLeft + GraphBoxWidth - GraphAxisOffset;
	gv.axisMidpoint = (gv.axisLeft + gv.axisRight) * 0.5f;
	gv.axisLength = gv.axisRight - gv.axisLeft;
	gv.seatColumnWidth = int(floor(gv.axisLength)) / gv.seatRange;
	return gv;
}

void DisplayFrameRenderer::drawGraphBoxBackground(GraphVariables const& gv) const
{
	wxRect graphBoxRect = wxRect(GraphBoxLeft, gv.GraphBoxTop, GraphBoxWidth, gv.GraphBoxHeight);
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

void DisplayFrameRenderer::drawSeatsBox() const
{
	drawSeatsBoxBackground();
	drawSeatsBoxTitle();
	drawSeatsList();
}

void DisplayFrameRenderer::drawSeatsBoxBackground() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - ProbabilityBoxMargin;
	float SeatsBoxHeight = backgroundHeight() - ProbabilityBoxMargin * 2.0f;
	wxRect seatsBoxRect = wxRect(SeatsBoxLeft, probabilityBoxTop(), SeatsBoxWidth, SeatsBoxHeight);
	setBrushAndPen(*wxWHITE);
	dc.DrawRoundedRectangle(seatsBoxRect, CornerRounding);
}

void DisplayFrameRenderer::drawSeatsBoxTitle() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - ProbabilityBoxMargin;
	wxRect titleRect = wxRect(SeatsBoxLeft, probabilityBoxTop(), SeatsBoxWidth, SeatsBoxTitleHeight);
	setBrushAndPen(*wxBLACK);
	dc.SetFont(font(15));
	dc.DrawLabel("Close Seats", titleRect, wxALIGN_CENTRE);
}

void DisplayFrameRenderer::drawSeatsList() const
{
	float SeatsBoxWidth = dv.DCwidth - SeatsBoxLeft - ProbabilityBoxMargin;
	float SeatsBoxHeight = backgroundHeight() - ProbabilityBoxMargin * 2.0f;
	float seatsListTop = probabilityBoxTop() + SeatsBoxTitleHeight;
	wxRect seatsBoxNameRect = wxRect(SeatsBoxLeft, seatsListTop,
		SeatsBoxWidth * 0.4f, SeatsBoxTextHeight);
	wxRect seatsBoxMarginRect = wxRect(seatsBoxNameRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.3f, SeatsBoxTextHeight);
	wxRect seatsBoxLnpWinRect = wxRect(seatsBoxMarginRect.GetRight(), seatsListTop,
		SeatsBoxWidth * 0.3f, SeatsBoxTextHeight);
	int seatsFittingInBox = int((SeatsBoxHeight - SeatsBoxTitleHeight) / SeatsBoxTextHeight);
	int closeSeat = simulation.findBestSeatDisplayCenter(GraphParty, seatsFittingInBox, project);
	int firstSeat = std::max(std::min(closeSeat - seatsFittingInBox / 2, int(simulation.classicSeatCount()) - seatsFittingInBox), 0);
	dc.SetFont(font(8));
	for (int seatIndex = firstSeat; seatIndex < firstSeat + seatsFittingInBox && seatIndex < int(simulation.classicSeatCount()); ++seatIndex) {
		Seat::Id seatId = simulation.classicSeatId(seatIndex);
		if (!project.seats().exists(seatId)) continue;
		Seat const& seat = project.seats().view(seatId);
		float lnpWinPercent = simulation.getClassicSeatMajorPartyWinRate(seatIndex, GraphParty, project); // NEED TO CHANGE THIS
		dc.DrawLabel(seat.name, seatsBoxNameRect, wxALIGN_CENTRE);
		dc.DrawLabel(project.parties().view(seat.incumbent).abbreviation + " (" + formatFloat(seat.margin, 1) + ")", seatsBoxMarginRect, wxALIGN_CENTRE);
		dc.DrawLabel(formatFloat(lnpWinPercent, 2), seatsBoxLnpWinRect, wxALIGN_CENTRE);
		seatsBoxNameRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxMarginRect.Offset(0, SeatsBoxTextHeight);
		seatsBoxLnpWinRect.Offset(0, SeatsBoxTextHeight);
	}
}

void DisplayFrameRenderer::defineGraphLimits() {

	dv.displayBottom = dv.DCheight;
	//dv.displayTop = toolBar->GetSize().GetHeight();
	dv.displayTop = 0;
}

float DisplayFrameRenderer::backgroundHeight() const
{
	return dv.DCheight - dv.displayTop;
}

float DisplayFrameRenderer::probabilityBoxTop() const
{
	return dv.displayTop + ProbabilityBoxMargin;
}

float DisplayFrameRenderer::expectationBoxTop() const
{
	return probabilityBoxTop() + ProbabilityBoxMargin + ProbabilityBoxHeight;
}