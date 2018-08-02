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

#include "wx/dataview.h"

#include <memory>
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"
#include "Debug.h"
#include "EditSimulationFrame.h"

class ProjectFrame;
class GenericChildFame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_SimulationsFrame_Base = 600, // To avoid mixing events with other frames.
	PA_SimulationsFrame_FrameID,
	PA_SimulationsFrame_DataViewID,
	PA_SimulationsFrame_NewSimulationID,
	PA_SimulationsFrame_EditSimulationID,
	PA_SimulationsFrame_RemoveSimulationID,
	PA_SimulationsFrame_RunSimulationID,
};

// *** SimulationsFrame ***
// Frame that allows the user to create aggregated simulations using the poll data.
class SimulationsFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	SimulationsFrame(ProjectFrame* const parent, PollingProject* project);

	// Calls on the frame to create a new simulation based on "Simulation".
	void OnNewSimulationReady(Simulation& simulation);

	// Calls on the frame to edit the currently selected simulation based on "Simulation".
	void OnEditSimulationReady(Simulation& simulation);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshData();

private:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new simulation.
	void OnNewSimulation(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing simulation.
	void OnEditSimulation(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing simulation.
	void OnRemoveSimulation(wxCommandEvent& WXUNUSED(event));

	// Runs the selected simulation.
	void OnRunSimulation(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the simulation "simulation".
	void addSimulation(Simulation simulation);

	// adds "simulation" to simulation data. Should not be used except within addSimulation.
	void addSimulationToSimulationData(Simulation simulation);

	// does everything required to replace the currently selected simulation with "simulation".
	void replaceSimulation(Simulation simulation);

	// does everything required to remove the currently selected simulation, if possible.
	void removeSimulation();

	// removes the currently selected simulation from simulation data.
	// Should not be used except within removeSimulation.
	void removeSimulationFromSimulationData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// does everything required to run the currently selected simulation, if possible.
	void runSimulation();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing simulation data.
	wxDataViewListCtrl* simulationData = nullptr;
};