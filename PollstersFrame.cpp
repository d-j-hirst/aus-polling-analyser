#include "PollstersFrame.h"
#include "General.h"

// frame constructor
PollstersFrame::PollstersFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_PollstersFrame_FrameID, "Polling Houses", wxPoint(0, 0), project),
	parent(parent)
{

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[3];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_PollstersFrame_NewPollsterID, "New Polling House", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Polling House");
	toolBar->AddTool(PA_PollstersFrame_EditPollsterID, "Edit Polling House", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Polling House");
	toolBar->AddTool(PA_PollstersFrame_RemovePollsterID, "Remove Polling House", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Polling House");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Pollster Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the pollster data control.
	pollsterData = new wxDataViewListCtrl(dataPanel,
		PA_PollstersFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Pollster Data Table Columns *** //

	// Add the data columns that show the properties of the pollsters.
	pollsterData->AppendTextColumn("Polling House Name", wxDATAVIEW_CELL_INERT, 122); // wide enough to fit the title
	pollsterData->AppendTextColumn("Weight", wxDATAVIEW_CELL_INERT, 55); // wide enough to fit the title
	pollsterData->AppendTextColumn("Use For Calibration", wxDATAVIEW_CELL_INERT, 130); // wide enough to fit the title
	pollsterData->AppendTextColumn("Ignore Initially", wxDATAVIEW_CELL_INERT, 115); // wide enough to fit the title

	// Add the pollster data
	for (int i = 0; i < project->getPollsterCount(); ++i) {
		addPollsterToPollsterData(project->getPollster(i));
	}

	updateInterface();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &PollstersFrame::OnResize, this, PA_PollstersFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &PollstersFrame::OnNewPollster, this, PA_PollstersFrame_NewPollsterID);
	Bind(wxEVT_TOOL, &PollstersFrame::OnEditPollster, this, PA_PollstersFrame_EditPollsterID);
	Bind(wxEVT_TOOL, &PollstersFrame::OnRemovePollster, this, PA_PollstersFrame_RemovePollsterID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PollstersFrame::OnSelectionChange, this, PA_PollstersFrame_DataViewID);
}

void PollstersFrame::addPollster(Pollster pollster) {
	// Simultaneously add to the pollster data control and to the polling project.
	addPollsterToPollsterData(pollster);
	project->addPollster(pollster);

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
	// Simultaneously replace data in the pollster data control and the polling project.
	replacePollsterInPollsterData(pollster);
	project->replacePollster(pollsterIndex, pollster);

	updateInterface();
}

void PollstersFrame::replacePollsterInPollsterData(Pollster pollster) {
	int pollsterIndex = pollsterData->GetSelectedRow();
	// There is no function to replace a row all at once, so we edit all cells individually.
	wxDataViewListStore* store = pollsterData->GetStore();
	store->SetValueByRow(pollster.name, pollsterIndex, PollsterColumn_Name);
	store->SetValueByRow(formatFloat(pollster.weight, 4), pollsterIndex, PollsterColumn_Weight);
	store->SetValueByRow((pollster.useForCalibration ? "Y" : "N"), pollsterIndex, PollsterColumn_UseForCalibration);
	store->SetValueByRow((pollster.ignoreInitially ? "Y" : "N"), pollsterIndex, PollsterColumn_IgnoreInitially);
}

void PollstersFrame::removePollster() {
	// Simultaneously add to the pollster data control and to the polling project.
	if (project->getPollsterCount() < 2) return;
	project->removePollster(pollsterData->GetSelectedRow());

	// this line must come second, otherwise the argument for the line above will be wrong.
	removePollsterFromPollsterData();

	updateInterface();
}

void PollstersFrame::removePollsterFromPollsterData() {
	// Create a vector with all the pollster data.
	pollsterData->DeleteItem(pollsterData->GetSelectedRow());
}

void PollstersFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	pollsterData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PollstersFrame::OnNewPollster(wxCommandEvent& WXUNUSED(event)) {

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollsterFrame *frame = new EditPollsterFrame(true, this);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PollstersFrame::OnEditPollster(wxCommandEvent& WXUNUSED(event)) {

	int pollsterIndex = pollsterData->GetSelectedRow();

	// If the button is somehow clicked when there is no pollster selected, just stop.
	if (pollsterIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollsterFrame *frame = new EditPollsterFrame(false, this, project->getPollster(pollsterIndex));

	// Show the frame.
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

	parent->refreshPollData();

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
	toolBar->EnableTool(PA_PollstersFrame_EditPollsterID, somethingSelected);
	toolBar->EnableTool(PA_PollstersFrame_RemovePollsterID, somethingSelected);
}