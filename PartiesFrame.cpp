#include "PartiesFrame.h"
#include "General.h"

#include "EditPartyFrame.h"
#include "PartySettingsFrame.h"

using namespace std::placeholders; // for function object parameter binding

// IDs for the controls and the menu commands
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

// frame constructor
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
	project->setOthersPreferenceFlow(partySettingsData.othersPreferenceFlow);
	project->setOthersExhaustRate(partySettingsData.othersExhaustRate);
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
	for (int i = 0; i < project->parties().getPartyCount(); ++i) {
		addPartyToPartyData(project->parties().getParty(i));
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
	partyData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PartiesFrame::OnNewParty(wxCommandEvent& WXUNUSED(event)) {
	if (project->parties().getPartyCount() >= 15) {

		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot have more than 15 parties.");

		message->ShowModal();
		return;
	}

	auto callback = std::bind(&PartiesFrame::newPartyCallback, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPartyFrame *frame = new EditPartyFrame(true, callback);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PartiesFrame::OnEditParty(wxCommandEvent& WXUNUSED(event)) {

	int partyIndex = partyData->GetSelectedRow();

	// If the button is somehow clicked when there is no party selected, just stop.
	if (partyIndex == -1) return;

	auto callback = std::bind(&PartiesFrame::editPartyCallback, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPartyFrame *frame = new EditPartyFrame(false, callback, project->parties().getParty(partyIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void PartiesFrame::OnRemoveParty(wxCommandEvent& WXUNUSED(event)) {

	int partyIndex = partyData->GetSelectedRow();

	// If the button is somehow clicked when there is no party selected, just stop.
	if (partyIndex == -1) return;

	int numParties = partyData->GetItemCount();

	if (numParties == 2) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot remove the final two remaining parties. Edit the party data instead.");
		
		message->ShowModal();
		return;
	}

	removeParty();

	refresher.refreshPollData();

	return;
}

void PartiesFrame::OnPartySettings(wxCommandEvent& WXUNUSED(event)) {

	PartySettingsData partySettingsData = PartySettingsData(project->getOthersPreferenceFlow(), project->getOthersExhaustRate());

	auto callback = std::bind(&PartiesFrame::partySettingsCallback, this, _1);

	// Create the party settings frame (where initial settings for the new project are chosen).
	PartySettingsFrame *frame = new PartySettingsFrame(partySettingsData, callback);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

// updates the interface after a change in item selection.
void PartiesFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void PartiesFrame::addParty(Party party) {
	project->parties().addParty(party);

	refreshDataTable();
	updateInterface();
}

void PartiesFrame::addPartyToPartyData(Party party) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(party.name));
	data.push_back(wxVariant(formatFloat(party.preferenceShare, 3)));
	data.push_back(wxVariant(formatFloat(party.exhaustRate, 3)));
	data.push_back(wxVariant(party.abbreviation));

	partyData->AppendItem(data);
}

void PartiesFrame::replaceParty(Party party) {
	int partyIndex = partyData->GetSelectedRow();
	project->parties().replaceParty(partyIndex, party);
	project->refreshCalc2PP();

	refreshDataTable();
	updateInterface();
}

void PartiesFrame::removeParty() {
	// Simultaneously add to the party data control and to the polling project.
	if (project->parties().getPartyCount() < 3) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Must always have at least 2 parties. Rename the existing parties if they are not the ones you want.");

		message->ShowModal();
		return;
	}
	project->parties().removeParty(partyData->GetSelectedRow());

	refreshDataTable();
	updateInterface();
}


void PartiesFrame::updateInterface() {
	bool somethingSelected = (partyData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}