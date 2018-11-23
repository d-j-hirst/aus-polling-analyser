#include "ResultsFrame.h"

// IDs for the controls and the menu commands
enum {
	PA_ResultsFrame_Base = 700, // To avoid mixing events with other frames.
	PA_ResultsFrame_FrameID,
	PA_ResultsFrame_DataViewID,
	PA_ResultsFrame_RunLiveSimulationsID,
};

// frame constructor
ResultsFrame::ResultsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_ResultsFrame_FrameID, "Live", wxPoint(0, 0), project),
	parent(parent)
{
	wxLogNull something;

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.

	refreshToolbar();

	// *** Model Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the model data control.
	resultsData = new wxDataViewListCtrl(dataPanel,
		PA_ResultsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ResultsFrame::OnResize, this, PA_ResultsFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &ModelsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ResultsFrame::OnRunLiveSimulations, this, PA_ResultsFrame_RunLiveSimulationsID);
}

void ResultsFrame::refreshData()
{
	resultsData->DeleteAllItems();
	resultsData->ClearColumns();
}

void ResultsFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
	// Set the pollster data table to the entire client size.
	resultsData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ResultsFrame::OnRunLiveSimulations(wxCommandEvent & WXUNUSED(event))
{
	for (int i = 0; i < project->getSimulationCount(); ++i) {
		Simulation* simulation = project->getSimulationPtr(i);
		if (simulation->live) simulation->run(*project);
	}
}

void ResultsFrame::updateInterface()
{
}

void ResultsFrame::refreshToolbar()
{
	wxBitmap toolBarBitmaps[1];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_ResultsFrame_RunLiveSimulationsID, "Run Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Run Live Simulations");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}
