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
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	MapFrame(ProjectFrame* const parent, PollingProject* project);

	// paints immediately if needed.
	void paint();

	// removes any mouse-over information.
	void resetMouseOver();

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	struct DisplayVariables {
		Point2Df dcTopLeft;
		Point2Df dcBottomRight;
		Point2Df dcSize() { return dcBottomRight - dcTopLeft; };
		Point2Df minCoords;
		Point2Df maxCoords;
		Point2Df coordsRange() { return maxCoords - minCoords; };
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

	void initialiseBackgroundMaps();

	// converts to a point between 0 and 1 on the globe
	Point2Df webMercatorProjection(Point2Df const& latLong);

	// Given a set of world coordinates (lat/long) converts it to a pixel location on the map
	Point2Df calculateScreenPosFromCoords(Point2Df coords);

	// calculates the coordinates that the given screen position will have on the given image, given the
	// world-coordinates of image boundaries are defined by imageTopLeft and imageBottomRight
	Point2Df calculateImageCoordsFromScreenPos(Point2Df screenPos, wxImage const& image, Point2Df imageTopLeft, Point2Df imageBottomRight);

	int calculateCircleSizeFromBooth(Results::Booth const& booth);

	wxColour decideCircleColourFromBooth(Results::Booth const& booth);

	bool decideCircleVisibilityFromBooth(Results::Booth const& booth);

	void drawBoothsForSeat(Seat const& seat, wxDC& dc);

	Point2Df getMinWorldCoords();
	Point2Df getMaxWorldCoords();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Selects a new seat and redraws the map.
	void OnSeatSelection(wxCommandEvent& WXUNUSED(event));

	// Gets the colour mode the user has chosen and redraws the map.
	void OnColourModeSelection(wxCommandEvent& WXUNUSED(event));

	// Repaints the display diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the display frame.
	void OnMouseMove(wxMouseEvent& event);

	// Handles the scrolling of the mouse wheel.
	void OnMouseWheel(wxMouseEvent& event);

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// function that carries out rendering the poll display.
	void render(wxDC& dc);

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	// clears the drawing area.
	void clearDC(wxDC& dc);

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour, wxDC& dc);

	// updates the toolbar
	void refreshToolbar();

	// updates the pointed-to booth based on the given mouse position (relative to top-left of the display region)
	void updateMouseoverBooth(Point2Di mousePos);

	// Returns text for the booth results tooltip
	std::string decideTooltipText(Results::Booth const& booth);

	// calculate the size for the booth tooltip
	// dc parameter is used to determine size of texts
	Point2Di calculateTooltipSize(wxDC const& dc, Results::Booth const& booth);

	// calculate the position for the tooltip given the cursor position and tooltip size
	Point2Di calculateTooltipPosition(Point2Di cursorPosition, Point2Di tooltipSize);

	// Draws a tooltip for the pointed-to booth on the map with details of the poll results.
	void drawBoothDetails(wxDC& dc);

	void drawBackgroundMaps(wxDC& dc);

	void drawBackgroundMap(wxDC& dc, BackgroundMap const& map);

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

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	bool displayPolls = true;

	std::vector<BackgroundMap> backgroundMaps;
};