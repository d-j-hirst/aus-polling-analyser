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

#include <sstream>
#include <wx/valnum.h>

#include "PartiesFrame.h"
#include "Debug.h"
#include "Party.h"

class PartiesFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditParty_ButtonID_OK,
	PA_EditParty_TextBoxID_Name,
	PA_EditParty_TextBoxID_PreferenceFlow,
	PA_EditParty_TextBoxID_ExhaustRate,
	PA_EditParty_TextBoxID_Abbreviation,
	PA_EditParty_ComboBoxID_CountAsParty,
};

// *** EditPartyFrame ***
// Frame that allows the user to edit an already-existing party
// or create a new one if isNewParty is set to true.
class EditPartyFrame : public wxDialog
{
public:
	// isNewParty: true if this dialog is for creating a new party, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// party: Party data to be used if editing (has default values for creating a new party).
	EditPartyFrame(bool isNewParty, PartiesFrame* const parent,
		Party party = Party("Enter party name here", 50.0f, 0.0f, "Enter abbreviation here", Party::CountAsParty::None));

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the preliminary preference flow data based on
	// the result of the GetFloat() method of "event".
	void updateTextPreferenceFlow(wxCommandEvent& event);

	// Calls upon the window to update the preliminary exhaust rate data based on
	// the result of the GetFloat() method of "event".
	void updateTextExhaustRate(wxCommandEvent& event);

	// Calls upon the window to update the preliminary abbreviation data based on
	// the result of the GetString() method of "event".
	void updateTextAbbreviation(wxCommandEvent& event);

	// Calls upon the window to update the "count-as-party" data based on
	// the properties of the event.
	void updateComboBoxCountAsParty(wxCommandEvent& event);

	// Calls on the frame to initialize a new project based on the
	// data in "newProjectData".
	void OnNewPartyReady(NewProjectData& newProjectData);

	// Data container for the preliminary settings for the party to be created.
	Party party;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* preferenceFlowStaticText;
	wxTextCtrl* preferenceFlowTextCtrl;
	wxStaticText* abbreviationStaticText;
	wxTextCtrl* abbreviationTextCtrl;
	wxStaticText* exhaustRateStaticText;
	wxTextCtrl* exhaustRateTextCtrl;
	wxStaticText* countAsPartyStaticText;
	wxComboBox* countAsPartyComboBox;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	PartiesFrame* const parent;

	// Stores whether this dialog is for creating a new party (true) or editing an existing one (false).
	bool isNewParty;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastPreferenceFlow;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastExhaustRate;
};