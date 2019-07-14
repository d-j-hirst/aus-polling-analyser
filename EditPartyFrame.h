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
#include <wx/clrpicker.h>

#include "PartiesFrame.h"
#include "Party.h"
#include "TextInput.h"

class PartiesFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// *** EditPartyFrame ***
// Frame that allows the user to edit an already-existing party
// or create a new one if isNewParty is set to true.
class EditPartyFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Party)> OkCallback;

	// function: whether this is for a new party or editing an existing party
	// callback: function to be called when this 
	EditPartyFrame(Function function, OkCallback callback,
		Party party = Party("Enter party name here", 50.0f, 0.0f, "Enter abbreviation here", Party::CountAsParty::None));

private:

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(std::string name);

	// Calls upon the window to update the preliminary preference flow data based on
	// the result of the GetFloat() method of "event".
	void updateTextPreferenceFlow(wxCommandEvent& event);

	// Calls upon the window to update the preliminary exhaust rate data based on
	// the result of the GetFloat() method of "event".
	void updateTextExhaustRate(wxCommandEvent& event);

	// Calls upon the window to update the preliminary abbreviation data based on
	// the result of the GetString() method of "event".
	void updateTextAbbreviation(wxCommandEvent& event);

	// Calls upon the window to update the preliminary abbreviation data based on
	// the result of the GetString() method of "event".
	void updateTextOfficialShortCodes(wxCommandEvent& event);

	// Calls upon the window to update the preliminary abbreviation data based on
	// the result of the GetString() method of "event".
	void updateColourPicker(wxColourPickerEvent& event);

	// Calls upon the window to update the "ideology" data based on
	// the properties of the event.
	void updateComboBoxIdeology(wxCommandEvent& event);

	// Calls upon the window to update the "ideology" data based on
	// the properties of the event.
	void updateComboBoxConsistency(wxCommandEvent& event);

	// Calls upon the window to update the preliminary booth colour multiplier data based on
	// the result of the GetString() method of "event".
	void updateBoothColourMult(wxCommandEvent& event);

	// Calls upon the window to update the "count-as-party" data based on
	// the properties of the event.
	void updateComboBoxCountAsParty(wxCommandEvent& event);

	// Calls upon the window to update the "supports party" data based on
	// the properties of the event.
	void updateComboBoxSupportsParty(wxCommandEvent& event);

	// Data container for the preliminary settings for the party to be created.
	Party party;

	std::unique_ptr<TextInput> nameTextInput;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* preferenceFlowStaticText;
	wxTextCtrl* preferenceFlowTextCtrl;
	wxStaticText* abbreviationStaticText;
	wxTextCtrl* abbreviationTextCtrl;
	wxStaticText* exhaustRateStaticText;
	wxTextCtrl* exhaustRateTextCtrl;
	wxStaticText* officialShortCodesStaticText;
	wxTextCtrl* officialShortCodesTextCtrl;
	wxStaticText* boothColourMultStaticText;
	wxTextCtrl* boothColourMultTextCtrl;
	wxStaticText* colourPickerText;
	wxColourPickerCtrl* colourPicker;
	wxStaticText* countAsPartyStaticText;
	wxComboBox* countAsPartyComboBox;
	wxStaticText* supportsPartyStaticText;
	wxComboBox* supportsPartyComboBox;
	wxStaticText* ideologyStaticText;
	wxComboBox* ideologyComboBox;
	wxStaticText* consistencyStaticText;
	wxComboBox* consistencyComboBox;
	wxButton* okButton;
	wxButton* cancelButton;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastPreferenceFlow;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastExhaustRate;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastBoothColourMult;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};