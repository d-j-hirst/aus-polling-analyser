#include "ModelsFrame.h"

#include "EditModelFrame.h"
#include "General.h"
#include "Log.h"

using namespace std::placeholders; // for function object parameter binding

// IDs for the controls and the menu commands
enum ControlId {
	Base = 500, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	Run,
	Extend,
};

// frame constructor
ModelsFrame::ModelsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Models", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
}

void ModelsFrame::refreshDataTable()
{

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

	modelData->Refresh();

	updateInterface();
}

void ModelsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the model data table to the entire client size.
	modelData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ModelsFrame::OnNewModel(wxCommandEvent& WXUNUSED(event)) {

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ModelsFrame::addModel, this, _1);

	Model model;
	model.startDate = wxDateTime(mjdToJdn(double(project->getEarliestDate())));
	model.endDate = wxDateTime(mjdToJdn(double(project->getLatestDate())));

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(EditModelFrame::Function::New, callback, model);

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

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ModelsFrame::replaceModel, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(EditModelFrame::Function::Edit, callback, *project->getModelPtr(modelIndex));

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

	refresher.refreshProjectionData();

	return;
}

void ModelsFrame::OnExtendModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	extendModel();

	refresher.refreshProjectionData();

	return;
}

void ModelsFrame::OnRunModel(wxCommandEvent& WXUNUSED(event)) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	runModel();

	refresher.refreshProjectionData();

	return;
}

// updates the interface after a change in item selection.
void ModelsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void ModelsFrame::setupToolbar()
{
	wxLogNull something;
	wxBitmap toolBarBitmaps[5];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\extend_to_last.png", wxBITMAP_TYPE_PNG);

	toolBar = new wxToolBar(this, wxID_ANY);

	toolBar->AddTool(ControlId::New, "New Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Model");
	toolBar->AddTool(ControlId::Edit, "Edit Model", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Model");
	toolBar->AddTool(ControlId::Remove, "Remove Model", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Model");
	toolBar->AddTool(ControlId::Run, "Run Model", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Model");
	toolBar->AddTool(ControlId::Extend, "Extend model to latest poll", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Extend model to latest poll");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void ModelsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the model data control.
	modelData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void ModelsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ModelsFrame::OnResize, this, ControlId::Frame);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ModelsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ModelsFrame::OnNewModel, this, ControlId::New);
	Bind(wxEVT_TOOL, &ModelsFrame::OnEditModel, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &ModelsFrame::OnRemoveModel, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &ModelsFrame::OnRunModel, this, ControlId::Run);
	Bind(wxEVT_TOOL, &ModelsFrame::OnExtendModel, this, ControlId::Extend);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ModelsFrame::OnSelectionChange, this, ControlId::DataView);
}

void ModelsFrame::addModel(Model model) {
	// Simultaneously add to the party data control and to the polling project.
	project->addModel(model);

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::addModelToModelData(Model model) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(model.name));
	data.push_back(wxVariant(std::to_string(model.numIterations)));
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

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::removeModel() {
	// Simultaneously add to the model data control and to the polling project.
	project->removeModel(modelData->GetSelectedRow());

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::extendModel() {
	// Simultaneously add to the model data control and to the polling project.
	project->extendModel(modelData->GetSelectedRow());

	refreshDataTable();
}

void ModelsFrame::runModel() {
	int modelIndex = modelData->GetSelectedRow();
	Model& thisModel = *project->getModelPtr(modelIndex);
	prepareModelForRun(thisModel);
	thisModel.run();
	refreshDataTable();
	project->invalidateProjectionsFromModel(&thisModel);
}

void ModelsFrame::prepareModelForRun(Model& model)
{
	wxDateTime earliestDate = project->MjdToDate(project->polls().getEarliestDate());
	wxDateTime latestDate = project->MjdToDate(project->polls().getLatestDate());
	int pollsterCount = project->pollsters().count();
	model.initializeRun(earliestDate, latestDate, pollsterCount);

	// placeholder, change this later!
	// This sets the first three polls (old Newspoll, Nielsen and Galaxy)
	// to be used for calibration, with combined house effect of 0.225 points to
	// the ALP (taken from their results at the 2004-2013 elections).
	for (auto const& pollster : project->pollsters()) {
		bool useForCalibration = pollster.second.useForCalibration;
		bool ignoreInitially = pollster.second.ignoreInitially;
		float weight = pollster.second.weight;
		model.setPollsterData(project->pollsters().idToIndex(pollster.first), useForCalibration, ignoreInitially, weight);
	}

	for (auto const& poll : project->polls()) {
		PollsterCollection::Index pollsterIndex = project->pollsters().idToIndex(poll.second.pollster);
		if (project->pollsters().view(poll.second.pollster).weight < 0.01f) continue;
		model.importPoll(poll.second.getBest2pp(), poll.second.date, pollsterIndex);
	}
	for (int i = 0; i < project->getEventCount(); ++i) {
		Event const* event = project->getEventPtr(i);
		if (event->eventType == EventType_Election) {
			model.importElection(event->vote, event->date);
		}
		else if (event->eventType == EventType_Discontinuity) {
			model.importDiscontinuity(event->date);
		}
	}
}

void ModelsFrame::updateInterface() {
	bool somethingSelected = (modelData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
	toolBar->EnableTool(ControlId::Run, somethingSelected);
	toolBar->EnableTool(ControlId::Extend, somethingSelected);
}