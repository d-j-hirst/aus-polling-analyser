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

#include "Seat.h"

class SeatsFrame;
class PollingProject;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditSeat_Base = 650, // To avoid mixing events with other frames.
	PA_EditSeat_ButtonID_OK,
	PA_EditSeat_TextBoxID_Name,
	PA_EditSeat_TextBoxID_PreviousName,
	PA_EditSeat_ComboBoxID_Incumbent,
	PA_EditSeat_ComboBoxID_Challenger,
	PA_EditSeat_ComboBoxID_Challenger2,
	PA_EditSeat_ComboBoxID_Region,
	PA_EditSeat_TextBoxID_Margin,
	PA_EditSeat_TextBoxID_LocalModifier,
	PA_EditSeat_TextBoxID_IncumbentOdds,
	PA_EditSeat_TextBoxID_ChallengerOdds,
	PA_EditSeat_TextBoxID_Challenger2Odds,
};

// *** EditSeatFrame ***
// Frame that allows the user to edit an already-existing seat
// or create a new one if isNewSeat is set to true.
class EditSeatFrame : public wxDialog
{
public:
	// isNewSeat: true if this dialog is for creating a new seat, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// seat: Seat data to be used if editing (has default values for creating a new seat).
	EditSeatFrame(bool isNewSeat, SeatsFrame* parent,
		PollingProject* project, Seat seat = Seat());

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextPreviousName(wxCommandEvent& event);

	// Calls upon the window to update the incumbent based on
	// the properties of the event.
	void updateComboBoxIncumbent(wxCommandEvent& event);

	// Calls upon the window to update the challenger based on
	// the properties of the event.
	void updateComboBoxChallenger(wxCommandEvent& event);

	// Calls upon the window to update the second challenger based on
	// the properties of the event.
	void updateComboBoxChallenger2(wxCommandEvent& event);

	// Calls upon the window to update the region based on
	// the properties of the event.
	void updateComboBoxRegion(wxCommandEvent& event);

	// Calls upon the window to update the margin based on
	// the result of the GetString() method of "event".
	void updateTextMargin(wxCommandEvent& event);

	// Calls upon the window to update the local 2PP modifier based on
	// the result of the GetString() method of "event".
	void updateTextLocalModifier(wxCommandEvent& event);

	// Calls upon the window to update the incumbent betting odds based on
	// the result of the GetString() method of "event".
	void updateTextIncumbentOdds(wxCommandEvent& event);

	// Calls upon the window to update the challenger betting odds based on
	// the result of the GetString() method of "event".
	void updateTextChallengerOdds(wxCommandEvent& event);

	// Calls upon the window to update the challenger betting odds based on
	// the result of the GetString() method of "event".
	void updateTextChallenger2Odds(wxCommandEvent& event);

	// Calls on the parent frame to initialize a new seat based on the
	// data in "newProjectData".
	void OnNewSeatReady();

	// Data container for the preliminary settings for the seat to be created.
	Seat seat;

	// Polling project pointer
	PollingProject* project;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* previousNameStaticText;
	wxTextCtrl* previousNameTextCtrl;
	wxStaticText* incumbentStaticText;
	wxComboBox* incumbentComboBox;
	wxStaticText* challengerStaticText;
	wxComboBox* challengerComboBox;
	wxStaticText* challenger2StaticText;
	wxComboBox* challenger2ComboBox;
	wxStaticText* regionStaticText;
	wxComboBox* regionComboBox;
	wxStaticText* marginStaticText;
	wxTextCtrl* marginTextCtrl;
	wxStaticText* localModifierStaticText;
	wxTextCtrl* localModifierTextCtrl;
	wxStaticText* incumbentOddsStaticText;
	wxTextCtrl* incumbentOddsTextCtrl;
	wxStaticText* challengerOddsStaticText;
	wxTextCtrl* challengerOddsTextCtrl;
	wxStaticText* challenger2OddsStaticText;
	wxTextCtrl* challenger2OddsTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	std::string lastMargin;
	std::string lastLocalModifier;
	std::string lastIncumbentOdds;
	std::string lastChallengerOdds;
	std::string lastChallenger2Odds;

	// A pointer to the parent frame.
	SeatsFrame* const parent;

	// Stores whether this dialog is for creating a new seat (true) or editing an existing one (false).
	bool isNewSeat;
};