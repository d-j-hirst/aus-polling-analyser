#include "LiveAutomation.h"
#include <wx/dirdlg.h>
#include <wx/choicdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
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
#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

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
    CallAfter([this] {
        auto const& settings = project->automationSettings();
        if (!settings.is_object()) return;
        try {
            LiveAutomation::Session session(project->paths().root(),settings);
            if (session.status().value("state", "paused") == "armed") startLiveWatch(false);
        } catch (std::exception const& error) {
            wxMessageBox(wxString::FromUTF8(error.what()), "Live automation needs attention", wxOK | wxICON_ERROR, this);
        }
    });
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
	// The prepared commentary replay uses the normal Run action to start its
	// feeder and watcher. Ordinary projects and explicit snapshot sequences keep
	// their existing behaviour; this is an opt-in local benchmark setting.
	if (!liveWatching && inputs.size() == 1 && !inputs.front() &&
		project->automationSettings().is_object() &&
		project->automationSettings().value("commentary_trial", nlohmann::json::object()).value("enabled", false)) {
		return startLiveWatch();
	}
	std::vector<Simulation::Id> ids;
	for (auto const& [id, simulation] : project->simulations())
		if (liveWatching ? simulation.isLiveAutomatic() : simulation.isLive()) ids.push_back(id);
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
	if (liveWatching) liveJob->configureAutomation(project->automationSettings(),liveWatchAcknowledge);
    liveWatchPending = false; liveWatchFinishState = "paused";
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
    if (liveWatching) {
        liveStop->SetLabel("Pause after current forecast");
        auto* disarm = new wxButton(liveProgress,wxID_ANY,"Stop / disarm");
        layout->Add(disarm,0,wxALIGN_RIGHT | wxALL,10);
        disarm->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
            if (!liveJob) return;
            liveWatchFinishState = "disarmed"; stopLiveRun();
        });
    }
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
	if (liveWatching) {
        if (liveWatchPending) return;
        liveWatchPending = liveJob->submit(liveGeneration, LiveSimulationJob::Input{});
        liveWatchPollAt = Timestamp::now().unixMilliseconds()+2000;
        return;
    }
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
	closeAfterLive = true; if (liveWatching) liveWatchFinishState = "armed"; stopLiveRun(); return true;
}

