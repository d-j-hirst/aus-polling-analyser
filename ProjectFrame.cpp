#include "ProjectFrame.h"

#include "PartiesFrame.h"
#include "PollstersFrame.h"
#include "PollsFrame.h"
#include "EventsFrame.h"
#include "VisualiserFrame.h"
#include "ModelsFrame.h"
#include "ProjectionsFrame.h"
#include "RegionsFrame.h"
#include "SeatsFrame.h"
#include "SimulationsFrame.h"
#include "DisplayFrame.h"
#include "ResultsFrame.h"
#include "DownloadFrame.h"
#include "MapFrame.h"

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
	Tab_Map
};

// Constructor for the ProjectFrame loading from a pre-existing file.
ProjectFrame::ProjectFrame(ParentFrame* parent, std::string pathName)
	: ProjectFrame(parent, 0)
{
	project.reset(new PollingProject(pathName));

	if (!project->isValid()) {
		project.reset(); // effectively, close the project if opening fails.
		wxMessageDialog* message = new wxMessageDialog(
			this,
			"Could not open file.",
			"Save Error",
			wxOK | wxCENTRE | wxICON_ERROR);
		message->ShowModal();

		Destroy();

		// kills this notebook.
		parent->notebook.reset();
		return;
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
	if (projectFrame.pollsFrame) projectFrame.pollsFrame->refreshData();
}

// Refreshes the Projections frame data.
void ProjectFrame::Refresher::refreshProjectionData() const {
	if (projectFrame.projectionsFrame) projectFrame.projectionsFrame->refreshData();
}

// Refreshes the Seats frame data.
void ProjectFrame::Refresher::refreshSeatData() const {
	if (projectFrame.seatsFrame) projectFrame.seatsFrame->refreshData();
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
		if (response == 5101) {
			return true;
		}
		else if (response == 5103) {
			wxCommandEvent temp; // never actually used
			parent->OnSaveAs(temp);
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
		"Polling Analysis files (*.pol)|*.pol",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (saveFileDialog->ShowModal() == wxID_CANCEL)
		return;     // the user changed their mind...

	std::string pathName = saveFileDialog->GetPath().ToStdString();
	saveUnderFilename(pathName);
}

// Constructor for the ProjectFrame without creating a project. Only used as a delegate for the above constructors.
ProjectFrame::ProjectFrame(ParentFrame* parent, int WXUNUSED(dummyInt))
	: wxNotebook(parent->notebookPanel.get(), wxID_ANY)
{
	wxSize parentSize = parent->GetClientSize();
	SetSize(parentSize);
	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ProjectFrame::OnSwitch, this, this->GetId());
}

ProjectFrame::Refresher ProjectFrame::createRefresher()
{
	return Refresher(*this);
}


void ProjectFrame::updateInterface() {
	parent->updateInterface();
}

void ProjectFrame::OnSwitch(wxBookCtrlEvent& event) {
	int tabselected = event.GetSelection();
	if (tabselected == Tab_Visualiser) {
		visualiserFrame->resetMouseOver();
		visualiserFrame->paint();
	}
	else if (tabselected == Tab_Map) {
		mapFrame->resetMouseOver();
		mapFrame->paint();
	}
}

void ProjectFrame::setupPages() {
	partiesFrame = new PartiesFrame(createRefresher(), project.get());
	pollstersFrame = new PollstersFrame(createRefresher(), project.get());
	pollsFrame = new PollsFrame(createRefresher(), project.get());
	eventsFrame = new EventsFrame(createRefresher(), project.get());
	visualiserFrame = new VisualiserFrame(createRefresher(), project.get());
	modelsFrame = new ModelsFrame(createRefresher(), project.get());
	projectionsFrame = new ProjectionsFrame(createRefresher(), project.get());
	regionsFrame = new RegionsFrame(createRefresher(), project.get());
	seatsFrame = new SeatsFrame(createRefresher(), project.get());
	simulationsFrame = new SimulationsFrame(createRefresher(), project.get());
	displayFrame = new DisplayFrame(createRefresher(), project.get());
	resultsFrame = new ResultsFrame(createRefresher(), project.get());
	downloadFrame = new DownloadFrame(createRefresher(), project.get());
	mapFrame = new MapFrame(createRefresher(), project.get());
	AddPage(partiesFrame, "Parties", true);
	AddPage(pollstersFrame, "Pollsters", true);
	AddPage(pollsFrame, "Polls", true);
	AddPage(eventsFrame, "Events", true);
	AddPage(visualiserFrame, "Visualiser", true);
	AddPage(modelsFrame, "Models", true);
	AddPage(projectionsFrame, "Projections", true);
	AddPage(regionsFrame, "Regions", true);
	AddPage(seatsFrame, "Seats", true);
	AddPage(simulationsFrame, "Simulations", true);
	AddPage(displayFrame, "Display", true);
	AddPage(resultsFrame, "Results", true);
	AddPage(downloadFrame, "Download", true);
	AddPage(mapFrame, "Map", true);
}

void ProjectFrame::saveUnderFilename(std::string const& pathName)
{
	if (project->save(pathName)) {
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