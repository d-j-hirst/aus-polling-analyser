#include "ModelsFrame.h"
#include "General.h"

enum ModelColumnsEnum {
	ModelColumn_Name,
	ModelColumn_NumIterations,
	ModelColumn_VoteTimeMultiplier,
	ModelColumn_HouseEffectTimeMultiplier,
	ModelColumn_CalibrationFirstPartyBias,
	ModelColumn_StartDate,
	ModelColumn_EndDate,
	ModelColumn_LatestUpdate,
};

// IDs for the controls and the menu commands
enum {
	PA_ModelsFrame_Base = 500, // To avoid mixing events with other frames.
	PA_ModelsFrame_FrameID,
	PA_ModelsFrame_DataViewID,
	PA_ModelsFrame_NewModelID,
	PA_ModelsFrame_EditModelID,
	PA_ModelsFrame_RemoveModelID,
	PA_ModelsFrame_RunModelID,
	PA_ModelsFrame_ExtendModelID,
};

// frame constructor
ModelsFrame::ModelsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_ModelsFrame_FrameID, "Models", wxPoint(0, 0), project),
	parent(parent)
{

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[5];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\extend_to_last.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_ModelsFrame_NewModelID, "New Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Model");
	toolBar->AddTool(PA_ModelsFrame_EditModelID, "Edit Model", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Model");
	toolBar->AddTool(PA_ModelsFrame_RemoveModelID, "Remove Model", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Model");
	toolBar->AddTool(PA_ModelsFrame_RunModelID, "Run Model", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Model");
	toolBar->AddTool(PA_ModelsFrame_ExtendModelID, "Extend model to latest poll", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Extend model to latest poll");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Model Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the model data control.
	modelData = new wxDataViewListCtrl(dataPanel,
		PA_ModelsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ModelsFrame::OnResize, this, PA_ModelsFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ModelsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ModelsFrame::OnNewModel, this, PA_ModelsFrame_NewModelID);
	Bind(wxEVT_TOOL, &ModelsFrame::OnEditModel, this, PA_ModelsFrame_EditModelID);
	Bind(wxEVT_TOOL, &ModelsFrame::OnRemoveModel, this, PA_ModelsFrame_RemoveModelID);
	Bind(wxEVT_TOOL, &ModelsFrame::OnRunModel, this, PA_ModelsFrame_RunModelID);
	Bind(wxEVT_TOOL, &ModelsFrame::OnExtendModel, this, PA_ModelsFrame_ExtendModelID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ModelsFrame::OnSelectionChange, this, PA_ModelsFrame_DataViewID);
}

void ModelsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	modelData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ModelsFrame::OnNewModel(wxCommandEvent& WXUNUSED(event)) {

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(true, this, project->generateBaseModel());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ModelsFrame::OnEditModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(false, this, project->getModel(modelIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ModelsFrame::OnRemoveModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	removeModel();

	parent->refreshProjectionData();

	return;
}

void ModelsFrame::OnExtendModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	extendModel();

	parent->refreshProjectionData();

	return;
}

void ModelsFrame::OnRunModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	runModel();

	parent->refreshProjectionData();

	return;
}

// updates the interface after a change in item selection.
void ModelsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void ModelsFrame::OnNewModelReady(Model& model) {
	addModel(model);
}

void ModelsFrame::OnEditModelReady(Model& model) {
	replaceModel(model);
	parent->refreshProjectionData();
}

void ModelsFrame::refreshData() {

	modelData->DeleteAllItems();
	modelData->ClearColumns();

	// *** Model Data Table Columns *** //

	// Add the data columns that show the properties of the models.
	modelData->AppendTextColumn("Model Name", wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("Number of Iterations", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("Vote Smoothing", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("House Effect Smoothing", wxDATAVIEW_CELL_INERT, 150, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("First-Party Calibration Bias", wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("Start Date", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("Finish Date", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	modelData->AppendTextColumn("Latest Update", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the model data
	if (project->getModelCount()) {
		for (int i = 0; i < project->getModelCount(); ++i) {
			addModelToModelData(project->getModel(i));
		}
	}

	updateInterface();
}

void ModelsFrame::addModel(Model model) {
	// Simultaneously add to the party data control and to the polling project.
	project->addModel(model);

	refreshData();

	parent->refreshVisualiser();
}

void ModelsFrame::addModelToModelData(Model model) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(model.name));
	data.push_back(wxVariant(model.numIterations));
	data.push_back(wxVariant(formatFloat(model.trendTimeScoreMultiplier, 2)));
	data.push_back(wxVariant(formatFloat(model.houseEffectTimeScoreMultiplier, 2)));
	data.push_back(wxVariant(formatFloat(model.calibrationFirstPartyBias, 3)));
	data.push_back(wxVariant(model.getStartDateString()));
	data.push_back(wxVariant(model.getEndDateString()));
	data.push_back(wxVariant(model.getLastUpdatedString()));
	modelData->AppendItem(data);
}

void ModelsFrame::replaceModel(Model model) {
	int modelIndex = modelData->GetSelectedRow();
	// Simultaneously replace data in the model data control and the polling project.
	project->replaceModel(modelIndex, model);

	refreshData();

	parent->refreshVisualiser();
}

void ModelsFrame::removeModel() {
	// Simultaneously add to the model data control and to the polling project.
	project->removeModel(modelData->GetSelectedRow());

	refreshData();

	parent->refreshVisualiser();
}

void ModelsFrame::extendModel() {
	// Simultaneously add to the model data control and to the polling project.
	project->extendModel(modelData->GetSelectedRow());

	refreshData();
}

void ModelsFrame::runModel() {
	int modelIndex = modelData->GetSelectedRow();
	Model* thisModel = project->getModelPtr(modelIndex);
	wxDateTime earliestDate = project->MjdToDate(project->getEarliestPollDate());
	wxDateTime latestDate = project->MjdToDate(project->getLatestPollDate());
	int pollsterCount = project->getPollsterCount();
	thisModel->initializeRun(earliestDate, latestDate, pollsterCount);

	// placeholder, change this later!
	// This sets the first three polls (old Newspoll, Nielsen and Galaxy)
	// to be used for calibration, with combined house effect of 0.225 points to
	// the ALP (taken from their results at the 2004-2013 elections).
	for (int pollsterIndex = 0; pollsterIndex < project->getPollsterCount(); ++pollsterIndex) {
		bool useForCalibration = project->getPollster(pollsterIndex).useForCalibration;
		bool ignoreInitially = project->getPollster(pollsterIndex).ignoreInitially;
		float weight = project->getPollster(pollsterIndex).weight;
		thisModel->setPollsterData(pollsterIndex, useForCalibration, ignoreInitially, weight);
	}

	for (int i = 0; i < project->getPollCount(); ++i) {
		Poll const* poll = project->getPollPtr(i);
		if (poll->pollster->weight < 0.01f) continue;
		thisModel->importPoll(poll->getBest2pp(), poll->date, project->getPollsterIndex(poll->pollster));
	}
	for (int i = 0; i < project->getEventCount(); ++i) {
		Event const* event = project->getEventPtr(i);
		if (event->eventType == EventType_Election) {
			thisModel->importElection(event->vote, event->date);
		}
		else if (event->eventType == EventType_Discontinuity) {
			thisModel->importDiscontinuity(event->date);
		}
	}
	thisModel->setInitialPath();
	thisModel->doModelIterations();
	thisModel->finalizeRun();

	refreshData();

	modelData->Refresh();

	project->invalidateProjectionsFromModel(thisModel);
}

void ModelsFrame::updateInterface() {
	bool somethingSelected = (modelData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_ModelsFrame_EditModelID, somethingSelected);
	toolBar->EnableTool(PA_ModelsFrame_RemoveModelID, somethingSelected);
	toolBar->EnableTool(PA_ModelsFrame_RunModelID, somethingSelected);
}