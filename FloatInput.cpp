#include "FloatInput.h"

#include "General.h"

FloatInput::FloatInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, float initialValue, wxPoint topLeft,
	TextChangeFunc textChangeFunc, FloatValidationFunc floatValidationFunc, int labelWidth, int textInputWidth, int initialDecimalPlaces)
	: textChangeFunc(textChangeFunc), floatValidationFunc(floatValidationFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	textCtrl = new wxTextCtrl(parent, textCtrlId, formatFloat(initialValue, initialDecimalPlaces), topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height));

	parent->Bind(wxEVT_TEXT, &FloatInput::updateText, this, textCtrl->GetId());
}

void FloatInput::updateText(wxCommandEvent & event)
{
	if (currentlyUpdating) return;
	currentlyUpdating = true;
	float value = 0.0f;
	std::string str = event.GetString().ToStdString();
	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			value = 0.0f;
		}
		else if (str == "-") {
			value = 0.0f;
		}
		else {
			value = std::stof(str);
			float validated = floatValidationFunc(value);
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
