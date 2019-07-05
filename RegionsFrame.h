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
#include "Region.h"
#include "EditRegionFrame.h"

class ProjectFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_RegionsFrame_Base = 250, // To avoid mixing events with other frames.
	PA_RegionsFrame_FrameID,
	PA_RegionsFrame_DataViewID,
	PA_RegionsFrame_NewRegionID,
	PA_RegionsFrame_EditRegionID,
	PA_RegionsFrame_RemoveRegionID,
};

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political region data.
class RegionsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	RegionsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// Calls on the frame to create a new region based on "Region".
	void OnNewRegionReady(Region& region);

	// Calls on the frame to edit the currently selected region based on "Region".
	void OnEditRegionReady(Region& region);

private:

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

	// replaces the currently selected region with "region" in region data.
	// Should not be used except within replaceRegion.
	void replaceRegionInRegionData(Region region);

	// does everything required to remove the currently selected region, if possible.
	void removeRegion();

	// removes the currently selected region from region data.
	// Should not be used except within removeRegion.
	void removeRegionFromRegionData();

	// refreshes the displayed data.
	void refreshData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing region data.
	wxDataViewListCtrl* regionData = nullptr;
};