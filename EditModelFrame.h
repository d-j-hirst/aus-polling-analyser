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

#include "StanModel.h"

class DateInput;
class FloatInput;
class IntInput;
class TextInput;

// *** EditModelFrame ***
// Frame that allows the user to edit an already-existing model
// or create a new one if isNewModel is set to true.
class EditModelFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(StanModel)> OkCallback;

	// function: whether this is for a new model or editing an existing model
	// callback: function to be called when the OK button is pressed
	EditModelFrame(Function function, OkCallback callback, StanModel model = StanModel(""));

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createTermCodeInput(int& y);
	void createPartyCodesInput(int& y);
	void createMeanAdjustmentsInput(int& y);
	void createDeviationAdjustmentsInput(int& y);
	void createPreferenceFlowInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Holds the preliminary settings for the model to be created.
	StanModel model;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<TextInput> termCodeInput;
	std::unique_ptr<TextInput> partyCodesInput;
	std::unique_ptr<TextInput> meanAdjustmentsInput;
	std::unique_ptr<TextInput> deviationAdjustmentsInput;
	std::unique_ptr<TextInput> preferenceFlowInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};