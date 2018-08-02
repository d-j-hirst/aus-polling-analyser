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

#include "Debug.h"
#include "Projection.h"

class ProjectionsFrame;
class PollingProject;

// *** EditProjectionFrame ***
// Frame that allows the user to edit an already-existing projection
// or create a new one if isNewProjection is set to true.
class EditProjectionFrame : public wxDialog
{
public:
	// isNewProjection: true if this dialog is for creating a new projection, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// projection: Projection data to be used if editing (has default values for creating a new projection).
	EditProjectionFrame(bool isNewProjection, ProjectionsFrame* parent,
		PollingProject const* project, Projection projection = Projection());

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the base model data based on
	// the properties of the event.
	void updateComboBoxBaseModel(wxCommandEvent& event);

	// Calls upon the window to update the preliminary end date data based on
	// the result of the GetDate() method of "event".
	void updateEndDatePicker(wxDateEvent& event);

	// Calls upon the window to update the preliminary number of iterations based on
	// the result of the GetString() method of "event".
	void updateTextNumIterations(wxCommandEvent& event);

	// Calls upon the window to update the preliminary vote loss based on
	// the result of the GetString() method of "event".
	void updateTextVoteLoss(wxCommandEvent& event);

	// Calls upon the window to update the daily change based on
	// the result of the GetString() method of "event".
	void updateTextDailyChange(wxCommandEvent& event);

	// Calls upon the window to update the initial change based on
	// the result of the GetString() method of "event".
	void updateTextInitialChange(wxCommandEvent& event);

	// Calls upon the window to update the initial change based on
	// the result of the GetString() method of "event".
	void updateTextNumElections(wxCommandEvent& event);

	// Calls on the parent frame to initialize a new projection based on the
	// data in "newProjectData".
	void OnNewProjectionReady();

	// Data container for the preliminary settings for the projection to be created.
	Projection projection;

	// Polling project pointer. Only used for accessing model names
	PollingProject const* project;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* modelStaticText;
	wxComboBox* modelComboBox;
	wxStaticText* endDateStaticText;
	wxDatePickerCtrl* endDatePicker;
	wxStaticText* numIterationsStaticText;
	wxTextCtrl* numIterationsTextCtrl;
	wxStaticText* voteLossStaticText;
	wxTextCtrl* voteLossTextCtrl;
	wxStaticText* dailyChangeStaticText;
	wxTextCtrl* dailyChangeTextCtrl;
	wxStaticText* initialChangeStaticText;
	wxTextCtrl* initialChangeTextCtrl;
	wxStaticText* numElectionsStaticText;
	wxTextCtrl* numElectionsTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	std::string lastNumIterations;

	std::string lastVoteLoss;

	std::string lastDailyChange;

	std::string lastInitialChange;

	std::string lastNumElections;

	// A pointer to the parent frame.
	ProjectionsFrame* const parent;

	// Stores whether this dialog is for creating a new projection (true) or editing an existing one (false).
	bool isNewProjection;
};