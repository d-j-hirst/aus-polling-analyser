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

#include "PollstersFrame.h"
#include "Debug.h"
#include "Pollster.h"

class PollstersFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditPollster_ButtonID_OK,
	PA_EditPollster_TextBoxID_Name,
	PA_EditPollster_TextBoxID_Weight,
	PA_EditPollster_ColourPickerID,
	PA_EditPollster_UseForCalibrationID,
	PA_EditPollster_IgnoreInitiallyID,
};

// *** EditPollsterFrame ***
// Frame that allows the user to edit an already-existing pollster
// or create a new one if isNewPollster is set to true.
class EditPollsterFrame : public wxDialog
{
public:
	// isNewPollster: true if this dialog is for creating a new pollster, false if it's for editing.
	// parent: Parent frame for this (must be a PollstersFrame).
	// pollster: Pollster data to be used if editing (has default values for creating a new pollster).
	EditPollsterFrame(bool isNewPollster, PollstersFrame* const parent,
		Pollster pollster = Pollster("Enter pollster name here", 1.0f, 0, false, false));

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the preliminary weight data based on
	// the result of the GetFloat() method of "event".
	void updateTextWeight(wxCommandEvent& event);

	// Calls upon the window to update the preliminary weight data based on
	// the result of the GetFloat() method of "event".
	void updateColour(wxColourPickerEvent& event);

	// Calls upon the window to update the preliminary weight data based on
	// the result of the GetFloat() method of "event".
	void updateUseForCalibration(wxCommandEvent& event);

	// Calls upon the window to update the preliminary weight data based on
	// the result of the GetFloat() method of "event".
	void updateIgnoreInitially(wxCommandEvent& event);

	// Data container for the preliminary settings for the pollster to be created.
	Pollster pollster;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* weightStaticText;
	wxTextCtrl* weightTextCtrl;
	wxStaticText* colourStaticText;
	wxColourPickerCtrl* colourColourPicker;
	wxStaticText* calibrationStaticText;
	wxCheckBox* calibrationCheckBox;
	wxStaticText* ignoreInitiallyStaticText;
	wxTextCtrl* ignoreInitiallyTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	PollstersFrame* const parent;

	// Stores whether this dialog is for creating a new pollster (true) or editing an existing one (false).
	bool isNewPollster;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastWeight;
};