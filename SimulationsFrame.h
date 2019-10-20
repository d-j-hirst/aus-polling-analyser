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
#include "EditSimulationFrame.h"

class ProjectFrame;

// *** SimulationsFrame ***
// Frame that allows the user to create aggregated simulations using the poll data.
class SimulationsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	SimulationsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshDataTable();

private:

	// Creates the toolbar and its accompanying tools
	void setupToolbar();

	// Create the data table from scratch
	void setupDataTable();

	void bindEventHandlers();

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
	void addSimulation(Simulation::Settings settings);

	// adds "simulation" to simulation data. Should not be used except within addSimulation.
	void addSimulationToSimulationData(Simulation simulation);

	// does everything required to replace the currently selected simulation with "simulation".
	void replaceSimulation(Simulation::Settings settings);

	// does everything required to remove the currently selected simulation, if possible.
	void removeSimulation();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// does everything required to run the currently selected simulation, if possible.
	void runSimulation();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing simulation data.
	wxDataViewListCtrl* simulationData = nullptr;
};