#include "PollstersFrame.h"

#include "EditPollsterFrame.h"
#include "General.h"

using namespace std::placeholders; // for function object parameter binding

enum ControlId {
	Base = 300, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
};

enum Column {
	Name,
	Weight,
	UseForCalibration,
	IgnoreInitially,
	NumColumns,
};

PollstersFrame::PollstersFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Pollsters", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
	updateInterface();
}

void PollstersFrame::newPollsterCallback(Pollster pollster)
{
	addPollster(pollster);
	refresher.refreshPollData();
}

void PollstersFrame::editPollsterCallback(Pollster pollster)
{
	replacePollster(pollster);
	refresher.refreshPollData();
}

void PollstersFrame::setupToolbar()
{
	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[3];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::New, "New Polling House", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Polling House");
	toolBar->AddTool(ControlId::Edit, "Edit Polling House", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Polling House");
	toolBar->AddTool(ControlId::Remove, "Remove Polling House", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Polling House");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void PollstersFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	pollsterData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// Set widths of columns so that they're wide enough to fit the titles
	pollsterData->AppendTextColumn("Polling House Name", wxDATAVIEW_CELL_INERT, 122);
	pollsterData->AppendTextColumn("Weight", wxDATAVIEW_CELL_INERT, 55);
	pollsterData->AppendTextColumn("Use For Calibration", wxDATAVIEW_CELL_INERT, 130);
	pollsterData->AppendTextColumn("Ignore Initially", wxDATAVIEW_CELL_INERT, 115);
}

void PollstersFrame::refreshDataTable()
{
	pollsterData->DeleteAllItems();

	for (int i = 0; i < project->getPollsterCount(); ++i) {
		addPollsterToPollsterData(project->getPollster(i));
	}
}

void PollstersFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &PollstersFrame::OnResize, this, ControlId::Frame);

	Bind(wxEVT_TOOL, &PollstersFrame::OnNewPollster, this, ControlId::New);
	Bind(wxEVT_TOOL, &PollstersFrame::OnEditPollster, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &PollstersFrame::OnRemovePollster, this, ControlId::Remove);

	// Need to update whether buttons are enabled if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PollstersFrame::OnSelectionChange, this, ControlId::DataView);
}

void PollstersFrame::addPollster(Pollster pollster) {
	project->addPollster(pollster);
	refreshDataTable();

	updateInterface();
}

void PollstersFrame::addPollsterToPollsterData(Pollster pollster) {
	// Create a vector with all the pollster data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(pollster.name));
	data.push_back(wxVariant(formatFloat(pollster.weight, 4)));
	data.push_back(wxVariant((pollster.useForCalibration ? "Y" : "N")));
	data.push_back(wxVariant((pollster.ignoreInitially ? "Y" : "N")));

	pollsterData->AppendItem(data);
}

void PollstersFrame::replacePollster(Pollster pollster) {
	int pollsterIndex = pollsterData->GetSelectedRow();
	project->replacePollster(pollsterIndex, pollster);
	refreshDataTable();

	updateInterface();
}

void PollstersFrame::removePollster() {
	if (project->getPollsterCount() < 2) return;
	project->removePollster(pollsterData->GetSelectedRow());
	refreshDataTable();

	updateInterface();
}

void PollstersFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	// The extra (0, 1) allows for slightly better alignment.
	pollsterData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PollstersFrame::OnNewPollster(wxCommandEvent& WXUNUSED(event)) {

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&PollstersFrame::newPollsterCallback, this, _1);

	EditPollsterFrame *frame = new EditPollsterFrame(EditPollsterFrame::Function::New, callback);
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PollstersFrame::OnEditPollster(wxCommandEvent& WXUNUSED(event)) {

	int pollsterIndex = pollsterData->GetSelectedRow();

	// If the button is somehow clicked when there is no pollster selected, just stop.
	if (pollsterIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&PollstersFrame::editPollsterCallback, this, _1);

	EditPollsterFrame *frame = new EditPollsterFrame(EditPollsterFrame::Function::Edit, callback, project->getPollster(pollsterIndex));
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PollstersFrame::OnRemovePollster(wxCommandEvent& WXUNUSED(event)) {

	int pollsterIndex = pollsterData->GetSelectedRow();

	// If the button is somehow clicked when there is no pollster selected, just stop.
	if (pollsterIndex == -1) return;

	int numPollsters = pollsterData->GetItemCount();

	if (numPollsters == 1) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot remove the final polling house. Edit the polling house data instead.");

		message->ShowModal();
		return;
	}

	removePollster();

	refresher.refreshPollData();

	return;
}

// updates the interface after a change in item selection.
void PollstersFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void PollstersFrame::OnNewPollsterReady(Pollster& pollster) {
	addPollster(pollster);
}

void PollstersFrame::OnEditPollsterReady(Pollster& pollster) {
	replacePollster(pollster);
}

void PollstersFrame::updateInterface() {
	bool somethingSelected = (pollsterData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}