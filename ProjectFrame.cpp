#include "ProjectFrame.h"

#include "PartiesFrame.h"
#include "PollstersFrame.h"
#include "PollsFrame.h"
#include "VisualiserFrame.h"
#include "ModelsFrame.h"
#include "ProjectionsFrame.h"
#include "RegionsFrame.h"
#include "SeatsFrame.h"
#include "SimulationsFrame.h"
#include "DisplayFrame.h"
#include "ResultsFrame.h"
#include "DownloadFrame.h"
#include "AnalysisFrame.h"
#include "MapFrame.h"
#include "LiveBoothFrame.h"
#include "GeneralSettingsFrame.h"
#include "ForecastSpecificationExport.h"

#include <exception>
#include <filesystem>

#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/wupdlock.h>
#include <wx/thread.h>
#include <wx/log.h>
#include "Beep.h"
#include "Log.h"

wxDEFINE_EVENT(PA_LIVE_COMPLETED, wxThreadEvent);

namespace {
// A disabled toolbar is not sufficient: accelerators and queued commands can
// still arrive. Read-only commands are explicitly registered by each page.
class LiveCommandFilter : public wxEventFilter {
public:
    explicit LiveCommandFilter(ProjectFrame& owner) : owner(owner) {}
    int FilterEvent(wxEvent& event) override {
        if (!owner.isLiveRunning()) return Event_Skip;
        if (event.GetEventType() != wxEVT_MENU && event.GetEventType() != wxEVT_BUTTON) return Event_Skip;
        auto* window = dynamic_cast<wxWindow*>(event.GetEventObject());
        for (auto* current = window; current; current = current->GetParent()) {
			if (event.GetEventType() == wxEVT_BUTTON && owner.isLiveProgressWindow(current)) return Event_Skip;
            if (auto* page = dynamic_cast<GenericChildFrame*>(current))
                return page->allowsLiveAction(event.GetId()) ? Event_Skip : Event_Processed;
        }
        return event.GetId() == wxID_EXIT || event.GetId() == wxID_ABOUT ? Event_Skip : Event_Processed;
    }
private:
    ProjectFrame& owner;
};
	class MacroWarningsDialog : public wxDialog {
	public:
		explicit MacroWarningsDialog(wxWindow* parent) :
			wxDialog(parent, wxID_ANY, "Macro warnings", wxDefaultPosition,
				wxSize(650, 380), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
		{
			text_ = new wxTextCtrl(
				this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
				wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
			auto* sizer = new wxBoxSizer(wxVERTICAL);
			sizer->Add(text_, 1, wxEXPAND | wxALL, 8);
			SetSizer(sizer);

			Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Hide(); });
		}

		void append(std::string const& warning)
		{
			if (!allWarnings_.empty()) allWarnings_ += "\n\n";
			allWarnings_ += warning;
			text_->SetValue(allWarnings_);
			text_->ShowPosition(text_->GetLastPosition());
			if (!IsShown()) Show();
			Raise();
			Update();
		}

		bool empty() const { return allWarnings_.empty(); }
		std::string const& text() const { return allWarnings_; }

	private:
		wxTextCtrl* text_ = nullptr;
		std::string allWarnings_;
	};
}

enum TabsEnum {
	Tab_Parties,
	Tab_Pollsters,
	Tab_Polls,
	Tab_Events,
	Tab_Visualiser,
	Tab_Models,
	Tab_Projections,
	Tab_Regions,
	Tab_Seats,
	Tab_Simulations,
	Tab_Display,
	Tab_Results,
	Tab_Downloads,
	Tab_Analysis,
	Tab_Map,
	Tab_LiveBooths
};

// Constructor for the ProjectFrame loading from a pre-existing file.
ProjectFrame::ProjectFrame(ParentFrame* parent, std::string pathName)
	: ProjectFrame(parent, 0)
{
	project.reset(new PollingProject(pathName));

	if (!project->isValid()) {
		// this will pop up a message to the user
		// and throw an exception which will be passed to the parent frame
		cancelConstructionFromFile();
	}

	setupPages();
	Show();
}

