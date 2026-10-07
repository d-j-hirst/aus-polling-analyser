#include "ResultsFrame.h"

#include "Beep.h"
#include "EditPollFrame.h"
#include "EditSimulationFrame.h"
#include "LiveResultsInput.h"
#include "LiveSnapshotSequenceDialog.h"
#include "LiveRunExport.h"
#include "Log.h"
#include "NonClassicFrame.h"

#include <wx/numdlg.h>
#include <wx/gauge.h>
#include <wx/utils.h>
#include <wx/valnum.h>

#include <filesystem>
#include <exception>
#include <fstream>
#include <optional>
#include <set>

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
	refresher(refresher), snapshotTimer(this)
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
	Bind(wxEVT_BUTTON, &ResultsFrame::OnConfigureSnapshotSequence, this, ControlId::ConfigureSnapshotSequence);
	Bind(wxEVT_BUTTON, &ResultsFrame::OnRunSnapshotSequence, this, ControlId::RunSnapshotSequence);
	Bind(wxEVT_TIMER, &ResultsFrame::OnNextSnapshot, this, snapshotTimer.GetId());
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddResult, this, ControlId::AddResult);
	Bind(wxEVT_TOOL, &ResultsFrame::OnAddFromScript, this, ControlId::AddFromScript);
	Bind(wxEVT_TOOL, &ResultsFrame::OnNonClassic, this, ControlId::NonClassic);
	Bind(wxEVT_COMBOBOX, &ResultsFrame::OnFilterSelection, this, ControlId::Filter);
	Bind(wxEVT_TOOL, &ResultsFrame::OnClearAll, this, ControlId::ClearAll);
}

void ResultsFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
	// Set the pollster data table to the entire client size.
	resultsData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void ResultsFrame::OnRunLiveSimulations(wxCommandEvent& WXUNUSED(event))
{
	if (snapshotRunning) return;
	if (wxGetKeyState(WXK_SHIFT)) {
		wxCommandEvent command;
		OnRunSnapshotSequence(command);
		return;
	}
	runLiveSimulations();
	finishLiveSimulationRun();
	if (project->config().getBeepOnCompletion()) beep();
}

bool ResultsFrame::runLiveSimulations(
	std::optional<LiveResultsInput::CurrentFile> const& snapshot,
	std::string* failureMessage)
{
	// Both controls use the same simulation selection and calculation. Only
	// feedback delivery differs: unattended replay must never open a prompt.
	bool allSucceeded = true;
	for (auto& [key, simulation] : project->simulations()) {
		if (!simulation.isLive()) continue;
		bool succeeded;
		if (snapshot) {
			bool actionRequired = false;
			std::string feedback;
			auto record = [&](std::string message) {
				logger << message << "\n";
				if (!feedback.empty()) feedback += "\n\n";
				feedback += message;
			};
			succeeded = simulation.run(*project, record,
				[&](std::string message) { actionRequired = true; record(std::move(message)); }, snapshot);
			// Some exports report an action-required failure after the numerical
			// run succeeded. Keep that report, but do not silently advance.
			if (!succeeded || actionRequired) {
				if (failureMessage) *failureMessage = simulation.getSettings().name + ":\n" + feedback;
				return false;
			}
		}
		else succeeded = simulation.run(*project, [](std::string message) { wxMessageBox(message); });
		allSucceeded = allSucceeded && succeeded;
	}
	return allSucceeded;
}

