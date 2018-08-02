#include "PartiesFrame.h"
#include "General.h"

// frame constructor
PartiesFrame::PartiesFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_PartiesFrame_FrameID, "Political Parties", wxPoint(0, 0), project),
	parent(parent)
{

	// *** Toolbar *** //

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
	toolBar->AddTool(PA_PartiesFrame_NewPartyID, "New Party", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Party");
	toolBar->AddTool(PA_PartiesFrame_EditPartyID, "Edit Party", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Party");
	toolBar->AddTool(PA_PartiesFrame_RemovePartyID, "Remove Party", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Party");
	toolBar->AddTool(PA_PartiesFrame_PartySettingsID, "General Settings", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "General Settings");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Party Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the party data control.
	partyData = new wxDataViewListCtrl(dataPanel,
		PA_PartiesFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	// Add the data columns that show the properties of the parties.
	partyData->AppendTextColumn("Party Name", wxDATAVIEW_CELL_INERT, 135); // wide enough to fit all significant party names
	partyData->AppendTextColumn("Preference Flow", wxDATAVIEW_CELL_INERT, 98); // wide enough to fit the title
	partyData->AppendTextColumn("Abbreviation");

	// Add the party data
	for (int i = 0; i < project->getPartyCount(); ++i) {
		addPartyToPartyData(project->getParty(i));
	}

	updateInterface();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &PartiesFrame::OnResize, this, PA_PartiesFrame_FrameID);

	// Need to record it if this frame is closed.
	Bind(wxEVT_CLOSE_WINDOW, &PartiesFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &PartiesFrame::OnNewParty, this, PA_PartiesFrame_NewPartyID);
	Bind(wxEVT_TOOL, &PartiesFrame::OnEditParty, this, PA_PartiesFrame_EditPartyID);
	Bind(wxEVT_TOOL, &PartiesFrame::OnRemoveParty, this, PA_PartiesFrame_RemovePartyID);
	Bind(wxEVT_TOOL, &PartiesFrame::OnPartySettings, this, PA_PartiesFrame_PartySettingsID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &PartiesFrame::OnSelectionChange, this, PA_PartiesFrame_DataViewID);
}

void PartiesFrame::OnNewPartyReady(Party& party) {
	addParty(party);
	parent->refreshPollData();
}

void PartiesFrame::OnEditPartyReady(Party& party) {
	replaceParty(party);
	project->refreshCalc2PP();
	parent->refreshPollData();
}

void PartiesFrame::OnPartySettingsReady(PartySettingsData& partySettingsData) {
	project->setOthersPreferenceFlow(partySettingsData.othersPreferenceFlow);
	project->refreshCalc2PP();
	parent->refreshPollData();
}

void PartiesFrame::addParty(Party party) {
	// Simultaneously add to the party data control and to the polling project.
	addPartyToPartyData(party);
	project->addParty(party);

	updateInterface();
}

void PartiesFrame::addPartyToPartyData(Party party) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(party.name));
	data.push_back(wxVariant(formatFloat(party.preferenceShare, 3)));
	data.push_back(wxVariant(party.abbreviation));

	partyData->AppendItem(data);
}

void PartiesFrame::replaceParty(Party party) {
	int partyIndex = partyData->GetSelectedRow();
	// Simultaneously replace data in the party data control and the polling project.
	replacePartyInPartyData(party);
	project->replaceParty(partyIndex, party);

	updateInterface();
}

void PartiesFrame::replacePartyInPartyData(Party party) {
	int partyIndex = partyData->GetSelectedRow();
	// There is no function to replace a row all at once, so we edit all cells individually.
	wxDataViewListStore* store = partyData->GetStore();
	store->SetValueByRow(party.name, partyIndex, PartyColumn_Name);
	store->SetValueByRow(formatFloat(party.preferenceShare, 3), partyIndex, PartyColumn_PreferenceFlow);
	store->SetValueByRow(party.abbreviation, partyIndex, PartyColumn_Abbreviation);
}

void PartiesFrame::removeParty() {
	// Simultaneously add to the party data control and to the polling project.
	if (project->getPartyCount() < 3) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Must always have at least 2 parties. Rename the existing parties if they are not the ones you want.");

		message->ShowModal();
		return;
	}
	project->removeParty(partyData->GetSelectedRow());

	// this line must come second, otherwise the argument for the line above will be wrong.
	removePartyFromPartyData();

	updateInterface();
}

void PartiesFrame::removePartyFromPartyData() {
	// Create a vector with all the party data.
	partyData->DeleteItem(partyData->GetSelectedRow());
}

void PartiesFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the party data table to the entire client size.
	partyData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void PartiesFrame::OnNewParty(wxCommandEvent& WXUNUSED(event)) {

	if (project->getPartyCount() >= 15) {

		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot have more than 15 parties.");

		message->ShowModal();
		return;
	}

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPartyFrame *frame = new EditPartyFrame(true, this);

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

	// Create the new project frame (where initial settings for the new project are chosen).
	EditPartyFrame *frame = new EditPartyFrame(false, this, project->getParty(partyIndex));

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

	parent->refreshPollData();

	return;
}

void PartiesFrame::OnPartySettings(wxCommandEvent& WXUNUSED(event)) {

	PartySettingsData partySettingsData = PartySettingsData(project->getOthersPreferenceFlow());

	// Create the new project frame (where initial settings for the new project are chosen).
	PartySettingsFrame *frame = new PartySettingsFrame(this, partySettingsData);

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

void PartiesFrame::updateInterface() {
	bool somethingSelected = (partyData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_PartiesFrame_EditPartyID, somethingSelected);
	toolBar->EnableTool(PA_PartiesFrame_RemovePartyID, somethingSelected);
}