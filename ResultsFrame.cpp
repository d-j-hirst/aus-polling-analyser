#include "ResultsFrame.h"

#include "EditPollFrame.h"
#include "EditSimulationFrame.h"
#include "NonClassicFrame.h"

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
	PA_ResultsFrame_AddResultID,
	PA_ResultsFrame_NonClassicID
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
	Bind(wxEVT_TOOL, &ResultsFrame::OnNonClassic, this, PA_ResultsFrame_NonClassicID);
}

void ResultsFrame::refreshData()
{
	resultsData->DeleteAllItems();
	resultsData->ClearColumns();

	resultsData->AppendTextColumn("Seat Name", wxDATAVIEW_CELL_INERT, 110, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	resultsData->AppendTextColumn("Swing", wxDATAVIEW_CELL_INERT, 50, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	resultsData->AppendTextColumn("Count %", wxDATAVIEW_CELL_INERT, 50, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	resultsData->AppendTextColumn("Updated", wxDATAVIEW_CELL_INERT, 80, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	for (int i = 0; i < project->getResultCount(); ++i) {
		addResultToResultData(project->getResult(i));
	}
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

	refreshData();

	parent->refreshSeatData();
}

void ResultsFrame::OnAddResult(wxCommandEvent & WXUNUSED(event))
{
	std::string enteredName = seatNameTextCtrl->GetLineText(0);
	auto seat = project->getSeatPtrByName(enteredName);
	if (!seat) {
		wxMessageBox("No seat found matching this name!");
		return;
	}
	Party const* const partyOne = project->getPartyPtr(0);
	Party const* const partyTwo = project->getPartyPtr(1);
	if (!seat->isClassic2pp(partyOne, partyTwo) && !seat->livePartyOne) {
		int result = wxMessageBox("This seat is currently using betting odds as it is considered to be non-classic. "
			"Should this be overridden so that the seat is indeed counted as being classic for the remained of this election? "
			" (You can always make it non-classic again by using the \"Non-classic\" tool.)", "Seat currently non-classic", wxYES_NO);
		if (result == wxYES) {
			seat->overrideBettingOdds = true;
		}
	}
	double swing; swingTextCtrl->GetLineText(0).ToDouble(&swing);
	double percentCounted; percentCountedTextCtrl->GetLineText(0).ToDouble(&percentCounted);
	long boothsIn; currentBoothCountTextCtrl->GetLineText(0).ToLong(&boothsIn);
	long totalBooths; totalBoothCountTextCtrl->GetLineText(0).ToLong(&totalBooths);
	if (percentCounted < 0.001) percentCounted = 0.0;
	Result result = Result(seat, swing, percentCounted, boothsIn, totalBooths);

	project->addResult(result);

	refreshData();
}

void ResultsFrame::OnNonClassic(wxCommandEvent & WXUNUSED(even))
{
	std::string enteredName = seatNameTextCtrl->GetLineText(0);
	auto seat = project->getSeatPtrByName(enteredName);
	if (!seat) {
		wxMessageBox("No seat found matching this name!");
		return;
	}

	// Create the new project frame (where initial settings for the new project are chosen).
	NonClassicFrame *frame = new NonClassicFrame(this, project, seat);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;

	refreshData();
}

void ResultsFrame::addResultToResultData(Result result)
{
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	float percentCounted = result.getPercentCountedEstimate();
	data.push_back(wxVariant(result.seat->name));
	data.push_back(wxVariant(formatFloat(result.incumbentSwing, 1)));
	data.push_back(wxVariant(formatFloat(percentCounted, 1)));
	data.push_back(wxVariant(result.updateTime.FormatISOTime()));
	resultsData->AppendItem(data);
}

void ResultsFrame::updateInterface()
{
}

void ResultsFrame::refreshToolbar()
{
	wxBitmap toolBarBitmaps[3];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\non_classic.png", wxBITMAP_TYPE_PNG);

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
	toolBar->AddTool(PA_ResultsFrame_NonClassicID, "Non-Classic Seat", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Non-Classic Seat");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}
