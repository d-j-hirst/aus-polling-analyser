#include "EditModelFrame.h"

#include "DateInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 550, // To avoid mixing events with other frames.
	Ok,
	Name,
	TermCode,
	PartyCodes
};

EditModelFrame::EditModelFrame(Function function, OkCallback callback, StanModel model)
	: wxDialog(NULL, 0, (function == Function::New ? "New Model" : "Edit Model")),
	model(model), callback(callback)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditModelFrame::createControls(int & y)
{
	createNameInput(y);
	createTermCodeInput(y);
	createPartyCodesInput(y);

	createOkCancelButtons(y);
}

void EditModelFrame::createNameInput(int& y)
{
	auto nameCallback = [this](std::string s) -> void {model.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", model.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditModelFrame::createTermCodeInput(int& y)
{
	auto termCodeCallback = [this](std::string s) -> void {model.termCode = s; };
	termCodeInput.reset(new TextInput(this, ControlId::TermCode, "Term Code:", model.termCode, wxPoint(2, y), termCodeCallback));
	y += termCodeInput->Height + ControlPadding;
}

void EditModelFrame::createPartyCodesInput(int& y)
{
	auto partyCodesCallback = [this](std::string s) -> void {model.partyCodes = s; };
	partyCodesInput.reset(new TextInput(this, ControlId::PartyCodes, "Party Codes:", model.partyCodes, wxPoint(2, y), partyCodesCallback));
	y += partyCodesInput->Height + ControlPadding;
}


void EditModelFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditModelFrame::OnOK, this, Ok);
	y += TextInput::Height + ControlPadding;
}

void EditModelFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditModelFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {
	callback(model);
	Close();
}