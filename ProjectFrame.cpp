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

// ----------------------------------------------------------------------------
// notebook frame
// ----------------------------------------------------------------------------

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
ProjectFrame::ProjectFrame(ParentFrame* parent)
	: ProjectFrame(parent, 0)
{
	NewProjectData newProjectData = NewProjectData();
	project.reset(new PollingProject(newProjectData));
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

// Constructor for the ProjectFrame without creating a project. Only used as a delegate for the above constructors.
ProjectFrame::ProjectFrame(ParentFrame* parent, int WXUNUSED(dummyInt))
	: wxNotebook(parent->notebookPanel.get(), wxID_ANY)
//ProjectFrame::ProjectFrame(ParentFrame* parent, int WXUNUSED(dummyInt))
//	: wxNotebook(parent, wxID_ANY)
{
	wxSize parentSize = parent->GetClientSize();
	SetSize(parentSize);
	Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &ProjectFrame::OnSwitch, this, this->GetId());
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
}

void ProjectFrame::setupPages() {
	partiesFrame = new PartiesFrame(this, project.get());
	pollstersFrame = new PollstersFrame(this, project.get());
	pollsFrame = new PollsFrame(this, project.get());
	eventsFrame = new EventsFrame(this, project.get());
	visualiserFrame = new VisualiserFrame(this, project.get());
	modelsFrame = new ModelsFrame(this, project.get());
	projectionsFrame = new ProjectionsFrame(this, project.get());
	regionsFrame = new RegionsFrame(this, project.get());
	seatsFrame = new SeatsFrame(this, project.get());
	simulationsFrame = new SimulationsFrame(this, project.get());
	displayFrame = new DisplayFrame(this, project.get());
	resultsFrame = new ResultsFrame(this, project.get());
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

// Refreshes the Polls frame data.
void ProjectFrame::refreshPollData() {
	if (pollsFrame) pollsFrame->refreshData();
}

// Refreshes the Polls frame data.
void ProjectFrame::refreshProjectionData() {
	if (projectionsFrame) projectionsFrame->refreshData();
}

// Refreshes the Polls frame data.
void ProjectFrame::refreshSeatData() {
	if (seatsFrame) seatsFrame->refreshData();
}

// Refreshes the Polls frame data.
void ProjectFrame::refreshVisualiser() {
	if (visualiserFrame) visualiserFrame->refreshData();
}

// Refreshes the Polls frame data.
void ProjectFrame::refreshDisplay() {
	if (displayFrame) displayFrame->refreshData();
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

void ParentFrame::updateInterface() {
	bool projectExists = notebook != nullptr;
	toolBar->EnableTool(PA_ToolID_SaveAs, projectExists);
}