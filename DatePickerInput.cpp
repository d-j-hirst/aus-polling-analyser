#include "DatePickerInput.h"

DatePickerInput::DatePickerInput(wxWindow* parent, wxWindowID datePickerCtrlId, std::string labelText, wxDateTime initialDate, wxPoint topLeft,
	DatePickerChangeFunc datePickerChangeFunc, int labelWidth, int textInputWidth)
	: datePickerChangeFunc(datePickerChangeFunc), parent(parent)
{
	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	datePicker = new wxDatePickerCtrl(parent, datePickerCtrlId, initialDate, topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height)
		, wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
											 // use wxDP_SPIN instead of wxDP_DROPDOWN);

	parent->Bind(wxEVT_DATE_CHANGED, &DatePickerInput::updateDatePicker, this, datePicker->GetId());
}

void DatePickerInput::updateDatePicker(wxDateEvent& event)
{
	datePickerChangeFunc(event.GetDate());
}