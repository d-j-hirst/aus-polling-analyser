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

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political poll data.
class VisualiserFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	VisualiserFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// paints immediately if needed.
	// If resetMouse is set to true then all mouseover data will be reset
	// (needed if it's called while the user is focused on another page)
	void paint(bool resetMouse = false);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	struct GraphicsVariables {

		enum class AxisTickInterval {
			Day,
			TwoDay,
			FourDay,
			Week,
			Fortnight,
			Month,
			TwoMonth,
			Quarter,
			HalfYear,
			Year,
			TwoYear,
			FiveYear,
			Decade,
			Num
		};

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

	// removes any mouse-over information.
	void resetMouseOver();

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
	void OnModelSelection(wxCommandEvent& event);

	// Handles selection of the displayed projection
	void OnProjectionSelection(wxCommandEvent& event);

	// Handles selection of the displayed party
	void OnPartySelection(wxCommandEvent& event);

	// sets the start and end days for the current visualiser view.
	void setVisualiserBounds(int startDay, int endDay);

	// sets the starting point for panning the visualiser graph
	void beginPan(int mouseX);

	// moves the screen to the appropriate position for panning to this x-ordinate
	void continuePan(int mouseX);

	// stop the screen from panning until the mouse button is pressed down again
	void endPan() { panStart = -1; }

	// zoom by the given number of increments.
	// Each increment represents a halving of the viewing timespan
	// use negative numbers 
	// x is the spot on screen to keep constant while zooming
	void zoom(float factor, int x);

	// updates the toolbar
	void refreshToolbar(); 
	
	// updates the party choice to match the parties run under the selected model
	void refreshPartyChoice();

	// updates the panel upon which the visualiser will be displayed
	void createDcPanel();

	// bind event handlers to the tools and the DC panel
	void bindEventHandlers();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// function that carries out rendering the poll visualiser.
	void render(wxDC& dc);

	// gets the start and end days for the graph.
	void getStartAndEndDays();

	// defines the basic variables that represent the pixel limits of the graph.
	void determineGraphLimits();

	// determines what interval will be used for axis ticks.
	void determineAxisTickInterval();

	// determines what interval will be used for axis ticks.
	void determineFirstAxisTick();

	// get all the other axis ticks stored in "gv".
	void determineLaterAxisTicks();

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
	void drawModel(StanModel const& model, wxDC& dc);

	// draws lines for the vote projections.
	void drawProjections(wxDC& dc);

	// draws lines for the given projection.
	void drawProjection(Projection const& projection, wxDC& dc);

	// draws the dots of the graph showing poll results.
	void drawPollDots(wxDC& dc);

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour, wxDC& dc);

	// gets the X ordinate from a given date.
	int getXFromDate(wxDateTime const& date);

	// gets the X ordinate from a given date, expressed as an integral modified Julian day number.
	int getXFromDate(int date);

	// gets the Y ordinate from a given two-party-preferred vote.
	int getYFromVote(float this2pp);

	// gets the date from a given X ordinate.
	wxDateTime getDateFromX(int x);

	// gets the nearest poll to the given mouse coordinates.
	Poll::Id getPollFromMouse(wxPoint point);

	// determines the size of the box used to display poll information.
	void determineMouseOverPollRect();

	// draws the rectangle in which poll information is displayed.
	void drawMouseOverPollRect(wxDC& dc);

	// draws the text showing poll information.
	void drawMouseOverPollText(wxDC& dc);

	// For handling horizontal panning of the visualiser
	int panStart = -1;
	int originalStartDay = -1;
	int originalEndDay = -1;

	// indicates the start day currently shown in the visualiser.
	int visStartDay = -1000000;

	// indicates the end day currently shown in the visualiser.
	int visEndDay = 1000000;

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Combo box used to select the displayed model
	wxComboBox* selectModelComboBox = nullptr;

	// Combo box used to select the displayed projection
	wxComboBox* selectProjectionComboBox = nullptr;

	// Combo box used to select the in-focus party
	wxComboBox* selectPartyComboBox = nullptr;

	// Panel containing poll data.
	wxPanel* dcPanel = nullptr;

	// The poll that has the mouse over it.
	Poll::Id mouseOverPoll = Poll::InvalidId;

	// The dimensions of the box used to display poll information.
	wxRect mouseOverPollRect;

	// Graphics variables, updated every time painting occurs.
	GraphicsVariables gv;

	int selectedModel = -1;
	int selectedProjection = -1;
	int selectedParty = -1;

	bool displayPolls = true;
	bool displayModels = true;
	bool displayHouseEffects = false;
	bool displayProjections = true;
};