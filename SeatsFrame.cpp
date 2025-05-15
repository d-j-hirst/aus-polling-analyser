#include "SeatsFrame.h"

#include "EditSeatFrame.h"
#include "EditSimulationFrame.h"
#include "General.h"
#include "Log.h"
#include "NonClassicFrame.h"

using namespace std::placeholders; // for function object parameter binding

// IDs for the controls and the menu commands
enum ControlId {
	Base = 600, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	ShowResults,
	ResetSeats,
	Export
};

// frame constructor
SeatsFrame::SeatsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Seats", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolBar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
}

void SeatsFrame::refreshDataTable() {

	seatData->DeleteAllItems();
	seatData->ClearColumns();

	// *** Seat Data Table Columns *** //

	// Add the data columns that show the properties of the seats.
	seatData->AppendTextColumn("Seat Name", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Incumbent", wxDATAVIEW_CELL_INERT, 75, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Challenger", wxDATAVIEW_CELL_INERT, 75, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Region", wxDATAVIEW_CELL_INERT, 60, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Margin", wxDATAVIEW_CELL_INERT, 55, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Local Inc. Modifier", wxDATAVIEW_CELL_INERT, 115, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the seat data
	for (int i = 0; i < project->seats().count(); ++i) {
		addSeatToSeatData(project->seats().viewByIndex(i));
	}

	updateInterface();
}

void SeatsFrame::OnResetSeats(wxCommandEvent&)
{
	auto answer = wxMessageBox("Do you want to reset individual seat modifiers?", "Reset individual seat modifiers?", wxYES_NO);
	if (answer == wxYES) project->seats().resetLocalModifiers();
	answer = wxMessageBox("Do you want to reset seat betting odds?", "Reset seat betting odds?", wxYES_NO);
	if (answer == wxYES) project->seats().resetBettingOdds();
}

void SeatsFrame::OnExport(wxCommandEvent&)
{
	project->seats().exportInfo();
}

void SeatsFrame::setupToolBar()
{
	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[5];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\reset.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\camera.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::New, "New Seat", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Seat");
	toolBar->AddTool(ControlId::Edit, "Edit Seat", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Seat");
	toolBar->AddTool(ControlId::Remove, "Remove Seat", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Seat");
	toolBar->AddTool(ControlId::ResetSeats, "Reset seats", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Reset seats");
	toolBar->AddTool(ControlId::Export, "Export", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Export");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void SeatsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the seat data control.
	seatData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());
}

void SeatsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &SeatsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &SeatsFrame::OnNewSeat, this, ControlId::New);
	Bind(wxEVT_TOOL, &SeatsFrame::OnEditSeat, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &SeatsFrame::OnRemoveSeat, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &SeatsFrame::OnResetSeats, this, ControlId::ResetSeats);
	Bind(wxEVT_TOOL, &SeatsFrame::OnExport, this, ControlId::Export);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SeatsFrame::OnSelectionChange, this, ControlId::DataView);
}

void SeatsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the data table to the entire client size.
	seatData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void SeatsFrame::OnNewSeat(wxCommandEvent& WXUNUSED(event)) {

	if (!project->regions().count()) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Seats are defined as belonging to a particular region. Please define one region before creating a seat.");

		message->ShowModal();
		return;
	}

	// This binding is needed to pass a member function as a callback for the editing frame
	auto callback = std::bind(&SeatsFrame::addSeat, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSeatFrame *frame = new EditSeatFrame(EditSeatFrame::Function::Edit, callback, project->parties(), project->regions());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SeatsFrame::OnEditSeat(wxCommandEvent& WXUNUSED(event)) {

	int seatIndex = seatData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (seatIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the editing frame
	auto callback = std::bind(&SeatsFrame::replaceSeat, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSeatFrame *frame = new EditSeatFrame(EditSeatFrame::Function::Edit, callback, project->parties(), project->regions(), project->seats().viewByIndex(seatIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SeatsFrame::OnRemoveSeat(wxCommandEvent& WXUNUSED(event)) {

	int seatIndex = seatData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (seatIndex == -1) return;

	removeSeat();

	return;
}

// updates the interface after a change in item selection.
void SeatsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void SeatsFrame::OnNewSeatReady(Seat& seat) {
	addSeat(seat);
}

void SeatsFrame::OnEditSeatReady(Seat& seat) {
	replaceSeat(seat);
}

void SeatsFrame::addSeat(Seat seat) {
	// Simultaneously add to the party data control and to the polling project.
	project->seats().add(seat);

	refreshDataTable();
}

void SeatsFrame::addSeatToSeatData(Seat seat) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(seat.name));
	data.push_back(wxVariant(seat.incumbent >= 0 ? project->parties().view(seat.incumbent).name : "Invalid"));
	data.push_back(wxVariant(seat.challenger >= 0 ? project->parties().view(seat.challenger).name : "Invalid"));
	data.push_back(wxVariant(seat.region >= 0 ? project->regions().view(seat.region).name : "Invalid"));
	data.push_back(wxVariant(formatFloat(seat.tppMargin, 2)));
	data.push_back(wxVariant(formatFloat(seat.miscTppModifier, 2)));

	seatData->AppendItem(data);
}

void SeatsFrame::replaceSeat(Seat seat) {
	int seatIndex = seatData->GetSelectedRow();
	Seat::Id seatId = project->seats().indexToId(seatIndex);
	project->seats().replace(seatId, seat);

	refreshDataTable();
}

void SeatsFrame::removeSeat() {
	int seatIndex = seatData->GetSelectedRow();
	Seat::Id seatId = project->seats().indexToId(seatIndex);
	project->seats().remove(seatId);

	refreshDataTable();
}

void SeatsFrame::updateInterface() {
	bool somethingSelected = (seatData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
	toolBar->EnableTool(ControlId::ShowResults, somethingSelected);
}