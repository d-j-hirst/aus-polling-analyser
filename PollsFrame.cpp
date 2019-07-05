#include "PollsFrame.h"
#include "General.h"

// frame constructor
PollsFrame::PollsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), PA_PollsFrame_FrameID, "Polls", wxPoint(0, 0), project),
	refresher(refresher)
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
	toolBar->AddTool(PA_PollsFrame_NewPollID, "New Poll", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Poll");
	toolBar->AddTool(PA_PollsFrame_EditPollID, "Edit Poll", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Poll");
	toolBar->AddTool(PA_PollsFrame_RemovePollID, "Remove Poll", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Poll");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Poll Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the poll data control.
	pollData = new wxDataViewListCtrl(dataPanel,
		PA_PollsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Poll Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &PollsFrame::OnResize, this, PA_PollsFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &PollsFrame::OnNewPoll, this, PA_PollsFrame_NewPollID);
	Bind(wxEVT_TOOL, &PollsFrame::OnEditPoll, this, PA_PollsFrame_EditPollID);
	Bind(wxEVT_TOOL, &PollsFrame::OnRemovePoll, this, PA_PollsFrame_RemovePollID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PollsFrame::OnSelectionChange, this, PA_PollsFrame_DataViewID);
}

void PollsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size of the data panel
	pollData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PollsFrame::addPoll(Poll poll) {
	// Simultaneously add to the poll data control and to the polling project.
	addPollToPollData(poll);
	project->addPoll(poll);

	updateInterface();
}

void PollsFrame::addPollToPollData(Poll poll) {
	// Create a vector with all the poll data.
	wxVector<wxVariant> data;
	if (poll.pollster) data.push_back(wxVariant(poll.pollster->name));
	else               data.push_back(wxVariant("invalid")); // in case something goes wrong and we end up with a null pointer.
	data.push_back(wxVariant(poll.date.FormatISODate()));
	data.push_back(wxVariant(poll.getReported2ppString()));
	data.push_back(wxVariant(poll.getRespondent2ppString()));
	data.push_back(wxVariant(poll.getCalc2ppString()));
	for (int i = 0; i < project->getPartyCount(); i++)
		data.push_back(wxVariant(poll.getPrimaryString(i)));
	data.push_back(wxVariant(poll.getPrimaryString(15)));

	pollData->AppendItem(data);
}

void PollsFrame::replacePoll(Poll poll) {
	int pollIndex = pollData->GetSelectedRow();
	// Simultaneously replace data in the poll data control and the polling project.
	replacePollInPollData(poll);
	project->replacePoll(pollIndex, poll);

	updateInterface();
}

void PollsFrame::replacePollInPollData(Poll poll) {
	int pollIndex = pollData->GetSelectedRow();
	// There is no function to replace a row all at once, so we edit all cells individually.
	wxDataViewListStore* store = pollData->GetStore();
	if (poll.pollster) store->SetValueByRow(poll.pollster->name, pollIndex, PollColumn_Name);
	else               store->SetValueByRow("invalid", pollIndex, PollColumn_Name); // in case something goes wrong and we end up with a null pointer.
	store->SetValueByRow(poll.date.FormatISODate(), pollIndex, PollColumn_Date);
	store->SetValueByRow(poll.getReported2ppString(), pollIndex, PollColumn_Reported2pp);
	store->SetValueByRow(poll.getRespondent2ppString(), pollIndex, PollColumn_Respondent2pp);
	store->SetValueByRow(poll.getCalc2ppString(), pollIndex, PollColumn_Calc2pp);
	int i = 0;
	for (; i < project->getPartyCount(); i++)
		store->SetValueByRow(poll.getPrimaryString(i), pollIndex, PollColumn_Primary + i);
	store->SetValueByRow(poll.getPrimaryString(15), pollIndex, PollColumn_Primary + i);
}

void PollsFrame::removePoll() {
	// Simultaneously add to the poll data control and to the polling project.
	project->removePoll(pollData->GetSelectedRow());

	// this line must come second, otherwise the argument for the line above will be wrong.
	removePollFromPollData();

	updateInterface();
}

void PollsFrame::removePollFromPollData() {
	// Create a vector with all the poll data.
	pollData->DeleteItem(pollData->GetSelectedRow());
}

void PollsFrame::OnNewPoll(wxCommandEvent& WXUNUSED(event)) {

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollFrame *frame = new EditPollFrame(true, this, project);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PollsFrame::OnEditPoll(wxCommandEvent& WXUNUSED(event)) {

	int pollIndex = pollData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (pollIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollFrame *frame = new EditPollFrame(false, this, project, project->getPoll(pollIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PollsFrame::OnRemovePoll(wxCommandEvent& WXUNUSED(event)) {

	int pollIndex = pollData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (pollIndex == -1) return;

	removePoll();

	return;
}

// updates the interface after a change in item selection.
void PollsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void PollsFrame::OnNewPollReady(Poll& poll) {
	addPoll(poll);
}

void PollsFrame::OnEditPollReady(Poll& poll) {
	replacePoll(poll);
}

void PollsFrame::updateInterface() {
	bool somethingSelected = (pollData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_PollsFrame_EditPollID, somethingSelected);
	toolBar->EnableTool(PA_PollsFrame_RemovePollID, somethingSelected);
}

void PollsFrame::refreshData() {

	pollData->DeleteAllItems();
	pollData->ClearColumns();

	// *** Poll Data Table Columns *** //

	// Add the data columns that show the properties of the polls.
	// These all need to be wide enough to fit the title.
	pollData->AppendTextColumn("Polling House", wxDATAVIEW_CELL_INERT, 116, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	pollData->AppendTextColumn("Date", wxDATAVIEW_CELL_INERT, 70, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	pollData->AppendTextColumn("Prev 2PP", wxDATAVIEW_CELL_INERT, 60, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	pollData->AppendTextColumn("Resp 2PP", wxDATAVIEW_CELL_INERT, 60, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	pollData->AppendTextColumn("Calc 2PP", wxDATAVIEW_CELL_INERT, 60, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// add columns for each party's primary votes
	for (int i = 0; i < project->getPartyCount(); i++)
		pollData->AppendTextColumn(project->getParty(i).abbreviation, wxDATAVIEW_CELL_INERT, 40, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// add column for Others primary votes
	pollData->AppendTextColumn("OTH", wxDATAVIEW_CELL_INERT, 40, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the poll data
	for (int i = 0; i < project->getPollCount(); ++i) {
		addPollToPollData(project->getPoll(i));
	}

	updateInterface();
}