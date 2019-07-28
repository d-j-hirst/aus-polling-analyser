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

#include "PollsFrame.h"
#include "Poll.h"
#include "PollsterCollection.h"

class ChoiceInput;
class DatePickerInput;
class FloatInput;
class PartyCollection;
class PollsterCollection;

// *** EditPollFrame ***
// Frame that allows the user to edit an already-existing poll
// or create a new one if isNewPoll is set to true.
class EditPollFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Poll)> OkCallback;

	// isNewPoll: true if this dialog is for creating a new poll, false if it's for editing.
	// parent: Parent frame for this (must be a PollsFrame).
	// project: The currently opened project.
	// poll: Poll data to be used if editing (has default values for creating a new poll).
	EditPollFrame(Function function, OkCallback callback, PartyCollection const& parties, PollsterCollection const& pollsters,
		Poll poll = Poll());

private:

	void createControls(int& y);

	void createPollsterInput(int& y);
	void createDateInput(int& y);
	void createReported2ppInput(int& y);
	void createRespondent2ppInput(int& y);
	void createPrimaryInputs(int& y);
	void createCalc2ppLabel(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// refreshes this poll's calculated two-party-preferred vote from the current primary votes.
	void refreshCalculated2PP();

	Poll poll; 

	PartyCollection const& parties;
	PollsterCollection const& pollsters;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	std::unique_ptr<ChoiceInput> pollsterInput;
	std::unique_ptr<DatePickerInput> dateInput;
	std::unique_ptr<FloatInput> reported2ppInput;
	std::unique_ptr<FloatInput> respondent2ppInput;
	wxStaticText* calc2ppStaticText;
	wxStaticText* calc2ppNumberText;
	
	// vectors for the primary votes since these may vary in number.
	std::vector<std::unique_ptr<FloatInput>> primaryVoteInput;

	wxButton* okButton;
	wxButton* cancelButton;

	OkCallback callback;
};