// Constructor for the ProjectFrame creating a new file.
ProjectFrame::ProjectFrame(ParentFrame* parent, NewProjectData newProjectData)
	: ProjectFrame(parent, 0)
{
	project.reset(new PollingProject(newProjectData));
	setupPages();
	Show();
}

// Refreshes the Polls frame data.
void ProjectFrame::Refresher::refreshPollData() const {
	if (projectFrame.pollsFrame) projectFrame.pollsFrame->refreshDataTable();
}

// Refreshes the Projections frame data.
void ProjectFrame::Refresher::refreshProjectionData() const {
	if (projectFrame.projectionsFrame) projectFrame.projectionsFrame->refreshDataTable();
}

// Refreshes the Seats frame data.
void ProjectFrame::Refresher::refreshSeatData() const {
	if (projectFrame.seatsFrame) projectFrame.seatsFrame->refreshDataTable();
}

// Refreshes the Visualiser frame data.
void ProjectFrame::Refresher::refreshVisualiser() const {
	if (projectFrame.visualiserFrame) projectFrame.visualiserFrame->refreshData();
}

// Refreshes the Display frame data.
void ProjectFrame::Refresher::refreshDisplay() const {
	if (projectFrame.displayFrame) projectFrame.displayFrame->refreshData();
}

// Refreshes the Results frame data.
void ProjectFrame::Refresher::refreshResults() const {
	if (projectFrame.resultsFrame) projectFrame.resultsFrame->refreshData();
}

// Refreshes the Display frame data.
void ProjectFrame::Refresher::refreshAnalysis() const {
	if (projectFrame.analysisFrame) projectFrame.analysisFrame->refreshData();
}

// Refreshes the Map frame data.
void ProjectFrame::Refresher::refreshMap() const {
	if (projectFrame.mapFrame) projectFrame.mapFrame->refreshData();
}

void ProjectFrame::Refresher::refreshLiveBooths() const {
	if (projectFrame.liveBoothFrame) projectFrame.liveBoothFrame->refreshData();
}

bool ProjectFrame::checkSave() {

	if (project.get()) {
		wxMessageDialog* message = new wxMessageDialog(
			this,
			"Another project is already open.\nDo you want to save the current project?",
			"Warning",
			wxYES_NO | wxCANCEL | wxICON_WARNING);
		int response = message->ShowModal();
		if (response == wxID_CANCEL) {
			return true;
		}
		else if (response == wxID_YES) {
			if (!save()) return true;
		}
	}
	return false;
}

bool ProjectFrame::save()
{
	if (!project->getLastFileName().empty()) {
		return saveUnderFilename(project->getLastFileName());
	}
	return saveAs();
}

