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

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent&);

	// Select the chosen simulation
	void OnSimulationSelection(wxCommandEvent&);

	// Save a permanent copy of the latest simulation report
	void OnSaveReport(wxCommandEvent&);

	// Select the chosen saved report
	void OnSavedReportSelection(wxCommandEvent&);

	// Save a permanent copy of the latest simulation report
	void OnSendToServer(wxCommandEvent&);

	// Repaints the display diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the display frame.
	void OnMouseMove(wxMouseEvent& event);

	void bindEventHandlers();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// updates the toolbar
	void refreshToolbar();

	void refreshSavedReports();

	// function that carries out rendering the poll display.
	void render(wxDC& dc);

	wxComboBox* selectSimulationComboBox = nullptr;
	wxComboBox* selectSavedReportComboBox = nullptr;

	wxPanel* dcPanel = nullptr;

	int selectedSimulation = -1;
	int selectedSaveReport = -1; // -1 for "latest report", 0 for the first saved report

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;
};