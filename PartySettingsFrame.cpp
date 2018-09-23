#include "PartySettingsFrame.h"
#include "General.h"

PartySettingsFrame::PartySettingsFrame(PartiesFrame* const parent, PartySettingsData partySettingsData)
	: wxDialog(NULL, 0, "General settings for political parties", wxDefaultPosition, wxSize(420, 122)),
	parent(parent), partySettingsData(partySettingsData)
{
	// Generate the string for the preference flow.
	std::string othersPreferenceFlowString = formatFloat(partySettingsData.othersPreferenceFlow, 3);

	// Store this string in case a text entry gives an error in the future.
	lastOthersPreferenceFlow = othersPreferenceFlowString;

	// Generate the string for the exhaust rate.
	std::string othersExhaustRateString = formatFloat(partySettingsData.othersExhaustRate, 3);

	// Store this string in case a text entry gives an error in the future.
	lastOthersExhaustRate = othersExhaustRateString;

	float currentHeight = 2;

	// Create the controls for the "others" preference flow
	othersPreferenceFlowStaticText = new wxStaticText(this, 0, """Others"" Preference Flow:", wxPoint(2, currentHeight), wxSize(180, 23));
	othersPreferenceFlowTextCtrl = new wxTextCtrl(this, PA_PartySettings_TextBoxID_OthersPreferenceFlow, othersPreferenceFlowString,
		wxPoint(180, currentHeight), wxSize(220, 23));

	currentHeight += 27;

	// Create the controls for the "others" preference flow
	othersExhaustRateStaticText = new wxStaticText(this, 0, """Others"" Exhaust Rate:", wxPoint(2, currentHeight), wxSize(180, 23));
	othersExhaustRateTextCtrl = new wxTextCtrl(this, PA_PartySettings_TextBoxID_OthersExhaustRate, othersExhaustRateString,
		wxPoint(180, currentHeight), wxSize(220, 23));

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_PartySettings_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &PartySettingsFrame::updateTextOthersPreferenceFlow, this, PA_PartySettings_TextBoxID_OthersPreferenceFlow);
	Bind(wxEVT_TEXT, &PartySettingsFrame::updateTextOthersExhaustRate, this, PA_PartySettings_TextBoxID_OthersExhaustRate);
	Bind(wxEVT_BUTTON, &PartySettingsFrame::OnOK, this, PA_PartySettings_ButtonID_OK);
}

void PartySettingsFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	// Get the parent frame to actually update the party settings
	parent->OnPartySettingsReady(partySettingsData);

	// Then close this dialog.
	Close();
}
void PartySettingsFrame::updateTextOthersPreferenceFlow(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			partySettingsData.othersPreferenceFlow = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		partySettingsData.othersPreferenceFlow = f;

		// save this valid string in case the next text entry gives an error.
		lastOthersPreferenceFlow = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		othersPreferenceFlowTextCtrl->SetLabel(lastOthersPreferenceFlow);
	}
}

void PartySettingsFrame::updateTextOthersExhaustRate(wxCommandEvent & event)
{

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			partySettingsData.othersExhaustRate = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		partySettingsData.othersExhaustRate = f;

		// save this valid string in case the next text entry gives an error.
		lastOthersExhaustRate = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		othersExhaustRateTextCtrl->SetLabel(lastOthersExhaustRate);
	}
}
