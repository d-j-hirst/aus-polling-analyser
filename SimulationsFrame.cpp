#include "SimulationsFrame.h"
#include "General.h"

enum SimulationColumnsEnum {
	SimulationColumn_Name,
	SimulationColumn_BaseProjection,
	SimulationColumn_NumIterations,
	SimulationColumn_PrevElection2pp,
	SimulationColumn_StateSD,
	SimulationColumn_StateDecay,
	SimulationColumn_LatestUpdate,
};

// frame constructor
SimulationsFrame::SimulationsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_SimulationsFrame_FrameID, "Simulations", wxPoint(0, 0), project),
	parent(parent)
{

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_SimulationsFrame_NewSimulationID, "New Simulation", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Simulation");
	toolBar->AddTool(PA_SimulationsFrame_EditSimulationID, "Edit Simulation", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Simulation");
	toolBar->AddTool(PA_SimulationsFrame_RemoveSimulationID, "Remove Simulation", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Simulation");
	toolBar->AddTool(PA_SimulationsFrame_RunSimulationID, "Run Simulation", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Simulation");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Simulation Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the simulation data control.
	simulationData = new wxDataViewListCtrl(dataPanel,
		PA_SimulationsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &SimulationsFrame::OnResize, this, PA_SimulationsFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &SimulationsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &SimulationsFrame::OnNewSimulation, this, PA_SimulationsFrame_NewSimulationID);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnEditSimulation, this, PA_SimulationsFrame_EditSimulationID);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnRemoveSimulation, this, PA_SimulationsFrame_RemoveSimulationID);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnRunSimulation, this, PA_SimulationsFrame_RunSimulationID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SimulationsFrame::OnSelectionChange, this, PA_SimulationsFrame_DataViewID);
}

void SimulationsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	simulationData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void SimulationsFrame::OnNewSimulation(wxCommandEvent& WXUNUSED(event)) {

	if (project->getProjectionCount() == 0) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Simulations run from a projection of a polling model. There must be at least one model projection defined before creating a simulation.");

		message->ShowModal();
		return;
	}

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSimulationFrame *frame = new EditSimulationFrame(true, this, project, Simulation());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SimulationsFrame::OnEditSimulation(wxCommandEvent& WXUNUSED(event)) {

	int simulationIndex = simulationData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (simulationIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSimulationFrame *frame = new EditSimulationFrame(false, this, project, project->getSimulation(simulationIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SimulationsFrame::OnRemoveSimulation(wxCommandEvent& WXUNUSED(event)) {

	int simulationIndex = simulationData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (simulationIndex == -1) return;

	removeSimulation();

	return;
}

void SimulationsFrame::OnRunSimulation(wxCommandEvent& WXUNUSED(event)) {

	int simulationIndex = simulationData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (simulationIndex == -1) return;

	runSimulation();

	parent->refreshSeatData();

	return;
}

// updates the interface after a change in item selection.
void SimulationsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void SimulationsFrame::OnNewSimulationReady(Simulation& simulation) {
	addSimulation(simulation);
}

void SimulationsFrame::OnEditSimulationReady(Simulation& simulation) {
	replaceSimulation(simulation);
}

void SimulationsFrame::refreshData() {

	simulationData->DeleteAllItems();
	simulationData->ClearColumns();

	// *** Simulation Data Table Columns *** //

	// Add the data columns that show the properties of the simulations.
	simulationData->AppendTextColumn("Simulation Name", wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("Base Projection", wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("# Iterations", wxDATAVIEW_CELL_INERT, 72, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("Last Election 2pp", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("State SD", wxDATAVIEW_CELL_INERT, 72, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("State Decay", wxDATAVIEW_CELL_INERT, 72, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	simulationData->AppendTextColumn("Latest Update", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the simulation data
	if (project->getSimulationCount()) {
		for (int i = 0; i < project->getSimulationCount(); ++i) {
			addSimulationToSimulationData(project->getSimulation(i));
		}
	}

	updateInterface();
}

void SimulationsFrame::addSimulation(Simulation simulation) {
	// Simultaneously add to the party data control and to the polling project.
	project->addSimulation(simulation);

	refreshData();

	updateInterface();

	parent->refreshDisplay();
}

void SimulationsFrame::addSimulationToSimulationData(Simulation simulation) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(simulation.name));
	data.push_back(wxVariant(simulation.baseProjection->name));
	data.push_back(wxVariant(std::to_string(simulation.numIterations)));
	data.push_back(wxVariant(formatFloat(simulation.prevElection2pp, 2)));
	data.push_back(wxVariant(formatFloat(simulation.stateSD, 3)));
	data.push_back(wxVariant(formatFloat(simulation.stateDecay, 5)));
	data.push_back(wxVariant(simulation.getLastUpdatedString()));
	simulationData->AppendItem(data);
}

void SimulationsFrame::replaceSimulation(Simulation simulation) {
	int simulationIndex = simulationData->GetSelectedRow();
	// Simultaneously replace data in the simulation data control and the polling project.
	project->replaceSimulation(simulationIndex, simulation);

	refreshData();

	updateInterface();

	parent->refreshDisplay();
}

void SimulationsFrame::removeSimulation() {
	// Simultaneously add to the simulation data control and to the polling project.
	project->removeSimulation(simulationData->GetSelectedRow());

	refreshData();

	updateInterface();

	parent->refreshDisplay();
}

void SimulationsFrame::removeSimulationFromSimulationData() {
	// Create a vector with all the simulation data.
	simulationData->DeleteItem(simulationData->GetSelectedRow());
}

void SimulationsFrame::runSimulation() {
	int simulationIndex = simulationData->GetSelectedRow();
	Simulation* thisSimulation = project->getSimulationPtr(simulationIndex);
	thisSimulation->run(*project);
	refreshData();
	simulationData->Refresh();
}

void SimulationsFrame::updateInterface() {
	bool somethingSelected = (simulationData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_SimulationsFrame_EditSimulationID, somethingSelected);
	toolBar->EnableTool(PA_SimulationsFrame_RemoveSimulationID, somethingSelected);
	toolBar->EnableTool(PA_SimulationsFrame_RunSimulationID, somethingSelected);
}