void ResultsFrame::finishLiveSimulationRun()
{
	refreshData();
	refresher.refreshSeatData();
	refresher.refreshLiveBooths();
	refresher.refreshDisplay();
	refresher.refreshMap();
	Refresh();
	Update();
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
	if (snapshotRunning) return;
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

void ResultsFrame::OnRunSnapshotSequence(wxCommandEvent& WXUNUSED(event))
{
	if (snapshotRunning) return;
	// This modeless progress window is also the error display: starting or
	// advancing a sequence never requires dismissing a message box.
	if (snapshotProgress) snapshotProgress->Destroy();
	snapshotProgress = new wxDialog(this, wxID_ANY, "Live snapshot sequence", wxDefaultPosition,
		wxSize(650, 320), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto layout = new wxBoxSizer(wxVERTICAL);
	snapshotStatus = new wxTextCtrl(snapshotProgress, wxID_ANY, "Preparing sequence...",
		wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	snapshotGauge = new wxGauge(snapshotProgress, wxID_ANY, 1);
	snapshotStartNow = new wxButton(snapshotProgress, wxID_ANY, "Start now");
	snapshotStop = new wxButton(snapshotProgress, wxID_ANY, "Cancel sequence");
	layout->Add(snapshotStatus, 1, wxEXPAND | wxALL, 10);
	layout->Add(snapshotGauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
	auto buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->Add(snapshotStartNow, 0, wxRIGHT, 8);
	buttons->Add(snapshotStop);
	layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
	snapshotProgress->SetSizer(layout);
	snapshotProgress->CentreOnParent();
	snapshotStartNow->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (snapshotRunning && snapshotStartDeadline) beginSnapshotRuns();
	});
	snapshotStop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (snapshotRunning) {
			if (snapshotStartDeadline) { finishSnapshotSequence("Cancelled before running any snapshots."); return; }
			snapshotStopRequested = true;
			snapshotStop->Disable();
		}
		else { snapshotProgress->Destroy(); snapshotProgress = nullptr; }
	});
	snapshotProgress->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
		if (snapshotRunning) {
			if (snapshotStartDeadline) finishSnapshotSequence("Cancelled before running any snapshots.");
			else snapshotStopRequested = true;
			event.Veto();
		}
		else { snapshotProgress = nullptr; event.Skip(); }
	});
	snapshotProgress->Show();
	try {
		auto settings = project->snapshotSequence();
		if (settings.election != snapshotElection())
			throw LiveSnapshotSequence::ConfigurationError("Configure and save a sequence for this election first.");
		if (settings.codes.empty()) throw LiveSnapshotSequence::ConfigurationError("No snapshot codes are selected. Use Configure snapshot sequence first.");
		auto available = LiveSnapshotSequence::discover(LiveSnapshotSequence::directory(project->paths().root(), settings), settings.election);
		if (!available.error.empty()) { finishSnapshotSequence(available.error); return; }
		snapshotFiles = LiveSnapshotSequence::resolve(available.files, settings.codes);
		ensureSnapshotOutputFolders(settings.election);
		for (auto const& [key, simulation] : project->simulations()) {
			if (!simulation.isLive()) continue;
			if (!simulation.isLiveAutomatic()) throw LiveSnapshotSequence::ConfigurationError("Manual live simulations are not supported by the current live pipeline.");
			if (auto error = LiveRunExport::validateOutputFolder(simulation.getSettings().liveOutputFolder))
				throw LiveSnapshotSequence::ConfigurationError(*error);
		}
		snapshotIndex = 0;
		snapshotStopRequested = false;
		snapshotRunning = true;
		snapshotGauge->SetRange(int(snapshotFiles.size()));
		// Keep configuration and project lifetime fixed while replay runs. The
		// progress window remains enabled for stopping between snapshots.
		snapshotDisabler = std::make_unique<wxWindowDisabler>(snapshotProgress);
		// The calculation waits for its workers on the GUI thread. Give the
		// user time to position this window while the event loop is responsive,
		// without introducing re-entrant event processing during a simulation.
		snapshotStartDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		snapshotStatus->ChangeValue("Starting in 10 seconds.\n\nMove or resize this window to keep the results visible, or select Start now.");
		snapshotTimer.StartOnce(1000);
	}
	catch (std::exception const& error) { finishSnapshotSequence(error.what()); }
}

void ResultsFrame::beginSnapshotRuns()
{
	// Both the countdown and Start now use this single transition. Stop the
	// waiting timer before scheduling the first run, so it can only start once.
	snapshotTimer.Stop();
	snapshotStartDeadline.reset();
	snapshotStartNow->Hide();
	snapshotStop->SetLabel("Stop after current snapshot");
	snapshotProgress->Layout();
	snapshotTimer.StartOnce(100);
}

