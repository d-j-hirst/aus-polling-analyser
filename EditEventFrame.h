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

#include "Event.h"

class ChoiceInput;
class DateInput;
class FloatInput;
class TextInput;

// *** EditEventFrame ***
// Frame that allows the user to edit an already-existing event
// or create a new one if isNewEvent is set to true.
class EditEventFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Event)> OkCallback;

	// event: Event data to be used if editing (has default values for creating a new event).
	EditEventFrame(Function function, OkCallback callback,
		Event event = Event());

private:
	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createTypeInput(int& y);
	void createDateInput(int& y);
	void createVoteInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent&);

	// Data container for the preliminary settings for the party to be created.
	Event event;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<ChoiceInput> typeInput;
	std::unique_ptr<DateInput> dateInput;
	std::unique_ptr<FloatInput> voteInput;

	wxButton* okButton;
	wxButton* cancelButton;

	OkCallback callback;
};