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

class ChoiceInput;
class FloatInput;
class PartyCollection;

class Seat;

// *** NonClassicFrame ***
// Frame that allows the user to add a subjective probability estimate
// for a non-classic 2pp seat
class NonClassicFrame : public wxDialog
{
public:
	// isNewPoll: true if this dialog is for creating a new poll, false if it's for editing.
	// parent: Parent frame for this (must be a PollsFrame).
	// project: The currently opened project.
	// poll: Poll data to be used if editing (has default values for creating a new poll).
	NonClassicFrame(PartyCollection const& parties, Seat& seat);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent&);

	// Removes the non-classic results from this seat
	void OnRemove(wxCommandEvent&);

private:

	void createControls(int& y);

	void createPartyOneInput(int& y);
	void createPartyTwoInput(int& y);
	void createPartyThreeInput(int& y);
	void createPartyOneProbText(int& y);
	void createPartyTwoProbInput(int& y);
	void createPartyThreeProbInput(int& y);

	void createButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to update the preliminary reported 2pp data based on
	// the current value in the two alternative party text boxes
	void updatePartyOneProbText();

	wxArrayString collectPartyStrings();

	PartyCollection const& parties;
	Seat& seat;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	std::unique_ptr<ChoiceInput> partyOneInput;
	std::unique_ptr<ChoiceInput> partyTwoInput;
	std::unique_ptr<ChoiceInput> partyThreeInput;
	wxStaticText* partyOneProbLabel;
	wxStaticText* partyOneProbValue;
	std::unique_ptr<FloatInput> partyTwoProbInput;
	std::unique_ptr<FloatInput> partyThreeProbInput;

	wxButton* okButton;
	wxButton* removeButton;
	wxButton* cancelButton;
};