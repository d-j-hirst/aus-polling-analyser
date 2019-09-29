#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "wx/dcbuffer.h"

#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "Points.h"
#include "ProjectFrame.h"

#include <memory>

class ProjectFrame;
class wxImage;

// *** DisplayFrame ***
// Frame that displays the results of election simulations
class MapFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	MapFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// paints immediately if needed.
	void paint();

	// removes any mouse-over information.
	void resetMouseOver();

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	struct RectF {
		Point2Df topLeft;
		Point2Df bottomRight;
	};

	struct DisplayVariables {
		Point2Df dcTopLeft;
		Point2Df dcBottomRight;
		Point2Df dcSize() const { return dcBottomRight - dcTopLeft; };
		Point2Df minCoords;
		Point2Df maxCoords;
		Point2Df coordsRange() const { return maxCoords - minCoords; };
	};

	struct BackgroundMap {
		std::string filename;
		Point2Df topLeft; // plain lat/long coordinates
		Point2Df bottomRight; // plain lat/long coordinates
		wxImage image;
		bool valid = false;
		BackgroundMap(std::string filename, Point2Df topLeft, Point2Df bottomRight)
			: filename(filename), topLeft(topLeft), bottomRight(bottomRight),
			image(filename, wxBITMAP_TYPE_PNG) {}
	};

	void refreshToolbar();

	wxArrayString collectColourModeStrings() const;

	// updates the panel upon which the map will be displayed
	void createDcPanel();

	// bind event handlers to the tools and the DC panel
	void bindEventHandlers();

	void initialiseBackgroundMaps();

	// converts to a point between 0 and 1 on the globe
	// latLong should be in degrees
	Point2Df webMercatorProjection(Point2Df latLong) const;

	// Given a set of world coordinates (lat/long) converts it to a pixel location on the map
	Point2Df calculateScreenPosFromCoords(Point2Df coords) const;

	// calculates the coordinates that the given screen position will have on the given image, given the
	// world-coordinates of image boundaries are defined by imageTopLeft and imageBottomRight
	Point2Df calculateImageCoordsFromScreenPos(Point2Df screenPos, wxImage const& image, Point2Df imageTopLeft, Point2Df imageBottomRight) const;

	int calculateBoothCircleSize(Results::Booth const& booth) const;

	wxColour decideBoothCircleColour(Results::Booth const& booth) const;

	wxColour decideBoothTcpMarginColour(Results::Booth const& booth) const;

	wxColour decideBoothTcpSwingColour(Results::Booth const& booth) const;

	wxColour decideBoothTopPrimaryColour(Results::Booth const& booth) const;

	wxColour decideBoothSpecificPrimaryColour(Results::Booth const& booth) const;

	bool decideBoothCircleVisibility(Results::Booth const& booth) const;

	bool decideBoothTcpMarginVisibility(Results::Booth const& booth) const;

	bool decideBoothTcpSwingVisibility(Results::Booth const& booth) const;

	bool decideBoothTopPrimaryVisibility(Results::Booth const& booth) const;

	bool decideBoothSpecificPrimaryVisibility(Results::Booth const& booth) const;

	void drawBoothsForSeat(Seat const& seat, wxDC& dc);

	Point2Df getMinWorldCoords();
	Point2Df getMaxWorldCoords();
	RectF getMaximumBounds();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Selects a new seat and redraws the map.
	void OnSeatSelection(wxCommandEvent& event);

	// Gets the colour mode the user has chosen and redraws the map.
	void OnColourModeSelection(wxCommandEvent& event);

	// Refreshes the map files so that they can be updated without requiring a game restart.
	void OnMapRefresh(wxCommandEvent& event);

	// Refreshes the map files so that they can be updated without requiring a game restart.
	void OnTogglePpvcs(wxCommandEvent& event);

	// Repaints the display diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the display frame.
	void OnMouseMove(wxMouseEvent& event);

	// Update the screen position and drag status for mouse movement to the given position
	void updatePositionForMouseMovement(Point2Di mousePos, bool dragging);

	// drag the screen according to a mouse movement given by pixelsMoved
	void dragScreen(Point2Di pixelsMoved);

	// Handles the scrolling of the mouse wheel.
	void OnMouseWheel(wxMouseEvent& event);

	// Update the screen position and drag status for mouse scrolling
	void updateScreenForMouseScroll(Point2Di mousePos, float scrollRatio);

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// function that carries out rendering the poll display.
	void render(wxDC& dc);

	// clears the drawing area.
	void clearDC(wxDC& dc);

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	void drawBlankBackground(wxDC& dc) const;

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour, wxDC& dc) const;

	// updates the pointed-to booth based on the given mouse position (relative to top-left of the display region)
	void updateMouseoverBooth(Point2Di mousePos);

	// Returns text for the booth results tooltip
	std::string decideTooltipText(Results::Booth const& booth) const;

	std::string decideTcpText(Results::Booth const& booth) const;

	std::string leadingCandidateText(Results::Booth const& booth) const;

	std::string trailingCandidateText(Results::Booth const& booth) const;

	std::string decideFpText(Results::Booth const& booth) const;

	// calculate the size for the booth tooltip
	// dc parameter is used to determine size of texts
	Point2Di calculateTooltipSize(wxDC const& dc, Results::Booth const& booth) const;

	// calculate the position for the tooltip given the cursor position and tooltip size
	Point2Di calculateTooltipPosition(Point2Di cursorPosition, Point2Di tooltipSize) const;

	// Draws a tooltip for the pointed-to booth on the map with details of the poll results.
	void drawBoothDetails(wxDC& dc) const;

	void drawBackgroundMaps(wxDC& dc) const;

	void drawBackgroundMap(wxDC& dc, BackgroundMap const& map) const;

	// For handling horizontal scrolling of the visualiser
	Point2Di dragStart = { -1, -1 };

	// last pointed-to booth
	int mouseoverBooth = -1;

	wxComboBox* selectSeatComboBox = nullptr;

	wxComboBox* colourModeComboBox = nullptr;

	wxPanel* dcPanel = nullptr;

	DisplayVariables dv;

	int selectedSeat = -1;

	enum ColourMode {
		TcpMargin,
		TcpSwing,
		TopPrimary,
		TppMargin,
		TppSwing,
		SpecificPrimary // add an offset to this for the particular party
	};

	// Use ColourMode enum for the presets
	int selectedColourMode = 0;

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	bool displayPpvcs = true;

	std::vector<BackgroundMap> backgroundMaps;
};