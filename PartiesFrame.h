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
#include "wx/bookctrl.h"
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"
#include "Debug.h"
#include "EditPartyFrame.h"
#include "PartySettingsFrame.h"

class ProjectFrame;
struct PartySettingsData;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_PartiesFrame_Base = 200, // To avoid mixing events with other frames.
	PA_PartiesFrame_FrameID,
	PA_PartiesFrame_DataViewID,
	PA_PartiesFrame_NewPartyID,
	PA_PartiesFrame_EditPartyID,
	PA_PartiesFrame_RemovePartyID,
	PA_PartiesFrame_PartySettingsID,
};

enum PartyColumnsEnum {
	PartyColumn_Name,
	PartyColumn_PreferenceFlow,
	PartyColumn_Abbreviation,
	PartyColumn_NumColumns,
};

// *** PartiesFrame ***
// Frame that allows the user to add/delete/modify political party data.
class PartiesFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	PartiesFrame(ProjectFrame* const parent, PollingProject* project);

	// Calls on the frame to create a new party based on "Party".
	void OnNewPartyReady(Party& party);

	// Calls on the frame to edit the currently selected party based on "Party".
	void OnEditPartyReady(Party& party);

	// Calls on the frame to create a new party based on "Party".
	void OnPartySettingsReady(PartySettingsData& partySettingsData);

private:

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

	// replaces the currently selected party with "party" in party data.
	// Should not be used except within replaceParty.
	void replacePartyInPartyData(Party party);

	// does everything required to remove the currently selected party, if possible.
	void removeParty();

	// removes the currently selected party from party data.
	// Should not be used except within removeParty.
	void removePartyFromPartyData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing party data.
	wxDataViewListCtrl* partyData = nullptr;
};