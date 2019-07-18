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

#include <memory>

class ProjectFrame;
struct PartySettingsData;

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political party data.
class PartiesFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	PartiesFrame(ProjectFrame::Refresher refresher, PollingProject* project);

private:

	// For the new party dialog to callback once the user has clicked OK.
	// Adds the new party to the project and updates the data panel.
	void newPartyCallback(Party party);

	// For the edit party dialog to callback once the user has clicked OK.
	// Replaces the existing party with the new party and updates the data panel
	void editPartyCallback(Party party);

	// For the party settings dialog to callback once the user has clicked OK.
	// Changes the relevant party settings in the project
	void partySettingsCallback(PartySettingsData partySettingsData);

	// Creates the tool bar and its icons
	void setupToolBar();

	// Create the data table from scratch
	void setupDataTable();

	// refresh the (already existing) data table
	void refreshDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new party.
	void OnNewParty(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing party.
	void OnEditParty(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing party.
	void OnRemoveParty(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing party.
	void OnPartySettings(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the party "party".
	void addParty(Party party);

	// adds "party" to party data. Should not be used except within addParty.
	void addPartyToPartyData(Party party);

	// does everything required to replace the currently selected party with "party".
	void replaceParty(Party party);

	// does everything required to remove the currently selected party, if possible.
	void removeParty();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher const refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing party data.
	wxDataViewListCtrl* partyData = nullptr;
};