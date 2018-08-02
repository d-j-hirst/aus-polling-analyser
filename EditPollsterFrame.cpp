#include "EditPollsterFrame.h"
#include "General.h"

EditPollsterFrame::EditPollsterFrame(bool isNewPollster, PollstersFrame* const parent, Pollster pollster)
	: wxDialog(NULL, 0, (isNewPollster ? "New Pollster" : "Edit Pollster")),
	isNewPollster(isNewPollster), parent(parent), pollster(pollster)
{
	// Generate the string for the preference flow.
	std::string weightString = formatFloat(pollster.weight, 4);

	// Store this string in case a text entry gives an error in the future.
	lastWeight = weightString;

	float currentHeight = 2;

	// Create the controls for the pollster name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight), wxSize(100, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditPollster_TextBoxID_Name, pollster.name, wxPoint(100, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	// Create the controls for the pollster weight.
	weightStaticText = new wxStaticText(this, 0, "Weight:", wxPoint(2, currentHeight), wxSize(100, 23));
	weightTextCtrl = new wxTextCtrl(this, PA_EditPollster_TextBoxID_Weight, weightString,
		wxPoint(100, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	// Create the controls for the pollster colour.
	colourStaticText = new wxStaticText(this, 0, "Colour:", wxPoint(2, currentHeight), wxSize(100, 23));
	colourColourPicker = new wxColourPickerCtrl(this, PA_EditPollster_ColourPickerID, pollster.colour,
		wxPoint(150, currentHeight - 2), wxSize(100, 30));

	currentHeight += 27;

	// Create the controls for choosing whether the pollster is used for polling calibration.
	calibrationStaticText = new wxStaticText(this, 0, "Use for calibration:", wxPoint(2, currentHeight), wxSize(100, 23));
	calibrationCheckBox = new wxCheckBox(this, PA_EditPollster_UseForCalibrationID, "", wxPoint(190, currentHeight - 2), wxSize(200, 23));

	calibrationCheckBox->SetValue(pollster.useForCalibration);

	currentHeight += 27;

	// Create the controls for choosing whether the pollster's polls are used in the initial path for models.
	calibrationStaticText = new wxStaticText(this, 0, "Ignore Initially:", wxPoint(2, currentHeight), wxSize(100, 23));
	calibrationCheckBox = new wxCheckBox(this, PA_EditPollster_IgnoreInitiallyID, "", wxPoint(190, currentHeight - 2), wxSize(200, 23));

	calibrationCheckBox->SetValue(pollster.ignoreInitially);

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditPollster_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditPollsterFrame::updateTextName, this, PA_EditPollster_TextBoxID_Name);
	Bind(wxEVT_TEXT, &EditPollsterFrame::updateTextWeight, this, PA_EditPollster_TextBoxID_Weight);
	Bind(wxEVT_COLOURPICKER_CHANGED, &EditPollsterFrame::updateColour, this, PA_EditPollster_ColourPickerID);
	Bind(wxEVT_CHECKBOX, &EditPollsterFrame::updateUseForCalibration, this, PA_EditPollster_UseForCalibrationID);
	Bind(wxEVT_CHECKBOX, &EditPollsterFrame::updateIgnoreInitially, this, PA_EditPollster_IgnoreInitiallyID);
	Bind(wxEVT_BUTTON, &EditPollsterFrame::OnOK, this, PA_EditPollster_ButtonID_OK);
}

void EditPollsterFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (isNewPollster) {
		// Get the parent frame to add a new pollster
		parent->OnNewPollsterReady(pollster);
	}
	else {
		// Get the parent frame to replace the old pollster with the current one
		parent->OnEditPollsterReady(pollster);
	}

	// Then close this dialog.
	Close();
}

void EditPollsterFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	pollster.name = event.GetString();
}

void EditPollsterFrame::updateTextWeight(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			pollster.weight = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		pollster.weight = f;

		// save this valid string in case the next text entry gives an error.
		lastWeight = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		weightTextCtrl->SetLabel(lastWeight);
	}
}

void EditPollsterFrame::updateColour(wxColourPickerEvent& event) {
	pollster.colour = event.GetColour().GetRGB();
}

void EditPollsterFrame::updateUseForCalibration(wxCommandEvent& event) {
	pollster.useForCalibration = event.IsChecked();
}

void EditPollsterFrame::updateIgnoreInitially(wxCommandEvent& event) {
	pollster.ignoreInitially = event.IsChecked();
}