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
#include "EditPollFrame.h"

class ProjectFrame;
class GenericChildFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_PollsFrame_Base = 350, // To avoid mixing events with other frames.
	PA_PollsFrame_FrameID,
	PA_PollsFrame_DataViewID,
	PA_PollsFrame_NewPollID,
	PA_PollsFrame_EditPollID,
	PA_PollsFrame_RemovePollID,
};

enum PollColumnsEnum {
	PollColumn_Name,
	PollColumn_Date,
	PollColumn_Reported2pp,
	PollColumn_Respondent2pp,
	PollColumn_Calc2pp,
	PollColumn_Primary,
	PollColumn_NumColumns,
};

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political poll data.
class PollsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	PollsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// Calls on the frame to create a new poll based on "Poll".
	void OnNewPollReady(Poll& poll);

	// Calls on the frame to edit the currently selected poll based on "Poll".
	void OnEditPollReady(Poll& poll);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

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

	// does everything required to add the poll "poll".
	void addPoll(Poll poll);

	// adds "poll" to poll data. Should not be used except within addPoll.
	void addPollToPollData(Poll poll);

	// does everything required to replace the currently selected poll with "poll".
	void replacePoll(Poll poll);

	// replaces the currently selected poll with "poll" in poll data.
	// Should not be used except within replacePoll.
	void replacePollInPollData(Poll poll);

	// does everything required to remove the currently selected poll, if possible.
	void removePoll();

	// removes the currently selected poll from poll data.
	// Should not be used except within removePoll.
	void removePollFromPollData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing poll data.
	wxDataViewListCtrl* pollData = nullptr;
};