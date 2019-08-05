#include "EditEventFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "FloatInput.h"
#include "General.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Ok,
	Name,
	Type,
	Date,
	Vote,
};

EditEventFrame::EditEventFrame(Function function, OkCallback callback, Event event)
	: wxDialog(NULL, 0, (function == Function::New ? "New Event" : "Edit Event")),
	callback(callback), event(event)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditEventFrame::createControls(int & y)
{
	createNameInput(y);
	createTypeInput(y);
	createDateInput(y);
	createVoteInput(y);
	createOkCancelButtons(y);
}

void EditEventFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {event.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", event.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditEventFrame::createTypeInput(int & y)
{
	wxArrayString typeArray;
	typeArray.push_back("None");
	typeArray.push_back("Election");
	typeArray.push_back("Discontinuity");

	auto typeCallback = [this](int i) -> void {event.eventType = EventType(i); };
	typeInput.reset(new ChoiceInput(this, ControlId::Type, "Event Type:", typeArray, int(event.eventType), wxPoint(2, y), typeCallback));
	y += nameInput->Height + ControlPadding;
}

void EditEventFrame::createDateInput(int & y)
{
	auto dateCallback = [this](wxDateTime d) {event.date = d; };
	dateInput.reset(new DateInput(this, ControlId::Date, "Date: ", event.date,
		wxPoint(2, y), dateCallback));
	y += dateInput->Height + ControlPadding;
}

void EditEventFrame::createVoteInput(int & y)
{
	auto voteCallback = [this](float f) -> void {event.vote = f; };
	auto voteValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	voteInput.reset(new FloatInput(this, ControlId::Vote, "Vote (for elections):", event.vote,
		wxPoint(2, y), voteCallback, voteValidator));
	y += voteInput->Height + ControlPadding;
}

void EditEventFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditEventFrame::OnOK, this, ControlId::Ok);
	y += TextInput::Height + ControlPadding;
}

void EditEventFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditEventFrame::OnOK(wxCommandEvent&) {
	callback(event);
	Close();
}