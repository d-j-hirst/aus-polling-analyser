#include "DateInput.h"

#include "Log.h"

DateInput::DateInput(wxWindow* parent, wxWindowID datePickerCtrlId, std::string labelText, wxDateTime initialDate, wxPoint topLeft,
	DatePickerChangeFunc datePickerChangeFunc, int labelWidth, int textInputWidth)
	: datePickerChangeFunc(datePickerChangeFunc), parent(parent)
{

	staticText = new wxStaticText(parent, 0, labelText, topLeft, wxSize(labelWidth, Height));
	datePicker = new wxDatePickerCtrl(parent, datePickerCtrlId, initialDate, topLeft + wxSize(labelWidth, 0), wxSize(textInputWidth, Height)
		, wxDP_DROPDOWN | wxDP_SHOWCENTURY | wxDP_ALLOWNONE); // not supported under OSX/Cocoa
											 // use wxDP_SPIN instead of wxDP_DROPDOWN);
	datePicker->SetValue(initialDate);

	parent->Bind(wxEVT_DATE_CHANGED, &DateInput::updateDatePicker, this, datePicker->GetId());
}

void DateInput::updateDatePicker(wxDateEvent& event)
{
	datePickerChangeFunc(event.GetDate());
}