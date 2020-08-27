#include "AnalysisFrame.h"

// #include "AnalysisFrameRenderer.h"
#include "General.h"

#include "wx/dcbuffer.h"

#include <algorithm>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectSimulation
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

void AnalysisFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
}

void AnalysisFrame::OnSimulationSelection(wxCommandEvent& WXUNUSED(event)) {
	selectedElection = selectElectionComboBox->GetCurrentSelection();
	paint();
}

// Handles the movement of the mouse in the display frame.
void AnalysisFrame::OnMouseMove(wxMouseEvent& WXUNUSED(event)) {
	paint();
}

void AnalysisFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &AnalysisFrame::OnResize, this, PA_AnalysisFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &AnalysisFrame::OnSimulationSelection, this, ControlId::SelectSimulation);
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

	selectElectionComboBox = new wxComboBox(toolBar, ControlId::SelectSimulation, comboBoxString, wxPoint(0, 0), wxSize(150, 30), electionArray);

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectElectionComboBox);

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
