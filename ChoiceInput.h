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

// Handles both a combo box for a selection of choices and also a static text label that
// describes what the input is for.
class ChoiceInput {
public:
	typedef std::function<void(int)> ChoiceChangeFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	ChoiceInput(wxWindow* parent, wxWindowID choiceCtrlId, std::string labelText, wxArrayString choices, int initialChoice, wxPoint topLeft,
		ChoiceChangeFunc choiceChangeFunc = [](int) {return; }, int labelWidth = 150, int textInputWidth = 200);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateChoice(wxCommandEvent& event);

	ChoiceChangeFunc choiceChangeFunc;

	wxWindow* parent;
	wxStaticText* staticText;
	wxComboBox* comboBox;
};