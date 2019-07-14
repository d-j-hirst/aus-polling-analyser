#include "TextInput.h"

TextInput::TextInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, std::string inputText, wxPoint topLeft, TextChangeFunc textChangeFunc, int labelWidth, int textInputWidth)
	: textChangeFunc(textChangeFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	textCtrl = new wxTextCtrl(parent, textCtrlId, inputText, topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height));

	parent->Bind(wxEVT_TEXT, &TextInput::updateText, this, textCtrl->GetId());
}

void TextInput::updateText(wxCommandEvent & event)
{
	textChangeFunc(std::string(event.GetString()));
}