void ResultsFrame::OnNextSnapshot(wxTimerEvent& WXUNUSED(event))
{
	if (!snapshotRunning) return;
	if (snapshotStopRequested) { finishSnapshotSequence("Stopped after " + std::to_string(snapshotIndex) + " completed snapshots."); return; }
	if (snapshotStartDeadline) {
		// Use elapsed time rather than timer ticks: dragging a native window can
		// delay a timer event, but should not add extra seconds to the countdown.
		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			*snapshotStartDeadline - std::chrono::steady_clock::now()).count();
		if (remaining <= 0) { beginSnapshotRuns(); return; }
		snapshotStatus->ChangeValue(wxString::FromUTF8("Starting in " + std::to_string((remaining + 999) / 1000) +
			" seconds.\n\nMove or resize this window to keep the results visible, or select Start now."));
		snapshotTimer.StartOnce(int(std::min<int64_t>(1000, remaining)));
		return;
	}
	auto const& selected = snapshotFiles.at(snapshotIndex);
	snapshotStatus->ChangeValue(wxString::FromUTF8("Running " + *selected.timestamp + " (" +
		std::to_string(snapshotIndex + 1) + "/" + std::to_string(snapshotFiles.size()) + ")\n" +
		LiveResultsInput::pathToUtf8(selected.path.filename())));
	snapshotProgress->Update();
	try {
		std::string failure;
		if (!runLiveSimulations(selected, &failure)) {
			finishLiveSimulationRun();
			finishSnapshotSequence("Stopped at " + *selected.timestamp + ".\n" + failure);
			return;
		}
		++snapshotIndex;
		snapshotGauge->SetValue(int(snapshotIndex));
		finishLiveSimulationRun();
		if (snapshotIndex == snapshotFiles.size()) {
			finishSnapshotSequence("Completed " + std::to_string(snapshotIndex) + " snapshots. Results are available in Live Booths.");
			if (project->config().getBeepOnCompletion()) beep();
		}
		// A fresh event, rather than a synchronous loop or wxYield, allows paint
		// and stop events to finish before the next expensive simulation starts.
		else snapshotTimer.StartOnce(100);
	}
	catch (std::exception const& error) {
		finishSnapshotSequence("Stopped at " + *selected.timestamp + ".\n" + error.what());
	}
}

