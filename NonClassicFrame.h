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
#include <wx/datectrl.h>
#include <wx/dateevt.h>

#include "ResultsFrame.h"
#include "Debug.h"
#include "Poll.h"
#include "PollingProject.h"

class PollsFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

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
	NonClassicFrame(ResultsFrame* const parent, PollingProject const* project, Seat* seat);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Removes the non-classic results from this seat
	void OnRemove(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary reported 2pp data based on
	// the current value in the two alternative party text boxes
	void updateTextEitherParty(wxCommandEvent& event);

	// Polling project pointer.
	PollingProject const* project;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* partyOneStaticText;
	wxComboBox* partyOneComboBox;
	wxStaticText* partyTwoStaticText;
	wxComboBox* partyTwoComboBox;
	wxStaticText* partyThreeStaticText;
	wxComboBox* partyThreeComboBox;
	wxStaticText* partyOneProbStaticTextLabel;
	wxStaticText* partyOneProbStaticText;
	wxStaticText* partyTwoProbStaticText;
	wxTextCtrl* partyTwoProbTextCtrl;
	wxStaticText* partyThreeProbStaticText;
	wxTextCtrl* partyThreeProbTextCtrl;

	wxButton* okButton;
	wxButton* removeButton;
	wxButton* cancelButton;

	Seat* seat;

	// A pointer to the parent frame.
	ResultsFrame* const parent;

	// Keeps the reported 2pp saved in case a text entry results in an invalid value.
	//std::string lastReported2pp;

	// Keeps the respondent 2pp saved in case a text entry results in an invalid value.
	//std::string lastRespondent2pp;

	// Keeps the calculated 2pp saved in case a text entry results in an invalid value.
	//std::string lastCalc2pp;
};