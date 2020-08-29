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

// *** AnalysisFrame ***
// Frame that displays analysis from past and present elections
class AnalysisFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	AnalysisFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// paints immediately if needed.
	void paint();

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	enum class AnalysisType {
		Party_Analysis,
		Invalid
	};

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& event);

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnElectionSelection(wxCommandEvent& event);

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnAnalysisSelection(wxCommandEvent& event);

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnAnalyse(wxCommandEvent& event);

	// Repaints the display diagram
	void OnPaint(wxPaintEvent& event);

	// Handles the movement of the mouse in the display frame.
	void OnMouseMove(wxMouseEvent& event);

	void bindEventHandlers();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// updates the toolbar
	void refreshToolbar();

	// function that carries out rendering the poll display.
	void render(wxDC& dc);

	wxComboBox* selectElectionComboBox = nullptr;

	wxComboBox* selectAnalysisComboBox = nullptr;

	wxButton* analyseButton = nullptr;

	wxPanel* dcPanel = nullptr;

	int selectedElection = -1;
	int selectedAnalysis = -1;

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;
};