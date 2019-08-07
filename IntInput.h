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

// Handles both a text box for general integer number input and also a static text label that
// describes what the input is for.
class IntInput {
public:
	typedef std::function<void(int)> TextChangeFunc;
	typedef std::function<float(int)> IntValidationFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	static float DefaultValidator(int a) { return a; };

	IntInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, int inputInt, wxPoint topLeft,
		TextChangeFunc textChangeFunc = [](int) {return; }, IntValidationFunc intValidationFunc = DefaultValidator,
		int labelWidth = DefaultLabelWidth, int textInputWidth = DefaultInputWidth);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateText(wxCommandEvent& event);

	std::string lastText;

	TextChangeFunc textChangeFunc;
	IntValidationFunc intValidationFunc;

	bool currentlyUpdating = false;

	wxWindow* parent;

	wxStaticText* staticText;
	wxTextCtrl* textCtrl;
};
