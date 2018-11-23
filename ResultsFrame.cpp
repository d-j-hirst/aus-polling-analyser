#include "ResultsFrame.h"

#include <wx/valnum.h>

// IDs for the controls and the menu commands
enum {
	PA_ResultsFrame_Base = 700, // To avoid mixing events with other frames.
	PA_ResultsFrame_FrameID,
	PA_ResultsFrame_DataViewID,
	PA_ResultsFrame_RunLiveSimulationsID,
	PA_ResultsFrame_SeatNameID,
	PA_ResultsFrame_SwingID,
	PA_ResultsFrame_PercentCountedID,
	PA_ResultsFrame_CurrentBoothCountID,
	PA_ResultsFrame_TotalBoothCountID,
	PA_ResultsFrame_AddResultID
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
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddResult, this, PA_ResultsFrame_AddResultID);
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

void ResultsFrame::OnAddResult(wxCommandEvent & WXUNUSED(event))
{
	std::string enteredName = seatNameTextCtrl->GetLineText(0);
	auto seat = project->getSeatPtrByName(enteredName);
	if (!seat) {
		wxMessageBox("No seat found matching this name!");
		return;
	}
}

void ResultsFrame::updateInterface()
{
}

void ResultsFrame::refreshToolbar()
{
	wxBitmap toolBarBitmaps[2];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	wxArrayString seatNames;
	for (auto seat = project->getSeatBegin(); seat != project->getSeatEnd(); ++seat) {
		seatNames.Add(seat->name);
	}

	auto seatNameStaticText = new wxStaticText(toolBar, wxID_ANY, "Seat name:");
	seatNameTextCtrl = new wxTextCtrl(toolBar, PA_ResultsFrame_SeatNameID, "", wxPoint(0, 0), wxSize(110, 22));
	seatNameTextCtrl->AutoComplete(seatNames);

	auto fpValidator = wxFloatingPointValidator<double>();
	fpValidator.SetPrecision(2);

	auto swingStaticText = new wxStaticText(toolBar, wxID_ANY, "Swing:");
	swingTextCtrl = new wxTextCtrl(toolBar, PA_ResultsFrame_SwingID, "", wxPoint(0, 0), wxSize(45, 22), 0, fpValidator);

	auto percentCountedStaticText = new wxStaticText(toolBar, wxID_ANY, "% counted:");
	percentCountedTextCtrl = new wxTextCtrl(toolBar, PA_ResultsFrame_PercentCountedID, "", wxPoint(0, 0), wxSize(37, 22), 0, fpValidator);

	auto currentBoothCountStaticText = new wxStaticText(toolBar, wxID_ANY, "Booths in:");
	currentBoothCountTextCtrl = new wxTextCtrl(toolBar, PA_ResultsFrame_CurrentBoothCountID, "", wxPoint(0, 0), wxSize(25, 22), 0, wxIntegerValidator<int>());

	auto totalBoothCountStaticText = new wxStaticText(toolBar, wxID_ANY, "Booths total:");
	totalBoothCountTextCtrl = new wxTextCtrl(toolBar, PA_ResultsFrame_TotalBoothCountID, "", wxPoint(0, 0), wxSize(25, 22), 0, wxIntegerValidator<int>());

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_ResultsFrame_RunLiveSimulationsID, "Run Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Run Live Simulations");
	toolBar->AddSeparator();
	toolBar->AddControl(seatNameStaticText);
	toolBar->AddControl(seatNameTextCtrl);
	toolBar->AddControl(swingStaticText);
	toolBar->AddControl(swingTextCtrl);
	toolBar->AddControl(percentCountedStaticText);
	toolBar->AddControl(percentCountedTextCtrl);
	toolBar->AddControl(currentBoothCountStaticText);
	toolBar->AddControl(currentBoothCountTextCtrl);
	toolBar->AddControl(totalBoothCountStaticText);
	toolBar->AddControl(totalBoothCountTextCtrl);
	toolBar->AddSeparator();
	toolBar->AddTool(PA_ResultsFrame_AddResultID, "Add Result", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Add Result");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}
