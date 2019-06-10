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
	};

	Point2Di calculateScreenPosFromCoords(Point2Df coords);

	int calculateCircleSizeFromBooth(Results::Booth const& booth);

	void drawBoothsForSeat(Seat const& seat, wxDC& dc);

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnSimulationSelection(wxCommandEvent& WXUNUSED(event));

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

	// For handling horizontal scrolling of the visualiser
	Point2Di dragStart = { -1, -1 };

	// last pointed-to booth
	int mouseoverBooth = -1;

	wxComboBox* selectSeatComboBox = nullptr;

	wxPanel* dcPanel = nullptr;

	DisplayVariables dv;

	int selectedSeat = -1;

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	bool displayPolls = true;
};