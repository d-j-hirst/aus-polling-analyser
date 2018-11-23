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
#include "Debug.h"

class ProjectFrame;
class GenericChildFrame;

// *** ResultsFrame ***
// Frame that allows the user to input real election data that can then be used in simulations
// to provide live updates to projections during vote counting
class ResultsFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	ResultsFrame(ProjectFrame* const parent, PollingProject* project);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Runs all "live" simulations found
	void OnRunLiveSimulations(wxCommandEvent& WXUNUSED(event));

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// updates the toolbar
	void refreshToolbar();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Text box used to enter the name of a seat to input results for
	wxTextCtrl* seatNameTextCtrl = nullptr;

	// Panel containing vote count data.
	wxPanel* dataPanel = nullptr;

	// Control containing vote count data (from the dataPanel above)
	wxDataViewListCtrl* resultsData = nullptr;

	bool displayPolls = true;
};