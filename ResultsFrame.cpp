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
	PA_ResultsFrame_NonClassicID,
	PA_ResultsFrame_FilterID
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
	resultsData = new wxGrid(dataPanel,
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
	Bind(wxEVT_COMBOBOX, &ResultsFrame::OnFilterSelection, this, PA_ResultsFrame_FilterID);
}

void ResultsFrame::refreshData()
{
	resultsData->BeginBatch(); // prevent updated while doing a lot of grid modifications

	if (!resultsData->GetNumberCols()) {
		resultsData->CreateGrid(0, int(9), wxGrid::wxGridSelectCells);
		resultsData->SetColLabelValue(0, "Seat Name");
		resultsData->SetColLabelValue(1, "Swing");
		resultsData->SetColLabelValue(2, "Count %");
		resultsData->SetColLabelValue(3, "Updated");
		resultsData->SetColLabelValue(4, "Proj. Margin");
		resultsData->SetColLabelValue(5, "ALP prob.");
		resultsData->SetColLabelValue(6, "LNP prob.");
		resultsData->SetColLabelValue(7, "Other prob.");
		resultsData->SetColLabelValue(8, "Status");
		resultsData->SetColSize(0, 100);
		resultsData->SetColSize(1, 40);
		resultsData->SetColSize(2, 60);
		resultsData->SetColSize(3, 60);
		resultsData->SetColSize(4, 85);
		resultsData->SetColSize(5, 70);
		resultsData->SetColSize(6, 70);
		resultsData->SetColSize(7, 70);
		resultsData->SetColSize(8, 150);
		resultsData->SetRowLabelSize(0);
	}

	if (resultsData->GetNumberRows()) resultsData->DeleteRows(0, resultsData->GetNumberRows());

	for (int i = 0; i < project->getResultCount(); ++i) {
		Result thisResult = project->getResult(i);
		if (resultPassesFilter(thisResult)) addResultToResultData(thisResult);
	}

	resultsData->EndBatch(); // refresh grid data on screen
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
		if (simulation->isLive()) simulation->run(*project);
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
	if ((!seat->isClassic2pp(partyOne, partyTwo, true) || seat->challenger2Odds < 8.0f) &&
			!seat->livePartyOne && !seat->overrideBettingOdds) {
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

void ResultsFrame::OnFilterSelection(wxCommandEvent& WXUNUSED(event))
{
	filter = Filter(filterComboBox->GetSelection());
	refreshData();
}

void ResultsFrame::addResultToResultData(Result result)
{
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	Party::Colour swingPartyColour = (result.incumbentSwing > 0.0f ? result.seat->incumbent->colour : result.seat->challenger->colour);
	Party::Colour inverseColour = Party::Colour{ 255 - swingPartyColour.r, 255 - swingPartyColour.g, 255 - swingPartyColour.b };
	float incSw = std::min(1.0f, float(abs(result.incumbentSwing)) * 0.08f);
	wxColour swingColour = wxColour(255 - int(inverseColour.r * incSw), 255 - int(inverseColour.g * incSw), 255 - int(inverseColour.b * incSw));
	float percentCounted = result.getPercentCountedEstimate();
	wxColour percentCountedColour = wxColour(255 - std::min(255, int(result.percentCounted * 2.55f)), 255, 255 - std::min(255, int(result.percentCounted * 2.55f)));
	float projectedSwing = result.seat->simulatedMarginAverage - result.seat->margin;
	std::string projectedMarginString = formatFloat(result.seat->simulatedMarginAverage, 2) + " (" +
		(projectedSwing >= 0 ? "+" : "") + formatFloat(projectedSwing, 2) + ")";
	float margin = abs(result.seat->simulatedMarginAverage);
	float marginSignificance = (margin ? 1.0f / (1.0f + abs(result.seat->simulatedMarginAverage)) : 0.0f);
	wxColour projectedMarginColour = wxColour(int(255.f), int(255.f - marginSignificance * 255.f), int(255.f - marginSignificance * 255.f));
	float p1 = result.seat->partyOneWinRate;
	float p2 = result.seat->partyTwoWinRate;
	float p3 = result.seat->partyOthersWinRate;
	float leaderProb = std::max(result.seat->partyOneWinRate * 100.0f,
		std::max(result.seat->partyTwoWinRate * 100.0f, result.seat->partyOthersWinRate * 100.0f));
	Party const* thisParty = (p1 > p2 && p1 > p3 ? project->getPartyPtr(0)
		: (p2 > p3 ? project->getPartyPtr(1) : nullptr));
	//PrintDebugFloat(p1);
	//PrintDebugFloat(p2);
	//PrintDebugFloat(p3);
	//PrintDebugFloat(leaderProb);
	//if (thisParty) PrintDebug(thisParty->name + " ");
	//PrintDebugLine(result.seat->name);
	std::string leadingPartyName = (thisParty ? thisParty->abbreviation : "OTH");
	int likelihoodRating = (leaderProb < 60.0f ? 0 : (leaderProb < 75.0f ? 1 : (leaderProb < 90.0f ? 2 : (
		leaderProb < 98.0f ? 3 : (leaderProb < 99.9f ? 4 : 5)))));
	std::string likelihoodString = (likelihoodRating == 0 ? "Slight Lean" : (likelihoodRating == 1 ? "Lean" :
		(likelihoodRating == 2 ? "Likely" : (likelihoodRating == 3 ? "Very Likely" : (
		(likelihoodRating == 4 ? "Solid" :  "Safe"))))));
	std::string statusString = likelihoodString + " (" + formatFloat(leaderProb, 2) + ") " + leadingPartyName;
	float lightnessFactor = (float(5 - likelihoodRating) * 0.2f) * 0.8f;
	wxColour resultColour = wxColour(int(255.0f * lightnessFactor + float(thisParty ? thisParty->colour.r : 128) * (1.0f - lightnessFactor)),
		int(255.0f * lightnessFactor + float(thisParty ? thisParty->colour.g : 128) * (1.0f - lightnessFactor)),
		int(255.0f * lightnessFactor + float(thisParty ? thisParty->colour.b : 128) * (1.0f - lightnessFactor)));

	resultsData->AppendRows(1);
	int row = resultsData->GetNumberRows() - 1;
	resultsData->SetCellValue(row, 0, result.seat->name);
	resultsData->SetCellValue(row, 1, formatFloat(result.incumbentSwing, 1));
	resultsData->SetCellBackgroundColour(row, 1, swingColour);
	resultsData->SetCellValue(row, 2, formatFloat(percentCounted, 1));
	resultsData->SetCellBackgroundColour(row, 2, percentCountedColour);
	resultsData->SetCellValue(row, 3, result.updateTime.FormatISOTime());
	resultsData->SetCellValue(row, 4, projectedMarginString);
	resultsData->SetCellBackgroundColour(row, 4, projectedMarginColour);
	resultsData->SetCellValue(row, 5, formatFloat(result.seat->partyOneWinRate * 100.0f, 2));
	resultsData->SetCellValue(row, 6, formatFloat(result.seat->partyTwoWinRate * 100.0f, 2));
	resultsData->SetCellValue(row, 7, formatFloat(result.seat->partyOthersWinRate * 100.0f, 2));
	resultsData->SetCellValue(row, 8, statusString);
	resultsData->SetCellBackgroundColour(row, 8, resultColour);
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

	wxArrayString choices;
	choices.push_back("Show All Results");
	choices.push_back("Show Latest Results");
	choices.push_back("Show Significant Results");
	choices.push_back("Show Key Results");
	filterComboBox = new wxComboBox(toolBar, PA_ResultsFrame_FilterID, "Show All Results", wxPoint(0, 0), wxSize(160, 22), choices);

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
	toolBar->AddControl(filterComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

bool ResultsFrame::resultPassesFilter(Result const& thisResult)
{
	if (filter == Filter::AllResults) return true;
	if (thisResult.seat->latestResult->updateTime != thisResult.updateTime) return false;
	if (filter == Filter::LatestResults) return true;

	float significance = 0.0f;
	significance += std::max(0.0f, 3.0f / (1.0f + std::max(2.0f, abs(thisResult.seat->margin))));
	if (thisResult.seat->simulatedMarginAverage) {
		significance += std::max(0.0f, 10.0f / (1.0f + std::max(1.0f, abs(float(thisResult.seat->simulatedMarginAverage)))));
	}
	if (thisResult.seat->simulatedMarginAverage < 0.0f) significance += 5.0f; // automatically treat seats changing hands as significant

	if (filter == Filter::SignificantResults) return significance > 1.5f;
	if (filter == Filter::KeyResults) return significance > 5.0f;

	// replace later
	return true;
}
