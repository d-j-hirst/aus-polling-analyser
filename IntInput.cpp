#include "IntInput.h"

#include "General.h"

IntInput::IntInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, int initialValue, wxPoint topLeft,
	TextChangeFunc textChangeFunc, IntValidationFunc intValidationFunc, int nullValue,
	int labelWidth, int textInputWidth, int initialDecimalPlaces)
	: textChangeFunc(textChangeFunc), intValidationFunc(intValidationFunc), nullValue(nullValue), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	textCtrl = new wxTextCtrl(parent, textCtrlId, formatFloat(initialValue, initialDecimalPlaces, false, nullValue),
		topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height));

	parent->Bind(wxEVT_TEXT, &IntInput::updateText, this, textCtrl->GetId());
}

void IntInput::updateText(wxCommandEvent & event)
{
	if (currentlyUpdating) return;
	currentlyUpdating = true;
	int value = nullValue;
	std::string str = event.GetString().ToStdString();
	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {

		// An empty string can be interpreted as zero or the preset null value, so it's ok.
		if (str.empty()) {
			value = nullValue;
		}
		else if (str == "-") {
			value = 0;
		}
		else {
			value = std::stoi(str);
			int validated = intValidationFunc(value);
			if (validated != value) {
				value = validated;
				textCtrl->SetLabel(std::to_string(value));
			}
		}
		// save this valid string in case the next text entry gives an error.
		lastText = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		textCtrl->SetLabel(lastText);
		currentlyUpdating = false;
		return;
	}
	textChangeFunc(value);
	currentlyUpdating = false;
}
