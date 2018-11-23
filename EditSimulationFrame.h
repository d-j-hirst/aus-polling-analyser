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
#include "Simulation.h"

class SimulationsFrame;
class PollingProject;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditSimulation_Base = 650, // To avoid mixing events with other frames.
	PA_EditSimulation_ButtonID_OK,
	PA_EditSimulation_TextBoxID_Name,
	PA_EditSimulation_ComboBoxID_BaseProjection,
	PA_EditSimulation_TextBoxID_NumIterations,
	PA_EditSimulation_TextBoxID_PrevElection2pp,
	PA_EditSimulation_TextBoxID_StateSD,
	PA_EditSimulation_TextBoxID_StateDecay,
	PA_EditSimulation_CheckBoxID_Live,
};

// *** EditSimulationFrame ***
// Frame that allows the user to edit an already-existing simulation
// or create a new one if isNewSimulation is set to true.
class EditSimulationFrame : public wxDialog
{
public:
	// isNewSimulation: true if this dialog is for creating a new simulation, false if it's for editing.
	// parent: Parent frame for this (must be a PartiesFrame).
	// simulation: Simulation data to be used if editing (has default values for creating a new simulation).
	EditSimulationFrame(bool isNewSimulation, SimulationsFrame* parent,
		PollingProject const* project, Simulation simulation = Simulation());

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the base projection data based on
	// the properties of the event.
	void updateComboBoxBaseProjection(wxCommandEvent& event);

	// Calls upon the window to update the preliminary number of iterations based on
	// the result of the GetString() method of "event".
	void updateTextNumIterations(wxCommandEvent& event);

	// Calls upon the window to update the preliminary last-election 2pp vote based on
	// the result of the GetString() method of "event".
	void updateTextPrevElection2pp(wxCommandEvent& event);

	// Calls upon the window to update the preliminary state vote standard deviation based on
	// the result of the GetString() method of "event".
	void updateTextStateSD(wxCommandEvent& event);

	// Calls upon the window to update the preliminary state vote decay based on
	// the result of the GetString() method of "event".
	void updateTextStateDecay(wxCommandEvent& event);

	// Calls upon the window to update whether the simulation is "live" or not
	// the result of the isChecked() method of "event".
	void updateLive(wxCommandEvent& event);

	// Calls on the parent frame to initialize a new simulation based on the
	// data in "newProjectData".
	void OnNewSimulationReady();

	// Data container for the preliminary settings for the simulation to be created.
	Simulation simulation;

	// Polling project pointer. Only used for accessing model names
	PollingProject const* project;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* projectionStaticText;
	wxComboBox* projectionComboBox;
	wxStaticText* numIterationsStaticText;
	wxTextCtrl* numIterationsTextCtrl;
	wxStaticText* prevElection2ppStaticText;
	wxTextCtrl* prevElection2ppTextCtrl;
	wxStaticText* stateSDStaticText;
	wxTextCtrl* stateSDTextCtrl;
	wxStaticText* stateDecayStaticText;
	wxTextCtrl* stateDecayTextCtrl;
	wxStaticText* liveStaticText;
	wxCheckBox* liveCheckBox;
	wxButton* okButton;
	wxButton* cancelButton;

	std::string lastNumIterations;
	std::string lastPrevElection2pp;
	std::string lastStateSD;
	std::string lastStateDecay;

	// A pointer to the parent frame.
	SimulationsFrame* const parent;

	// Stores whether this dialog is for creating a new simulation (true) or editing an existing one (false).
	bool isNewSimulation;
};