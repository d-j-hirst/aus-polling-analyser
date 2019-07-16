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

#include <wx/clrpicker.h>

// Handles both a colour picker for some colour input and also a static text label that
// describes what the input is for.
class ColourInput {
public:
	typedef std::function<void(wxColour)> ColourChangeFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	ColourInput(wxWindow* parent, wxWindowID colourCtrlId, std::string labelText, wxColour initialColour, wxPoint topLeft,
		ColourChangeFunc colourChangeFunc = [](wxColour) {return; }, int labelWidth = 150, int textInputWidth = 200);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateColour(wxColourPickerEvent& event);

	ColourChangeFunc colourChangeFunc;

	wxWindow* parent;
	wxStaticText* staticText;
	wxColourPickerCtrl* colourCtrl;
};
