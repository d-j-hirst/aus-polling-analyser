#include "GeneralSettingsFrame.h"

#include "TextInput.h"
#include "General.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Ok,
	ElectionName
};

GeneralSettingsFrame::GeneralSettingsFrame(OkCallback okCallback, GeneralSettingsData data)
	: wxDialog(NULL, 0, "General Settings", wxDefaultPosition, wxSize(500, 400)),
	okCallback(okCallback), data(data)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void GeneralSettingsFrame::createControls(int& y)
{
	createElectionNameInput(y);

	createOkCancelButtons(y);
}

void GeneralSettingsFrame::createElectionNameInput(int& y)
{
	auto callback = [this](std::string s) {data.electionName = s; };
	electionNameInput.reset(new TextInput(this, ControlId::ElectionName, "Election name: ", data.electionName,
		wxPoint(2, y), callback));
	y += electionNameInput->Height + ControlPadding;
}

void GeneralSettingsFrame::createOkCancelButtons(int& y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &GeneralSettingsFrame::OnOK, this, ControlId::Ok);
	y += TextInput::Height + ControlPadding;
}

void GeneralSettingsFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void GeneralSettingsFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	okCallback(data);

	// Then close this dialog.
	Close();
}