bool ProjectFrame::saveAs() {
	if (!project.get()) return false; // There is no project to save.

	// The remembered destination includes its directory. Give the dialog that
	// directory and the basename separately so Save As starts beside this project.
	auto const savedPath = std::filesystem::path(project->getLastFileName());
	wxFileDialog* saveFileDialog = new wxFileDialog(
		this,
		"Save Project As",
		savedPath.parent_path().string(),
		savedPath.filename().string(),
		"Detailed Polling Analysis files (*.pol2)|*.pol2|Polling Analysis files (*.pol)|*.pol",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (saveFileDialog->ShowModal() == wxID_CANCEL)
		return false;     // the user changed their mind...

	std::string pathName = saveFileDialog->GetPath().ToStdString();
	return saveUnderFilename(pathName);
}

void ProjectFrame::exportForecastConfiguration()
{
	if (!project) return;
	auto defaultDirectory = project->paths().resolve("forecasts");
	if (project->models().count() > 0 &&
		!project->models().viewByIndex(0).getTermCode().empty()) {
		defaultDirectory /= project->models().viewByIndex(0).getTermCode();
	}
	wxDirDialog dialog(this, "Export Forecast Configuration",
		defaultDirectory.string(),
		wxDD_DEFAULT_STYLE | wxDD_NEW_DIR_BUTTON);
	if (dialog.ShowModal() == wxID_CANCEL) return;

	auto const directory = std::filesystem::path(dialog.GetPath().ToStdString());
	bool existingFiles = false;
	for (auto const* filename : { "forecast.json", "parties.csv",
		"party-official-codes.csv", "nonclassic-preferences.csv", "regions.csv" }) {
		std::error_code error;
		if (std::filesystem::exists(directory / filename, error) && !error) {
			existingFiles = true;
			break;
		}
	}
	if (existingFiles) {
		auto const response = wxMessageBox(
			"The selected directory already contains forecast configuration files. "
			"Replace them?", "Confirm Forecast Export",
			wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this);
		if (response != wxYES) return;
	}

	auto result = exportForecastSpecification(*project, directory);
	if (result.valid()) {
		std::string message = "Forecast configuration exported and validated successfully.";
		for (auto const& diagnostic : result.diagnostics) {
			if (diagnostic.severity != ForecastSpecificationDiagnostic::Severity::Warning) continue;
			message += "\n\nWarning: " + diagnostic.location + ": " + diagnostic.message;
		}
		wxMessageBox(message,
			"Forecast Export", wxOK | wxICON_INFORMATION, this);
		return;
	}

	std::string message = "Forecast configuration could not be exported:\n";
	for (auto const& diagnostic : result.diagnostics) {
		if (diagnostic.severity != ForecastSpecificationDiagnostic::Severity::Error) continue;
		message += "\n" + diagnostic.location + ": " + diagnostic.message;
	}
	wxMessageBox(message, "Forecast Export", wxOK | wxICON_ERROR, this);
}

void ProjectFrame::runMacro()
{
	auto dialog = wxTextEntryDialog(this, "Enter macro:", "", project->getLastMacro());
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;
	std::string newMacro = std::string(dialog.GetValue());
	MacroWarningsDialog warningsDialog(this);
	auto feedback = [this, &warningsDialog](
		MacroFeedbackType type, std::string message) {
		wxBell();
		switch (type) {
		case MacroFeedbackType::Fatal:
			wxMessageBox(message, "Macro failed",
				wxOK | wxICON_ERROR, this);
			break;
		case MacroFeedbackType::ActionRequired:
			wxMessageBox(message, "Macro action required",
				wxOK | wxICON_INFORMATION, this);
			break;
		case MacroFeedbackType::Warning:
			warningsDialog.append(message);
			break;
		}
	};
	auto error = project->runMacro(newMacro, feedback);
	warningsDialog.Hide();
	if (!error.has_value() && !warningsDialog.empty()) {
		wxBell();
		wxMessageBox(
			"Macro completed with the following warnings:\n\n" +
			warningsDialog.text(),
			"Macro completed with warnings",
			wxOK | wxICON_WARNING, this);
	}
	else if (!error.has_value()) {
		wxBell();
		wxMessageBox("Macro completed successfully", "Macro completed",
			wxOK | wxICON_INFORMATION, this);
	}
	Refresher refresher(*this);
	refresher.refreshPollData();
	refresher.refreshProjectionData();
	refresher.refreshSeatData();
	refresher.refreshVisualiser();
	refresher.refreshDisplay();
}

void ProjectFrame::updateMacro()
{
	auto dialog = wxTextEntryDialog(this, "Enter macro (it will not be run, just stored):", "", project->getLastMacro());
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;
	std::string newMacro = std::string(dialog.GetValue());
	project->updateMacro(newMacro);
}

void ProjectFrame::editGeneralSettings()
{
	GeneralSettingsData data(project->getElectionName());
	auto callback = [=](GeneralSettingsData data) {project->setElectionName(data.electionName);};

	// Create the new project frame (where initial settings for the new project are chosen).
	GeneralSettingsFrame* frame = new GeneralSettingsFrame(callback, data);

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
}

// Constructor for the ProjectFrame without creating a project. Only used as a delegate for the above constructors.
ProjectFrame::ProjectFrame(ParentFrame* parent, int WXUNUSED(dummyInt))
	: wxNotebook(parent->accessNotebookPanel(), wxID_ANY), liveProgressTimer(this), parent(parent)
{
	SetSize(parent->GetClientSize());
	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ProjectFrame::OnSwitch, this, this->GetId());
	Bind(PA_LIVE_COMPLETED, &ProjectFrame::onLiveCompletion, this);
	Bind(wxEVT_TIMER, &ProjectFrame::onLiveProgress, this, liveProgressTimer.GetId());
	liveCommandFilter = std::make_unique<LiveCommandFilter>(*this);
	wxEvtHandler::AddFilter(liveCommandFilter.get());
}

