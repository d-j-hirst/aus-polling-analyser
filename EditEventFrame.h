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

#include "EventsFrame.h"
#include "Debug.h"
#include "Event.h"

class EventsFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditEvent_ButtonID_OK,
	PA_EditEvent_TextBoxID_Name,
	PA_EditEvent_ComboBoxID_EventType,
	PA_EditEvent_DatePickerID_Date,
	PA_EditEvent_TextBoxID_Vote,
};

// *** EditEventFrame ***
// Frame that allows the user to edit an already-existing event
// or create a new one if isNewEvent is set to true.
class EditEventFrame : public wxDialog
{
public:
	// isNewEvent: true if this dialog is for creating a new event, false if it's for editing.
	// parent: Parent frame for this (must be an EventsFrame).
	// event: Event data to be used if editing (has default values for creating a new event).
	EditEventFrame(bool isNewEvent, EventsFrame* const parent,
		Event event = Event());

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the event data based on
	// the properties of the event.
	void updateComboBoxEventType(wxCommandEvent& event);

	// Calls upon the window to update the preliminary date data based on
	// the result of the GetDate() method of "event".
	void updateDatePicker(wxDateEvent& event);

	// Calls upon the window to update the election vote based on
	// the result of the GetFloat() method of "event".
	void updateTextVote(wxCommandEvent& event);

	// Data container for the preliminary settings for the party to be created.
	Event event;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* eventTypeStaticText;
	wxComboBox* eventTypeComboBox;
	wxStaticText* dateStaticText;
	wxDatePickerCtrl* datePicker;
	wxStaticText* voteStaticText;
	wxTextCtrl* voteTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	EventsFrame* const parent;

	// Stores whether this dialog is for creating a new party (true) or editing an existing one (false).
	bool isNewEvent;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastVote;
};