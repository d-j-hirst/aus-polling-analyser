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

#include <memory>
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"

class GenericChildFrame;

// *** DisplayFrame ***
// Frame that displays the results of election simulations
class DisplayFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	DisplayFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// paints immediately if needed.
	void paint();

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	struct DisplayVariables {
		float DCwidth;
		float DCheight;
		float displayTop;
		float displayBottom;
	};

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnSimulationSelection(wxCommandEvent& WXUNUSED(event));

	// Repaints the display diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the display frame.
	void OnMouseMove(wxMouseEvent& event);

	void bindEventHandlers();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// function that carries out rendering the poll display.
	void render(wxDC& dc);

	void drawBackground(wxDC& dc) const;

	void drawProbabilityBox(wxDC& dc) const;

	wxColour lightenedPartyColour(Party::Id partyId) const;

	void drawProbabilityBoxBackground(wxDC& dc) const;

	void drawProbabilityBoxLabels(wxDC& dc) const;

	void drawProbabilityBoxData(wxDC& dc) const;

	void drawProbabilityBoxText(wxDC& dc, wxRect& rect, std::string const& text, wxPoint subsequentOffset) const;

	void drawSumOfLeads(wxDC& dc) const;

	void drawSumOfLeadsText(wxDC& dc, wxRect& outerRect, std::string const& annotationText, std::string const& dataText) const;

	void drawExpectationsBox(wxDC& dc) const;

	void drawExpectationsBoxBackground(wxDC& dc) const;

	void drawExpectationsBoxTitle(wxDC& dc) const;

	void drawExpectationsBoxRows(wxDC& dc) const;

	void drawExpectationsBoxRow(wxDC& dc, wxRect& nameRect, PartyCollection::Index partyIndex) const;

	void drawRegionsBox(wxDC& dc) const;

	void drawRegionsBoxBackground(wxDC& dc) const;

	void drawRegionsBoxTitle(wxDC& dc) const;

	void drawRegionsBoxRows(wxDC& dc) const;

	void drawRegionsBoxRowTitles(wxDC& dc) const;

	void drawRegionsBoxRowList(wxDC& dc) const;

	void drawBoundsBox(wxDC& dc) const;

	void drawGraphBox(wxDC& dc) const;

	void drawSeatsBox(wxDC& dc) const;

	// defines the basic variables that represent the pixel limits of the graph.
	void defineGraphLimits();

	// clears the drawing area.
	void clearDC(wxDC& dc) const;

	// sets the brush and pen to a particular colour.
	void setBrushAndPen(wxColour currentColour, wxDC& dc) const;

	// updates the toolbar
	void refreshToolbar();

	float backgroundHeight() const;

	float probabilityBoxTop() const;

	float expectationBoxTop() const;

	Simulation const& simulation() const;

	wxComboBox* selectSimulationComboBox = nullptr;

	wxPanel* dcPanel = nullptr;

	DisplayVariables dv;

	int selectedSimulation = -1;

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	bool displayPolls = true;
};