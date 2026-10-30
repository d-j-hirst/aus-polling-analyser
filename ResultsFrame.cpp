#include "ResultsFrame.h"

#include "Beep.h"
#include "EditPollFrame.h"
#include "EditSimulationFrame.h"
#include "LiveResultsInput.h"
#include "LiveSnapshotSequenceDialog.h"
#include "LiveRunExport.h"
#include "LiveInputWarningPanel.h"
#include "Log.h"
#include "NonClassicFrame.h"

#include <wx/numdlg.h>
#include <wx/gauge.h>
#include <wx/utils.h>
#include <wx/valnum.h>
#include <wx/clipbrd.h>

#include <filesystem>
#include <exception>
#include <fstream>
#include <optional>
#include <set>

namespace {

// Keep cell population, labels and browsing anchors tied to the same columns.
enum ResultColumn { ResultSeatName, RawSwing, EstimatedSwing, Counted, TcpBasis, Updated,
	ProjectedMargin, PartyOneProbability, PartyTwoProbability, OtherProbability, Status, ColumnCount };

// Reloaded projects can contain counted results before a live simulation has
// produced a report. Forecast cells stay blank until that seat's data exists.
bool hasProjectedMargin(Simulation::Report const* report, int seat)
{
	return report && seat >= 0 &&
		std::size_t(seat) < report->seatPartyOneMarginAverage.size();
}

bool hasWinProbabilities(Simulation::Report const* report, int seat)
{
	return report && seat >= 0 &&
		std::size_t(seat) < report->partyOneWinProportion.size() &&
		std::size_t(seat) < report->partyTwoWinProportion.size() &&
		std::size_t(seat) < report->othersWinProportion.size();
}

struct ResultRowIdentity {
	std::string seat, updated;
	bool operator==(ResultRowIdentity const&) const = default;
};

ResultRowIdentity resultRowIdentity(wxGrid& grid, int row) {
	if (row < 0 || row >= grid.GetNumberRows()) return {};
	return {grid.GetCellValue(row,ResultSeatName).ToStdString(), grid.GetCellValue(row,Updated).ToStdString()};
}

struct ResultsBrowseState {
	int x = 0, y = 0, unitY = 1, offsetY = 0, cursorColumn = -1;
	ResultRowIdentity anchor, cursor;
	std::vector<std::pair<ResultRowIdentity,std::vector<bool>>> selection;
};

ResultsBrowseState captureResultsBrowsing(wxGrid& grid) {
	ResultsBrowseState state;
	int unitX = 1;
	grid.GetViewStart(&state.x,&state.y);
	grid.GetScrollPixelsPerUnit(&unitX,&state.unitY);
	int top = grid.YToRow(state.y * state.unitY);
	state.anchor = resultRowIdentity(grid,top);
	if (top >= 0) state.offsetY = state.y * state.unitY - grid.CellToRect(top,0).GetTop();
	state.cursor = resultRowIdentity(grid,grid.GetGridCursorRow());
	state.cursorColumn = grid.GetGridCursorCol();
	// Retain selected cells by outcome identity, not row number: inserting a
	// completed snapshot at the top must not silently select different seats.
	if (grid.IsSelection()) for (int row = 0; row < grid.GetNumberRows(); ++row) {
		std::vector<bool> columns(grid.GetNumberCols());
		bool selected = false;
		for (int column = 0; column < grid.GetNumberCols(); ++column)
			selected |= columns[column] = grid.IsInSelection(row,column);
		if (selected) state.selection.emplace_back(resultRowIdentity(grid,row),std::move(columns));
	}
	return state;
}

void restoreResultsBrowsing(wxGrid& grid, ResultsBrowseState const& state) {
	auto find = [&](ResultRowIdentity const& identity) {
		int sameSeat = -1;
		if (identity.seat.empty()) return sameSeat;
		for (int row = 0; row < grid.GetNumberRows(); ++row) {
			auto value = resultRowIdentity(grid,row);
			if (value == identity) return row;
			if (sameSeat < 0 && value.seat == identity.seat) sameSeat = row;
		}
		// In Latest/Significant/Key the old outcome is replaced by its seat's
		// new result. Stay with that seat when the exact old row is no longer shown.
		return sameSeat;
	};
	std::map<int,std::vector<bool>> selected;
	for (auto const& [identity,columns] : state.selection)
		if (int row = find(identity); row >= 0) selected[row] = columns;
	grid.ClearSelection();
	for (auto it = selected.begin(); it != selected.end();) {
		int firstRow = it->first, lastRow = firstRow;
		auto columns = it->second;
		while (++it != selected.end() && it->first == lastRow + 1 && it->second == columns) lastRow = it->first;
		// Merge neighbouring rows/columns so a large rectangular selection is
		// restored with a few blocks rather than thousands of individual cells.
		for (int column = 0; column < int(columns.size()); ++column) if (columns[column]) {
			int firstColumn = column;
			while (column + 1 < int(columns.size()) && columns[column + 1]) ++column;
			grid.SelectBlock(firstRow,firstColumn,lastRow,column,true);
		}
	}
	if (int cursor = find(state.cursor); cursor >= 0 && state.cursorColumn >= 0)
		grid.SetGridCursor(cursor,state.cursorColumn);
	int y = state.y;
	if (int top = find(state.anchor); top >= 0)
		y = (grid.CellToRect(top,0).GetTop() + state.offsetY) / std::max(1,state.unitY);
	grid.Scroll(state.x,y);
}

}

