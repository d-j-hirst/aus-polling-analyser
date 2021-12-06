#include "ResultsFrame.h"

#include "EditPollFrame.h"
#include "EditSimulationFrame.h"
#include "Log.h"
#include "NonClassicFrame.h"

#include <wx/valnum.h>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 700, // To avoid mixing events with other frames.
	Frame,
	SummaryText,
	DataView,
	RunLiveSimulations,
	SeatName,
	Swing,
	PercentCounted,
	CurrentBoothCount,
	TotalBoothCount,
	AddResult,
	NonClassic,
	ClearAll,
	Filter
};

// frame constructor
ResultsFrame::ResultsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Results", wxPoint(0, 0), project),
	refresher(refresher)
{
	refreshToolbar();
	createSummaryBar();
	createDataTable();
	refreshData();
	bindEventHandlers();
}

void ResultsFrame::refreshData()
{
	refreshSummaryBar();
	refreshTable();
}

void ResultsFrame::createSummaryBar()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	constexpr int SummaryPanelHeight = 40;
	summaryPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), wxSize(GetClientSize().GetX(), SummaryPanelHeight));

	summaryText = new wxStaticText(summaryPanel, ControlId::SummaryText, "", wxPoint(0, 0), summaryPanel->GetClientSize());
	summaryText->SetBackgroundColour(wxColour(237, 237, 237));

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight + SummaryPanelHeight), GetClientSize() - wxSize(0, toolBarHeight + SummaryPanelHeight));
}

void ResultsFrame::createDataTable()
{
	resultsData = new wxGrid(dataPanel, ControlId::DataView, wxPoint(0, 0), dataPanel->GetClientSize());
}

void ResultsFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ResultsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ResultsFrame::OnRunLiveSimulations, this, ControlId::RunLiveSimulations);
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddResult, this, ControlId::AddResult);
	Bind(wxEVT_TOOL, &ResultsFrame::OnNonClassic, this, ControlId::NonClassic);
	Bind(wxEVT_COMBOBOX, &ResultsFrame::OnFilterSelection, this, ControlId::Filter);
	Bind(wxEVT_TOOL, &ResultsFrame::OnClearAll, this, ControlId::ClearAll);
}

void ResultsFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
	// Set the pollster data table to the entire client size.
	resultsData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ResultsFrame::OnRunLiveSimulations(wxCommandEvent & WXUNUSED(event))
{
	for (auto& [key, simulation] : project->simulations()) {
		if (simulation.isLive()) simulation.run(*project, [](std::string s) {wxMessageBox(s); });
	}

	refreshData();

	refresher.refreshSeatData();
}

void ResultsFrame::OnAddResult(wxCommandEvent & WXUNUSED(event))
{
	std::string enteredName = seatNameTextCtrl->GetLineText(0).ToStdString();
	try {
		auto [seatId, seat] = project->seats().accessByName(enteredName);

		// If appropriate, pops up a dialog box to ask user if they want to override the non-classic status
		confirmOverrideNonClassicStatus(seat);

		addEnteredOutcome(seatId);

		refreshData();
	}
	catch (SeatDoesntExistException) {
		wxMessageBox("No seat found matching this name!");
		return;
	}
}

void ResultsFrame::OnNonClassic(wxCommandEvent& WXUNUSED(even))
{
	std::string enteredName = seatNameTextCtrl->GetLineText(0).ToStdString();
	try {
		auto [seatId, seat] = project->seats().accessByName(enteredName);

		// Create the new project frame (where initial settings for the new project are chosen).
		NonClassicFrame* frame = new NonClassicFrame(project->parties(), seat);

		// Show the frame.
		frame->ShowModal();

		// This is needed to avoid a memory leak.
		delete frame;

		refreshData();
	}
	catch (SeatDoesntExistException) {
		wxMessageBox("No seat found matching this name!");
		return;
	}
}

void ResultsFrame::OnFilterSelection(wxCommandEvent& WXUNUSED(event))
{
	filter = Filter(filterComboBox->GetSelection());
	refreshData();
}

