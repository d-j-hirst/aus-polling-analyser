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

#include <memory>
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"
#include "Poll.h"

class ProjectFrame;
class GenericChildFrame;

enum AxisTickInterval {
	AXISTICK_DAILY,
	AXISTICK_TWO_DAY,
	AXISTICK_FOUR_DAY,
	AXISTICK_WEEKLY,
	AXISTICK_FORTNIGHTLY,
	AXISTICK_MONTHLY,
	AXISTICK_BIMONTHLY,
	AXISTICK_QUARTERLY,
	AXISTICK_HALFYEARLY,
	AXISTICK_YEARLY,
	AXISTICK_TWO_YEAR,
	AXISTICK_FIVE_YEAR,
	AXISTICK_DECADE,
	AXISTICK_NUM_AXIS_TICK_TYPES
};

struct GraphicsVariables {
	float DCwidth; // width of the entire display area
	float DCheight; // height of the entire display area

	float graphMargin; // margin of the ends of the axes from the edge of the display area
	float graphWidth; // width of the graph area (between axes)
	float graphRight; // x-ordinate of the right-hand axis
	float graphBottom; // y-ordinate of the bottom of the axes.
	float graphTop; // y-ordinate of the top of the axes.
	float horzAxis; // position of the horizontal axis
	AxisTickInterval interval;
	std::vector<wxDateTime> AxisTick;
};

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political poll data.
class VisualiserFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	VisualiserFrame(ProjectFrame* const parent, PollingProject* project);

	// paints immediately if needed.
	void paint();

	// removes any mouse-over information.
	void resetMouseOver();

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Repaints the visualiser diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the visualiser frame.
	void OnMouseMove(wxMouseEvent& event);

	// Handles the movement of the mouse in the visualiser frame.
	void OnMouseDown(wxMouseEvent& event);

	// Handles the scrolling of the mouse wheel.
	void OnMouseWheel(wxMouseEvent& event);

	// Handles toggling of polls on and off.
	void OnTogglePolls(wxCommandEvent& event);

	// Handles toggling of models on and off.
	void OnToggleModels(wxCommandEvent& event);

	// Handles toggling of house effects on and off.
	void OnToggleHouseEffects(wxCommandEvent& event);

	// Handles toggling of projections on and off.
	void OnToggleProjections(wxCommandEvent& event);

	// Handles selection of the displayed model
	void OnModelSelection(wxCommandEvent& WXUNUSED(event));

	// Handles selection of the displayed projection
	void OnProjectionSelection(wxCommandEvent& WXUNUSED(event));

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// function that carries out rendering the poll visualiser.
	void render(wxDC& dc);

	// gets the start and end days for the graph.
	void getStartAndEndDays();

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	// determines what interval will be used for axis ticks.
	void determineAxisTickInterval();

	// determines what interval will be used for axis ticks.
	void determineFirstAxisTick();

	// get all the other axis ticks stored in "gv".
	void getAxisTicks();

	// sets the week day of the given date to Monday.
	void setWeekDayToMonday(wxDateTime& dt);

	// sets the month to that of the Jan/Mar/May/Jul/Sep/Nov cycle.
	void setToTwoMonth(wxDateTime& dt);

	// sets the month to the start of the quarter.
	void setToQuarter(wxDateTime& dt);

	// sets the month to the start of the half-year.
	void setToHalfYear(wxDateTime& dt);

	// clears the drawing area.
	void clearDC(wxDC& dc);

	// draw the lines of the axes.
	void drawAxesLines(wxDC& dc);

	// draw the ticks of the axes.
	void drawAxisTickLines(wxDC& dc);

	// draws lines for the polling models.
	void drawModels(wxDC& dc);

	// draws lines for the given polling model.
	void drawModel(Model const* model, wxDC& dc);

	// draws lines for the vote projections.
	void drawProjections(wxDC& dc);

	// draws lines for the given projection.
	void drawProjection(Projection const* projection, wxDC& dc);

	// draws the dots of the graph showing poll results.
	void drawPollDots(wxDC& dc);

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour, wxDC& dc);

	// gets the X ordinate from a given date.
	int getXFromDate(wxDateTime const& date);

	// gets the X ordinate from a given date, expressed as an integral modified Julian day number.
	int getXFromDate(int date);

	// gets the Y ordinate from a given two-party-preferred vote.
	int getYFrom2PP(float this2pp);

	// gets the date from a given X ordinate.
	wxDateTime getDateFromX(int x);

	// gets the nearest poll to the given mouse coordinates.
	Poll const* getPollFromMouse(wxPoint point);

	// determines the size of the box used to display poll information.
	void determineMouseOverPollRect();

	// draws the rectangle in which poll information is displayed.
	void drawMouseOverPollRect(wxDC& dc);

	// draws the text showing poll information.
	void drawMouseOverPollText(wxDC& dc);

	// updates the toolbar
	void refreshToolbar();

	// For handling horizontal scrolling of the visualiser
	int dragStart = -1;
	int originalStartDay = -1;
	int originalEndDay = -1;

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Combo box used to select the displayed model
	wxComboBox* selectModelComboBox = nullptr;

	// Combo box used to select the displayed projection
	wxComboBox* selectProjectionComboBox = nullptr;

	// Panel containing poll data.
	wxPanel* dcPanel = nullptr;

	// The poll that has the mouse over it.
	Poll const* mouseOverPoll;

	// The dimensions of the box used to display poll information.
	wxRect mouseOverPollRect;

	// Graphics variables, updated every time painting occurs.
	GraphicsVariables gv;

	int selectedModel = -1;
	int selectedProjection = -1;

	bool displayPolls = true;
	bool displayModels = true;
	bool displayHouseEffects = false;
	bool displayProjections = true;
};