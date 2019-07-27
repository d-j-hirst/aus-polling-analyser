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

// Handles both a text box for general text input and also a static text label that
// describes what the input is for.
class TextInput {
public:
	typedef std::function<void(std::string)> TextChangeFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	TextInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, std::string inputText, wxPoint topLeft,
		TextChangeFunc textChangeFunc = [](std::string) {return;}, int labelWidth = DefaultLabelWidth, int textInputWidth = DefaultInputWidth);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateText(wxCommandEvent& event);

	TextChangeFunc textChangeFunc;

	wxWindow* parent;
	wxStaticText* staticText;
	wxTextCtrl* textCtrl;
};
