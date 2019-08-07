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

	typedef std::function<void(Projection)> OkCallback;

	// isNewProjection: true if this dialog is for creating a new projection, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// projection: Projection data to be used if editing (has default values for creating a new projection).
	EditProjectionFrame(Function function, OkCallback callback, ModelCollection const& models, Projection projection = Projection());

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createModelInput(int& y);
	void createEndDateInput(int& y);
	void createNumIterationsInput(int& y);
	void createVoteLossInput(int& y);
	void createDailyChangeInput(int& y);
	void createInitialChangeInput(int& y);
	void createNumElectionsInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));
	
	// Holds the preliminary settings for the projection to be created.
	Projection projection;

	ModelCollection const& models;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<ChoiceInput> modelInput;
	std::unique_ptr<DateInput> endDateInput;
	std::unique_ptr<IntInput> numIterationsInput;
	std::unique_ptr<FloatInput> voteLossInput;
	std::unique_ptr<FloatInput> dailyChangeInput;
	std::unique_ptr<FloatInput> initialChangeInput;
	std::unique_ptr<IntInput> numElectionsInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};