ProjectFrame::Refresher ProjectFrame::createRefresher()
{
	return Refresher(*this);
}

void ProjectFrame::OnSwitch(wxBookCtrlEvent& event) {
	int tabselected = event.GetSelection();
	if (tabselected == Tab_Visualiser) {
		visualiserFrame->paint(true);
	}
	else if (tabselected == Tab_Map) {
		mapFrame->resetMouseOver();
		mapFrame->paint();
	}
	if (liveBoothFrame && GetPage(tabselected) == liveBoothFrame) {
		liveBoothFrame->refreshData();
	}
}

void ProjectFrame::setupPages() {
	createPage<PartiesFrame>(partiesFrame);
	createPage<PollstersFrame>(pollstersFrame);
	createPage<PollsFrame>(pollsFrame);
	createPage<VisualiserFrame>(visualiserFrame);
	createPage<ModelsFrame>(modelsFrame);
	createPage<ProjectionsFrame>(projectionsFrame);
	createPage<RegionsFrame>(regionsFrame);
	createPage<SeatsFrame>(seatsFrame);
	createPage<SimulationsFrame>(simulationsFrame);
	createPage<DisplayFrame>(displayFrame);
	createPage<ResultsFrame>(resultsFrame);
	createPage<DownloadFrame>(downloadFrame);
	createPage<AnalysisFrame>(analysisFrame);
	createPage<MapFrame>(mapFrame);
	createPage<LiveBoothFrame>(liveBoothFrame);
}

bool ProjectFrame::saveUnderFilename(std::string const& pathName)
{
	try {
		auto const result = project->save(pathName);
		std::string messageText = "File successfully saved: " + pathName;
		for (auto const& warning : result.warnings) {
			messageText += "\n\nWarning: " + warning;
		}
		wxMessageDialog message(this, messageText, "Save Complete",
			wxOK | wxCENTRE | (result.warnings.empty() ?
				wxICON_INFORMATION : wxICON_WARNING));
		message.ShowModal();
		return true;
	}
	catch (std::exception const& error)
	{
		wxMessageDialog message(
			this, "Could not save file.\n\n" + std::string(error.what()),
			"Save Error",
			wxOK | wxCENTRE | wxICON_ERROR);
		message.ShowModal();
		return false;
	}
	catch (...)
	{
		wxMessageDialog message(this,
			"Could not save file because of an unknown error.",
			"Save Error", wxOK | wxCENTRE | wxICON_ERROR);
		message.ShowModal();
		return false;
	}
}

void ProjectFrame::cancelConstructionFromFile()
{
	std::string messageText = "Could not open file.";
	if (project && !project->getLoadError().empty()) {
		messageText += "\n\n" + project->getLoadError();
	}
	wxMessageDialog* message = new wxMessageDialog(
		this,
		messageText,
		"Open Error",
		wxOK | wxCENTRE | wxICON_ERROR);
	message->ShowModal();

	throw LoadProjectFailedException();
}

ProjectFrame::~ProjectFrame() {
	liveProgressTimer.Stop();
	liveJob.reset();
	if (liveCommandFilter) wxEvtHandler::RemoveFilter(liveCommandFilter.get());
}

bool ProjectFrame::startLiveRun(std::vector<LiveSimulationJob::Input> inputs) {
	if (isLiveRunning() || inputs.empty()) return false;
	std::vector<Simulation::Id> ids;
	for (auto const& [id, simulation] : project->simulations())
		if (simulation.isLive()) ids.push_back(id);
	if (ids.empty()) return false;
	try {
		initialiseLiveJob(std::move(inputs),std::move(ids));
		createLiveProgress();
		updateLiveActivityPolicy();
		liveProgressTimer.Start(200);
		CallAfter(&ProjectFrame::dispatchLiveSnapshot);
		return true;
	} catch (std::exception const& error) {
		liveJob.reset(); updateLiveActivityPolicy();
		wxMessageBox(wxString::FromUTF8(error.what()), "Cannot start live simulation", wxOK | wxICON_ERROR, this);
		return false;
	}
}

