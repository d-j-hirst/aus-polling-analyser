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
#include "Poll.h"

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political poll data.
class PollsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	PollsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

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

	// Opens the dialog that allows the user to define settings for a new poll.
	void OnNewPoll(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing poll.
	void OnEditPoll(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing poll.
	void OnRemovePoll(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	void OnCollectPolls(wxCommandEvent& WXUNUSED(event));

	// does everything required to add the poll "poll".
	void addPoll(Poll poll);

	// adds "poll" to poll data. Should not be used except within addPoll.
	void addPollToPollData(Poll poll);

	// does everything required to replace the currently selected poll with "poll".
	void replacePoll(Poll poll);

	// does everything required to remove the currently selected poll, if possible.
	void removePoll();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing poll data.
	wxDataViewListCtrl* pollData = nullptr;
};