void ResultsFrame::OnClearAll(wxCommandEvent& WXUNUSED(even))
{
	auto dlog = wxTextEntryDialog(this, "This will delete ALL manually entered results. Please enter \"clear\" to confirm.");
	dlog.ShowModal();
	if (dlog.GetValue() == "clear") {
		project->outcomes().clear();
		refreshData();
	}
}

void ResultsFrame::addResultToResultData(Outcome result)
{
	auto const* report = latestReport();
	wxVector<wxVariant> data;
	auto seat = project->seats().viewByIndex(result.seat);
	wxColour swingColour = decideSwingColour(result);
	float percentCounted = result.getPercentCountedEstimate();
	wxColour percentCountedColour = decidePercentCountedColour(result);
	std::string projectedMarginString = decideProjectedMarginString(result);
	wxColour projectedMarginColour = decideProjectedMarginColour(result);
	std::string statusString = decideStatusString(result);
	wxColour statusColour = decideStatusColour(result);

	resultsData->AppendRows(1);
	int row = resultsData->GetNumberRows() - 1;
	resultsData->SetCellValue(row, 0, seat.name);
	resultsData->SetCellValue(row, 1, formatFloat(result.incumbentSwing, 1));
	resultsData->SetCellBackgroundColour(row, 1, swingColour);
	resultsData->SetCellValue(row, 2, formatFloat(percentCounted, 1));
	resultsData->SetCellBackgroundColour(row, 2, percentCountedColour);
	resultsData->SetCellValue(row, 3, result.updateTime.FormatISOTime());
	resultsData->SetCellValue(row, 4, projectedMarginString);
	resultsData->SetCellBackgroundColour(row, 4, projectedMarginColour);
	if (report && report->partyOneWinPercent.size()) {
		resultsData->SetCellValue(row, 5, formatFloat(report->partyOneWinPercent[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, 6, formatFloat(report->partyTwoWinPercent[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, 7, formatFloat(report->othersWinPercent[result.seat] * 100.0f, 2));
	}
	else {
		resultsData->SetCellValue(row, 5, "");
		resultsData->SetCellValue(row, 6, "");
		resultsData->SetCellValue(row, 7, "");
	}
	resultsData->SetCellValue(row, 8, statusString);
	resultsData->SetCellBackgroundColour(row, 8, statusColour);
	resultsData->Refresh();
}

void ResultsFrame::updateInterface()
{
}

void ResultsFrame::refreshToolbar()
{
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\non_classic.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	wxArrayString seatNames;
	for (auto const& [key, seat] : project->seats()) {
		seatNames.Add(seat.name);
	}

	auto seatNameStaticText = new wxStaticText(toolBar, wxID_ANY, "Seat name:");
	seatNameTextCtrl = new wxTextCtrl(toolBar, ControlId::SeatName, "", wxPoint(0, 0), wxSize(110, 22));
	seatNameTextCtrl->AutoComplete(seatNames);

	auto fpValidator = wxFloatingPointValidator<double>();
	fpValidator.SetPrecision(2);

	auto swingStaticText = new wxStaticText(toolBar, wxID_ANY, "Swing:");
	swingTextCtrl = new wxTextCtrl(toolBar, ControlId::Swing, "", wxPoint(0, 0), wxSize(45, 22), 0, fpValidator);

	auto percentCountedStaticText = new wxStaticText(toolBar, wxID_ANY, "% counted:");
	percentCountedTextCtrl = new wxTextCtrl(toolBar, ControlId::PercentCounted, "", wxPoint(0, 0), wxSize(37, 22), 0, fpValidator);

	auto currentBoothCountStaticText = new wxStaticText(toolBar, wxID_ANY, "Booths in:");
	currentBoothCountTextCtrl = new wxTextCtrl(toolBar, ControlId::CurrentBoothCount, "", wxPoint(0, 0), wxSize(25, 22), 0, wxIntegerValidator<int>());

	auto totalBoothCountStaticText = new wxStaticText(toolBar, wxID_ANY, "Booths total:");
	totalBoothCountTextCtrl = new wxTextCtrl(toolBar, ControlId::TotalBoothCount, "", wxPoint(0, 0), wxSize(25, 22), 0, wxIntegerValidator<int>());

	wxArrayString choices;
	choices.push_back("Show All Results");
	choices.push_back("Show Latest Results");
	choices.push_back("Show Significant Results");
	choices.push_back("Show Key Results");
	filterComboBox = new wxComboBox(toolBar, ControlId::Filter, "Show All Results", wxPoint(0, 0), wxSize(160, 22), choices);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(ControlId::RunLiveSimulations, "Run Model", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Run Live Simulations");
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
	toolBar->AddTool(ControlId::AddResult, "Add Result", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Add Result");
	toolBar->AddTool(ControlId::NonClassic, "Non-Classic Seat", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Non-Classic Seat");
	toolBar->AddControl(filterComboBox);
	toolBar->AddTool(ControlId::ClearAll, "Clear All Results", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Clear All Results");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void ResultsFrame::refreshSummaryBar()
{
	summaryText->SetLabel(decideSummaryString());
}

void ResultsFrame::refreshTable()
{
	resultsData->BeginBatch(); // prevent updates while doing a lot of grid modifications

	resetTableColumns();

	addTableData();

	resultsData->EndBatch(); // refresh grid data on screen
}

bool ResultsFrame::resultPassesFilter(Outcome const& thisResult)
{
	if (filter == Filter::AllResults) return true;
	Seat const& seat = project->seats().view(thisResult.seat);
	// if (seat.outcome && seat.outcome->updateTime != thisResult.updateTime) return false;
	if (filter == Filter::LatestResults) return true;

	float significance = 0.0f;
	significance += std::max(0.0f, 3.0f / (1.0f + std::max(2.0f, abs(seat.tppMargin))));
	auto const* report = latestReport();
	if (report && report->seatPartyOneMarginAverage.size()) {
		double simulatedMarginAverage = report->seatPartyOneMarginAverage[thisResult.seat];
		significance += std::max(0.0f, 10.0f / (1.0f + std::max(1.0f, abs(float(simulatedMarginAverage)))));
		// automatically treat seats expected to change hands as significant
		// this can be detected by the simulated tpp average and previous margin having opposite signs
		if (simulatedMarginAverage * seat.tppMargin < 0.0f) significance += 5.0f;
	}

	if (filter == Filter::SignificantResults) return significance > 1.5f;
	if (filter == Filter::KeyResults) return significance > 5.0f;

	// replace later
	return true;
}

wxColour ResultsFrame::decideSwingColour(Outcome const& thisResult)
{
	auto const& seat = project->seats().viewByIndex(thisResult.seat);
	Party::Colour swingPartyColour = (thisResult.incumbentSwing > 0.0f ?
		project->parties().view(seat.incumbent).colour : project->parties().view(seat.challenger).colour);
	Party::Colour inverseColour = Party::Colour{ 255 - swingPartyColour.r, 255 - swingPartyColour.g, 255 - swingPartyColour.b };
	float incSw = std::min(1.0f, float(abs(thisResult.incumbentSwing)) * 0.08f);
	return wxColour(255 - int(inverseColour.r * incSw), 255 - int(inverseColour.g * incSw), 255 - int(inverseColour.b * incSw));
}

wxColour ResultsFrame::decidePercentCountedColour(Outcome const & thisResult)
{
	float percentCounted = thisResult.getPercentCountedEstimate();
	return wxColour(255 - std::min(255, int(percentCounted * 2.55f)), 255, 255 - std::min(255, int(percentCounted * 2.55f)));
}

std::string ResultsFrame::decideProjectedMarginString(Outcome const& thisResult)
{
	// *** Currently messed up because of changing margins from incumbent-relative to party-one-relative
	//     Fix later once reporting of non-classic results is properly sorted
	auto const* report = latestReport();
	if (!report || !report->seatPartyOneMarginAverage.size()) return "";
	double simulatedMarginAverage = report->seatPartyOneMarginAverage[thisResult.seat];
	Seat const& seat = project->seats().view(thisResult.seat);
	float projectedSwing = simulatedMarginAverage - seat.tppMargin;
	return formatFloat(simulatedMarginAverage, 2) + " (" +
		(projectedSwing >= 0 ? "+" : "") + formatFloat(projectedSwing, 2) + ")";
}

wxColour ResultsFrame::decideProjectedMarginColour(Outcome const& thisResult)
{
	// *** Currently messed up because of changing margins from incumbent-relative to party-one-relative
	//     Fix later once reporting of non-classic results is properly sorted
	auto const* report = latestReport();
	if (report) {
		double simulatedMarginAverage = report->seatPartyOneMarginAverage[thisResult.seat];
		float margin = abs(simulatedMarginAverage);
		float marginSignificance = (margin ? 1.0f / (1.0f + margin) : 0.0f);
		wxColour projectedMarginColour = wxColour(int(255.f), int(255.f - marginSignificance * 255.f), int(255.f - marginSignificance * 255.f));
		return projectedMarginColour;
	}
	return *wxWHITE;
}

std::string ResultsFrame::decideLeadingPartyName(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!report || !report->partyOneWinPercent.size()) return "";
	float p1 = float(report->partyOneWinPercent[thisResult.seat]);
	float p2 = float(report->partyTwoWinPercent[thisResult.seat]);
	float p3 = float(report->othersWinPercent[thisResult.seat]);
	Party::Id thisParty = (p1 > p2 && p1 > p3 ? 0 : (p2 > p3 ? 1 : -1));
	std::string leadingPartyName = (thisParty != Party::InvalidId ? project->parties().view(thisParty).abbreviation : "OTH");
	return leadingPartyName;
}

float ResultsFrame::decideLeaderProbability(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!report || !report->partyOneWinPercent.size()) return 0.0f;
	float p1 = float(report->partyOneWinPercent[thisResult.seat]);
	float p2 = float(report->partyTwoWinPercent[thisResult.seat]);
	float p3 = float(report->othersWinPercent[thisResult.seat]);
	float leaderProb = std::max(p1 * 100.0f, std::max(p2 * 100.0f, p3 * 100.0f));
	return leaderProb;
}

std::string ResultsFrame::decideLikelihoodString(Outcome const& thisResult)
{
	float leaderProb = decideLeaderProbability(thisResult);
	int likelihoodRating = (leaderProb < 60.0f ? 0 : (leaderProb < 75.0f ? 1 : (leaderProb < 90.0f ? 2 : (
		leaderProb < 98.0f ? 3 : (leaderProb < 99.9f ? 4 : 5)))));
	std::string likelihoodString = (likelihoodRating == 0 ? "Slight Lean" : (likelihoodRating == 1 ? "Lean" :
		(likelihoodRating == 2 ? "Likely" : (likelihoodRating == 3 ? "Very Likely" : (
		(likelihoodRating == 4 ? "Solid" : "Safe"))))));
	return likelihoodString;
}

std::string ResultsFrame::decideStatusString(Outcome const& thisResult)
{
	return decideLikelihoodString(thisResult) + " (" +
		formatFloat(decideLeaderProbability(thisResult), 2) + "%) " + 
		decideLeadingPartyName(thisResult);
}

wxColour ResultsFrame::decideStatusColour(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!report || !report->partyOneWinPercent.size()) return *wxWHITE;
	float p1 = float(report->partyOneWinPercent[thisResult.seat]);
	float p2 = float(report->partyTwoWinPercent[thisResult.seat]);
	float p3 = float(report->othersWinPercent[thisResult.seat]);
	float leaderProb = std::max(p1 * 100.0f, std::max(p2 * 100.0f, p3 * 100.0f));
	Party::Id thisParty = (p1 > p2 && p1 > p3 ? 0 : (p2 > p3 ? 1 : -1));
	int likelihoodRating = (leaderProb < 60.0f ? 0 : (leaderProb < 75.0f ? 1 : (leaderProb < 90.0f ? 2 : (
		leaderProb < 98.0f ? 3 : (leaderProb < 99.9f ? 4 : 5)))));
	float lightnessFactor = (float(5 - likelihoodRating) * 0.2f) * 0.8f;
	wxColour statusColour = wxColour(int(255.0f * lightnessFactor + float(thisParty != Party::InvalidId ? project->parties().view(thisParty).colour.r : 128) * (1.0f - lightnessFactor)),
		int(255.0f * lightnessFactor + float(thisParty != Party::InvalidId ? project->parties().view(thisParty).colour.g : 128) * (1.0f - lightnessFactor)),
		int(255.0f * lightnessFactor + float(thisParty != Party::InvalidId ? project->parties().view(thisParty).colour.b : 128) * (1.0f - lightnessFactor)));
	return statusColour;
}

void ResultsFrame::confirmOverrideNonClassicStatus(Seat& seat)
{
	if ((!seat.isClassic2pp() || seat.challenger2Odds < 8.0f) &&
		!seat.livePartyOne && !seat.overrideBettingOdds) {
		int result = wxMessageBox("This seat is currently using betting odds as it is considered to be non-classic. "
			"Should this be overridden so that the seat is indeed counted as being classic for the remained of this election? "
			" (You can always make it non-classic again by using the \"Non-classic\" tool.)", "Seat currently non-classic", wxYES_NO);
		if (result == wxYES) {
			seat.overrideBettingOdds = true;
		}
	}
}

void ResultsFrame::addEnteredOutcome(Seat::Id seatId)
{
	// wxWidgets requires these roundabout ways of getting values from the text boxes
	double swing; swingTextCtrl->GetLineText(0).ToDouble(&swing);
	double percentCounted; percentCountedTextCtrl->GetLineText(0).ToDouble(&percentCounted);
	long boothsIn; currentBoothCountTextCtrl->GetLineText(0).ToLong(&boothsIn);
	long totalBooths; totalBoothCountTextCtrl->GetLineText(0).ToLong(&totalBooths);

	if (percentCounted < 0.001) percentCounted = 0.0;
	Outcome outcome = Outcome(seatId, swing, percentCounted, boothsIn, totalBooths);

	project->outcomes().add(outcome);
}

std::string ResultsFrame::decideSummaryString()
{
	std::string party1 = project->parties().view(0).abbreviation;
	std::string party2 = project->parties().view(1).abbreviation;
	auto const* report = latestReport();
	if (!report || !report->partyOneWinPercent.size()) return "";
	std::string summaryString = party1 + " win chance: " + formatFloat(report->getPartyOverallWinPercent(Simulation::MajorParty::One), 2) +
		"   Projected 2PP: " + party1 + " " + formatFloat(float(report->getPartyOne2pp()), 2) +
		"   Seats: " + party1 + " " + formatFloat(report->getPartyWinExpectation(0), 2) + " " +
		party2 + " " + formatFloat(report->getPartyWinExpectation(1), 2) +
		" Others " + formatFloat(report->getOthersWinExpectation(), 2) +
		"   Count progress: " + formatFloat(report->get2cpPercentCounted(), 2) + "%\n" +
		party1 + " swing by region: ";
	//for (auto const& regionPair : project->regions()) {
	//	Region const& thisRegion = regionPair.second;
	//	summaryString += thisRegion.name + " " + formatFloat(thisRegion.liveSwing, 2) + " ";
	//}
	return summaryString;
}

Simulation::Report const* ResultsFrame::latestReport()
{
	for (auto const& [key, simulation] : project->simulations()) {
		if (simulation.isLive() && simulation.isValid()) {
			return &simulation.getLatestReport();
		}
	}
	return nullptr;
}

void ResultsFrame::resetTableColumns()
{
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
}

void ResultsFrame::addTableData()
{
	for (int i = 0; i < project->outcomes().count(); ++i) {
		Outcome thisResult = project->outcomes().get(i);
		if (resultPassesFilter(thisResult)) addResultToResultData(thisResult);
	}
}
