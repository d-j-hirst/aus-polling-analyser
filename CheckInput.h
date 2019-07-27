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

// Handles both a check box for a toggled choice and also a static text label that
// describes what the input is for.
class CheckInput {
public:
	typedef std::function<void(int)> CheckChangeFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	CheckInput(wxWindow* parent, wxWindowID choiceCtrlId, std::string labelText, int initialChoice, wxPoint topLeft,
		CheckChangeFunc checkChangeFunc = [](int) {return; }, int labelWidth = DefaultLabelWidth, int textInputWidth = DefaultInputWidth);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetInt() method of "event".
	void updateCheck(wxCommandEvent& event);

	CheckChangeFunc checkChangeFunc;

	wxWindow* parent;
	wxStaticText* staticText;
	wxCheckBox* checkBox;
};