// IDs for the controls and the menu commands
enum ControlId {
	Base = 700, // To avoid mixing events with other frames.
	Frame,
	SummaryText,
	DataView,
	RunLiveSimulations,
	ConfigureSnapshotSequence,
	RunSnapshotSequence,
	SeatName,
	Swing,
	PercentCounted,
	CurrentBoothCount,
	TotalBoothCount,
	AddResult,
	AddFromScript,
	NonClassic,
	ClearAll,
	Filter
};

// frame constructor
ResultsFrame::ResultsFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Results", wxPoint(0, 0), project),
	refresher(refresher)
{
	allowDuringLiveRun({ControlId::Filter});
	refreshToolbar();
	createSummaryBar();
	createDataTable();
	refreshData();
	bindEventHandlers();
}

void ResultsFrame::refreshData()
{
	refreshSummaryBar();
	refreshInputWarnings();
	refreshTable();
}

void ResultsFrame::createSummaryBar()
{
	int toolBarHeight = toolBar->GetSize().GetHeight();

	constexpr int SummaryPanelHeight = 40;
	summaryPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), wxSize(GetClientSize().GetX(), SummaryPanelHeight));

	summaryText = new wxStaticText(summaryPanel, ControlId::SummaryText, "", wxPoint(0, 0), summaryPanel->GetClientSize());
	summaryText->SetBackgroundColour(wxColour(237, 237, 237));
	summaryText->SetToolTip("Counted FP and TCP: accepted published votes divided by the same estimated final formal turnout. "
		"TCP includes classic TPP, excludes FP-derived estimates, and follows the current published candidate pair after a reset. "
		"Exhausted votes are not part of the two candidates' TCP totals. Older saved reports may have no coverage data.");
	inputWarnings = new LiveInputWarningPanel(this);

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight + SummaryPanelHeight), GetClientSize() - wxSize(0, toolBarHeight + SummaryPanelHeight));
}

void ResultsFrame::createDataTable()
{
	resultsData = new wxGrid(dataPanel, ControlId::DataView, wxPoint(0, 0), dataPanel->GetClientSize());
}

void ResultsFrame::bindEventHandlers()
{
	resultsData->Bind(wxEVT_KEY_DOWN, &ResultsFrame::OnResultKey, this);
	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &ResultsFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &ResultsFrame::OnRunLiveSimulations, this, ControlId::RunLiveSimulations);
	Bind(wxEVT_BUTTON, &ResultsFrame::OnConfigureSnapshotSequence, this, ControlId::ConfigureSnapshotSequence);
	Bind(wxEVT_BUTTON, &ResultsFrame::OnRunSnapshotSequence, this, ControlId::RunSnapshotSequence);
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddResult, this, ControlId::AddResult);
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddFromScript, this, ControlId::AddFromScript);
	Bind(wxEVT_TOOL, &ResultsFrame::OnNonClassic, this, ControlId::NonClassic);
	Bind(wxEVT_COMBOBOX, &ResultsFrame::OnFilterSelection, this, ControlId::Filter);
	Bind(wxEVT_TOOL, &ResultsFrame::OnClearAll, this, ControlId::ClearAll);
}

