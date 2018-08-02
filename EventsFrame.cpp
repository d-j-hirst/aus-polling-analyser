#include "EventsFrame.h"
#include "General.h"

// frame constructor
EventsFrame::EventsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_EventsFrame_FrameID, "Events", wxPoint(0, 0), project),
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
	toolBar->AddTool(PA_EventsFrame_NewEventID, "New Event", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Event");
	toolBar->AddTool(PA_EventsFrame_EditEventID, "Edit Event", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Event");
	toolBar->AddTool(PA_EventsFrame_RemoveEventID, "Remove Event", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Event");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Model Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the model data control.
	eventData = new wxDataViewListCtrl(dataPanel,
		PA_EventsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &EventsFrame::OnResize, this, PA_EventsFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ModelsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &EventsFrame::OnNewEvent, this, PA_EventsFrame_NewEventID);
	Bind(wxEVT_TOOL, &EventsFrame::OnEditEvent, this, PA_EventsFrame_EditEventID);
	Bind(wxEVT_TOOL, &EventsFrame::OnRemoveEvent, this, PA_EventsFrame_RemoveEventID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &EventsFrame::OnSelectionChange, this, PA_EventsFrame_DataViewID);
}

void EventsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the pollster data table to the entire client size.
	eventData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void EventsFrame::OnNewEvent(wxCommandEvent& WXUNUSED(event)) {

	// Create the new project frame (where initial settings for the new project are chosen).
	EditEventFrame *frame = new EditEventFrame(true, this, Event());

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

	// Create the new project frame (where initial settings for the new project are chosen).
	EditEventFrame *frame = new EditEventFrame(false, this, project->getEvent(eventIndex));

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

void EventsFrame::refreshData() {

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

void EventsFrame::addEvent(Event event) {
	// Simultaneously add to the party data control and to the polling project.
	addEventToEventData(event);
	project->addEvent(event);

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
	replaceEventInEventData(event);
	project->replaceEvent(eventIndex, event);

	updateInterface();
}

void EventsFrame::replaceEventInEventData(Event event) {
	int eventIndex = eventData->GetSelectedRow();
	// There is no function to replace a row all at once, so we edit all cells individually.
	wxDataViewListStore* store = eventData->GetStore();
	store->SetValueByRow(event.name, eventIndex, EventColumn_Name);
	store->SetValueByRow(event.getEventTypeString(), eventIndex, EventColumn_EventType);
	store->SetValueByRow(event.getDateString(), eventIndex, EventColumn_EventDate);
	store->SetValueByRow(event.getVoteString(), eventIndex, EventColumn_2PP);
}

void EventsFrame::removeEvent() {
	// Simultaneously add to the event data control and to the polling project.
	project->removeEvent(eventData->GetSelectedRow());

	// this line must come second, otherwise the argument for the line above will be wrong.
	removeEventFromEventData();

	updateInterface();
}

void EventsFrame::removeEventFromEventData() {
	// Create a vector with all the event data.
	eventData->DeleteItem(eventData->GetSelectedRow());
}

void EventsFrame::updateInterface() {
	bool somethingSelected = (eventData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_EventsFrame_EditEventID, somethingSelected);
	toolBar->EnableTool(PA_EventsFrame_RemoveEventID, somethingSelected);
}