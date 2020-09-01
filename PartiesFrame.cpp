#include "PartiesFrame.h"

#include "EditPartyFrame.h"
#include "General.h"
#include "PartySettingsFrame.h"

using namespace std::placeholders; // for function object parameter binding

enum ControlId {
	Base = 200, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
	PartySettings,
};

enum PartyColumn {
	Name,
	PreferenceFlow,
	ExhaustRate,
	Abbreviation,
	NumColumns,
};

PartiesFrame::PartiesFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Parties", wxPoint(0, 0), project),
	refresher(refresher)
{
	setupToolBar();
	setupDataTable();
	bindEventHandlers();
	updateInterface();
}

void PartiesFrame::newPartyCallback(Party party)
{
	addParty(party);
	refresher.refreshPollData();
}

void PartiesFrame::editPartyCallback(Party party)
{
	replaceParty(party);
	refresher.refreshPollData();
}

void PartiesFrame::partySettingsCallback(PartySettingsData partySettingsData)
{
	project->parties().setOthersPreferenceFlow(partySettingsData.othersPreferenceFlow);
	project->parties().setOthersExhaustRate(partySettingsData.othersExhaustRate);
	project->refreshCalc2PP();
	refresher.refreshPollData();
}

void PartiesFrame::setupToolBar()
{
	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\tools.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::New, "New Party", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Party");
	toolBar->AddTool(ControlId::Edit, "Edit Party", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Party");
	toolBar->AddTool(ControlId::Remove, "Remove Party", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Party");
	toolBar->AddTool(ControlId::PartySettings, "General Party Settings", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "General Party Settings");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void PartiesFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	partyData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// Add the data columns that show the properties of the parties.
	partyData->AppendTextColumn("Party Name", wxDATAVIEW_CELL_INERT, 135); // wide enough to fit all significant party names
	partyData->AppendTextColumn("Preference Flow", wxDATAVIEW_CELL_INERT, 98); // wide enough to fit the title
	partyData->AppendTextColumn("Exhaust Rate", wxDATAVIEW_CELL_INERT, 98); // wide enough to fit the title
	partyData->AppendTextColumn("Abbreviation");

	refreshDataTable();
}

void PartiesFrame::refreshDataTable()
{
	partyData->DeleteAllItems();

	// Add the party data
	for (int i = 0; i < project->parties().count(); ++i) {
		addPartyToPartyData(project->parties().viewByIndex(i));
	}
}

void PartiesFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &PartiesFrame::OnResize, this, ControlId::Frame);

	// Need to record it if this frame is closed.
	Bind(wxEVT_CLOSE_WINDOW, &PartiesFrame::OnClose, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &PartiesFrame::OnNewParty, this, ControlId::New);
	Bind(wxEVT_TOOL, &PartiesFrame::OnEditParty, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &PartiesFrame::OnRemoveParty, this, ControlId::Remove);
	Bind(wxEVT_TOOL, &PartiesFrame::OnPartySettings, this, ControlId::PartySettings);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PartiesFrame::OnSelectionChange, this, ControlId::DataView);
}

void PartiesFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the party data table to the entire client size.
	// The extra (0, 1) allows for slightly better alignment.
	partyData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PartiesFrame::OnNewParty(wxCommandEvent& WXUNUSED(event)) {

	auto canAdd = project->parties().canAdd();

	if (canAdd == PartyCollection::Result::TooManyParties) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot have more than " + std::to_string(PartyCollection::MaxParties) + " parties.");
		message->ShowModal();
		return;
	}
	else if (canAdd != PartyCollection::Result::Ok) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot add party.");
		message->ShowModal();
		return;
	}

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&PartiesFrame::newPartyCallback, this, _1);

	EditPartyFrame *frame = new EditPartyFrame(EditPartyFrame::Function::New, callback, project->parties());
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
}

void PartiesFrame::OnEditParty(wxCommandEvent& WXUNUSED(event)) {

	int partyIndex = partyData->GetSelectedRow();

	// If the button is somehow clicked when there is no party selected, just stop.
	if (partyIndex == -1) return;

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&PartiesFrame::editPartyCallback, this, _1);

	EditPartyFrame *frame = new EditPartyFrame(EditPartyFrame::Function::Edit, callback, project->parties(), project->parties().viewByIndex(partyIndex));
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
}

void PartiesFrame::OnRemoveParty(wxCommandEvent& WXUNUSED(event)) {

	int partyIndex = partyData->GetSelectedRow();

	// If the button is somehow clicked when there is no party selected, just stop.
	if (partyIndex == -1) return;

	auto canRemove = project->parties().canRemove(project->parties().indexToId(partyIndex));

	if (canRemove == PartyCollection::Result::CantRemoveMajorParty) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot remove the first two parties. Edit the party data instead.");
		message->ShowModal();
		return;
	}
	else if (canRemove != PartyCollection::Result::Ok) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot remove this party.");
		message->ShowModal();
		return;
	}

	removeParty();

	refresher.refreshPollData();
	refresher.refreshMap();
	refresher.refreshResults();
	refresher.refreshSeatData();

	return;
}

void PartiesFrame::OnPartySettings(wxCommandEvent& WXUNUSED(event)) {

	PartySettingsData partySettingsData = PartySettingsData(project->parties().getOthersPreferenceFlow(), project->parties().getOthersExhaustRate());

	auto callback = std::bind(&PartiesFrame::partySettingsCallback, this, _1);

	PartySettingsFrame *frame = new PartySettingsFrame(partySettingsData, callback);
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
}

void PartiesFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	// updates whether edit/remove buttons are enabled
	updateInterface();
}

void PartiesFrame::addParty(Party party) {
	project->parties().add(party);

	refreshDataTable();
	updateInterface();
}

void PartiesFrame::addPartyToPartyData(Party party) {
	wxVector<wxVariant> data;
	data.push_back(wxVariant(party.name));
	data.push_back(wxVariant(formatFloat(party.preferenceShare, 3)));
	data.push_back(wxVariant(formatFloat(party.exhaustRate, 3)));
	data.push_back(wxVariant(party.abbreviation));

	partyData->AppendItem(data);
}

void PartiesFrame::replaceParty(Party party) {
	int partyIndex = partyData->GetSelectedRow();
	project->parties().replace(project->parties().indexToId(partyIndex), party);
	project->refreshCalc2PP();

	refreshDataTable();
	updateInterface();
}

void PartiesFrame::removeParty() {
	project->parties().remove(project->parties().indexToId(partyData->GetSelectedRow()));

	refreshDataTable();
	updateInterface();
}


void PartiesFrame::updateInterface() {
	bool somethingSelected = (partyData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}