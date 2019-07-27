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
class FloatInput {
public:
	typedef std::function<void(float)> TextChangeFunc;
	typedef std::function<float(float)> FloatValidationFunc;

	// public because the calling frame will want to know what height the control is
	static constexpr int Height = InputControlHeight;

	static float DefaultValidator(float a) {return std::clamp(a, 0.0f, 100.0f); };

	FloatInput(wxWindow* parent, wxWindowID textCtrlId, std::string labelText, float inputFloat, wxPoint topLeft,
		TextChangeFunc textChangeFunc = [](float) {return; }, FloatValidationFunc floatValidationFunc = DefaultValidator,
		float nullValue = std::numeric_limits<float>::lowest(), int labelWidth = DefaultLabelWidth, int textInputWidth = DefaultInputWidth, int initialDecimalPlaces = 3);

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateText(wxCommandEvent& event);

	std::string lastText;

	TextChangeFunc textChangeFunc;
	FloatValidationFunc floatValidationFunc;

	bool currentlyUpdating = false;

	float nullValue;

	wxWindow* parent;

	wxStaticText* staticText;
	wxTextCtrl* textCtrl;
};
