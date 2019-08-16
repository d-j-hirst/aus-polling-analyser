#include "RegionsFrame.h"

#include "EditRegionFrame.h"
#include "General.h"

using namespace std::placeholders; // for function object parameter binding

enum ControlId {
	Base = 250, // To avoid mixing events with other frames.
	Frame,
	DataView,
	New,
	Edit,
	Remove,
};

// frame constructor
RegionsFrame::RegionsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Regions", wxPoint(0, 218), project),
	refresher(refresher)
{
	setupToolBar();
	setupDataTable();
	refreshDataTable();
	bindEventHandlers();
	updateInterface();
}

void RegionsFrame::newRegionCallback(Region region)
{
	addRegion(region);
	refresher.refreshSeatData();
}

void RegionsFrame::editRegionCallback(Region region)
{
	replaceRegion(region);
	refresher.refreshSeatData();
}

void RegionsFrame::setupToolBar()
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
	toolBar->AddTool(ControlId::New, "New Region", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Region");
	toolBar->AddTool(ControlId::Edit, "Edit Region", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Region");
	toolBar->AddTool(ControlId::Remove, "Remove Region", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Region");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void RegionsFrame::setupDataTable()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the region data control.
	regionData = new wxDataViewListCtrl(dataPanel,
		ControlId::DataView,
		wxDefaultPosition,
		dataPanel->GetClientSize());
}

void RegionsFrame::refreshDataTable() {
	regionData->DeleteAllItems();
	regionData->ClearColumns();

	regionData->AppendTextColumn("Region Name", wxDATAVIEW_CELL_INERT, 122); // wide enough to fit the title
	regionData->AppendTextColumn("Population", wxDATAVIEW_CELL_INERT, 90); // wide enough to fit the title
	regionData->AppendTextColumn("Previous Election 2PP", wxDATAVIEW_CELL_INERT, 130); // wide enough to fit the title
	regionData->AppendTextColumn("Sample 2PP", wxDATAVIEW_CELL_INERT, 90); // wide enough to fit the title
	regionData->AppendTextColumn("Swing Deviation", wxDATAVIEW_CELL_INERT, 120); // wide enough to fit the title
	regionData->AppendTextColumn("Additional Uncertainty", wxDATAVIEW_CELL_INERT, 130); // wide enough to fit the title

	for (auto const& regionPair : project->regions()) {
		addRegionToRegionData(regionPair.second);
	}

}

void RegionsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &RegionsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &RegionsFrame::OnNewRegion, this, ControlId::New);
	Bind(wxEVT_TOOL, &RegionsFrame::OnEditRegion, this, ControlId::Edit);
	Bind(wxEVT_TOOL, &RegionsFrame::OnRemoveRegion, this, ControlId::Remove);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &RegionsFrame::OnSelectionChange, this, ControlId::DataView);
}

void RegionsFrame::addRegion(Region region) {
	// Simultaneously add to the region data control and to the polling project.
	project->regions().add(region);

	refreshDataTable();

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
	RegionCollection::Index regionIndex = regionData->GetSelectedRow();
	Region::Id regionId = project->regions().indexToId(regionIndex);
	project->regions().replace(regionId, region);
	refreshDataTable();
	refresher.refreshSeatData();
	updateInterface();
}

void RegionsFrame::removeRegion() {
	RegionCollection::Index regionIndex = regionData->GetSelectedRow();
	Region::Id regionId = project->regions().indexToId(regionIndex);
	project->regions().remove(regionId);
	refreshDataTable();
	refresher.refreshSeatData();
	updateInterface();
}

void RegionsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the region data table to the entire client size.
	regionData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void RegionsFrame::OnNewRegion(wxCommandEvent& WXUNUSED(event)) {

	// This binding is needed to pass a member function as a callback for the EditRegionFrame
	auto callback = std::bind(&RegionsFrame::newRegionCallback, this, _1);

	EditRegionFrame *frame = new EditRegionFrame(EditRegionFrame::Function::New, callback);

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

	// This binding is needed to pass a member function as a callback for the EditPartyFrame
	auto callback = std::bind(&RegionsFrame::editRegionCallback, this, _1);

	// Create the new project frame (where initial settings for the new project are chosen).
	EditRegionFrame *frame = new EditRegionFrame(EditRegionFrame::Function::New, callback, project->regions().viewByIndex(regionIndex));

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

void RegionsFrame::updateInterface() {
	bool somethingSelected = (regionData->GetSelectedRow() != -1);
	toolBar->EnableTool(ControlId::Edit, somethingSelected);
	toolBar->EnableTool(ControlId::Remove, somethingSelected);
}