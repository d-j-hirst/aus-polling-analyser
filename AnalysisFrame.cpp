#include "AnalysisFrame.h"

#include "AnalysisFrameRenderer.h"
#include "ElectionAnalyser.h"
#include "General.h"

#include "wx/dcbuffer.h"

#include <algorithm>

const int TextVerticalMovement = 400;
const int TextHorizontalMovement = 600;

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectElection,
	SelectAnalysis,
	Analyse,
	TextUp,
	TextDown,
	TextRight,
	TextLeft
};

// frame constructor
AnalysisFrame::AnalysisFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Analysis", wxPoint(333, 0), project),
	refresher(refresher)
{

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
	analyser = std::make_unique<ElectionAnalyser>(project->elections());
	if (selectedAnalysis == 0) {
		analyser->run(ElectionAnalyser::Type::Parties, selectedElection);
	}
	else if (selectedAnalysis == 1) {
		analyser->run(ElectionAnalyser::Type::Swing, selectedElection);
	}
	else if (selectedAnalysis == 2) {
		analyser->run(ElectionAnalyser::Type::Cluster, selectedElection);
	}
	paint();
}

// Handles the movement of the mouse in the display frame.
void AnalysisFrame::OnMouseMove(wxMouseEvent&) {
	paint();
}

void AnalysisFrame::OnTextDown(wxCommandEvent&)
{
	textOffset.y -= TextVerticalMovement;
	paint();
}

void AnalysisFrame::OnTextUp(wxCommandEvent&)
{
	textOffset.y += TextVerticalMovement;
	textOffset.y = std::min(textOffset.y, 0);
	paint();
}

void AnalysisFrame::OnTextRight(wxCommandEvent&)
{
	textOffset.x -= TextHorizontalMovement;
	paint();
}

void AnalysisFrame::OnTextLeft(wxCommandEvent&)
{
	textOffset.x += TextHorizontalMovement;
	textOffset.x = std::min(textOffset.x, 0);
	paint();
}

void AnalysisFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &AnalysisFrame::OnResize, this, PA_AnalysisFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &AnalysisFrame::OnElectionSelection, this, ControlId::SelectElection);
	Bind(wxEVT_COMBOBOX, &AnalysisFrame::OnAnalysisSelection, this, ControlId::SelectAnalysis);
	Bind(wxEVT_BUTTON, &AnalysisFrame::OnAnalyse, this, ControlId::Analyse);
	Bind(wxEVT_TOOL, &AnalysisFrame::OnTextDown, this, ControlId::TextDown);
	Bind(wxEVT_TOOL, &AnalysisFrame::OnTextUp, this, ControlId::TextUp);
	Bind(wxEVT_TOOL, &AnalysisFrame::OnTextRight, this, ControlId::TextRight);
	Bind(wxEVT_TOOL, &AnalysisFrame::OnTextLeft, this, ControlId::TextLeft);
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

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\up_arrow.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\down_arrow.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\left_arrow.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\right_arrow.png", wxBITMAP_TYPE_PNG);

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
	analysisArray.push_back("Swing Analysis");
	analysisArray.push_back("Cluster Analysis");
	if (selectedAnalysis >= 0) {
		comboBoxString = electionArray[selectedAnalysis];
	}
	else comboBoxString = "";

	selectAnalysisComboBox = new wxComboBox(toolBar, ControlId::SelectAnalysis, comboBoxString, wxPoint(0, 0), wxSize(150, 30), analysisArray);

	analyseButton = new wxButton(toolBar, ControlId::Analyse, "Analyse", wxPoint(0, 0), wxSize(90, 24));

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectElectionComboBox);
	toolBar->AddControl(selectAnalysisComboBox);
	toolBar->AddControl(analyseButton);

	toolBar->AddTool(ControlId::TextUp, "Text Up", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Text Up");
	toolBar->AddTool(ControlId::TextDown, "Text Down", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Text Down");
	toolBar->AddTool(ControlId::TextLeft, "Text Left", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Text Left");
	toolBar->AddTool(ControlId::TextRight, "Text Right", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Text Right");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void AnalysisFrame::render(wxDC& dc)
{
	AnalysisFrameRenderer::clearDC(dc);

	if (selectedElection < 0 || selectedElection >= project->elections().count()) return;

	if (!analyser) return;

	AnalysisFrameRenderer renderer(*project, dc, *analyser, dcPanel->GetClientSize(), textOffset);

	renderer.render();
}
