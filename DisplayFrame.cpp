#include "DisplayFrame.h"

#include "DisplayFrameRenderer.h"
#include "General.h"

#include "wx/dcbuffer.h"

#include <algorithm>

// IDs for the controls and the menu commands
enum ControlId {
	Base = 450, // To avoid mixing events with other frames.
	Frame,
	DcPanel,
	SelectSimulation,
	SaveReport,
	SelectSavedReport
};

// frame constructor
DisplayFrame::DisplayFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Display", wxPoint(333, 0), project),
	refresher(refresher)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();

	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	bindEventHandlers();

	Layout();

	paint();
}

void DisplayFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void DisplayFrame::refreshData() {
	refreshToolbar();
}

void DisplayFrame::OnResize(wxSizeEvent&) {
	// Set the poll data table to the entire client size.
	//pollData->SetSize(wxSize(this->GetClientSize().x,
	//	this->GetClientSize().y));
}

void DisplayFrame::OnSimulationSelection(wxCommandEvent&) {
	// Don't want to reset things if we're reselecting the same simulation
	if (selectSimulationComboBox->GetCurrentSelection() != selectedSimulation) {
		selectedSimulation = selectSimulationComboBox->GetCurrentSelection();
		selectedSaveReport = -1; // always reset to latest report
		refreshSavedReports(); // need to update the saved reports combobox
		paint();
	}
}

void DisplayFrame::OnSaveReport(wxCommandEvent&)
{
	if (selectedSimulation < 0 || selectedSimulation >= project->simulations().count()) {
		wxMessageBox("Could not save report: no simulation selected");
		return;
	}
	auto& simulation = project->simulations().access(project->simulations().indexToId(selectedSimulation));
	if (!simulation.isValid()) {
		wxMessageBox("Could not save report: simulation has not yet been run");
		return;
	}
	std::string label = wxGetTextFromUser("Enter a label for this saved report:", "Saved Report").ToStdString();
	if (!label.size()) {
		wxMessageBox("Report not saved.");
		return;
	}
	simulation.saveReport(label);
	refreshToolbar();
}

void DisplayFrame::OnSavedReportSelection(wxCommandEvent&)
{
	selectedSaveReport = selectSavedReportComboBox->GetCurrentSelection() - 1;
	paint();
}

// Handles the movement of the mouse in the display frame.
void DisplayFrame::OnMouseMove(wxMouseEvent&) {
	paint();
}

void DisplayFrame::bindEventHandlers()
{
	// Need to resize controls if this frame is resized.
	//Bind(wxEVT_SIZE, &DisplayFrame::OnResize, this, PA_DisplayFrame_FrameID);

	Bind(wxEVT_COMBOBOX, &DisplayFrame::OnSimulationSelection, this, ControlId::SelectSimulation);
	Bind(wxEVT_TOOL, &DisplayFrame::OnSaveReport, this, ControlId::SaveReport);
	Bind(wxEVT_COMBOBOX, &DisplayFrame::OnSavedReportSelection, this, ControlId::SelectSavedReport);
	dcPanel->Bind(wxEVT_MOTION, &DisplayFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &DisplayFrame::OnPaint, this, ControlId::DcPanel);
}

void DisplayFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
	toolBar->Refresh();
}

void DisplayFrame::updateInterface() {
}

void DisplayFrame::refreshToolbar() {

	if (toolBar) toolBar->Destroy();

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[1];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\camera.png", wxBITMAP_TYPE_PNG);

	// *** Simulation Combo Box *** //

	// Create the choices for the combo box.
	// Set the selected simulation to be the first simulation
	wxArrayString simulationArray;
	for (auto const&[key, simulation] : project->simulations()) {
		simulationArray.push_back(simulation.getSettings().name);
	}
	std::string comboBoxString;
	if (selectedSimulation < 0 && simulationArray.size()) {
		selectedSimulation = 0;
	}
	if (selectedSimulation >= int(simulationArray.size())) {
		selectedSimulation = int(simulationArray.size()) - 1;
	}
	if (selectedSimulation >= 0) {
		comboBoxString = simulationArray[selectedSimulation];
	}

	selectSimulationComboBox = new wxComboBox(toolBar, ControlId::SelectSimulation, comboBoxString, wxPoint(0, 0), wxSize(150, 30), simulationArray);

	selectSavedReportComboBox = new wxComboBox(toolBar, ControlId::SelectSavedReport, "", wxPoint(0, 0), wxSize(150, 30), wxArrayString());
	refreshSavedReports();

	// Add the tools that will be used on the toolbar.
	toolBar->AddControl(selectSimulationComboBox);
	toolBar->AddTool(ControlId::SaveReport, "Save report", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Save report");
	toolBar->AddControl(selectSavedReportComboBox);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void DisplayFrame::refreshSavedReports()
{
	// Create the choices for the combo box.
	// Set the selected simulation to be the first simulation
	wxArrayString saveReportArray;
	saveReportArray.push_back("Latest Report");
	if (selectedSimulation >= 0 && selectedSimulation < project->simulations().count()) {
		auto thisSimulation = project->simulations().viewByIndex(selectedSimulation);
		for (auto const& savedReport : thisSimulation.viewSavedReports()) {
			std::string label = savedReport.label + " - " + savedReport.dateSaved.FormatISODate().ToStdString();
			saveReportArray.push_back(label);
		}
	}
	std::string comboBoxString = "";
	if (selectedSaveReport >= int(saveReportArray.size()) - 1) {
		selectedSaveReport = int(saveReportArray.size()) - 2;
	}
	comboBoxString = saveReportArray[selectedSaveReport + 1];

	selectSavedReportComboBox->Clear();
	selectSavedReportComboBox->Append(saveReportArray);
	selectSavedReportComboBox->SetSelection(selectedSaveReport);
	selectSavedReportComboBox->SetValue(comboBoxString);
}

void DisplayFrame::render(wxDC& dc)
{
	DisplayFrameRenderer::clearDC(dc);

	if (selectedSimulation < 0 || selectedSimulation >= project->simulations().count()) return;

	Simulation const& simulation = project->simulations().view(project->simulations().indexToId(selectedSimulation));

	if (!simulation.isValid() && selectedSaveReport == -1) return;

	Simulation::Report const& thisReport = (selectedSaveReport >= 0 ?
		simulation.viewSavedReports()[selectedSaveReport].report :
		simulation.getLatestReport());

	DisplayFrameRenderer renderer(dc, thisReport, dcPanel->GetClientSize());

	renderer.render();
}