void ProjectFrame::onLiveProgress(wxTimerEvent&) {
    if (liveWatching && liveJob && !liveFinishMessage) {
        if (!liveWatchPending && (liveStopRequested || Timestamp::now().unixMilliseconds() >= liveWatchPollAt))
            dispatchLiveSnapshot();
        if (Timestamp::now().unixMilliseconds() >= liveWatchStatusAt) {
            liveWatchStatusAt = Timestamp::now().unixMilliseconds()+2000;
            auto directory = project->paths().root()/"live_runs"/project->automationSettings().at("run_id").get<std::string>();
            try {
                auto status = std::filesystem::exists(directory/"status.json") ? LiveAutomation::readJson(directory/"status.json") : nlohmann::json::object();
                auto arrival = std::filesystem::exists(directory/"arrival-status.json") ? LiveAutomation::readJson(directory/"arrival-status.json") : nlohmann::json::object();
                auto reporting = std::filesystem::exists(directory/"reporting-status.json") ? LiveAutomation::readJson(directory/"reporting-status.json") : nlohmann::json::object();
                liveWatchSummary = "Received: "+arrival.value("latest_received",std::string("none"))+"\nAccepted: "+status.value("latest_accepted",std::string("none"))+
                    "\nForecasted: "+status.value("latest_forecast",std::string("none"));
                auto counts = status.value("counts",nlohmann::json::object());
                int notYetAccepted = std::max(0,arrival.value("collected",0)-status.value("known_arrivals",0)) + counts.value("discovered",0);
                liveWatchSummary += "\nAwaiting acceptance: "+std::to_string(notYetAccepted)+
                    "; accepted since last attempt: "+std::to_string(status.value("awaiting_forecast",0))+
                    "; rejected: "+std::to_string(counts.value("rejected",0));
                for (auto const* key : {"error","last_forecast_error","last_rejection"})
                    if (!status.value(key,std::string()).empty()) liveWatchSummary += "\n"+status[key].get<std::string>();
                liveWatchSummary += "\nReporting: "+(reporting.empty() ? std::string("upload/reporting watcher has not started") :
                    reporting.value("error",std::string("active; requests pending: ")+std::to_string(reporting.value("pending_commentary_requests",0))));
                if (std::filesystem::exists(directory/"trial-status.json")) {
                    auto trial = LiveAutomation::readJson(directory/"trial-status.json");
                    liveWatchSummary += "\nCommentary trial: "+trial.value("message", std::string("starting"));
                    std::string stage = trial.value("stage", std::string());
                    if (!liveWatchPending && (stage == "complete" || stage == "error")) {
                        finishLiveJob(liveWatchSummary);
                        return;
                    }
                }
                if (std::filesystem::is_directory(directory/"receipts"))
                    for (auto const& entry : std::filesystem::directory_iterator(directory/"receipts")) {
                        if (entry.path().extension() != ".json") continue;
                        auto receipt = LiveAutomation::readJson(entry.path());
                        std::string published = "none";
                        if (receipt.contains("latest_source_time") && receipt["latest_source_time"].is_string())
                            published = receipt["latest_source_time"].get<std::string>();
                        else if (receipt.contains("latest_forecast_id") && receipt["latest_forecast_id"].is_string())
                            published = receipt["latest_forecast_id"].get<std::string>();
                        liveWatchSummary += "\nPublished ("+entry.path().stem().string()+"): "+published;
                        if (receipt.contains("forecasts")) for (auto const& [id,record] : receipt["forecasts"].items())
                            if (!record.value("accepted",false)) { liveWatchSummary += " - "+record.value("error",std::string("pending")); break; }
                    }
            } catch (std::exception const& error) { liveWatchSummary = std::string("Could not read watcher status: ")+error.what(); }
        }
        if (liveJob) updateLiveStatus(wxString::FromUTF8(liveWatchSummary+"\n\n"+liveJob->progress()));
        return;
    }
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
	liveWatchPending = false;
	auto& update = *completed->result;
	if (update.finished) {
		finishLiveJob(liveFinishMessage.value_or("Live run finished."));
		return;
	}
	publishLiveUpdate(update);
	if (liveWatching) {
        if (!update.succeeded) finishLiveJob(update.failure);
        else if (liveStopRequested) finishLiveJob("Folder watching paused.");
    } else advanceLiveSequence(update);
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
		if (liveJob->finish(liveGeneration,liveWatchFinishState)) return;
	}
	liveWatching = false; liveWatchPending = false;
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


