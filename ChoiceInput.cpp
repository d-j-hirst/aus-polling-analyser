#include "ChoiceInput.h"

ChoiceInput::ChoiceInput(wxWindow* parent, wxWindowID choiceCtrlId, std::string labelText, wxArrayString choices, int initialChoice, wxPoint topLeft,
	ChoiceChangeFunc choiceChangeFunc, int labelWidth, int textInputWidth)
	: choiceChangeFunc(choiceChangeFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	comboBox = new wxComboBox(parent, choiceCtrlId, choices[initialChoice], topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height), choices);
	comboBox->SetSelection(initialChoice);

	parent->Bind(wxEVT_COMBOBOX, &ChoiceInput::updateChoice, this, comboBox->GetId());
}

void ChoiceInput::updateChoice(wxCommandEvent & event)
{
	choiceChangeFunc(event.GetInt());
}
