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
#include "wx/grid.h"

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

	enum class Filter {
		AllResults,
		LatestResults,
		SignificantResults,
		KeyResults
	};

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& event);

	// Runs all "live" simulations found
	void OnRunLiveSimulations(wxCommandEvent& event);

	// Adds the currently entered result to the records
	void OnAddResult(wxCommandEvent& event);

	// Brings up dialog box for setting a seat to have non-classic results
	// (i.e. parties other than the two majors are competitive)
	void OnNonClassic(wxCommandEvent& event);

	// Handles selection of the results filter
	void OnFilterSelection(wxCommandEvent& event);

	// adds "result" to result data.
	void addResultToResultData(Result result);

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// updates the toolbar
	void refreshToolbar();

	// checks whether thisResult passes the current filter
	bool resultPassesFilter(Result const& thisResult);

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Text box used to enter the name of a seat to input results for
	wxTextCtrl* seatNameTextCtrl = nullptr;

	// Text box used to enter the swing for the seat selected
	wxTextCtrl* swingTextCtrl = nullptr;

	// Text box used to enter the percent counted for the seat
	wxTextCtrl* percentCountedTextCtrl = nullptr;

	// Text box used to enter the booths counted for the seat
	// (only used if a percent counted is not available)
	wxTextCtrl* currentBoothCountTextCtrl = nullptr;

	// Text box used to enter the total number of booths for the seat
	wxTextCtrl* totalBoothCountTextCtrl = nullptr;

	// Text box used to enter the total number of booths for the seat
	wxComboBox* filterComboBox = nullptr;

	// Panel containing vote count data.
	wxPanel* dataPanel = nullptr;

	// Control containing vote count data (from the dataPanel above)
	wxGrid* resultsData = nullptr;

	bool displayPolls = true;

	Filter filter = Filter::AllResults;
};