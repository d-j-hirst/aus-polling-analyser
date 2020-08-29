#include "AnalysisFrame.h"

// #include "AnalysisFrameRenderer.h"
#include "ElectionAnalyser.h"
#include "General.h"

#include "wx/dcbuffer.h"

#include <algorithm>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectElection,
	SelectAnalysis,
	Analyse,
};

// frame constructor
AnalysisFrame::AnalysisFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Analysis", wxPoint(333, 0), project),
	refresher(refresher)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();

	paint();

	bindEventHandlers();
}

void AnalysisFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void AnalysisFrame::refreshData() {
	refreshToolbar();
}

void AnalysisFrame::OnResize(wxSizeEvent&) {
}

void AnalysisFrame::OnElectionSelection(wxCommandEvent&) {
	selectedElection = selectElectionComboBox->GetCurrentSelection();
	paint();
}

void AnalysisFrame::OnAnalysisSelection(wxCommandEvent&)
{
	selectedAnalysis = selectAnalysisComboBox->GetCurrentSelection();
	paint();
}

void AnalysisFrame::OnAnalyse(wxCommandEvent&)
{
	if (selectedAnalysis < 0) {
		wxMessageBox("Please select a type of analysis to perform.");
		return;
	}
	if (selectedElection < 0) {
		wxMessageBox("Please select an election to analyse.");
		return;
	}
	ElectionAnalyser analyser(project->elections());
	analyser.run(ElectionAnalyser::Type::Parties, selectedElection);
}

// Handles the movement of the mouse in the display frame.
void AnalysisFrame::OnMouseMove(wxMouseEvent&) {
	paint();
}

void AnalysisFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &AnalysisFrame::OnResize, this, PA_AnalysisFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &AnalysisFrame::OnElectionSelection, this, ControlId::SelectElection);
	Bind(wxEVT_COMBOBOX, &AnalysisFrame::OnAnalysisSelection, this, ControlId::SelectAnalysis);
	Bind(wxEVT_BUTTON, &AnalysisFrame::OnAnalyse, this, ControlId::Analyse);
	dcPanel->Bind(wxEVT_MOTION, &AnalysisFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &AnalysisFrame::OnPaint, this, ControlId::DcPanel);
}

void AnalysisFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void AnalysisFrame::updateInterface() {
}

void AnalysisFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// *** Simulation Combo Box *** //

	// Create the choices for the combo box.
	// Set the selected election to be the first election
	wxArrayString electionArray;
	for (auto const& [key, election] : project->elections()) {
		electionArray.push_back(election.name);
	}
	std::string comboBoxString;
	if (selectedElection >= int(electionArray.size())) {
		selectedElection = int(electionArray.size()) - 1;
	}
	if (selectedElection >= 0) {
		comboBoxString = electionArray[selectedElection];
	}

	selectElectionComboBox = new wxComboBox(toolBar, ControlId::SelectElection, comboBoxString, wxPoint(0, 0), wxSize(150, 30), electionArray);

	// Add the tools that will be used on the toolbar.

	wxArrayString analysisArray;
	analysisArray.push_back("Party Analysis");
	if (selectedAnalysis >= 0) {
		comboBoxString = electionArray[selectedAnalysis];
	}

	selectAnalysisComboBox = new wxComboBox(toolBar, ControlId::SelectAnalysis, comboBoxString, wxPoint(0, 0), wxSize(150, 30), analysisArray);

	analyseButton = new wxButton(toolBar, ControlId::Analyse, "Analyse", wxPoint(0, 0), wxSize(90, 24));

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectElectionComboBox);
	toolBar->AddControl(selectAnalysisComboBox);
	toolBar->AddControl(analyseButton);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void AnalysisFrame::render(wxDC& dc)
{
	dc;

	//AnalysisFrameRenderer::clearDC(dc);

	if (selectedElection < 0 || selectedElection >= project->elections().count()) return;

	//Results2::Election const& election = project->elections().viewByIndex(selectedElection);

	//AnalysisFrameRenderer renderer(*project, dc, simulation, dcPanel->GetClientSize());

	//renderer.render();
}
