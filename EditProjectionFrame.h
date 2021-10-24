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

#include "Projection.h"

class ChoiceInput;
class DateInput;
class FloatInput;
class IntInput;
class ModelCollection;
class TextInput;

// *** EditProjectionFrame ***
// Frame that allows the user to edit an already-existing projection
// or create a new one if isNewProjection is set to true.
class EditProjectionFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Projection::Settings)> OkCallback;

	// function: whether this is for a new projection or editing an existing projection
	// callback: function to be called when the OK button is pressed
	EditProjectionFrame(Function function, OkCallback callback, ModelCollection const& models, Projection::Settings projectionSettings);

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createModelInput(int& y);
	void createEndDateInput(int& y);
	void createNumIterationsInput(int& y);
	void createPossibleDatesInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	void updatePossibleDates(std::string possibleDates);
	
	// Holds the preliminary settings for the projection to be created.
	Projection::Settings projectionSettings;

	ModelCollection const& models;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<ChoiceInput> modelInput;
	std::unique_ptr<DateInput> endDateInput;
	std::unique_ptr<IntInput> numIterationsInput;
	std::unique_ptr<TextInput> possibleDatesInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};