void ProjectFrame::initialiseLiveJob(std::vector<LiveSimulationJob::Input> inputs, std::vector<Simulation::Id> ids) {
	// Capture configured inputs once, before any calculation starts. Neither
	// later tab selections nor temporary calibration reports alter that context.
	liveJob = std::make_unique<LiveSimulationJob>(*project,ids,[this] {
		wxQueueEvent(this,new wxThreadEvent(PA_LIVE_COMPLETED));
	},[] {
		// File/ZIP failures must not open GUI logging dialogs on the worker.
		// The readers still report their normal structured input diagnostics.
		thread_local wxLogStderr workerLog;
		wxLog::SetThreadActiveTarget(&workerLog);
	});
	std::vector<std::shared_ptr<SimulationRun>> retired;
	for (auto id : ids) retired.push_back(project->simulations().access(id).detachRetainedLiveRun());
	liveJob->retire(std::move(retired));
	liveInputs = std::move(inputs); liveIndex = 0; liveStopRequested = false;
	++liveGeneration; lastPublishedCode.clear();
}

void ProjectFrame::createLiveProgress() {
	if (liveProgress) liveProgress->Destroy();
	liveProgress = new wxDialog(this, wxID_ANY,
		liveInputs.size() > 1 ? "Live snapshot sequence" : "Live simulations",
		wxDefaultPosition, wxSize(650, 320), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* layout = new wxBoxSizer(wxVERTICAL);
	liveStatus = new wxTextCtrl(liveProgress, wxID_ANY, "Preparing live run...",
		wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	liveGauge = new wxGauge(liveProgress, wxID_ANY, int(liveInputs.size()));
	liveStop = new wxButton(liveProgress, wxID_ANY, "Stop after current snapshot");
	layout->Add(liveStatus, 1, wxEXPAND | wxALL, 10);
	layout->Add(liveGauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
	layout->Add(liveStop, 0, wxALIGN_RIGHT | wxALL, 10);
	liveProgress->SetSizer(layout); liveProgress->CentreOnParent();
	liveStop->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (isLiveRunning()) stopLiveRun();
		else { liveProgress->Destroy(); liveProgress = nullptr; }
	});
	liveProgress->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
		if (isLiveRunning()) { stopLiveRun(); event.Veto(); }
		else { liveProgress = nullptr; event.Skip(); }
	});
	liveProgress->Show();
}

void ProjectFrame::dispatchLiveSnapshot() {
	if (!liveJob) return;
	if (liveStopRequested) { finishLiveJob("Stopped after " + std::to_string(liveIndex) + " completed snapshots."); return; }
	if (!liveJob->submit(liveGeneration, liveInputs.at(liveIndex)))
		finishLiveJob("Could not dispatch the next live snapshot.");
}

void ProjectFrame::stopLiveRun() {
	if (!liveJob) return;
	liveStopRequested = true;
	if (liveStop) { liveStop->SetLabel("Stopping after current snapshot..."); liveStop->Disable(); }
}

bool ProjectFrame::deferCloseForLiveRun() {
	if (!liveJob) return false;
	closeAfterLive = true; stopLiveRun(); return true;
}

void ProjectFrame::onLiveProgress(wxTimerEvent&) {
	// A timer event already queued before Stop() can arrive during worker
	// cleanup, after the final input index has advanced beyond the sequence.
	if (!liveJob || !liveStatus || liveFinishMessage || liveIndex >= liveInputs.size()) return;
	auto const& input = liveInputs.at(liveIndex);
	std::string code = input ? input->timestamp.value_or("selected feed") : "current feed";
	updateLiveStatus(wxString::FromUTF8("Running " + code + " (" +
		std::to_string(liveIndex + 1) + "/" + std::to_string(liveInputs.size()) + ")\n" +
		"Completed: " + std::to_string(liveIndex) + " of " + std::to_string(liveInputs.size()) + " snapshots\n" +
		"Last published: " + (lastPublishedCode.empty() ? "no snapshot in this job yet" : lastPublishedCode) +
		"\n\n" + liveJob->progress()));
}

