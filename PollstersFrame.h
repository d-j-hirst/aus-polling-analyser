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
#include "Pollster.h"

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political pollster data.
class PollstersFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	PollstersFrame(ProjectFrame::Refresher const refresher, PollingProject* project);

	// Calls on the frame to create a new pollster based on "Pollster".
	void OnNewPollsterReady(Pollster& pollster);

	// Calls on the frame to edit the currently selected pollster based on "Pollster".
	void OnEditPollsterReady(Pollster& pollster);

private:

	// For the new party dialog to callback once the user has clicked OK.
	// Adds the new party to the project and updates the data panel.
	void newPollsterCallback(Pollster pollster);

	// For the edit party dialog to callback once the user has clicked OK.
	// Replaces the existing party with the new party and updates the data panel
	void editPollsterCallback(Pollster pollster);

	// Creates the toolbar and its accompanying tools
	void setupToolbar();

	// Create the data table from scratch
	void setupDataTable();

	// refresh the (already existing) data table
	void refreshDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new pollster.
	void OnNewPollster(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing pollster.
	void OnEditPollster(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing pollster.
	void OnRemovePollster(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the pollster "pollster".
	void addPollster(Pollster pollster);

	// adds "pollster" to pollster data. Should not be used except within addPollster.
	void addPollsterToPollsterData(Pollster pollster);

	// does everything required to replace the currently selected pollster with "pollster".
	void replacePollster(Pollster pollster);

	// replaces the currently selected pollster with "pollster" in pollster data.
	// Should not be used except within replacePollster.
	void replacePollsterInPollsterData(Pollster pollster);

	// does everything required to remove the currently selected pollster, if possible.
	void removePollster();

	// removes the currently selected pollster from pollster data.
	// Should not be used except within removePollster.
	void removePollsterFromPollsterData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher const refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing pollster data.
	wxDataViewListCtrl* pollsterData = nullptr;
};