#include "ColourInput.h"

ColourInput::ColourInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, wxColour initialColour,
	wxPoint topLeft, ColourChangeFunc colourChangeFunc, int labelWidth, int textInputWidth)
	: colourChangeFunc(colourChangeFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	colourCtrl = new wxColourPickerCtrl(parent, textCtrlId, initialColour, topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height));

	parent->Bind(wxEVT_COLOURPICKER_CHANGED, &ColourInput::updateColour, this, colourCtrl->GetId());
}

void ColourInput::updateColour(wxColourPickerEvent & event)
{
	colourChangeFunc(event.GetColour());
}
