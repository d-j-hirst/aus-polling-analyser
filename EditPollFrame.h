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

#include "PollsFrame.h"
#include "Debug.h"
#include "Poll.h"
#include "PollingProject.h"

class PollsFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditPoll_ButtonID_OK,
	PA_EditPoll_ComboBoxID_Pollster,
	PA_EditPoll_DatePickerID_Date,
	PA_EditPoll_TextBoxID_Reported2pp,
	PA_EditPoll_TextBoxID_Respondent2pp,
	PA_EditPoll_TextBoxID_Calc2pp,
	PA_EditPoll_TextBoxID_Primary, // this must be last as all the primary vote text boxes will follow ...
};

// *** EditPollFrame ***
// Frame that allows the user to edit an already-existing poll
// or create a new one if isNewPoll is set to true.
class EditPollFrame : public wxDialog
{
public:
	// isNewPoll: true if this dialog is for creating a new poll, false if it's for editing.
	// parent: Parent frame for this (must be a PollsFrame).
	// project: The currently opened project.
	// poll: Poll data to be used if editing (has default values for creating a new poll).
	EditPollFrame(bool isNewPoll, PollsFrame* const parent, PollingProject const* project,
		Poll poll = Poll(nullptr, wxDateTime::Now(), 50.0f, -1.0f, -1.0f));

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the pollster data based on
	// the properties of the event.
	void updateComboBoxPollster(wxCommandEvent& event);

	// Calls upon the window to update the preliminary reported 2pp data based on
	// the result of the GetFloat() method of "event".
	void updateTextReported2pp(wxCommandEvent& event);

	// Calls upon the window to update the preliminary respondent-allocated 2pp data based on
	// the result of the GetFloat() method of "event".
	void updateTextRespondent2pp(wxCommandEvent& event);

	// Calls upon the window to update the preliminary respondent-allocated 2pp data based on
	// the result of the GetFloat() method of "event".
	void updateTextPrimary(wxCommandEvent& event);

	// Calls upon the window to update the preliminary date data based on
	// the result of the GetDate() method of "event".
	void updateDatePicker(wxDateEvent& event);

	// Calls on the frame to initialize a new project based on the
	// data in "newProjectData".
	void OnNewPollReady(NewProjectData& newProjectData);

	// refreshes this poll's calculated two-party-preferred vote from the current primary votes.
	void refreshCalculated2PP();

	// Data container for the preliminary settings for the poll to be created.
	Poll poll; 
	
	// Polling project pointer. Only used for accessing pollster names.
	PollingProject const* project;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* pollsterStaticText;
	wxComboBox* pollsterComboBox;
	wxStaticText* dateStaticText;
	wxDatePickerCtrl* datePicker;
	wxStaticText* reported2ppStaticText;
	wxTextCtrl* reported2ppTextCtrl;
	wxStaticText* respondent2ppStaticText;
	wxTextCtrl* respondent2ppTextCtrl;
	wxStaticText* calc2ppStaticText;
	wxStaticText* calc2ppNumberText;
	
	// vectors for the primary votes since these may vary in number.
	std::vector<wxStaticText*> primaryStaticText;
	std::vector<wxTextCtrl*> primaryTextCtrl;

	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	PollsFrame* const parent;

	// Stores whether this dialog is for creating a new poll (true) or editing an existing one (false).
	bool isNewPoll;

	// Keeps the reported 2pp saved in case a text entry results in an invalid value.
	std::string lastReported2pp;

	// Keeps the respondent 2pp saved in case a text entry results in an invalid value.
	std::string lastRespondent2pp;

	// Keeps the calculated 2pp saved in case a text entry results in an invalid value.
	std::string lastCalc2pp;

	// Keeps the primary votes saved in case a text entry results in an invalid value.
	std::vector<std::string> lastPrimary;
};