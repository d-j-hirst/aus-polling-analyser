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
#include "Party.h"
#include "TextInput.h"

class ColourInput;
class FloatInput;
class TextInput;

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

	// Callbacks for the controls to update the party data.
	void updateName(std::string name);
	void updatePreferenceFlow(float preferenceFlow);
	void updateExhaustRate(float exhuastRate);
	void updateAbbreviation(std::string abbreviation);
	void updateShortCodes(std::string shortCodes);
	void updateColour(wxColour colour);

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

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<FloatInput> preferenceFlowInput;
	std::unique_ptr<FloatInput> exhaustRateInput;
	std::unique_ptr<TextInput> abbreviationInput;
	std::unique_ptr<TextInput> shortCodesInput;
	std::unique_ptr<ColourInput> colourInput;

	wxStaticText* boothColourMultStaticText;
	wxTextCtrl* boothColourMultTextCtrl;
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
	std::string lastBoothColourMult;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};