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
#include "GeneralSettingsFrame.h"

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
	Tab_Map
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
			save();
		}
	}
	return false;
}

void ProjectFrame::save()
{
	if (!project->getLastFileName().empty()) {
		saveUnderFilename(project->getLastFileName());
	}
	else {
		saveAs();
	}
}

void ProjectFrame::saveAs() {
	if (!project.get()) return; // There is no project to save.

								// initialize the save dialog
	wxFileDialog* saveFileDialog = new wxFileDialog(
		this,
		"Save Project As",
		wxEmptyString,
		project->getLastFileName(),
		"Detailed Polling Analysis files (*.pol2)|*.pol2|Polling Analysis files (*.pol)|*.pol",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (saveFileDialog->ShowModal() == wxID_CANCEL)
		return;     // the user changed their mind...

	std::string pathName = saveFileDialog->GetPath().ToStdString();
	saveUnderFilename(pathName);
}

void ProjectFrame::runMacro()
{
	auto dialog = wxTextEntryDialog(this, "Enter macro:", "", project->getLastMacro());
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;
	std::string newMacro = std::string(dialog.GetValue());
	auto error = project->runMacro(newMacro);
	if (error.has_value()) {
		wxMessageBox(error.value());
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
}

void ProjectFrame::saveUnderFilename(std::string const& pathName)
{
	try {
		project->save(pathName);
	}
	catch (...)
	{
		wxMessageDialog* message = new wxMessageDialog(
			this,
			"Could not save file.",
			"Save Error",
			wxOK | wxCENTRE | wxICON_ERROR);
		message->ShowModal();
		return;
	}
	wxMessageDialog* message = new wxMessageDialog(
		this,
		"File successfully saved: " + pathName);
	message->ShowModal();
}

void ProjectFrame::cancelConstructionFromFile()
{
	wxMessageDialog* message = new wxMessageDialog(
		this,
		"Could not open file.",
		"Save Error",
		wxOK | wxCENTRE | wxICON_ERROR);
	message->ShowModal();

	throw LoadProjectFailedException();
}
