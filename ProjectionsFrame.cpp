#include "ProjectionsFrame.h"
#include "General.h"

enum ProjectionColumnsEnum {
	ProjectionColumn_Name,
	ProjectionColumn_BaseModel,
	ProjectionColumn_EndDate,
	ProjectionColumn_NumIterations,
	ProjectionColumn_VoteLoss,
	ProjectionColumn_DailyChange,
	ProjectionColumn_InitialChange,
	ProjectionColumn_NumElections,
	ProjectionColumn_LatestUpdate,
};

// frame constructor
ProjectionsFrame::ProjectionsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), PA_ProjectionsFrame_FrameID, "Projections", wxPoint(0, 0), project),
	refresher(refresher)
{

	// *** Toolbar *** //

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
	toolBar->AddTool(PA_ProjectionsFrame_NewProjectionID, "New Projection", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Projection");
	toolBar->AddTool(PA_ProjectionsFrame_EditProjectionID, "Edit Projection", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Projection");
	toolBar->AddTool(PA_ProjectionsFrame_RemoveProjectionID, "Remove Projection", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Projection");
	toolBar->AddTool(PA_ProjectionsFrame_RunProjectionID, "Run Projection", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Projection");
	toolBar->AddTool(PA_ProjectionsFrame_NowCastID, "Set as Now-Cast", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Set as Now-Cast");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Projection Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the projection data control.
	projectionData = new wxDataViewListCtrl(dataPanel,
		PA_ProjectionsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ProjectionsFrame::OnResize, this, PA_ProjectionsFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ProjectionsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnNewProjection, this, PA_ProjectionsFrame_NewProjectionID);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnEditProjection, this, PA_ProjectionsFrame_EditProjectionID);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnRemoveProjection, this, PA_ProjectionsFrame_RemoveProjectionID);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnRunProjection, this, PA_ProjectionsFrame_RunProjectionID);
	Bind(wxEVT_TOOL, &ProjectionsFrame::OnNowCast, this, PA_ProjectionsFrame_NowCastID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ProjectionsFrame::OnSelectionChange, this, PA_ProjectionsFrame_DataViewID);
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

	// Create the new project frame (where initial settings for the new project are chosen).
	EditProjectionFrame *frame = new EditProjectionFrame(true, this, project, Projection());

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

	// Create the new project frame (where initial settings for the new project are chosen).
	EditProjectionFrame *frame = new EditProjectionFrame(false, this, project, project->getProjection(projectionIndex));

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

void ProjectionsFrame::OnNewProjectionReady(Projection& projection) {
	addProjection(projection);
}

void ProjectionsFrame::OnEditProjectionReady(Projection& projection) {
	replaceProjection(projection);
}

void ProjectionsFrame::refreshData() {

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
	if (project->getProjectionCount()) {
		for (int i = 0; i < project->getProjectionCount(); ++i) {
			addProjectionToProjectionData(project->getProjection(i));
		}
	}

	updateInterface();
}

void ProjectionsFrame::addProjection(Projection projection) {
	// Simultaneously add to the party data control and to the polling project.
	project->addProjection(projection);

	refreshData();
}

void ProjectionsFrame::addProjectionToProjectionData(Projection projection) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(projection.name));
	data.push_back(wxVariant(project->models().view(projection.baseModel).name));
	data.push_back(wxVariant(projection.getEndDateString()));
	data.push_back(wxVariant(std::to_string(projection.numIterations)));
	data.push_back(wxVariant(formatFloat(projection.leaderVoteLoss, 5)));
	data.push_back(wxVariant(formatFloat(projection.dailyChange, 4)));
	data.push_back(wxVariant(formatFloat(projection.initialStdDev, 4)));
	data.push_back(wxVariant(std::to_string(projection.numElections)));
	data.push_back(wxVariant(projection.getLastUpdatedString()));
	projectionData->AppendItem(data);
}

void ProjectionsFrame::replaceProjection(Projection projection) {
	int projectionIndex = projectionData->GetSelectedRow();
	// Simultaneously replace data in the projection data control and the polling project.
	project->replaceProjection(projectionIndex, projection);

	refreshData();
}

void ProjectionsFrame::removeProjection() {
	// Simultaneously add to the projection data control and to the polling project.
	project->removeProjection(projectionData->GetSelectedRow());

	refreshData();
}

void ProjectionsFrame::removeProjectionFromProjectionData() {
	// Create a vector with all the projection data.
	projectionData->DeleteItem(projectionData->GetSelectedRow());
}

void ProjectionsFrame::runProjection() {
	int projectionIndex = projectionData->GetSelectedRow();
	Projection* thisProjection = project->getProjectionPtr(projectionIndex);
	thisProjection->run(project->models());

	refreshData();
}

// Sets the projection to be a "now-cast" (ends one day after the model ends)
void ProjectionsFrame::setAsNowCast() {
	int projectionIndex = projectionData->GetSelectedRow();
	Projection* thisProjection = project->getProjectionPtr(projectionIndex);
	thisProjection->setAsNowCast(project->models());

	refreshData();
}

void ProjectionsFrame::updateInterface() {
	bool somethingSelected = (projectionData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_ProjectionsFrame_EditProjectionID, somethingSelected);
	toolBar->EnableTool(PA_ProjectionsFrame_RemoveProjectionID, somethingSelected);
	toolBar->EnableTool(PA_ProjectionsFrame_RunProjectionID, somethingSelected);
}