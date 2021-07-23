#include "PollsFrame.h"

#include "EditPollFrame.h"
#include "General.h"

using namespace std::placeholders; // for function object parameter binding

enum ControlId {
	Base = 300, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	CollectPolls,
};

// frame constructor
PollsFrame::PollsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Polls", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
	updateInterface();
}

void PollsFrame::setupToolbar()
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	std::array<wxBitmap, 4> toolBarBitmaps;
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\toggle_polls.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::New, "New Poll", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Poll");
	toolBar->AddTool(ControlId::Edit, "Edit Poll", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Poll");
	toolBar->AddTool(ControlId::Remove, "Remove Poll", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Poll");
	toolBar->AddTool(ControlId::CollectPolls, "Collect Polls", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Collect Polls");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void PollsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the poll data control.
	pollData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void PollsFrame::refreshDataTable()
{
	pollData->DeleteAllItems();
	// clearing the columns is necessary in the case that parties are added/removed
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
	for (int i = 0; i < project->parties().count(); i++) {
		std::string abbreviation = project->parties().viewByIndex(i).abbreviation;
		pollData->AppendTextColumn(abbreviation, wxDATAVIEW_CELL_INERT, 40, wxALIGN_LEFT,
			wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	}

	// add column for Others primary votes
	pollData->AppendTextColumn("OTH", wxDATAVIEW_CELL_INERT, 40, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the poll data
	for (int i = 0; i < project->polls().count(); ++i) {
		addPollToPollData(project->polls().viewByIndex(i));
	}
}

void PollsFrame::bindEventHandlers()
{
	Bind(wxEVT_SIZE, &PollsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &PollsFrame::OnNewPoll, this, ControlId::New);
	Bind(wxEVT_TOOL, &PollsFrame::OnEditPoll, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &PollsFrame::OnRemovePoll, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &PollsFrame::OnCollectPolls, this, ControlId::CollectPolls);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PollsFrame::OnSelectionChange, this, ControlId::DataView);
}


void PollsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size of the data panel
	pollData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PollsFrame::addPoll(Poll poll) {
	// Simultaneously add to the poll data control and to the polling project.
	project->polls().add(poll);
	refreshDataTable();

	updateInterface();
}

void PollsFrame::addPollToPollData(Poll poll) {
	// Create a vector with all the poll data.
	wxVector<wxVariant> data;
	std::string pollsterName;
	if (poll.pollster != Pollster::InvalidId) pollsterName = project->pollsters().view(poll.pollster).name;
	else pollsterName = "Invalid";
	data.push_back(wxVariant(pollsterName)); // in case something goes wrong and we end up with a null pointer.
	data.push_back(wxVariant(poll.date.FormatISODate()));
	data.push_back(wxVariant(poll.getReported2ppString()));
	data.push_back(wxVariant(poll.getRespondent2ppString()));
	data.push_back(wxVariant(poll.getCalc2ppString()));
	for (int i = 0; i < project->parties().count(); i++)
		data.push_back(wxVariant(poll.getPrimaryString(i)));
	data.push_back(wxVariant(poll.getPrimaryString(PartyCollection::MaxParties)));

	pollData->AppendItem(data);
}

void PollsFrame::replacePoll(Poll poll) {
	PollCollection::Index pollIndex = pollData->GetSelectedRow();
	Poll::Id pollId = project->polls().indexToId(pollIndex);
	// Simultaneously replace data in the poll data control and the polling project.
	project->polls().replace(pollId, poll);
	refreshDataTable();

	updateInterface();
}

void PollsFrame::removePoll() {
	PollCollection::Index pollIndex = pollData->GetSelectedRow();
	Poll::Id pollId = project->polls().indexToId(pollIndex);
	// Simultaneously add to the poll data control and to the polling project.
	project->polls().remove(pollId);
	refreshDataTable();

	updateInterface();
}

void PollsFrame::OnNewPoll(wxCommandEvent& WXUNUSED(event)) {

	// This binding is needed to pass a member function as a callback for the editing frame
	auto callback = std::bind(&PollsFrame::addPoll, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollFrame *frame = new EditPollFrame(EditPollFrame::Function::New, callback, project->parties(), project->pollsters());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
}

void PollsFrame::OnEditPoll(wxCommandEvent& WXUNUSED(event)) {

	int pollIndex = pollData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (pollIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the editing frame
	auto callback = std::bind(&PollsFrame::replacePoll, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPollFrame *frame = new EditPollFrame(EditPollFrame::Function::Edit, callback, project->parties(),
		project->pollsters(), project->polls().viewByIndex(pollIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
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

void PollsFrame::updateInterface() {
	bool somethingSelected = (pollData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}

void PollsFrame::OnCollectPolls(wxCommandEvent&) {
	auto answer = wxMessageDialog(this, "Warning: This will completely replace all polls in this file. Please confirm!", "Confirm replace polls", wxOK | wxCANCEL | wxCANCEL_DEFAULT).ShowModal();

	if (answer != wxID_OK) return;

	auto requestFunc = [this](std::string caption, std::string default) -> std::string {
		auto dialog = wxTextEntryDialog(this, caption, "", default);
		dialog.ShowModal();
		return std::string(dialog.GetValue());
	};

	auto messageFunc = [](std::string s) {wxMessageBox(s); };

	project->polls().collectPolls(requestFunc, messageFunc);

	refresher.refreshPollData();
	refresher.refreshVisualiser();
}
