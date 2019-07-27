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

#include "PollstersFrame.h"
#include "Pollster.h"

class CheckInput;
class ColourInput;
class FloatInput;
class TextInput;

// *** EditPollsterFrame ***
// Frame that allows the user to edit an already-existing pollster
// or create a new one if isNewPollster is set to true.
class EditPollsterFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Pollster)> OkCallback;

	// pollster: Pollster data to be used if editing (has default values for creating a new pollster).
	EditPollsterFrame(Function function, OkCallback callback,
		Pollster pollster = Pollster("Enter pollster name here", 1.0f, 0, false, false));

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createWeightInput(int& y);
	void createColourInput(int& y);
	void createCalibrationInput(int& y);
	void createIgnoreInitiallyInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Data container for the preliminary settings for the pollster to be created.
	Pollster pollster;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<FloatInput> weightInput;
	std::unique_ptr<ColourInput> colourInput;
	std::unique_ptr<CheckInput> calibrationInput;
	std::unique_ptr<CheckInput> ignoreInitiallyInput;

	wxButton* okButton;
	wxButton* cancelButton;

	OkCallback callback;
};