void ResultsFrame::OnResultKey(wxKeyEvent& event) {
	if (event.CmdDown() && (event.GetKeyCode() == 'C' || event.GetKeyCode() == 'c')) copySelectedResults();
	else event.Skip();
}

void ResultsFrame::copySelectedResults() {
	// Copying displayed text is a viewing action, including during a run. The
	// grid is never editable; Ctrl+C does not touch counted outcomes or reports.
	if (!resultsData->GetNumberRows()) return;
	int top = resultsData->GetNumberRows(), bottom = -1;
	int left = resultsData->GetNumberCols(), right = -1;
	auto include = [&](int row, int column) {
		top = std::min(top,row); bottom = std::max(bottom,row);
		left = std::min(left,column); right = std::max(right,column);
	};
	// wxWidgets 3.1's coordinate array exposes indexed access, not iterators.
	auto cells = resultsData->GetSelectedCells();
	for (std::size_t i = 0; i < cells.size(); ++i) include(cells[i].GetRow(),cells[i].GetCol());
	auto tops = resultsData->GetSelectionBlockTopLeft(), bottoms = resultsData->GetSelectionBlockBottomRight();
	for (std::size_t i = 0; i < tops.size(); ++i) { include(tops[i].GetRow(),tops[i].GetCol()); include(bottoms[i].GetRow(),bottoms[i].GetCol()); }
	for (auto row : resultsData->GetSelectedRows()) { include(row,0); include(row,resultsData->GetNumberCols()-1); }
	for (auto column : resultsData->GetSelectedCols()) { include(0,column); include(resultsData->GetNumberRows()-1,column); }
	bool selected = bottom >= 0;
	if (!selected) include(resultsData->GetGridCursorRow(),resultsData->GetGridCursorCol());
	if (top < 0 || left < 0 || bottom < 0 || right < 0) return;
	wxString text;
	for (int row = top; row <= bottom; ++row) {
		if (row != top) text += "\n";
		for (int column = left; column <= right; ++column) {
			if (column != left) text += "\t";
			if (!selected || resultsData->IsInSelection(row,column)) text += resultsData->GetCellValue(row,column);
		}
	}
	if (wxTheClipboard->Open()) { wxTheClipboard->SetData(new wxTextDataObject(text)); wxTheClipboard->Close(); }
}

void ResultsFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
	layoutResultPanels();
}

void ResultsFrame::layoutResultPanels()
{
	if (!dataPanel || !resultsData) return;
	int width = GetClientSize().GetWidth(), top = toolBar->GetSize().GetHeight();
	summaryPanel->SetSize(0,top,width,40); summaryText->SetSize(summaryPanel->GetClientSize());
	top += 40;
	int warningHeight = inputWarnings->IsShown() ? 170 : 0;
	inputWarnings->SetSize(0,top,width,warningHeight); top += warningHeight;
	dataPanel->SetSize(0,top,width,std::max(0,GetClientSize().GetHeight()-top));
	resultsData->SetSize(dataPanel->GetClientSize() + wxSize(0,1));
}

void ResultsFrame::refreshInputWarnings()
{
	// Each attempt retains its own structured diagnostics even if it fails.
	// The usual project has one automatic live simulation; its latest attempt
	// remains visible while the previous numerical forecast stays on screen.
	for (auto const& [id,simulation] : project->simulations()) {
		if (!simulation.isLiveAutomatic() || !simulation.getLiveInputDiagnostic().is_object()) continue;
		inputWarnings->setDiagnostic(simulation.getLiveInputDiagnostic(),simulation.getLiveInputDetails());
		layoutResultPanels(); return;
	}
	inputWarnings->setDiagnostic({},{}); layoutResultPanels();
}

void ResultsFrame::OnRunLiveSimulations(wxCommandEvent& WXUNUSED(event))
{
	if (refresher.isLiveRunning()) return;
	if (wxGetKeyState(WXK_SHIFT)) {
		wxCommandEvent command;
		OnRunSnapshotSequence(command);
		return;
	}
	refresher.startLiveRun({std::nullopt});
}

