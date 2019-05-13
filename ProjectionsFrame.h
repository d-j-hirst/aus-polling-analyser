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

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_ProjectionsFrame_Base = 600, // To avoid mixing events with other frames.
	PA_ProjectionsFrame_FrameID,
	PA_ProjectionsFrame_DataViewID,
	PA_ProjectionsFrame_NewProjectionID,
	PA_ProjectionsFrame_EditProjectionID,
	PA_ProjectionsFrame_RemoveProjectionID,
	PA_ProjectionsFrame_RunProjectionID,
	PA_ProjectionsFrame_NowCastID,
};

// *** ProjectionsFrame ***
// Frame that allows the user to create aggregated projections using the poll data.
class ProjectionsFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	ProjectionsFrame(ProjectFrame* const parent, PollingProject* project);

	// Calls on the frame to create a new projection based on "Projection".
	void OnNewProjectionReady(Projection& projection);

	// Calls on the frame to edit the currently selected projection based on "Projection".
	void OnEditProjectionReady(Projection& projection);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

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

	// replaces the currently selected projection with "projection" in projection data.
	// Should not be used except within replaceProjection.
	void replaceProjectionInProjectionData(Projection projection);

	// does everything required to remove the currently selected projection, if possible.
	void removeProjection();

	// removes the currently selected projection from projection data.
	// Should not be used except within removeProjection.
	void removeProjectionFromProjectionData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// does everything required to run the currently selected projection, if possible.
	void runProjection();

	// Sets the projection to be a "now-cast" (ends one day after the model ends)
	void setAsNowCast();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing projection data.
	wxDataViewListCtrl* projectionData = nullptr;
};