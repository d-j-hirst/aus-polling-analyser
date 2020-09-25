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

#include "Party.h"

class CheckInput;
class ChoiceInput;
class ColourInput;
class FloatInput;
class PartyCollection;
class TextInput;

// *** EditPartyFrame ***
// Frame that allows the user to edit an already-existing party
// or create a new one.
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
	EditPartyFrame(Function function, OkCallback callback, PartyCollection const& parties,
		Party party = Party("Enter party name here", 50.0f, 0.0f, "Enter abbreviation here", Party::CountAsParty::None));

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createPreferenceFlowInput(int& y);
	void createExhaustRateInput(int& y);
	void createAbbreviationInput(int& y);
	void createShortCodesInput(int& y);
	void createColourInput(int& y);
	void createIdeologyInput(int& y);
	void createConsistencyInput(int& y);
	void createBoothColourMultInput(int& y);
	void createRelationInput(int& y);
	void createRelationTypeInput(int& y);
	void createIncludeInOthersInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Callbacks for the controls to update the party data.
	void updateShortCodes(std::string shortCodes);
	void updateRelation(int countAsParty);
	void updateRelationType(int supportsParty);

	// Data container for the preliminary settings for the party to be created.
	Party party;

	PartyCollection const& parties;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<FloatInput> preferenceFlowInput;
	std::unique_ptr<FloatInput> exhaustRateInput;
	std::unique_ptr<TextInput> abbreviationInput;
	std::unique_ptr<TextInput> shortCodesInput;
	std::unique_ptr<ColourInput> colourInput;
	std::unique_ptr<ChoiceInput> ideologyInput;
	std::unique_ptr<ChoiceInput> consistencyInput;
	std::unique_ptr<FloatInput> boothColourMultInput;
	std::unique_ptr<ChoiceInput> relationInput;
	std::unique_ptr<ChoiceInput> relationTypeInput;
	std::unique_ptr<CheckInput> includeInOthersInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};