std::string ResultsFrame::snapshotElection() const
{
	// Resolve the election from the configured automatic simulations before
	// opening or running a sequence. These settings can legitimately be absent
	// or inconsistent while the user edits a project, so report a configuration
	// error that can be excluded from debugger breaks on calculation failures.
	std::string election;
	for (auto const& [key, simulation] : project->simulations()) {
		if (!simulation.isLiveAutomatic()) continue;
		if (project->projections().idToIndex(simulation.getSettings().baseProjection) == ProjectionCollection::InvalidIndex)
			throw LiveSnapshotSequence::ConfigurationError("An automatic live simulation has no valid base projection.");
		auto const& projection = project->projections().view(simulation.getSettings().baseProjection);
		auto term = projection.getBaseModel(project->models()).getTermCode();
		if (!election.empty() && election != term)
			throw LiveSnapshotSequence::ConfigurationError("All automatic live simulations must use the same election for a snapshot sequence.");
		election = std::move(term);
	}
	if (election.empty()) throw LiveSnapshotSequence::ConfigurationError("No automatic live simulation is configured.");
	return election;
}

void ResultsFrame::ensureSnapshotOutputFolders(std::string const& election)
{
	// Live Booths reads the usual retained diagnostic exports. Give every
	// participating simulation an independent default when export was disabled.
	std::set<std::string> used;
	for (auto const& [key, simulation] : project->simulations())
		if (!simulation.getSettings().liveOutputFolder.empty()) used.insert(simulation.getSettings().liveOutputFolder);
	for (auto& [key, simulation] : project->simulations()) {
		if (!simulation.isLiveAutomatic()) continue;
		auto settings = simulation.getSettings();
		if (settings.liveOutputFolder.empty()) {
			auto base = election + "-snapshot-sequence-" + std::to_string(key);
			settings.liveOutputFolder = base;
			for (int suffix = 2; used.contains(settings.liveOutputFolder); ++suffix)
				settings.liveOutputFolder = base + "-" + std::to_string(suffix);
			used.insert(settings.liveOutputFolder);
			simulation.replaceSettings(std::move(settings));
		}
	}
}

void ResultsFrame::OnConfigureSnapshotSequence(wxCommandEvent& WXUNUSED(event))
{
	if (refresher.isLiveRunning()) return;
	try {
		auto settings = project->snapshotSequence();
		auto election = snapshotElection();
		if (!settings.election.empty() && settings.election != election) settings = {};
		settings.election = election;
		LiveSnapshotSequenceDialog dialog(this, project->paths().root(), settings);
		if (dialog.ShowModal() != wxID_OK) return;
		project->setSnapshotSequence(dialog.selection());
		ensureSnapshotOutputFolders(election);
		refresher.refreshLiveBooths();
	}
	catch (std::exception const& error) {
		wxMessageBox(wxString::FromUTF8(error.what()), "Cannot configure snapshot sequence", wxOK | wxICON_ERROR, this);
	}
}

void ResultsFrame::OnRunSnapshotSequence(wxCommandEvent&)
{
    if (refresher.isLiveRunning()) return;
    try {
        auto settings = project->snapshotSequence();
        if (settings.election != snapshotElection())
            throw LiveSnapshotSequence::ConfigurationError("Configure and save a sequence for this election first.");
        if (settings.codes.empty())
            throw LiveSnapshotSequence::ConfigurationError("No snapshot codes are selected. Use Configure snapshot sequence first.");
        auto available = LiveSnapshotSequence::discover(
            LiveSnapshotSequence::directory(project->paths().root(), settings), settings.election);
        if (!available.error.empty()) {
            wxMessageBox(wxString::FromUTF8(available.error), "Cannot run snapshot sequence", wxOK | wxICON_ERROR, this);
            return;
        }
        auto files = LiveSnapshotSequence::resolve(available.files, settings.codes);
        ensureSnapshotOutputFolders(settings.election);
        for (auto const& [id, simulation] : project->simulations()) {
            if (!simulation.isLive()) continue;
            if (!simulation.isLiveAutomatic())
                throw LiveSnapshotSequence::ConfigurationError("Manual live simulations are not supported by the current live pipeline.");
            if (auto error = LiveRunExport::validateOutputFolder(simulation.getSettings().liveOutputFolder))
                throw LiveSnapshotSequence::ConfigurationError(*error);
        }
        std::vector<LiveSimulationJob::Input> inputs;
        for (auto& file : files) inputs.push_back(std::move(file));
        refresher.startLiveRun(std::move(inputs));
    } catch (std::exception const& error) {
        wxMessageBox(wxString::FromUTF8(error.what()), "Cannot run snapshot sequence", wxOK | wxICON_ERROR, this);
    }
}