namespace {
// The reporting worker reads these settings from the run when it starts.
// Copy the preset so edits to the source JSON do not affect an existing run.
bool selectLiveCommentary(wxWindow* parent, std::filesystem::path const& root, nlohmann::json& config) {
    auto const current = config.value("commentary", nlohmann::json::object());
    bool const hasPreset = current.value("enabled", false);
    wxArrayString choices;
    choices.Add("Off");
    if (hasPreset) {
        auto label = "Keep saved preset (" + current.at("model").get<std::string>() +
            ", US$" + current.at("spending_limit_usd").dump() + " per run)";
        choices.Add(wxString::FromUTF8(label));
    }
    auto const presetDirectory = root/"uploads";
    std::map<int,std::filesystem::path> bundledPresets;
    if (config.at("election") == "2026sa") {
        auto addPreset = [&](char const* filename, wxString const& label) {
            auto path = presetDirectory/filename;
            if (std::filesystem::is_regular_file(path)) {
                int const index = static_cast<int>(choices.Add(label));
                bundledPresets.emplace(index, path);
            }
        };
        addPreset("sa-local-replay-commentary.json", "SA trial (Terra Standard/low, US$1 per run)");
        addPreset("sa-full-local-replay-commentary.json", "SA full run (Terra Standard/low, US$10 per run)");
    }
    int const fileIndex = static_cast<int>(choices.size());
    choices.Add("Choose a commentary preset JSON file...");
    wxSingleChoiceDialog dialog(parent, "Choose commentary for this run. Restart the upload manager after changing this setting.",
        "Forecast commentary", choices);
    dialog.SetSelection(hasPreset ? 1 : 0);
    if (dialog.ShowModal() != wxID_OK) return false;
    auto const selected = dialog.GetSelection();
    if (selected == 0) {
        config["commentary"] = {{"enabled",false}};
        return true;
    }
    if (hasPreset && selected == 1) return true;

    std::filesystem::path path;
    if (selected == fileIndex) {
        wxFileDialog file(parent, "Choose a commentary preset", presetDirectory.string(), "", "JSON files (*.json)|*.json",
            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (file.ShowModal() != wxID_OK) return false;
        path = LiveResultsInput::pathFromUtf8(file.GetPath().ToStdString(wxConvUTF8));
    } else {
        path = bundledPresets.at(selected);
    }
    auto const preset = LiveAutomation::readJson(path);
    if (!preset.is_object() || !preset.value("enabled", false) ||
        preset.value("model", "").empty() || preset.value("spending_limit_usd", 0.0) <= 0 ||
        preset.value("input_usd_per_million", 0.0) <= 0 || preset.value("output_usd_per_million", 0.0) <= 0)
        throw std::runtime_error("Choose an enabled commentary preset with a model, positive prices and a run spending limit.");

    // The run manifest holds operational choices. Credentials are supplied by
    // the reporting worker's environment, so only supported settings are copied.
    nlohmann::json settings = nlohmann::json::object();
    for (auto key : {"enabled","model","service_tier","reasoning_effort","prompt_version",
        "minimum_count_change","spending_limit_usd","input_usd_per_million","cached_input_usd_per_million",
        "output_usd_per_million","concurrency","attempts","max_output_tokens","request_timeout_seconds",
        "cache_comparison","compare_writers"}) {
        if (preset.contains(key)) settings[key] = preset.at(key);
    }
    auto label = settings.at("model").get<std::string>() + " with a US$" +
        settings.at("spending_limit_usd").dump() + " spending limit for this run.";
    if (wxMessageBox(wxString::FromUTF8(label), "Enable forecast commentary",
        wxOK | wxCANCEL | wxICON_INFORMATION, parent) != wxOK) return false;
    config["commentary"] = std::move(settings);
    return true;
}
}

void ProjectFrame::configureLiveWatch() {
    if (isLiveRunning()) return;
    try {
        std::string election;
        for (auto const& [id,sim] : project->simulations()) {
            if (!sim.isLiveAutomatic()) continue;
            if (!election.empty()) throw std::runtime_error("Configure one automatic live simulation for folder watching.");
            election = project->projections().view(sim.getSettings().baseProjection).getBaseModel(project->models()).getTermCode();
        }
        if (election.empty()) throw std::runtime_error("Configure an automatic live simulation first.");
        wxArrayString choices; choices.Add("New replay run"); choices.Add("New official live run"); choices.Add("Edit selected run");
        wxSingleChoiceDialog kind(this,"Choose the run to configure. A new run has separate history and a separate URL.","Live automation",choices);
        if (kind.ShowModal() != wxID_OK) return;
        auto const& root = project->paths().root();
        auto config = project->automationSettings();
        bool newRun = kind.GetSelection() < 2;
        if (newRun) {
            std::string const runKind = kind.GetSelection() == 1 ? "live" : "replay";
            auto proposedId = LiveAutomation::suggestRunId(root, election, runKind);
            for (;;) {
                wxTextEntryDialog name(this,"Choose the ID for the uploader, feed inbox and replay URL.",
                    "Run ID",wxString::FromUTF8(proposedId));
                if (name.ShowModal() != wxID_OK) return;
                auto id = name.GetValue().ToStdString(wxConvUTF8);
                LiveAutomation::validateRunId(id);
                auto folder = root/"live_runs"/id;
                auto const conflict = LiveAutomation::newRunConflict(folder);
                if (!conflict) {
                    config = {{"version",1},{"election",election},{"run_id",id},{"kind",runKind},
                        {"visibility","private"},{"night_end_hour",6},
                        {"commentary_timeout_seconds",election.ends_with("fed") ? 30 : 90},
                        {"source_directory","live_runs/"+id+"/inbox"}};
                    break;
                }
                auto manifest = folder/"run.json";
                if (*conflict != folder && std::filesystem::is_regular_file(manifest)) {
                    auto existing = LiveAutomation::readJson(manifest);
                    if (existing.value("election","") == election && existing.value("kind","") == runKind &&
                        existing.value("run_id","") == id) {
                        int answer = wxMessageBox("This ID is already in use. Open the saved run?",
                            "Run ID in use",wxYES_NO | wxCANCEL | wxICON_QUESTION,this);
                        if (answer == wxCANCEL) return;
                        if (answer == wxYES) {
                            config = std::move(existing);
                            newRun = false;
                            break;
                        }
                    } else {
                        wxMessageBox("This ID belongs to a different election or run type. Choose another ID.",
                            "Run ID in use",wxOK | wxICON_INFORMATION,this);
                    }
                } else {
                    auto message = "Cannot start a new run here. Choose another ID or inspect: " +
                        LiveResultsInput::pathToUtf8(*conflict);
                    wxMessageBox(wxString::FromUTF8(message),
                        "Run ID in use",wxOK | wxICON_INFORMATION,this);
                }
            }
        }
        if (!config.is_object()) throw std::runtime_error("Create a new run first.");
        if (config.at("election") != election) throw std::runtime_error("The selected run belongs to another election. Create a new run.");
        auto id = config.at("run_id").get<std::string>();
        if (!newRun) {
            // A project opened from an older saved copy can point to a run
            // whose inbox or commentary was edited since that copy was made.
            auto manifest = root/"live_runs"/id/"run.json";
            if (std::filesystem::is_regular_file(manifest)) config = LiveAutomation::readJson(manifest);
            if (config.at("election") != election || config.at("run_id") != id)
                throw std::runtime_error("The saved run does not match this project.");
        }
        wxArrayString inboxOptions;
        inboxOptions.Add(wxString::FromUTF8(config.at("source_directory").get<std::string>()));
        inboxOptions.Add("Choose another folder...");
        wxSingleChoiceDialog inbox(this,"Folder receiving complete election feed files","Feed inbox",inboxOptions);
        inbox.SetSelection(0);
        if (inbox.ShowModal() != wxID_OK) return;
        if (inbox.GetSelection() == 1) {
            auto source = LiveResultsInput::resolveDirectory(config.at("source_directory").get<std::string>());
            wxDirDialog directory(this,"Folder receiving complete election feed files",
                wxString::FromUTF8(LiveResultsInput::pathToUtf8(project->paths().resolve(source))),
                wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (directory.ShowModal() != wxID_OK) return;
            config["source_directory"] = LiveResultsInput::portableDirectory(directory.GetPath().ToStdString(wxConvUTF8));
        }
        if (newRun) {
            wxTextEntryDialog date(this,"Election date (YYYY-MM-DD)","Comparison windows");
            if (date.ShowModal() != wxID_OK) return;
            config["election_date"] = date.GetValue().ToStdString();
            wxTextEntryDialog label(this,"Display name (optional)","Live automation",wxString::FromUTF8(id));
            if (label.ShowModal() != wxID_OK) return;
            config["display_name"] = label.GetValue().empty() ? id : label.GetValue().ToStdString(wxConvUTF8);
            wxArrayString visibilityOptions;
            visibilityOptions.Add("Private replay / operator preview");
            visibilityOptions.Add("Publicly viewable");
            wxSingleChoiceDialog visibility(this,"Visibility requested from servers that support this run type",
                "Publication visibility",visibilityOptions);
            visibility.SetSelection(0);
            if (visibility.ShowModal() != wxID_OK) return;
            config["visibility"] = visibility.GetSelection() == 1 ? "public" : "private";
            auto region = election.substr(4);
            config["timezone"] = region == "sa" ? "Australia/Adelaide" : region == "wa" ? "Australia/Perth" :
                region == "qld" ? "Australia/Brisbane" : region == "nt" ? "Australia/Darwin" :
                region == "tas" ? "Australia/Hobart" : "Australia/Sydney";
        }
        wxTextEntryDialog timing(this,"Morning cutoff hour and commentary deadline in seconds (for example 6 90)","Reporting timing",
            wxString::FromUTF8(std::to_string(config.value("night_end_hour",6))+" "+std::to_string(config.value("commentary_timeout_seconds",election.ends_with("fed") ? 30 : 90))));
        if (timing.ShowModal() != wxID_OK) return;
        int cutoffHour = 0, deadlineSeconds = 0; std::string extra;
        std::istringstream timingValues(timing.GetValue().ToStdString());
        if (!(timingValues >> cutoffHour >> deadlineSeconds) || timingValues >> extra || cutoffHour < 0 || cutoffHour > 23 || deadlineSeconds < 0 || deadlineSeconds > 600)
            throw std::runtime_error("Use a cutoff hour from 0 to 23 and deadline from 0 to 600 seconds.");
        config["night_end_hour"] = cutoffHour; config["commentary_timeout_seconds"] = deadlineSeconds;
        if (!selectLiveCommentary(this, root, config)) return;
        LiveAutomation::Session session(root, config);
        session.saveConfiguration(newRun);
        config.erase("input_revision");
        project->setAutomationSettings(config);
        // Save the association in the project. The manifest already lets the
        // separate publisher attach, even before the forecast watcher starts.
        if (!save()) return;
        wxMessageBox(wxString::FromUTF8("Run " + id + " is ready. Start Watch / Resume to calculate forecasts."),
            "Live automation",wxOK | wxICON_INFORMATION,this);
    } catch (std::exception const& error) {
        wxMessageBox(wxString::FromUTF8(error.what()),"Cannot configure watching",wxOK | wxICON_ERROR,this);
    }
}

bool ProjectFrame::startLiveWatch(bool acknowledgeChange) {
    if (isLiveRunning()) return false;
    if (!project->automationSettings().is_object()) {
        wxMessageBox("Use Configure watching first.","Live automation",wxOK | wxICON_INFORMATION,this); return false;
    }
    try {
        LiveAutomation::validateSettings(project->automationSettings());
        // The calculation worker writes normal diagnostic snapshots in this
        // run's folder. Give the editable project the same location so Live
        // Booths can find them too; previously only the worker's private copy
        // knew where to look. This changes output selection, not model inputs.
        auto const folder = project->automationSettings().at("run_id").get<std::string>();
        for (auto& [simulationId, simulation] : project->simulations()) {
            if (!simulation.isLiveAutomatic()) continue;
            auto settings = simulation.getSettings();
            settings.liveOutputFolder = folder;
            simulation.replaceSettings(std::move(settings));
        }
    } catch (std::exception const& error) {
        wxMessageBox(wxString::FromUTF8(error.what()),"Cannot start watching",wxOK | wxICON_ERROR,this);
        return false;
    }
    liveWatching = true; liveWatchAcknowledge = acknowledgeChange;
    if (!startLiveRun({LiveSimulationJob::Input{}})) { liveWatching = false; return false; }
    try {
        launchCommentaryTrial();
    } catch (std::exception const& error) {
        finishLiveJob(error.what());
        return false;
    }
    return true;
}

void ProjectFrame::launchCommentaryTrial() {
    // Forecasting stays inside the existing C++ worker. A fixed Python helper
    // delivers the next selected feed only after the previous forecast succeeds,
    // prepares numerical comparisons, then runs the configured local benchmark.
    // No command text comes from the project, and no upload process is started.
    auto const& settings = project->automationSettings();
    if (!settings.value("commentary_trial", nlohmann::json::object()).value("enabled", false)) return;
    auto root = project->paths().root();
#ifdef _WIN32
    auto python = root/"analysis"/".venv-win"/"Scripts"/"python.exe";
#else
    auto python = root/"analysis"/"env"/"bin"/"python";
#endif
    auto script = root/"live_scripts"/"run_commentary_trial.py";
    auto directory = root/"live_runs"/settings.at("run_id").get<std::string>();
    if (!std::filesystem::is_regular_file(python) || !std::filesystem::is_regular_file(script)) {
        throw std::runtime_error("The commentary trial needs the analysis Python environment and live_scripts/run_commentary_trial.py.");
    }
    std::vector<std::wstring> arguments = {python.wstring(), L"-B", script.wstring(), L"--run", directory.wstring()};
    std::vector<wchar_t const*> argv;
    for (auto const& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    wxExecuteEnv environment;
    environment.cwd = wxString(root.wstring());
    if (wxExecute(argv.data(), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, nullptr, &environment) == 0) {
        throw std::runtime_error("Could not launch the commentary trial helper. No benchmark requests were started.");
    }
}
