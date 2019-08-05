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

#include "Model.h"

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

	typedef std::function<void(Model)> OkCallback;

	// function: whether this is for a new party or editing an existing party
	// callback: function to be called when this 
	EditModelFrame(Function function, OkCallback callback,
		Model model = Model());

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createNumIterationsInput(int& y);
	void createVoteTimeMultiplierInput(int& y);
	void createHouseEffectTimeMultiplierInput(int& y);
	void createCalibrationFirstPartyBiasInput(int& y);
	void createStartDateInput(int& y);
	void createEndDateInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Data container for the preliminary settings for the model to be created.
	Model model;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<IntInput> numIterationsInput;
	std::unique_ptr<FloatInput> voteTimeMultiplierInput;
	std::unique_ptr<FloatInput> houseEffectTimeMultiplierInput;
	std::unique_ptr<FloatInput> calibrationFirstPartyBiasInput;
	std::unique_ptr<DateInput> startDateInput;
	std::unique_ptr<DateInput> endDateInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};