void ResultsFrame::OnAddResult(wxCommandEvent& WXUNUSED(event))
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

void ResultsFrame::OnAddFromScript(wxCommandEvent& WXUNUSED(event))
{
	try {
		std::map<std::string, std::pair<float, float>> tempResults;
		auto file = std::ifstream("live_scripts/output.csv");
		if (!file) {
			wxMessageBox("File not found!");
		}
		do {
			std::string line;
			std::getline(file, line);
			if (!file) break;
			auto values = splitString(line, ",");
			auto name = values[0];
			auto swing = std::stof(values[1]);
			auto counted = std::stof(values[2]);
			tempResults[name] = { swing, counted };
		} while (true);
		std::stringstream ss;
		int counter = 0;
		for (auto const& result : tempResults) {
			ss << result.first << ", s: " << result.second.first << "%, c: " << result.second.second;
			++counter;
			if (counter % 2 == 0) {
				ss << "\n";
			}
			else {
				for (int a = 0; a < 16 - int(result.first.size()); ++a) ss << "  ";
			}
		}
		wxMessageBox(ss.str());
		for (auto const& result : tempResults) {
			try {
				auto [seatId, seat] = project->seats().accessByName(result.first);
				auto seatIndex = project->seats().idToIndex(seatId);
				Outcome outcome = Outcome(seatIndex, result.second.first, result.second.second, result.second.second, 0, 0);
				project->outcomes().add(outcome);
			}
			catch (SeatDoesntExistException) {
				wxMessageBox("Seat not found - " + result.first);
			}
		}
		refreshData();
	}
	catch (...) {
		wxMessageBox("An error was caught. Save to avoid losing progress.");
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
		for (auto& seat : project->seats()) seat.second.resetLiveData();
		refreshData();
	}
}

