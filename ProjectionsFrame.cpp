#include "ProjectionsFrame.h"
#include "General.h"

using namespace std::placeholders; // for function object parameter binding

enum ControlId {
	Base = 600, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	Run,
	NowCast,
};

// frame constructor
ProjectionsFrame::ProjectionsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Projections", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
}

void ProjectionsFrame::setupToolbar()
{
	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[5];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\nowcast.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::New, "New Projection", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Projection");
	toolBar->AddTool(ControlId::Edit, "Edit Projection", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Projection");
	toolBar->AddTool(ControlId::Remove, "Remove Projection", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Projection");
	toolBar->AddTool(ControlId::Run, "Run Projection", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Projection");
	toolBar->AddTool(ControlId::NowCast, "Set as Now-Cast", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Set as Now-Cast");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void ProjectionsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the projection data control.
	projectionData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void ProjectionsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ProjectionsFrame::OnResize, this, ControlId::Frame);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ProjectionsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnNewProjection, this, ControlId::New);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnEditProjection, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnRemoveProjection, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnRunProjection, this, ControlId::Run);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnNowCast, this, ControlId::NowCast);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ProjectionsFrame::OnSelectionChange, this, ControlId::DataView);
}

void ProjectionsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	projectionData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ProjectionsFrame::OnNewProjection(wxCommandEvent& WXUNUSED(event)) {

	if (project->models().count() == 0) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Projections run from the endpoint of a polling model. There must be at least one model defined before creating a projection.");

		message->ShowModal();
		return;
	}

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ProjectionsFrame::addProjection, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditProjectionFrame *frame = new EditProjectionFrame(
		EditProjectionFrame::Function::New, callback, project->models(), Projection::Settings());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ProjectionsFrame::OnEditProjection(wxCommandEvent& WXUNUSED(event)) {

	int projectionIndex = projectionData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (projectionIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ProjectionsFrame::replaceProjection, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditProjectionFrame *frame = new EditProjectionFrame(
		EditProjectionFrame::Function::Edit, callback, project->models(), project->projections().viewByIndex(projectionIndex).getSettings());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ProjectionsFrame::OnRemoveProjection(wxCommandEvent& WXUNUSED(event)) {

	int projectionIndex = projectionData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (projectionIndex == -1) return;

	removeProjection();

	return;
}

void ProjectionsFrame::OnRunProjection(wxCommandEvent& WXUNUSED(event)) {

	int projectionIndex = projectionData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (projectionIndex == -1) return;

	runProjection();

	return;
}

void ProjectionsFrame::OnNowCast(wxCommandEvent& WXUNUSED(event)) {

	int projectionIndex = projectionData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (projectionIndex == -1) return;

	setAsNowCast();

	return;
}

// updates the interface after a change in item selection.
void ProjectionsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void ProjectionsFrame::refreshDataTable() {

	projectionData->DeleteAllItems();
	projectionData->ClearColumns();

	// *** Projection Data Table Columns *** //

	// Add the data columns that show the properties of the projections.
	projectionData->AppendTextColumn("Projection Name", wxDATAVIEW_CELL_INERT, 140, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Base Model", wxDATAVIEW_CELL_INERT, 140, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Finish Date", wxDATAVIEW_CELL_INERT, 75, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("# Iterations", wxDATAVIEW_CELL_INERT, 72, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Leader Vote Loss", wxDATAVIEW_CELL_INERT, 105, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Daily Vote Change", wxDATAVIEW_CELL_INERT, 110, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Initial Vote Change", wxDATAVIEW_CELL_INERT, 115, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("# Previous Elections", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	projectionData->AppendTextColumn("Latest Update", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the projection data
	for (auto projection : project->projections()) {
		addProjectionToProjectionData(projection.second);
	}

	updateInterface();
}

void ProjectionsFrame::addProjection(Projection::Settings projectionSettings) {
	// Simultaneously add to the party data control and to the polling project.
	project->projections().add(Projection(projectionSettings));

	refreshDataTable();
}

void ProjectionsFrame::addProjectionToProjectionData(Projection projection) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(projection.getSettings().name));
	data.push_back(wxVariant(project->models().view(projection.getSettings().baseModel).getName()));
	data.push_back(wxVariant(projection.getEndDateString()));
	data.push_back(wxVariant(std::to_string(projection.getSettings().numIterations)));
	data.push_back(wxVariant(formatFloat(projection.getSettings().leaderVoteDecay, 5)));
	data.push_back(wxVariant(formatFloat(projection.getSettings().dailyChange, 4)));
	data.push_back(wxVariant(formatFloat(projection.getSettings().initialStdDev, 4)));
	data.push_back(wxVariant(std::to_string(projection.getSettings().numElections)));
	data.push_back(wxVariant(projection.getLastUpdatedString()));
	projectionData->AppendItem(data);
}

void ProjectionsFrame::replaceProjection(Projection::Settings projectionSettings) {
	int projectionIndex = projectionData->GetSelectedRow();
	int projectionId = project->projections().indexToId(projectionIndex);
	project->projections().replace(projectionId, Projection(projectionSettings));
	refreshDataTable();
}

void ProjectionsFrame::removeProjection() {
	int projectionIndex = projectionData->GetSelectedRow();
	int projectionId = project->projections().indexToId(projectionIndex);
	project->projections().remove(projectionId);
	refreshDataTable();
}

void ProjectionsFrame::runProjection() {
	int projectionIndex = projectionData->GetSelectedRow();
	int projectionId = project->projections().indexToId(projectionIndex);
	project->projections().run(projectionId);
	refreshDataTable();
}

// Sets the projection to be a "now-cast" (ends one day after the model ends)
void ProjectionsFrame::setAsNowCast() {
	int projectionIndex = projectionData->GetSelectedRow();
	int projectionId = project->projections().indexToId(projectionIndex);
	project->projections().setAsNowCast(projectionId);
	refreshDataTable();
}

void ProjectionsFrame::updateInterface() {
	bool somethingSelected = (projectionData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
	toolBar->EnableTool(ControlId::Run, somethingSelected);
	toolBar->EnableTool(ControlId::NowCast, somethingSelected);
}