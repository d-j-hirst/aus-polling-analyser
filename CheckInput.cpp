#include "CheckInput.h"

CheckInput::CheckInput(wxWindow* parent, wxWindowID checkCtrlId, std::string labelText, int initialChoice, wxPoint topLeft,
	CheckChangeFunc checkChangeFunc, int labelWidth, int textInputWidth)
	: checkChangeFunc(checkChangeFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	checkBox = new wxCheckBox(parent, checkCtrlId, "", topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height));
	checkBox->SetValue(initialChoice);

	parent->Bind(wxEVT_CHECKBOX, &CheckInput::updateCheck, this, checkBox->GetId());
}

void CheckInput::updateCheck(wxCommandEvent & event)
{
	checkChangeFunc(event.GetInt());
}