void ProjectFrame::updateLiveStatus(wxString const& message) {
	// The timer polls throughout each simulation phase, often finding the same
	// status. Leave that text untouched, and hold native repaints while replacing
	// a changed message so the cleared control is never shown between messages.
	if (!liveStatus || liveStatus->GetValue() == message) return;
	wxWindowUpdateLocker lock(liveStatus);
	liveStatus->ChangeValue(message);
}

void ProjectFrame::onLiveCompletion(wxThreadEvent&) {
	if (!liveJob) return;
	auto completed = liveJob->take();
	if (!completed || completed->generation != liveGeneration) return;
	if (completed->error) {
		try { std::rethrow_exception(completed->error); }
		catch (std::exception const& error) { finishLiveJob(error.what()); }
		catch (...) { finishLiveJob("Unknown live-worker failure."); }
		return;
	}
	auto& update = *completed->result;
	if (update.finished) {
		finishLiveJob(liveFinishMessage.value_or("Live run finished."));
		return;
	}
	publishLiveUpdate(update);
	advanceLiveSequence(update);
}

void ProjectFrame::publishLiveUpdate(LiveSimulationUpdate& update) {
	for (auto const& simulation : update.simulations)
		if (simulation.completed) lastPublishedCode = simulation.completed->report.dateCode;
	LiveSimulationJob::publish(*project, update);
	liveJob->retire(std::move(update.retiredRuns));
	// Publication precedes every refresh; no page observes calibration's
	// temporary reports or counted outcomes from an unfinished calculation.
	resultsFrame->refreshData(); seatsFrame->refreshDataTable();
	simulationsFrame->refreshDataTable(); displayFrame->refreshData(); displayFrame->paint();
	mapFrame->refreshData();
	if (liveBoothFrame) liveBoothFrame->notifyCompletedExports(update.exports);
}

void ProjectFrame::advanceLiveSequence(LiveSimulationUpdate const& update) {
	// Keep successful preceding simulations on a failure. A stop request is
	// checked at dispatch, after all simulations in this snapshot have finished.
	if (!update.succeeded) {
		auto const& input = liveInputs.at(liveIndex);
		finishLiveJob("Stopped at " + (input ? input->timestamp.value_or("the current snapshot") : "the current feed") + ".\n" + update.failure);
		return;
	}
	++liveIndex; liveGauge->SetValue(int(liveIndex));
	if (liveIndex == liveInputs.size()) {
		finishLiveJob("Completed " + std::to_string(liveIndex) + " snapshots. Results are available in Live Booths.");
	} else CallAfter(&ProjectFrame::dispatchLiveSnapshot);
}

void ProjectFrame::finishLiveJob(std::string message) {
	liveProgressTimer.Stop();
	if (liveJob && !liveFinishMessage) {
		liveFinishMessage = message;
		updateLiveStatus("Finishing live run...");
		if (liveJob->finish(liveGeneration)) return;
	}
	liveJob.reset(); // Worker is between tasks; joining never waits for a calculation here.
	liveFinishMessage.reset();
	updateLiveActivityPolicy();
	if (liveIndex == liveInputs.size() && !liveStopRequested && project->config().getBeepOnCompletion()) beep();
	updateLiveStatus(wxString::FromUTF8(message));
	if (liveStop) { liveStop->SetLabel("Close"); liveStop->Enable(); }
	logger << "Live simulation job: " << message << "\n";
	if (closeAfterLive) {
		closeAfterLive = false;
		// The event belongs to the parent. Capture that window directly so a
		// queued project switch cannot leave a callback referring to this page.
		parent->CallAfter([frame = parent] { frame->Close(); });
	}
}

void ProjectFrame::updateLiveActivityPolicy() {
	for (std::size_t i = 0; i < GetPageCount(); ++i)
		if (auto* page = dynamic_cast<GenericChildFrame*>(GetPage(i))) page->refreshActivityPolicy();
	parent->refreshLiveActivityPolicy();
}
