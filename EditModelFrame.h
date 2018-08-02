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

#include <sstream>
#include <wx/valnum.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>

#include "ModelsFrame.h"
#include "Debug.h"
#include "Model.h"
#include "PollingProject.h"

class ModelsFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditModel_Base = 550, // To avoid mixing events with other frames.
	PA_EditModel_ButtonID_OK,
	PA_EditModel_TextBoxID_Name,
	PA_EditModel_TextBoxID_NumIterations,
	PA_EditModel_TextBoxID_VoteTimeMultiplier,
	PA_EditModel_TextBoxID_HouseEffectTimeMultiplier,
	PA_EditModel_TextBoxID_CalibrationFirstPartyBias,
	PA_EditModel_DatePickerID_StartDate,
	PA_EditModel_DatePickerID_EndDate,
};

// *** EditModelFrame ***
// Frame that allows the user to edit an already-existing model
// or create a new one if isNewModel is set to true.
class EditModelFrame : public wxDialog
{
public:
	// isNewModel: true if this dialog is for creating a new model, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// model: Model data to be used if editing (has default values for creating a new model).
	EditModelFrame(bool isNewModel, ModelsFrame* const parent,
		Model model = Model());

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the iteration number data based on
	// the result of the GetInt() method of "event".
	void updateTextNumIterations(wxCommandEvent& event);

	// Calls upon the window to update the preliminary vote time-multiplier data based on
	// the result of the GetFloat() method of "event".
	void updateTextVoteTimeMultiplier(wxCommandEvent& event);

	// Calls upon the window to update the preliminary house effect time-multiplier data based on
	// the result of the GetFloat() method of "event".
	void updateTextHouseEffectTimeMultiplier(wxCommandEvent& event);

	// Calls upon the window to update the preliminary house effect time-multiplier data based on
	// the result of the GetFloat() method of "event".
	void updateTextCalibrationFirstPartyBias(wxCommandEvent& event);

	// Calls upon the window to update the preliminary start date data based on
	// the result of the GetDate() method of "event".
	void updateStartDatePicker(wxDateEvent& event);

	// Calls upon the window to update the preliminary end date data based on
	// the result of the GetDate() method of "event".
	void updateEndDatePicker(wxDateEvent& event);

	// Calls on the parent frame to initialize a new model based on the
	// data in "newProjectData".
	void OnNewModelReady();

	// Data container for the preliminary settings for the model to be created.
	Model model;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* numIterationsStaticText;
	wxTextCtrl* numIterationsTextCtrl;
	wxStaticText* voteTimeMultiplierStaticText;
	wxTextCtrl* voteTimeMultiplierTextCtrl;
	wxStaticText* houseEffectTimeMultiplierStaticText;
	wxTextCtrl* houseEffectTimeMultiplierTextCtrl;
	wxStaticText* calibrationFirstPartyBiasStaticText;
	wxTextCtrl* calibrationFirstPartyBiasTextCtrl;
	wxStaticText* startDateStaticText;
	wxDatePickerCtrl* startDatePicker;
	wxStaticText* endDateStaticText;
	wxDatePickerCtrl* endDatePicker;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	ModelsFrame* const parent;

	// Stores whether this dialog is for creating a new model (true) or editing an existing one (false).
	bool isNewModel;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastNumIterations;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastVoteTimeMultiplier;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastHouseEffectTimeMultiplier;

	// Keeps the preference flow saved in case a text entry results in an invalid value.
	std::string lastCalibrationFirstPartyBias;
};