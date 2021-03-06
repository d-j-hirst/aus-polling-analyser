#include "MapFrame.h"

#include "General.h"
#include "MiscGeometry.h"

#include <wx/tokenzr.h> 

#include <algorithm>
#include <cmath>
#include <fstream>

using Results::Booth;
using Candidate = Results::Booth::Candidate;

enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectSeat,
	ColourMode,
	RefreshMaps,
	ShowPpvc,
};

const wxFont TooltipFont = wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Segoe UI");

constexpr int TooltipSpacing = 3;

// square of the maximum distance from the mouse pointer to a booth point that will allow it to be selected.
const int SelectDistanceSquaredMaximum = 16;

wxColour mixColour(wxColour const& colour1, wxColour const& colour2, float colour1Percent) {
	unsigned char red = unsigned char(std::clamp(float(colour1.Red()) * colour1Percent + float(colour2.Red()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	unsigned char green = unsigned char(std::clamp(float(colour1.Green()) * colour1Percent + float(colour2.Green()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	unsigned char blue = unsigned char(std::clamp(float(colour1.Blue()) * colour1Percent + float(colour2.Blue()) * (1.0f - colour1Percent), 0.0f, 255.0f));
	return wxColour(red, green, blue);
}

// frame constructor
MapFrame::MapFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Map", wxPoint(333, 0), project),
	refresher(refresher)
{
	refreshToolbar();
	createDcPanel();
	Layout();
	bindEventHandlers();
	initialiseBackgroundMaps();
	paint();
}

void MapFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void MapFrame::resetMouseOver() {
	mouseoverBooth = -1;
}

void MapFrame::refreshData() {
	refreshToolbar();
}

void MapFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_HORZ_TEXT | wxTB_NOICONS);

	// *** Simulation Combo Box *** //

	// Create the choices for the combo box.
	// By default "no seat" is the selected seat
	// Set the selected seat to be the first seat
	wxArrayString seatStrings;
	seatStrings.push_back("None");
	for (auto const&[key, seat] : project->seats()) {
		seatStrings.push_back(seat.name);
	}
	std::string comboBoxString;
	selectedSeat = 0;
	comboBoxString = seatStrings[selectedSeat];

	selectSeatComboBox = new wxComboBox(toolBar, ControlId::SelectSeat, comboBoxString, wxPoint(0, 0), wxSize(150, 30), seatStrings);
	selectSeatComboBox->Select(selectedSeat);

	wxArrayString colourModeStrings = collectColourModeStrings();

	selectedColourMode = 0;
	comboBoxString = colourModeStrings[selectedColourMode];

	colourModeComboBox = new wxComboBox(toolBar, ControlId::ColourMode, comboBoxString, wxPoint(0, 0), wxSize(150, 30), colourModeStrings);
	colourModeComboBox->Select(selectedColourMode);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSeatComboBox);
	toolBar->AddControl(colourModeComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::RefreshMaps, "Refresh Map", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Refresh Map");
	toolBar->AddCheckTool(ControlId::ShowPpvc, "Show PPVCs", wxNullBitmap);
	toolBar->ToggleTool(ControlId::ShowPpvc, true);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

wxArrayString MapFrame::collectColourModeStrings() const
{
	wxArrayString colourModeStrings;
	colourModeStrings.push_back("Two-candidate preferred margin");
	colourModeStrings.push_back("Two-candidate preferred swing");
	colourModeStrings.push_back("Highest primary vote");
	colourModeStrings.push_back("Two-party preferred margin (not implemented)");
	colourModeStrings.push_back("Two-party preferred swing (not implemented)");
	for (auto party = project->parties().cbegin(); party != project->parties().cend(); ++party) {
		colourModeStrings.push_back(party->second.name + "");
	}
	return colourModeStrings;
}

void MapFrame::createDcPanel()
{
	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));
}

void MapFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &MapFrame::OnResize, this, PA_MapFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &MapFrame::OnSeatSelection, this, ControlId::SelectSeat);
	Bind(wxEVT_COMBOBOX, &MapFrame::OnColourModeSelection, this, ControlId::ColourMode);
	Bind(wxEVT_TOOL, &MapFrame::OnMapRefresh, this, ControlId::RefreshMaps);
	Bind(wxEVT_TOOL, &MapFrame::OnTogglePpvcs, this, ControlId::ShowPpvc);
	dcPanel->Bind(wxEVT_MOTION, &MapFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &MapFrame::OnPaint, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_MOUSEWHEEL, &MapFrame::OnMouseWheel, this, ControlId::DcPanel);
}

void MapFrame::initialiseBackgroundMaps()
{
	backgroundMaps.clear();
	std::ifstream is("maps/maps.txt");
	while (is) {
		std::string filename;
		is >> filename;
		if (!is) break;
		Point2Df topLeft;
		is >> topLeft.x;
		is >> topLeft.y;
		Point2Df bottomRight;
		is >> bottomRight.x;
		is >> bottomRight.y;
		// The constructor of BackgroundMap automatically loads the image so that it's ready
		backgroundMaps.push_back(BackgroundMap(filename, topLeft, bottomRight));
	}
}

Point2Df MapFrame::webMercatorProjection(Point2Df latLong) const
{
	float globeX = 0.5f * (1 + toRadians(latLong.x) / Pi_f);
	// The 0.7f that follows is a fudge factor to keep directional proportionality when using OSM's map tiles
	// Value was found via trial and error and does not seem to be related to anything in particular
	// but it works ...
	float globeY = 0.7f * (1 - std::log(std::tan(toRadians(-latLong.y) * 0.5f + Pi_f * 0.25f)));
	return { globeX, globeY };
}

Point2Df MapFrame::calculateScreenPosFromCoords(Point2Df coords) const
{
	Point2Df pointProjection = webMercatorProjection(coords);
	Point2Df minProjection = webMercatorProjection(dv.minCoords);
	Point2Df maxProjection = webMercatorProjection(dv.maxCoords);
	Point2Df scaledProjection = pointProjection.scale(minProjection, maxProjection);
	Point2Df screenProjection = scaledProjection.componentMultiplication(dv.dcSize());
	return screenProjection;
}

Point2Df MapFrame::calculateImageCoordsFromScreenPos(Point2Df screenPos, wxImage const& image, Point2Df imageTopLeft, Point2Df imageBottomRight) const
{
	Point2Df imageTopLeftScreenPos = calculateScreenPosFromCoords(imageTopLeft);
	Point2Df imageBottomRightScreenPos = calculateScreenPosFromCoords(imageBottomRight);
	Point2Df screenPosRelative = screenPos.scale(imageTopLeftScreenPos, imageBottomRightScreenPos);
	Point2Df imageSize = { float(image.GetWidth()), float(image.GetHeight()) };
	Point2Df imageCoords = screenPosRelative.componentMultiplication(imageSize);
	return imageCoords;
}

int MapFrame::calculateBoothCircleSize(Booth const& booth) const
{
	return std::clamp(int(std::log(booth.totalNewTcpVotes()) - 2.5f), 2, 6);
}

wxColour MapFrame::decideBoothCircleColour(Results::Booth const & booth) const
{
	switch (selectedColourMode) {
	case ColourMode::TcpMargin:
		return decideBoothTcpMarginColour(booth);
	case ColourMode::TcpSwing:
		return decideBoothTcpSwingColour(booth);
	case ColourMode::TopPrimary:
		return decideBoothTopPrimaryColour(booth);
	}
	if (selectedColourMode >= ColourMode::SpecificPrimary && selectedColourMode <= ColourMode::SpecificPrimary + project->parties().count()) {
		return decideBoothSpecificPrimaryColour(booth);
	}
	return wxColour(255, 255, 255);
}

wxColour MapFrame::decideBoothTcpMarginColour(Results::Booth const & booth) const
{
	int winnerId = (booth.newTcpVote[0] > booth.newTcpVote[1] ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
	Party const& winnerParty = project->parties().view(project->results().getPartyByCandidate(winnerId));
	wxColour winnerColour = wxColour(winnerParty.colour.r, winnerParty.colour.g, winnerParty.colour.b, 255);
	float tcpMargin = float(std::max(booth.newTcpVote[0], booth.newTcpVote[1])) / float(booth.totalNewTcpVotes()) - 0.5f;
	float colourFactor = std::clamp(tcpMargin * 4.0f, 0.0f, 1.0f);
	wxColour finalColour = mixColour(winnerColour, wxColour(255, 255, 255), colourFactor);
	return finalColour;
}

wxColour MapFrame::decideBoothTcpSwingColour(Results::Booth const & booth) const
{
	wxColour finalColour = wxColour(255, 255, 255);
	if (booth.hasOldAndNewResults()) {
		int swingToId = (booth.rawSwing(0) > 0.0f ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
		Party const& swingToParty = project->parties().view(project->results().getPartyByCandidate(swingToId));
		float swingSize = abs(booth.rawSwing(0));
		float colourFactor = std::clamp(swingSize * 6.0f, 0.0f, 1.0f);
		wxColour partyColour = wxColour(swingToParty.colour.r, swingToParty.colour.g, swingToParty.colour.b, 255);
		finalColour = mixColour(partyColour, finalColour, colourFactor);
	}
	return finalColour;
}

wxColour MapFrame::decideBoothTopPrimaryColour(Results::Booth const & booth) const
{
	wxColour finalColour = wxColour(255, 255, 255);
	auto topPrimaryIt = std::max_element(booth.fpCandidates.begin(), booth.fpCandidates.end(),
		[](Candidate const& a, Candidate const& b) {return a.fpVotes < b.fpVotes; });
	Party const& topPrimaryParty = project->parties().view(project->results().getPartyByCandidate(topPrimaryIt->candidateId));
	float primaryVoteProportion = float(topPrimaryIt->fpVotes) / float(booth.totalNewFpVotes());
	float colourFactor = std::clamp((primaryVoteProportion - 0.2f) * 2.0f, 0.0f, 1.0f);
	wxColour partyColour = wxColour(topPrimaryParty.colour.r, topPrimaryParty.colour.g, topPrimaryParty.colour.b, 255);
	finalColour = mixColour(partyColour, finalColour, colourFactor);
	return finalColour;
}

wxColour MapFrame::decideBoothSpecificPrimaryColour(Results::Booth const & booth) const
{
	int partyIndex = selectedColourMode - ColourMode::SpecificPrimary;
	wxColour finalColour = wxColour(255, 255, 255);
	int partyFpVotes = 0;
	auto selectedPartyId = project->parties().indexToId(partyIndex);
	auto const& selectedParty = project->parties().viewByIndex(partyIndex);
	for (auto const& candidate : booth.fpCandidates) {
		if (project->results().getPartyByCandidate(candidate.candidateId) == selectedPartyId) {
			partyFpVotes += candidate.fpVotes;
		}
	}
	float primaryVoteProportion = float(partyFpVotes) / float(booth.totalNewFpVotes());
	float colourFactor = std::clamp((primaryVoteProportion) * 1.4f, 0.0f, 1.0f);
	if (partyIndex >= 2) {
		colourFactor = std::clamp(std::sqrt(primaryVoteProportion) * selectedParty.boothColourMult, 0.0f, 1.0f);
	}
	wxColour partyColour = wxColour(selectedParty.colour.r, selectedParty.colour.g, selectedParty.colour.b, 255);
	finalColour = mixColour(partyColour, finalColour, colourFactor);
	return finalColour;
}

bool MapFrame::decideBoothCircleVisibility(Results::Booth const & booth) const
{
	if (booth.isPPVC() && !displayPpvcs) return false;
	switch (selectedColourMode) {
	case ColourMode::TcpMargin:
		return decideBoothTcpMarginVisibility(booth);
	case ColourMode::TcpSwing:
		return decideBoothTcpSwingVisibility(booth);
	case ColourMode::TopPrimary:
		return decideBoothTopPrimaryVisibility(booth);
	}
	if (selectedColourMode >= ColourMode::SpecificPrimary && selectedColourMode <= ColourMode::SpecificPrimary + project->parties().count()) {
		return decideBoothSpecificPrimaryVisibility(booth);
	}
	return false;
}

bool MapFrame::decideBoothTcpMarginVisibility(Results::Booth const & booth) const
{
	if (!booth.totalNewTcpVotes()) return false;
	int winnerCandidateId = (booth.newTcpVote[0] > booth.newTcpVote[1] ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
	if (project->results().getPartyByCandidate(winnerCandidateId) == Party::InvalidId) return false;
	return true;
}

bool MapFrame::decideBoothTcpSwingVisibility(Results::Booth const & booth) const
{
	if (!booth.hasOldAndNewResults()) return false;
	int swingToCandidateId = (booth.rawSwing(0) > 0.0f ? booth.tcpCandidateId[0] : booth.tcpCandidateId[1]);
	if (project->results().getPartyByCandidate(swingToCandidateId) == Party::InvalidId) return false;
	return true;
}

bool MapFrame::decideBoothTopPrimaryVisibility(Results::Booth const & booth) const
{
	if (!booth.totalNewFpVotes()) return false;
	auto topPrimaryIt = std::max_element(booth.fpCandidates.begin(), booth.fpCandidates.end(),
		[](Candidate const& a, Candidate const& b) {return a.fpVotes < b.fpVotes; });
	if (project->results().getPartyByCandidate(topPrimaryIt->candidateId) == Party::InvalidId) return false;
	return true;
}

bool MapFrame::decideBoothSpecificPrimaryVisibility(Results::Booth const & booth) const
{
	int partyIndex = selectedColourMode - ColourMode::SpecificPrimary;
	if (!booth.totalNewFpVotes()) return false;
	auto selectedPartyId = project->parties().indexToId(partyIndex);
	for (auto const& candidate : booth.fpCandidates) {
		if (project->results().getPartyByCandidate(candidate.candidateId) == selectedPartyId) {
			return true;
		}
	}
	return false;
}

void MapFrame::drawBoothsForSeat(Seat const& seat, wxDC& dc)
{
	seat;
	dc;
	//if (!seat.latestResults) return;
	//for (int boothId : seat.latestResults->booths) {
	//	auto const& booth = project->results().getBooth(boothId);
	//	if (!decideBoothCircleVisibility(booth)) continue;
	//	Point2Df mapCoords = calculateScreenPosFromCoords(Point2Df(booth.coords.longitude, booth.coords.latitude));
	//	int circleSize = calculateBoothCircleSize(booth);
	//	wxColour boothColour = decideBoothCircleColour(booth);
	//	dc.SetBrush(boothColour);
	//	dc.DrawCircle(wxPoint(int(std::floor(mapCoords.x)), int(std::floor(mapCoords.y))), circleSize);
	//}
}

Point2Df MapFrame::getMinWorldCoords()
{
	auto currentLatitudeRange = project->results().boothLatitudeRange();
	auto currentLongitudeRange = project->results().boothLongitudeRange();
	return { currentLongitudeRange.x - 1.0f, currentLatitudeRange.x - 1.0f };
}

Point2Df MapFrame::getMaxWorldCoords()
{
	auto currentLatitudeRange = project->results().boothLatitudeRange();
	auto currentLongitudeRange = project->results().boothLongitudeRange();
	return { currentLongitudeRange.y + 1.0f, currentLatitudeRange.y + 1.0f };
}

MapFrame::RectF MapFrame::getMaximumBounds()
{
	Point2Df minCoords = getMinWorldCoords();
	Point2Df maxCoords = getMaxWorldCoords();
	float dcRatio = dv.dcSize().y / dv.dcSize().x;
	for (int i = 0; i < 1; ++i) {
		Point2Df minProjection = webMercatorProjection(minCoords);
		Point2Df maxProjection = webMercatorProjection(maxCoords);
		float projectionRatio = (maxProjection.y - minProjection.y) / (maxProjection.x / minProjection.x);
		if (projectionRatio < dcRatio) {
			maxCoords.x = minCoords.x + (maxCoords.x - minCoords.x) * (dcRatio / projectionRatio);
		}
		else if (projectionRatio > dcRatio) {
			maxCoords.y = minCoords.y + (maxCoords.y - minCoords.y) * (projectionRatio / dcRatio);
		}
	}
	return { minCoords, maxCoords };
}

void MapFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
}

void MapFrame::OnSeatSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedSeat = selectSeatComboBox->GetCurrentSelection();
	paint();
}

void MapFrame::OnColourModeSelection(wxCommandEvent& WXUNUSED(event))
{
	selectedColourMode = colourModeComboBox->GetCurrentSelection();
	paint();
}

void MapFrame::OnMapRefresh(wxCommandEvent& WXUNUSED(event))
{
	initialiseBackgroundMaps();
}

void MapFrame::OnTogglePpvcs(wxCommandEvent & event)
{
	displayPpvcs = bool(event.GetInt());
	paint();
}

// Handles the movement of the mouse in the display frame.
void MapFrame::OnMouseMove(wxMouseEvent& event) {
	Point2Di mousePos = Point2Di(event.GetX(), event.GetY());
	updatePositionForMouseMovement(mousePos, event.Dragging());
	updateMouseoverBooth(mousePos);
	paint();
}

void MapFrame::updatePositionForMouseMovement(Point2Di mousePos, bool dragging)
{
	if (dragging) {
		if (dragStart.x == -1) {
			dragStart = mousePos;
		}
		else {
			Point2Di pixelsMoved = mousePos - dragStart;
			dragScreen(pixelsMoved);
			dragStart = mousePos;
		}
	}
	else {
		dragStart = Point2Di(-1, -1);
	}
}

void MapFrame::dragScreen(Point2Di pixelsMoved)
{
	Point2Df mapSize = dv.maxCoords - dv.minCoords;
	Point2Df scaledPixelsMoved = Point2Df(pixelsMoved).scale(dv.dcTopLeft, dv.dcBottomRight);
	Point2Df degreesMoved = -scaledPixelsMoved.componentMultiplication(mapSize);
	Point2Df newTopLeft = dv.minCoords + degreesMoved;
	RectF bounds = getMaximumBounds();
	newTopLeft = newTopLeft.max(bounds.topLeft);
	newTopLeft = newTopLeft.min(bounds.bottomRight - mapSize);
	dv.minCoords = newTopLeft;
	dv.maxCoords = newTopLeft + mapSize;
}

void MapFrame::OnMouseWheel(wxMouseEvent& event)
{
	Point2Di mousePos = Point2Di(event.GetX(), event.GetY());
	float scrollRatio = float(event.GetWheelRotation()) / float(event.GetWheelDelta());
	updateScreenForMouseScroll(mousePos, scrollRatio);
	updateMouseoverBooth(mousePos);
	paint();
}

void MapFrame::updateScreenForMouseScroll(Point2Di mousePos, float scrollRatio)
{
	float zoomFactor = pow(2.0f, -scrollRatio);
	Point2Df mapSize = dv.maxCoords - dv.minCoords;
	Point2Df scaledScrollPos = Point2Df(mousePos).scale(dv.dcTopLeft, dv.dcBottomRight);
	Point2Df scrollCoords = scaledScrollPos.componentMultiplication(mapSize) + dv.minCoords;
	Point2Df newMapSize = mapSize * zoomFactor;
	RectF bound = getMaximumBounds();
	newMapSize = newMapSize.min(bound.bottomRight - bound.topLeft);
	Point2Df newTopLeft = scrollCoords - newMapSize * 0.5f;
	newTopLeft = newTopLeft.max(bound.topLeft);
	newTopLeft = newTopLeft.min(bound.bottomRight - newMapSize);
	dv.minCoords = newTopLeft;
	dv.maxCoords = newTopLeft + newMapSize;
}

void MapFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void MapFrame::updateInterface() {
}

void MapFrame::render(wxDC& dc) {

	using std::to_string;

	clearDC(dc);

	if (project->results().boothLatitudeRange().isZero()) return;

	defineGraphLimits();


	drawBackgroundMaps(dc);

	if (dv.minCoords.isZero()) {
		RectF bound = getMaximumBounds();
		dv.minCoords = bound.topLeft;
		dv.maxCoords = bound.bottomRight;
	}

	dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
	if (selectedSeat <= 0) {
		for (auto const& [key, seat] : project->seats()) {
			drawBoothsForSeat(seat, dc);
		}
	}
	else {
		drawBoothsForSeat(project->seats().viewByIndex(selectedSeat - 1), dc);
	}

	drawBoothDetails(dc);
}

void MapFrame::clearDC(wxDC& dc) {
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();
}

void MapFrame::defineGraphLimits() {
	dv.dcTopLeft = { 0.0f, 0.0f };
	dv.dcBottomRight = {float(dcPanel->GetClientSize().GetWidth()), float(dcPanel->GetClientSize().GetHeight())};
}

void MapFrame::drawBlankBackground(wxDC& dc) const
{
	wxRect backgroundRect = wxRect(dv.dcTopLeft.x, dv.dcTopLeft.y, dv.dcSize().x, dv.dcSize().y);
	setBrushAndPen(*wxWHITE, dc);
	dc.DrawRectangle(backgroundRect);
	const wxColour backgroundGrey = wxColour(210, 210, 210); // light grey
	setBrushAndPen(backgroundGrey, dc);
	dc.DrawRectangle(backgroundRect);
}

void MapFrame::setBrushAndPen(wxColour currentColour, wxDC& dc) const {
	dc.SetBrush(wxBrush(currentColour));
	dc.SetPen(wxPen(currentColour));
}

void MapFrame::updateMouseoverBooth(Point2Di mousePos)
{
	mousePos;
	//Point2Df mousePosF = Point2Df(mousePos);
	//int seatIndex = 0;
	//float smallestDistance = std::numeric_limits<float>::max();
	//int bestBooth = -1;
	//for (auto const& [key, seat] : project->seats()) {
	//	++seatIndex; // need this to be increased even if the seat isn't checked so that it'll be correct when we get to the selected seat
	//	if (selectedSeat > 0 && selectedSeat != seatIndex) continue;
	//	if (!seat.latestResults) return;
	//	for (int boothId : seat.latestResults->booths) {
	//		auto const& booth = project->results().getBooth(boothId);
	//		if (!booth.totalNewTcpVotes()) continue;
	//		Point2Df mapCoords = calculateScreenPosFromCoords(Point2Df(booth.coords.longitude, booth.coords.latitude));
	//		float thisDistance = mousePosF.distance(mapCoords);
	//		int circleSize = calculateBoothCircleSize(booth);
	//		if (thisDistance < smallestDistance && thisDistance <= circleSize + 1) {
	//			smallestDistance = thisDistance;
	//			bestBooth = boothId;
	//		}
	//	}
	//}
	//mouseoverBooth = bestBooth;
}

std::string MapFrame::decideTooltipText(Booth const & booth) const
{
	std::string returnString = booth.name;
	returnString += decideTcpText(booth);
	returnString += "\n";
	returnString += decideFpText(booth);
	return returnString;
}

std::string MapFrame::decideTcpText(Results::Booth const & booth) const
{
	std::string returnString;
	returnString += "\n";
	returnString += leadingCandidateText(booth);
	returnString += "\n";
	returnString += trailingCandidateText(booth);
	return returnString;
}

std::string MapFrame::leadingCandidateText(Results::Booth const & booth) const
{
	std::string returnString;
	bool firstCandidateLeading = booth.newTcpVote[0] > booth.newTcpVote[1];
	int leadingCandidate = (firstCandidateLeading ? 0 : 1);
	returnString += project->results().getCandidateById(booth.tcpCandidateId[leadingCandidate])->name;
	returnString += " (";
	returnString += project->results().getAffiliationById(project->results().getCandidateAffiliationId(booth.tcpCandidateId[leadingCandidate]))->shortCode;
	returnString += "): ";
	returnString += std::to_string(booth.newTcpVote[leadingCandidate]);
	returnString += " - ";
	float leadingProportion = float(booth.newTcpVote[leadingCandidate]) / float(booth.totalNewTcpVotes());
	returnString += formatFloat(leadingProportion * 100.0f, 2);
	returnString += "%";
	if (booth.hasOldAndNewResults()) {
		float swingPercent = booth.rawSwing(leadingCandidate) * 100.0f;
		returnString += " (";
		returnString += formatFloat(swingPercent, 2, true);
		returnString += ")";
	}
	return returnString;
}

std::string MapFrame::trailingCandidateText(Results::Booth const & booth) const
{
	std::string returnString;
	bool firstCandidateLeading = booth.newTcpVote[0] > booth.newTcpVote[1];
	int trailingCandidate = (firstCandidateLeading ? 1 : 0);

	returnString += project->results().getCandidateById(booth.tcpCandidateId[trailingCandidate])->name;
	returnString += " (";
	returnString += project->results().getAffiliationById(project->results().getCandidateAffiliationId(booth.tcpCandidateId[trailingCandidate]))->shortCode;
	returnString += "): ";
	returnString += std::to_string(booth.newTcpVote[trailingCandidate]);
	returnString += " - ";
	float trailingProportion = float(booth.newTcpVote[trailingCandidate]) / float(booth.totalNewTcpVotes());
	returnString += formatFloat(trailingProportion * 100.0f, 2);
	returnString += "%";
	if (booth.hasOldAndNewResults()) {
		float swingPercent = booth.rawSwing(trailingCandidate) * 100.0f;
		returnString += " (";
		returnString += formatFloat(swingPercent, 2, true);
		returnString += "%)";
	}
	return returnString;
}

std::string MapFrame::decideFpText(Results::Booth const & booth) const
{
	std::string returnString;
	int totalFpVotes = booth.totalNewFpVotes();
	if (totalFpVotes) {
		std::vector<Candidate> sortedCandidates(booth.fpCandidates.begin(), booth.fpCandidates.end());
		std::sort(sortedCandidates.begin(), sortedCandidates.end(), [](Candidate c1, Candidate c2) {return c1.fpVotes > c2.fpVotes; });
		for (auto const& candidate : sortedCandidates) {
			returnString += "\n";
			returnString += project->results().getCandidateById(candidate.candidateId)->name;
			returnString += " (";
			returnString += project->results().getAffiliationById(project->results().getCandidateAffiliationId(candidate.candidateId))->shortCode;
			returnString += "): ";
			returnString += std::to_string(candidate.fpVotes);
			returnString += " - ";
			float proportion = float(candidate.fpVotes) / float(totalFpVotes);
			returnString += formatFloat(proportion * 100.0f, 2);
			returnString += "%";
			int matchedCandidateVotes = 0;
			int matchedPartyVotes = 0;

			for (auto const& oldCandidate : booth.oldFpCandidates) {
				bool matchedParty = project->results().getPartyByCandidate(oldCandidate.candidateId) ==
					project->results().getPartyByCandidate(candidate.candidateId);
				bool matchedCandidate = project->results().getCandidateById(oldCandidate.candidateId)->name ==
					project->results().getCandidateById(candidate.candidateId)->name;
				// Matching to "independent party" or "invalid party" is not actually a match
				if (project->results().getCandidateAffiliationId(oldCandidate.candidateId) <= 0) {
					matchedParty = false;
				}
				// If we match the party, but not the exact affiliation, and another candidate DOES match the exact affiliation
				// then this is no longer a match
				if (matchedParty) {
					if (project->results().getCandidateAffiliationId(oldCandidate.candidateId) != project->results().getCandidateAffiliationId(candidate.candidateId)) {
						for (auto const& otherCandidate : sortedCandidates) {
							if (project->results().getCandidateAffiliationId(oldCandidate.candidateId) == project->results().getCandidateAffiliationId(otherCandidate.candidateId)) {
								matchedParty = false;
								break;
							}
						}
					}
				}

				if (matchedCandidate) matchedCandidateVotes = std::max(matchedCandidateVotes, oldCandidate.fpVotes);
				if (matchedParty) matchedPartyVotes = std::max(matchedPartyVotes, oldCandidate.fpVotes);
			}

			// match with same coalition first (to ensure continuation of parties rather than matching to a former independent)
			// then with same candidate if no other match is found
			int matchedVotes = 0;
			if (matchedPartyVotes) matchedVotes = matchedPartyVotes;
			else if (matchedCandidateVotes) matchedVotes = matchedCandidateVotes;

			if (matchedVotes) {
				int totalOldFpVotes = booth.totalOldFpVotes();
				returnString += " (";
				float oldProportion = float(matchedVotes) / float(totalOldFpVotes);
				float swing = proportion - oldProportion;
				returnString += formatFloat(swing * 100.0f, 2, true);
				returnString += "%)";
			}
		}
	}
	return returnString;
}

Point2Di MapFrame::calculateTooltipSize(wxDC const& dc, Booth const& booth) const
{
	std::string tooltipText = decideTooltipText(booth);
	wxArrayString lines = wxStringTokenize(tooltipText, "\n");
	wxSize maxTextExtent = wxSize(0, 0);
	for (auto const& line : lines) {
		wxSize textExtent = dc.GetTextExtent(line);
		maxTextExtent.x = std::max(maxTextExtent.x, textExtent.x);
	}
	maxTextExtent.x += TooltipSpacing * 2;
	maxTextExtent.y = (dc.GetTextExtent(lines[0]).y + TooltipSpacing) * lines.size() + TooltipSpacing;
	Point2Di size = Point2Di(maxTextExtent.x, maxTextExtent.y);
	return size;
}

Point2Di MapFrame::calculateTooltipPosition(Point2Di cursorPosition, Point2Di tooltipSize) const
{
	Point2Di potentialTopLeft = cursorPosition + Point2Di(10, 0);
	Point2Di potentialBottomRight = potentialTopLeft + tooltipSize;
	if (potentialBottomRight.y >= dv.dcBottomRight.y) {
		int upwardShift = potentialBottomRight.y - dv.dcBottomRight.y + 1;
		potentialTopLeft.y -= upwardShift;
		potentialBottomRight = potentialTopLeft + tooltipSize;
	}
	if (potentialBottomRight.x > dv.dcBottomRight.x) {
		potentialTopLeft.x = std::max(0, cursorPosition.x - tooltipSize.x);
		potentialBottomRight = potentialTopLeft + tooltipSize;
	}
	return potentialTopLeft;
}

void MapFrame::drawBoothDetails(wxDC& dc) const
{
	if (mouseoverBooth == -1) return;
	dc.SetFont(TooltipFont);
	auto const& booth = project->results().getBooth(mouseoverBooth);
	Point2Df screenPos = calculateScreenPosFromCoords(Point2Df(booth.coords.longitude, booth.coords.latitude));
	Point2Di tooltipSize = calculateTooltipSize(dc, booth);
	Point2Di tooltipPos = calculateTooltipPosition(Point2Di(screenPos), tooltipSize);
	Point2Di textPoint = tooltipPos + Point2Di(3, 3);
	dc.SetBrush(wxBrush(wxColour(255, 255, 255))); // white background
	dc.SetPen(wxPen(wxColour(0, 0, 0))); // black text & border
	dc.DrawRoundedRectangle(wxRect(tooltipPos.x, tooltipPos.y, tooltipSize.x, tooltipSize.y), 3);
	dc.DrawText(decideTooltipText(booth), wxPoint(textPoint.x, textPoint.y));
}

void MapFrame::drawBackgroundMaps(wxDC& dc) const
{
	if (dv.coordsRange().isZero()) return;
	for (auto const& backgroundMap : backgroundMaps) {
		drawBackgroundMap(dc, backgroundMap);
	}
}

void MapFrame::drawBackgroundMap(wxDC& dc, BackgroundMap const& map) const
{
	const Point2Df imageScreenTopLeft = calculateScreenPosFromCoords(map.topLeft);
	const Point2Df imageScreenBottomRight = calculateScreenPosFromCoords(map.bottomRight);
	const Point2Df imageScreenSize = imageScreenBottomRight - imageScreenTopLeft;

	// don't draw really small maps since the detail won't be sufficiently visible
	if (imageScreenSize.x < dv.dcSize().x * 0.88f && imageScreenSize.y < dv.dcSize().y * 0.88f) return;

	const Point2Df screenImageTopLeft = calculateImageCoordsFromScreenPos(dv.dcTopLeft, map.image, map.topLeft, map.bottomRight);
	const Point2Df screenImageBottomRight = calculateImageCoordsFromScreenPos(dv.dcBottomRight, map.image, map.topLeft, map.bottomRight);
	const Point2Di imageSize = { map.image.GetWidth(), map.image.GetHeight() };
	const Point2Di subimageImageTopLeft = Point2Di(screenImageTopLeft).max({ 0, 0 });
	const Point2Di subimageImageBottomRight = (Point2Di(screenImageBottomRight) + Point2Di(1, 1)).min(imageSize - Point2Di(1, 1));

	// shouldn't attempt to draw maps with zero or smaller size
	if (subimageImageBottomRight.x <= subimageImageTopLeft.x || subimageImageBottomRight.y <= subimageImageTopLeft.y) return;
	wxRect subImageRect = wxRect(wxPoint(subimageImageTopLeft.x, subimageImageTopLeft.y),
		wxPoint(subimageImageBottomRight.x, subimageImageBottomRight.y));
	wxImage subimage = map.image.GetSubImage(subImageRect);

	const Point2Df subimageScreenPosTopLeft = Point2Df(subimageImageTopLeft).scale({ 0, 0 }, Point2Df(imageSize))
		.componentMultiplication(imageScreenSize) + imageScreenTopLeft;
	const Point2Df subimageScreenPosBottomRight = Point2Df(subimageImageBottomRight).scale({ 0, 0 }, Point2Df(imageSize))
		.componentMultiplication(imageScreenSize) + imageScreenTopLeft;

	const Point2Di pixelSize = Point2Di(subimageScreenPosBottomRight) - Point2Di(subimageScreenPosTopLeft);
	const Point2Di pixelTopLeft = Point2Di(subimageScreenPosTopLeft);

	subimage.Rescale(pixelSize.x, pixelSize.y);
	dc.DrawBitmap(wxBitmap(subimage), pixelTopLeft.x, pixelTopLeft.y);
}
