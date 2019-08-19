#include "SimulationsFrame.h"
#include "General.h"

enum ControlId {
	Base = 600, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	Run,
};

// frame constructor
SimulationsFrame::SimulationsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Simulations", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
}

void SimulationsFrame::setupToolbar()
{
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
	toolBar->AddTool(ControlId::New, "New Simulation", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Simulation");
	toolBar->AddTool(ControlId::Edit , "Edit Simulation", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Simulation");
	toolBar->AddTool(ControlId::Remove, "Remove Simulation", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Simulation");
	toolBar->AddTool(ControlId::Run, "Run Simulation", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Simulation");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void SimulationsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the simulation data control.
	simulationData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void SimulationsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &SimulationsFrame::OnResize, this, ControlId::Frame);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &SimulationsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &SimulationsFrame::OnNewSimulation, this, ControlId::New);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnEditSimulation, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnRemoveSimulation, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &SimulationsFrame::OnRunSimulation, this, ControlId::Run);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SimulationsFrame::OnSelectionChange, this, ControlId::DataView);
}

void SimulationsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	simulationData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void SimulationsFrame::OnNewSimulation(wxCommandEvent& WXUNUSED(event)) {

	if (!project->projections().count()) {
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

	refresher.refreshSeatData();

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

void SimulationsFrame::refreshDataTable() {

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

	refreshDataTable();

	updateInterface();

	refresher.refreshDisplay();
	refresher.refreshMap();
}

void SimulationsFrame::addSimulationToSimulationData(Simulation simulation) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(simulation.name));
	data.push_back(wxVariant(project->projections().view(simulation.baseProjection).name));
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

	refreshDataTable();

	updateInterface();

	refresher.refreshDisplay();
	refresher.refreshMap();
}

void SimulationsFrame::removeSimulation() {
	// Simultaneously add to the simulation data control and to the polling project.
	project->removeSimulation(simulationData->GetSelectedRow());

	refreshDataTable();

	updateInterface();

	refresher.refreshDisplay();
	refresher.refreshMap();
}

void SimulationsFrame::runSimulation() {
	int simulationIndex = simulationData->GetSelectedRow();
	Simulation* thisSimulation = project->getSimulationPtr(simulationIndex);
	thisSimulation->run(*project);
	refreshDataTable();
	simulationData->Refresh();
}

void SimulationsFrame::updateInterface() {
	bool somethingSelected = (simulationData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
	toolBar->EnableTool(ControlId::Run, somethingSelected);
}