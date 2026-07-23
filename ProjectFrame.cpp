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

namespace {
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

								// initialize the save dialog
	wxFileDialog* saveFileDialog = new wxFileDialog(
		this,
		"Save Project As",
		wxEmptyString,
		project->getLastFileName(),
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
		MacroRunner::FeedbackType type, std::string message) {
		wxBell();
		switch (type) {
		case MacroRunner::FeedbackType::Fatal:
			wxMessageBox(message, "Macro failed",
				wxOK | wxICON_ERROR, this);
			break;
		case MacroRunner::FeedbackType::ActionRequired:
			wxMessageBox(message, "Macro action required",
				wxOK | wxICON_INFORMATION, this);
			break;
		case MacroRunner::FeedbackType::Warning:
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
	: parent(parent), wxNotebook(parent->accessNotebookPanel(), wxID_ANY)
{
	SetSize(parent->GetClientSize());
	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ProjectFrame::OnSwitch, this, this->GetId());
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
