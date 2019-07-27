#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "InputGeneral.h"

#include <wx/datectrl.h>
#include <wx/dateevt.h>

// Handles both a date picker and also a static text label that
// describes what the input is for.
class DatePickerInput {
public:
	typedef std::function<void(wxDateTime)> DatePickerChangeFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	DatePickerInput(wxWindow* parent, wxWindowID datePickerCtrlId, std::string labelText, wxDateTime initialDate, wxPoint topLeft,
		DatePickerChangeFunc datePickerChangeFunc = [](wxDateTime) {return; }, int labelWidth = DefaultLabelWidth, int textInputWidth = DefaultInputWidth);

private:

	void updateDatePicker(wxDateEvent& event);

	DatePickerChangeFunc datePickerChangeFunc;

	wxWindow* parent;
	wxStaticText* staticText;
	wxDatePickerCtrl* datePicker;
};