void ResultsFrame::addResultToResultData(Outcome result)
{
	auto const* report = latestReport();
	wxVector<wxVariant> data;
	// Since seats are now only loaded when simulations are run,
	// we need to make sure that the seats actually exist before
	// trying to display them
	if (result.seat >= project->seats().count() || result.seat < 0) return;
	auto seat = project->seats().viewByIndex(result.seat);
	wxColour swingColour = decideSwingColour(result.partyOneSwing);
	float percentCounted = result.getPercentCountedEstimate();
	wxColour percentCountedColour = decidePercentCountedColour(result.getPercentCountedEstimate());
	wxColour swingBasisColour = decidePercentCountedColour(result.tcpSwingBasis);
	std::string projectedMarginString = decideProjectedMarginString(result);
	wxColour projectedMarginColour = decideProjectedMarginColour(result);
	std::string statusString = decideStatusString(result);
	wxColour statusColour = decideStatusColour(result);

	resultsData->AppendRows(1);
	int row = resultsData->GetNumberRows() - 1;
	resultsData->SetCellValue(row, ResultSeatName, seat.name);
	resultsData->SetCellValue(row, RawSwing,
		std::isfinite(result.rawPartyOneSwing) ? formatFloat(result.rawPartyOneSwing, 1) : "");
	resultsData->SetCellBackgroundColour(row, RawSwing, decideSwingColour(result.rawPartyOneSwing));
	resultsData->SetCellValue(row, EstimatedSwing,
        std::isfinite(result.partyOneSwing) ? formatFloat(result.partyOneSwing, 1) : "");
	resultsData->SetCellBackgroundColour(row, EstimatedSwing, swingColour);
	resultsData->SetCellValue(row, Counted, formatFloat(percentCounted, 1));
	resultsData->SetCellBackgroundColour(row, Counted, percentCountedColour);
	resultsData->SetCellValue(row, TcpBasis, formatFloat(result.tcpSwingBasis, 1));
	resultsData->SetCellBackgroundColour(row, TcpBasis, swingBasisColour);
	resultsData->SetCellValue(
		row, Updated, result.updateTime.formatIsoTimeLocal());
	resultsData->SetCellValue(row, ProjectedMargin, projectedMarginString);
	resultsData->SetCellBackgroundColour(row, ProjectedMargin, projectedMarginColour);
	if (hasWinProbabilities(report, result.seat)) {
		resultsData->SetCellValue(row, PartyOneProbability, formatFloat(report->partyOneWinProportion[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, PartyTwoProbability, formatFloat(report->partyTwoWinProportion[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, OtherProbability, formatFloat(report->othersWinProportion[result.seat] * 100.0f, 2));
	}
	else {
		resultsData->SetCellValue(row, PartyOneProbability, "");
		resultsData->SetCellValue(row, PartyTwoProbability, "");
		resultsData->SetCellValue(row, OtherProbability, "");
	}
	resultsData->SetCellValue(row, Status, statusString);
	resultsData->SetCellBackgroundColour(row, Status, statusColour);
	resultsData->Refresh();
}

void ResultsFrame::updateInterface()
{
	applyActivityPolicy();
}

void ResultsFrame::refreshToolbar()
{
	wxLogNull something;
	wxBitmap toolBarBitmaps[5];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\run.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\non_classic.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[4] = wxBitmap("bitmaps\\open.png", wxBITMAP_TYPE_PNG);

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
	toolBar->AddTool(
		ControlId::RunLiveSimulations, "Run Model", toolBarBitmaps[0],
		wxNullBitmap, wxITEM_NORMAL,
		"Run Live Simulations (Shift-click runs the saved snapshot sequence)");
	toolBar->AddControl(new wxButton(toolBar, ControlId::ConfigureSnapshotSequence, "Configure snapshot sequence..."));
	toolBar->AddControl(new wxButton(toolBar, ControlId::RunSnapshotSequence, "Run snapshot sequence"));
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
	toolBar->AddTool(ControlId::AddFromScript, "Add Results From Script", toolBarBitmaps[4], wxNullBitmap, wxITEM_NORMAL, "Add Results From Script");
	toolBar->AddTool(ControlId::NonClassic, "Non-Classic Seat", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Non-Classic Seat");
	toolBar->AddControl(filterComboBox);
	toolBar->AddTool(ControlId::ClearAll, "Clear All Results", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Clear All Results");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
	applyActivityPolicy();
}

void ResultsFrame::refreshSummaryBar()
{
	summaryText->SetLabel(decideSummaryString());
}

void ResultsFrame::refreshTable()
{
	auto browsing = captureResultsBrowsing(*resultsData);
    resultsData->BeginBatch();
    resetTableColumns(); addTableData();
    resultsData->EnableEditing(false);
    resultsData->EndBatch();
	restoreResultsBrowsing(*resultsData,browsing);
}

bool ResultsFrame::resultPassesFilter(Outcome const& thisResult)
{
	if (thisResult.seat < 0 || thisResult.seat >= project->seats().count()) {
		logger << "Warning: A seat number was invalid in ResultsFrame::resultPassesFilter\n";
		return false;
	}
	// Show seats once counting has supplied some data, including an FP-only
	// result or a manually entered booth count. Zero-count outcomes still mark
	// the latest state, so recovery to unavailable data hides an older result
	// rather than leaving its stale progress on screen. Swing may legitimately
	// be zero and is not evidence that any votes have reported.
	if (!(thisResult.percentCounted > 0.0 || thisResult.tcpSwingBasis > 0.0
		|| thisResult.boothsIn > 0)) return false;
	if (filter == Filter::AllResults) return true;
	Seat const& seat = project->seats().viewByIndex(thisResult.seat);
	if (filter == Filter::LatestResults) return true;

	float significance = 0.0f;
	significance += std::max(0.0f, 3.0f / (1.0f + std::max(2.0f, abs(seat.tppMargin))));
	auto const* report = latestReport();
	// Significance can use the previous election's margin without a forecast.
	// Add forecast evidence only after a simulation has supplied it for this seat.
	if (hasProjectedMargin(report, thisResult.seat)) {
		double simulatedMarginAverage = report->seatPartyOneMarginAverage[thisResult.seat];
		significance += std::max(0.0f, 10.0f / (1.0f + std::max(1.0f, abs(float(simulatedMarginAverage)))));
		// automatically treat seats expected to change hands as significant
		// this can be detected by the simulated tpp average and previous margin having opposite signs
		if (simulatedMarginAverage * seat.tppMargin < 0.0f) significance += 5.0f;
	}
	if (hasWinProbabilities(report, thisResult.seat)) {
		significance += std::max(0.0f, (0.5f - abs(0.5f - report->othersWinProportion[thisResult.seat])) * 30.0f);
	}

	if (filter == Filter::SignificantResults) return significance > 1.5f;
	if (filter == Filter::KeyResults) return significance > 5.0f;

	// replace later
	return true;
}

wxColour ResultsFrame::decideSwingColour(double swing)
{
	if (!std::isfinite(swing)) return wxColour(255, 255, 255);
	Party::Colour swingPartyColour = (swing > 0.0 ?
		project->parties().viewByIndex(0).colour : project->parties().viewByIndex(1).colour);
	Party::Colour inverseColour = Party::Colour{ 255 - swingPartyColour.r, 255 - swingPartyColour.g, 255 - swingPartyColour.b };
	float incSw = std::min(1.0f, float(abs(swing)) * 0.08f);
	return wxColour(255 - int(inverseColour.r * incSw), 255 - int(inverseColour.g * incSw), 255 - int(inverseColour.b * incSw));
}

wxColour ResultsFrame::decidePercentCountedColour(float percentCounted)
{
	return wxColour(255 - std::min(255, int(percentCounted * 2.55f)), 255, 255 - std::min(255, int(percentCounted * 2.55f)));
}

std::string ResultsFrame::decideProjectedMarginString(Outcome const& thisResult)
{
	// *** Currently messed up because of changing margins from incumbent-relative to party-one-relative
	//     Fix later once reporting of non-classic results is properly sorted
	auto const* report = latestReport();
	if (!hasProjectedMargin(report, thisResult.seat)) return "";
	double simulatedMarginAverage = report->seatPartyOneMarginAverage[thisResult.seat];
	Seat const& seat = project->seats().viewByIndex(thisResult.seat);
	float projectedSwing = simulatedMarginAverage - seat.tppMargin;
	return formatFloat(simulatedMarginAverage, 2) + " (" +
		(projectedSwing >= 0 ? "+" : "") + formatFloat(projectedSwing, 2) + ")";
}

wxColour ResultsFrame::decideProjectedMarginColour(Outcome const& thisResult)
{
	// *** Currently messed up because of changing margins from incumbent-relative to party-one-relative
	//     Fix later once reporting of non-classic results is properly sorted
	auto const* report = latestReport();
	if (hasProjectedMargin(report, thisResult.seat)) {
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
	if (!hasWinProbabilities(report, thisResult.seat)) return "";
	float p1 = float(report->partyOneWinProportion[thisResult.seat]);
	float p2 = float(report->partyTwoWinProportion[thisResult.seat]);
	float p3 = float(report->othersWinProportion[thisResult.seat]);
	Party::Id thisParty = (p1 > p2 && p1 > p3 ? 0 : (p2 > p3 ? 1 : -1));
	std::string leadingPartyName = (thisParty != Party::InvalidId ? project->parties().view(thisParty).abbreviation : "OTH");
	return leadingPartyName;
}

float ResultsFrame::decideLeaderProbability(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!hasWinProbabilities(report, thisResult.seat)) return 0.0f;
	float p1 = float(report->partyOneWinProportion[thisResult.seat]);
	float p2 = float(report->partyTwoWinProportion[thisResult.seat]);
	float p3 = float(report->othersWinProportion[thisResult.seat]);
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
	if (!hasWinProbabilities(latestReport(), thisResult.seat)) return "";
	return decideLikelihoodString(thisResult) + " (" +
		formatFloat(decideLeaderProbability(thisResult), 2) + "%) " + 
		decideLeadingPartyName(thisResult);
}

wxColour ResultsFrame::decideStatusColour(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!hasWinProbabilities(report, thisResult.seat)) return *wxWHITE;
	float p1 = float(report->partyOneWinProportion[thisResult.seat]);
	float p2 = float(report->partyTwoWinProportion[thisResult.seat]);
	float p3 = float(report->othersWinProportion[thisResult.seat]);
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
	if ((!seat.isClassic2pp()) &&
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
	Outcome outcome = Outcome(seatId, swing, percentCounted, percentCounted, boothsIn, totalBooths);

	project->outcomes().add(outcome);
}

std::string ResultsFrame::decideSummaryString()
{
	std::string party1 = project->parties().view(0).abbreviation;
	std::string party2 = project->parties().view(1).abbreviation;
	auto const* report = latestReport();
	if (!report || !report->partyOneWinProportion.size()) return "";
	// Split Coalition reports store Liberal and National expectations separately,
	// while the Others total already excludes both. Show their combined total
	// here; ordinary two-party reports and older reports without a Coalition
	// distribution continue to use the second party's own total and label.
	bool const hasCoalition = !report->coalitionSeatWinFrequency.empty();
	std::string const partyTwoLabel = hasCoalition ? "Coalition" : party2;
	float const partyTwoSeats = hasCoalition ? report->getCoalitionWinExpectation() :
		report->getPartyWinExpectation(1);
	std::string const progress = report->countProgress ?
		"FP " + formatFloat(float(report->countProgress->fpPercent()), 2) + "%  TCP " +
		formatFloat(float(report->countProgress->tcpPercent()), 2) + "%" : "FP —  TCP —";
	std::string summaryString = party1 + " win chance: " + formatFloat(report->getPartyOverallWinPercent(Simulation::MajorParty::One), 2) +
		"   Projected 2PP: " + party1 + " " + formatFloat(float(report->getPartyOne2pp()), 2) +
		"   Seats: " + party1 + " " + formatFloat(report->getPartyWinExpectation(0), 2) + " " +
		partyTwoLabel + " " + formatFloat(partyTwoSeats, 2) +
		" Others " + formatFloat(report->getOthersWinExpectation(), 2) +
		"   Counted: " + progress + "\n" +
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
		resultsData->CreateGrid(0, ColumnCount, wxGrid::wxGridSelectCells);
		char const* labels[ColumnCount] = {"Seat Name", "Raw Swing", "Est. Swing", "Count %",
			"Tcp Basis", "Updated", "Proj. Margin", "ALP prob.", "LNP prob.", "Other prob.", "Status"};
		int const widths[ColumnCount] = {100,75,75,60,60,60,85,70,70,70,150};
		for (int column = 0; column < ColumnCount; ++column) {
			resultsData->SetColLabelValue(column,labels[column]);
			resultsData->SetColSize(column,widths[column]);
		}
		resultsData->SetRowLabelSize(0);
	}

	if (resultsData->GetNumberRows()) resultsData->DeleteRows(0, resultsData->GetNumberRows());
}

void ResultsFrame::addTableData()
{
	PA_LOG_VAR("Adding table data\n");
	std::vector<bool> seenSeat;
	seenSeat.resize(project->seats().count(), false);
	int numAdded = 0, seenCount = 0;
	constexpr int MaxResultsShown = 1000;
	for (auto const& thisResult : project->outcomes()) {
		// Saved results can refer to seats that are not loaded yet. Reject those
		// before either accessing the seat or tracking which seats were displayed.
		if (thisResult.seat < 0 || thisResult.seat >= project->seats().count()) continue;
		if (filter != Filter::AllResults) {
			if (seenSeat[thisResult.seat]) continue;
			seenSeat[thisResult.seat] = true;
			++seenCount;
		}

		if (resultPassesFilter(thisResult)) {
			addResultToResultData(thisResult);
			++numAdded;
		}
		if (numAdded >= MaxResultsShown || (filter != Filter::AllResults && seenCount == project->seats().count())) break;
	}
}
