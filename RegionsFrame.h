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

#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"

#include "wx/dataview.h"
#include "wx/bookctrl.h"

class ProjectFrame;

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political region data.
class RegionsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	RegionsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

private:

	// For the new region dialog to callback once the user has clicked OK.
	// Adds the new region to the project and updates the data panel.
	void newRegionCallback(Region region);

	// For the edit region dialog to callback once the user has clicked OK.
	// Replaces the existing region with the new region and updates the data panel
	void editRegionCallback(Region region);

	// Creates the tool bar and its icons
	void setupToolBar();

	// Create the data table from scratch
	void setupDataTable();

	// refresh the (already existing) data table
	void refreshDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new region.
	void OnNewRegion(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing region.
	void OnEditRegion(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing region.
	void OnRemoveRegion(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the region "region".
	void addRegion(Region region);

	// adds "region" to region data. Should not be used except within addRegion.
	void addRegionToRegionData(Region region);

	// does everything required to replace the currently selected region with "region".
	void replaceRegion(Region region);

	// does everything required to remove the currently selected region, if possible.
	void removeRegion();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing region data.
	wxDataViewListCtrl* regionData = nullptr;
};