#include "EventsFrame.h"

#include "EditEventFrame.h"
#include "General.h"

using namespace std::placeholders; // for function object parameter binding

// IDs for the controls and the menu commands
enum ControlId {
	Base = 600, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
};

// frame constructor
EventsFrame::EventsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Events", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolbar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
}

void EventsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	eventData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void EventsFrame::OnNewEvent(wxCommandEvent& WXUNUSED(event)) {

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&EventsFrame::addEvent, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditEventFrame *frame = new EditEventFrame(EditEventFrame::Function::New, callback, Event());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void EventsFrame::OnEditEvent(wxCommandEvent& WXUNUSED(event)) {

	int eventIndex = eventData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (eventIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&EventsFrame::replaceEvent, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditEventFrame *frame = new EditEventFrame(EditEventFrame::Function::Edit, callback, project->getEvent(eventIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void EventsFrame::OnRemoveEvent(wxCommandEvent& WXUNUSED(event)) {

	int eventIndex = eventData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (eventIndex == -1) return;

	removeEvent();

	return;
}

// updates the interface after a change in item selection.
void EventsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void EventsFrame::OnNewEventReady(Event& event) {
	addEvent(event);
}

void EventsFrame::OnEditEventReady(Event& event) {
	replaceEvent(event);
}

void EventsFrame::refreshDataTable() {

	eventData->DeleteAllItems();
	eventData->ClearColumns();

	// *** Model Data Table Columns *** //

	// Add the data columns that show the properties of the events.
	eventData->AppendTextColumn("Event Name", wxDATAVIEW_CELL_INERT, 160, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	eventData->AppendTextColumn("Event Type", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	eventData->AppendTextColumn("Event Date", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	eventData->AppendTextColumn("Vote", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the model data
	if (project->getEventCount()) {
		for (int i = 0; i < project->getEventCount(); ++i) {
			addEventToEventData(project->getEvent(i));
		}
	}

	updateInterface();
}

void EventsFrame::setupToolbar()
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
	toolBar->AddTool(ControlId::New, "New Event", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Event");
	toolBar->AddTool(ControlId::Edit, "Edit Event", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Event");
	toolBar->AddTool(ControlId::Remove, "Remove Event", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Event");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void EventsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the model data control.
	eventData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void EventsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &EventsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &EventsFrame::OnNewEvent, this, ControlId::New);
	Bind(wxEVT_TOOL, &EventsFrame::OnEditEvent, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &EventsFrame::OnRemoveEvent, this, ControlId::Remove);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &EventsFrame::OnSelectionChange, this, ControlId::DataView);
}

void EventsFrame::addEvent(Event event) {
	project->addEvent(event);
	refreshDataTable();

	updateInterface();
}

void EventsFrame::addEventToEventData(Event event) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(event.name));
	data.push_back(wxVariant(event.getEventTypeString()));
	data.push_back(wxVariant(event.getDateString()));
	data.push_back(wxVariant(event.getVoteString()));
	eventData->AppendItem(data);
}

void EventsFrame::replaceEvent(Event event) {
	int eventIndex = eventData->GetSelectedRow();
	// Simultaneously replace data in the event data control and the polling project.
	project->replaceEvent(eventIndex, event);
	refreshDataTable();

	updateInterface();
}

void EventsFrame::removeEvent() {
	project->removeEvent(eventData->GetSelectedRow());
	refreshDataTable();

	updateInterface();
}

void EventsFrame::updateInterface() {
	bool somethingSelected = (eventData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}