void ResultsFrame::finishSnapshotSequence(std::string const& message)
{
	snapshotTimer.Stop();
	snapshotRunning = false;
	snapshotStartDeadline.reset();
	snapshotDisabler.reset();
	snapshotStatus->ChangeValue(wxString::FromUTF8(message));
	snapshotStop->SetLabel("Close");
	snapshotStop->Enable();
	snapshotStartNow->Hide();
	snapshotProgress->Layout();
	logger << "Live snapshot sequence: " << message << "\n";
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
	wxColour swingColour = decideSwingColour(result);
	float percentCounted = result.getPercentCountedEstimate();
	wxColour percentCountedColour = decidePercentCountedColour(result.getPercentCountedEstimate());
	wxColour swingBasisColour = decidePercentCountedColour(result.tcpSwingBasis);
	std::string projectedMarginString = decideProjectedMarginString(result);
	wxColour projectedMarginColour = decideProjectedMarginColour(result);
	std::string statusString = decideStatusString(result);
	wxColour statusColour = decideStatusColour(result);

	resultsData->AppendRows(1);
	int row = resultsData->GetNumberRows() - 1;
	resultsData->SetCellValue(row, 0, seat.name);
	resultsData->SetCellValue(row, 1, formatFloat(result.partyOneSwing, 1));
	resultsData->SetCellBackgroundColour(row, 1, swingColour);
	resultsData->SetCellValue(row, 2, formatFloat(percentCounted, 1));
	resultsData->SetCellBackgroundColour(row, 2, percentCountedColour);
	resultsData->SetCellValue(row, 3, formatFloat(result.tcpSwingBasis, 1));
	resultsData->SetCellBackgroundColour(row, 3, swingBasisColour);
	resultsData->SetCellValue(
		row, 4, result.updateTime.formatIsoTimeLocal());
	resultsData->SetCellValue(row, 5, projectedMarginString);
	resultsData->SetCellBackgroundColour(row, 5, projectedMarginColour);
	if (report && report->partyOneWinProportion.size()) {
		resultsData->SetCellValue(row, 6, formatFloat(report->partyOneWinProportion[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, 7, formatFloat(report->partyTwoWinProportion[result.seat] * 100.0f, 2));
		resultsData->SetCellValue(row, 8, formatFloat(report->othersWinProportion[result.seat] * 100.0f, 2));
	}
	else {
		resultsData->SetCellValue(row, 6, "");
		resultsData->SetCellValue(row, 7, "");
		resultsData->SetCellValue(row, 8, "");
	}
	resultsData->SetCellValue(row, 9, statusString);
	resultsData->SetCellBackgroundColour(row, 9, statusColour);
	resultsData->Refresh();
}

void ResultsFrame::updateInterface()
{
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
	Seat const& seat = project->seats().viewByIndex(thisResult.seat);
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
	if (thisResult.seat < 0 || thisResult.seat >= project->seats().count()) {
		logger << "Warning: A seat number was invalid in ResultsFrame::resultPassesFilter\n";
		return false;
	}
	significance += std::max(0.0f, (0.5f - abs(0.5f - report->othersWinProportion[thisResult.seat])) * 30.0f);

	if (filter == Filter::SignificantResults) return significance > 1.5f;
	if (filter == Filter::KeyResults) return significance > 5.0f;

	// replace later
	return true;
}

wxColour ResultsFrame::decideSwingColour(Outcome const& thisResult)
{
	if (std::isnan(thisResult.partyOneSwing)) return wxColour(255, 255, 255);
	Party::Colour swingPartyColour = (thisResult.partyOneSwing > 0.0f ?
		project->parties().viewByIndex(0).colour : project->parties().viewByIndex(1).colour);
	Party::Colour inverseColour = Party::Colour{ 255 - swingPartyColour.r, 255 - swingPartyColour.g, 255 - swingPartyColour.b };
	float incSw = std::min(1.0f, float(abs(thisResult.partyOneSwing)) * 0.08f);
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
	if (!report || !report->seatPartyOneMarginAverage.size()) return "";
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
	if (!report || !report->partyOneWinProportion.size()) return "";
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
	if (!report || !report->partyOneWinProportion.size()) return 0.0f;
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
	return decideLikelihoodString(thisResult) + " (" +
		formatFloat(decideLeaderProbability(thisResult), 2) + "%) " + 
		decideLeadingPartyName(thisResult);
}

wxColour ResultsFrame::decideStatusColour(Outcome const& thisResult)
{
	auto const* report = latestReport();
	if (!report || !report->partyOneWinProportion.size()) return *wxWHITE;
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
	std::string summaryString = party1 + " win chance: " + formatFloat(report->getPartyOverallWinPercent(Simulation::MajorParty::One), 2) +
		"   Projected 2PP: " + party1 + " " + formatFloat(float(report->getPartyOne2pp()), 2) +
		"   Seats: " + party1 + " " + formatFloat(report->getPartyWinExpectation(0), 2) + " " +
		party2 + " " + formatFloat(report->getPartyWinExpectation(1), 2) +
		" Others " + formatFloat(report->getOthersWinExpectation(), 2) +
		"   Count progress: " + formatFloat(report->getTcpPercentCounted(), 2) + "%\n" +
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
		resultsData->CreateGrid(0, int(10), wxGrid::wxGridSelectCells);
		resultsData->SetColLabelValue(0, "Seat Name");
		resultsData->SetColLabelValue(1, "ALP Swing");
		resultsData->SetColLabelValue(2, "Count %");
		resultsData->SetColLabelValue(3, "Tcp Basis");
		resultsData->SetColLabelValue(4, "Updated");
		resultsData->SetColLabelValue(5, "Proj. Margin");
		resultsData->SetColLabelValue(6, "ALP prob.");
		resultsData->SetColLabelValue(7, "LNP prob.");
		resultsData->SetColLabelValue(8, "Other prob.");
		resultsData->SetColLabelValue(9, "Status");
		resultsData->SetColSize(0, 100);
		resultsData->SetColSize(1, 40);
		resultsData->SetColSize(2, 60);
		resultsData->SetColSize(3, 60);
		resultsData->SetColSize(4, 60);
		resultsData->SetColSize(5, 85);
		resultsData->SetColSize(6, 70);
		resultsData->SetColSize(7, 70);
		resultsData->SetColSize(8, 70);
		resultsData->SetColSize(9, 150);
		resultsData->SetRowLabelSize(0);
	}

	if (resultsData->GetNumberRows()) resultsData->DeleteRows(0, resultsData->GetNumberRows());
}

void ResultsFrame::addTableData()
{
	PA_LOG_VAR("Adding table data\n");
	std::vector<bool> seenSeat;
	seenSeat.resize(project->seats().count(), false);
	int numAdded = 0;
	constexpr int MaxResultsShown = 1000;
	for (int i = 0; i < project->outcomes().count(); ++i) {
		Outcome thisResult = project->outcomes().get(i);
		if (thisResult.seat == -1) continue;
		if (filter != Filter::AllResults) {
			if (seenSeat[thisResult.seat]) continue;
			seenSeat[thisResult.seat] = true;
		}

		if (resultPassesFilter(thisResult)) {
			addResultToResultData(thisResult);
			++numAdded;
		}
		if (numAdded >= MaxResultsShown) break;
	}
}
