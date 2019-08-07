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

#include "wx/dataview.h"

#include <memory>
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"
#include "EditProjectionFrame.h"

class ProjectFrame;
class GenericChildFame;

// *** ProjectionsFrame ***
// Frame that allows the user to create aggregated projections using the poll data.
class ProjectionsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	ProjectionsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// Calls on the frame to create a new projection based on "Projection".
	void OnNewProjectionReady(Projection& projection);

	// Calls on the frame to edit the currently selected projection based on "Projection".
	void OnEditProjectionReady(Projection& projection);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshDataTable();

private:

	// Creates the toolbar and its accompanying tools
	void setupToolbar();

	// Create the data table from scratch
	void setupDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new projection.
	void OnNewProjection(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing projection.
	void OnEditProjection(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing projection.
	void OnRemoveProjection(wxCommandEvent& WXUNUSED(event));

	// Runs the selected projection.
	void OnRunProjection(wxCommandEvent& WXUNUSED(event));

	// Sets the selected projection as a now-cast.
	void OnNowCast(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the projection "projection".
	void addProjection(Projection projection);

	// adds "projection" to projection data. Should not be used except within addProjection.
	void addProjectionToProjectionData(Projection projection);

	// does everything required to replace the currently selected projection with "projection".
	void replaceProjection(Projection projection);

	// does everything required to remove the currently selected projection, if possible.
	void removeProjection();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// does everything required to run the currently selected projection, if possible.
	void runProjection();

	// Sets the projection to be a "now-cast" (ends one day after the model ends)
	void setAsNowCast();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing projection data.
	wxDataViewListCtrl* projectionData = nullptr;
};