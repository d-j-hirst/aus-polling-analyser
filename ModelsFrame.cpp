#include "ModelsFrame.h"

#include "EditModelFrame.h"
#include "General.h"
#include "Log.h"

#include <fstream>

using namespace std::placeholders; // for function object parameter binding
using namespace std::string_literals;

// IDs for the controls and the menu commands
enum ControlId {
	Base = 500, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	CollectData,
	DisplayResults
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
	modelData->AppendTextColumn("Party Codes", wxDATAVIEW_CELL_INERT, 400, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the model data
	if (project->models().count()) {
		for (int i = 0; i < project->models().count(); ++i) {
			addModelToModelData(project->models().viewByIndex(i));
		}
	}

	modelData->Refresh();

	updateInterface();
}

void ModelsFrame::OnResize(wxSizeEvent&) {
	// Set the model data table to the entire client size.
	modelData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ModelsFrame::OnNewModel(wxCommandEvent&) {

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ModelsFrame::addModel, this, _1);

	StanModel model;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(EditModelFrame::Function::New, callback, model);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ModelsFrame::OnEditModel(wxCommandEvent&) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&ModelsFrame::replaceModel, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditModelFrame *frame = new EditModelFrame(EditModelFrame::Function::Edit, callback, project->models().viewByIndex(modelIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ModelsFrame::OnRemoveModel(wxCommandEvent&) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	removeModel();

	refresher.refreshProjectionData();

	return;
}

void ModelsFrame::OnCollectData(wxCommandEvent&) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	try {
		collectData();
	}
	catch (StanModel::Exception const& e) {
		wxMessageBox("Could not load or process model data, because:\n"s + e.what());
	}

	refresher.refreshProjectionData();

	return;
}

void ModelsFrame::OnShowResult(wxCommandEvent&) {

	int modelIndex = modelData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (modelIndex == -1) return;

	displayResults();

	refresher.refreshProjectionData();

	return;
}

// updates the interface after a change in item selection.
void ModelsFrame::OnSelectionChange(wxDataViewEvent&) {
	updateInterface();
}

void ModelsFrame::setupToolbar()
{
	wxLogNull something;
	wxBitmap toolBarBitmaps[6];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\extend_polls.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[5] = wxBitmap("bitmaps\\details.png", wxBITMAP_TYPE_PNG);

	toolBar = new wxToolBar(this, wxID_ANY);

	toolBar->AddTool(ControlId::New, "New Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Model");
	toolBar->AddTool(ControlId::Edit, "Edit Model", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Model");
	toolBar->AddTool(ControlId::Remove, "Remove Model", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Model");
	toolBar->AddTool(ControlId::CollectData, "Run Model", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Run Model");
	toolBar->AddTool(ControlId::DisplayResults, "Display Results", toolBarBitmaps[5], wxNullBitmap, wxITEM_NORMAL, "Display Results");

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
	Bind(wxEVT_TOOL, &ModelsFrame::OnCollectData, this, ControlId::CollectData);
	Bind(wxEVT_TOOL, &ModelsFrame::OnShowResult, this, ControlId::DisplayResults);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ModelsFrame::OnSelectionChange, this, ControlId::DataView);
}

void ModelsFrame::addModel(StanModel model) {
	project->models().add(model);

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::addModelToModelData(StanModel model) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(model.getName()));
	data.push_back(wxVariant(model.getPartyCodes()));
	modelData->AppendItem(data);
}

void ModelsFrame::replaceModel(StanModel model) {
	int modelIndex = modelData->GetSelectedRow();
	project->models().access(project->models().indexToId(modelIndex)) = model;

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::removeModel() {
	int modelIndex = modelData->GetSelectedRow();
	project->models().remove(project->models().indexToId(modelIndex));

	refreshDataTable();

	refresher.refreshVisualiser();
}

void ModelsFrame::displayResults() {
	int modelIndex = modelData->GetSelectedRow();
	std::string resultsTexts = project->models().access(project->models().indexToId(modelIndex)).getTextReport();
	auto splitText = splitString(resultsTexts, ";");
	for (auto const& text : splitText) wxMessageBox(text);
}

void ModelsFrame::collectData() {
	ModelCollection::Index modelIndex = modelData->GetSelectedRow();
	StanModel::Id modelId = project->models().indexToId(modelIndex);
	StanModel& thisModel = project->models().access(modelId);
	thisModel.loadData([](std::string s) {wxMessageBox(s); },
		project->config().getNumThreads());
	refreshDataTable();
	refresher.refreshVisualiser();
	project->invalidateProjectionsFromModel(modelId);
}

void ModelsFrame::updateInterface() {
	bool somethingSelected = (modelData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
	toolBar->EnableTool(ControlId::CollectData, somethingSelected);
	toolBar->EnableTool(ControlId::DisplayResults, somethingSelected);
}