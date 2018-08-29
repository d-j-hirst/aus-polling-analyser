#include "RegionsFrame.h"
#include "General.h"

enum RegionColumnsEnum {
	RegionColumn_Name,
	RegionColumn_LastElection2PP,
	RegionColumn_Population,
	RegionColumn_Sample2PP,
	RegionColumn_SwingDeviation,
	RegionColumn_AdditionalUncertainty,
};

// frame constructor
RegionsFrame::RegionsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_RegionsFrame_FrameID, "Polling Houses", wxPoint(0, 218), project),
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
	toolBar->AddTool(PA_RegionsFrame_NewRegionID, "New Region", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Region");
	toolBar->AddTool(PA_RegionsFrame_EditRegionID, "Edit Region", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Region");
	toolBar->AddTool(PA_RegionsFrame_RemoveRegionID, "Remove Region", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Region");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Region Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the region data control.
	regionData = new wxDataViewListCtrl(dataPanel,
		PA_RegionsFrame_DataViewID,
		wxDefaultPosition,
		dataPanel->GetClientSize());

	// *** Region Data Table Columns *** //

	refreshData();

	updateInterface();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &RegionsFrame::OnResize, this, PA_RegionsFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &RegionsFrame::OnNewRegion, this, PA_RegionsFrame_NewRegionID);
	Bind(wxEVT_TOOL, &RegionsFrame::OnEditRegion, this, PA_RegionsFrame_EditRegionID);
	Bind(wxEVT_TOOL, &RegionsFrame::OnRemoveRegion, this, PA_RegionsFrame_RemoveRegionID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &RegionsFrame::OnSelectionChange, this, PA_RegionsFrame_DataViewID);
}

void RegionsFrame::addRegion(Region region) {
	// Simultaneously add to the region data control and to the polling project.
	project->addRegion(region);

	refreshData();

	updateInterface();
}

void RegionsFrame::addRegionToRegionData(Region region) {
	// Create a vector with all the region data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(region.name));
	data.push_back(wxVariant(std::to_string(region.population)));
	data.push_back(wxVariant(formatFloat(region.lastElection2pp, 2)));
	data.push_back(wxVariant(formatFloat(region.sample2pp, 2)));
	data.push_back(wxVariant(formatFloat(region.swingDeviation, 2)));
	data.push_back(wxVariant(formatFloat(region.additionalUncertainty, 1)));

	regionData->AppendItem(data);
}

void RegionsFrame::replaceRegion(Region region) {
	int regionIndex = regionData->GetSelectedRow();
	// Simultaneously replace data in the region data control and the polling project.
	project->replaceRegion(regionIndex, region);

	refreshData();

	updateInterface();
}

void RegionsFrame::removeRegion() {
	// Simultaneously add to the region data control and to the polling project.
	project->removeRegion(regionData->GetSelectedRow());

	refreshData();

	updateInterface();
}

void RegionsFrame::removeRegionFromRegionData() {
	// Create a vector with all the region data.
	regionData->DeleteItem(regionData->GetSelectedRow());
}

void RegionsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the region data table to the entire client size.
	regionData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void RegionsFrame::OnNewRegion(wxCommandEvent& WXUNUSED(event)) {

	// Create the new project frame (where initial settings for the new project are chosen).
	EditRegionFrame *frame = new EditRegionFrame(true, this);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void RegionsFrame::OnEditRegion(wxCommandEvent& WXUNUSED(event)) {

	int regionIndex = regionData->GetSelectedRow();

	// If the button is somehow clicked when there is no region selected, just stop.
	if (regionIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditRegionFrame *frame = new EditRegionFrame(false, this, project->getRegion(regionIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void RegionsFrame::OnRemoveRegion(wxCommandEvent& WXUNUSED(event)) {

	int regionIndex = regionData->GetSelectedRow();

	// If the button is somehow clicked when there is no region selected, just stop.
	if (regionIndex == -1) return;

	int numRegions = regionData->GetItemCount();

	if (numRegions == 1) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Cannot remove the final polling house. Edit the polling house data instead.");

		message->ShowModal();
		return;
	}

	removeRegion();

	return;
}

// updates the interface after a change in item selection.
void RegionsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void RegionsFrame::OnNewRegionReady(Region& region) {
	addRegion(region);
}

void RegionsFrame::OnEditRegionReady(Region& region) {
	replaceRegion(region);
}

void RegionsFrame::refreshData() {
	regionData->DeleteAllItems();
	regionData->ClearColumns();

	regionData->AppendTextColumn("Region Name", wxDATAVIEW_CELL_INERT, 122); // wide enough to fit the title
	regionData->AppendTextColumn("Population", wxDATAVIEW_CELL_INERT, 90); // wide enough to fit the title
	regionData->AppendTextColumn("Previous Election 2PP", wxDATAVIEW_CELL_INERT, 130); // wide enough to fit the title
	regionData->AppendTextColumn("Sample 2PP", wxDATAVIEW_CELL_INERT, 90); // wide enough to fit the title
	regionData->AppendTextColumn("Swing Deviation", wxDATAVIEW_CELL_INERT, 120); // wide enough to fit the title
	regionData->AppendTextColumn("Additional Uncertainty", wxDATAVIEW_CELL_INERT, 130); // wide enough to fit the title

	for (int i = 0; i < project->getRegionCount(); ++i) {
		addRegionToRegionData(project->getRegion(i));
	}

}

void RegionsFrame::updateInterface() {
	bool somethingSelected = (regionData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_RegionsFrame_EditRegionID, somethingSelected);
	toolBar->EnableTool(PA_RegionsFrame_RemoveRegionID, somethingSelected);
}