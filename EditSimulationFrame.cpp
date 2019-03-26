#include "EditSimulationFrame.h"
#include "SimulationsFrame.h"
#include "PollingProject.h"
#include "General.h"

// IDs for the controls and the menu commands
enum
{
	PA_EditSimulation_Base = 650, // To avoid mixing events with other frames.
	PA_EditSimulation_ButtonID_OK,
	PA_EditSimulation_TextBoxID_Name,
	PA_EditSimulation_ComboBoxID_BaseProjection,
	PA_EditSimulation_TextBoxID_NumIterations,
	PA_EditSimulation_TextBoxID_PrevElection2pp,
	PA_EditSimulation_TextBoxID_StateSD,
	PA_EditSimulation_TextBoxID_StateDecay,
	PA_EditSimulation_ComboBoxID_Live,
};

EditSimulationFrame::EditSimulationFrame(bool isNewSimulation, SimulationsFrame* const parent, PollingProject const* project, Simulation simulation)
	: wxDialog(NULL, 0, (isNewSimulation ? "New Simulation" : "Edit Simulation"), wxDefaultPosition, wxSize(375, 260)),
	isNewSimulation(isNewSimulation), parent(parent), project(project), simulation(simulation)
{
	// If a model has not been specified it should default to the first.
	if (this->simulation.baseProjection == nullptr) this->simulation.baseProjection = project->getProjectionPtr(0);

	// Generate the string for the number of iterations
	std::string numIterationsString = std::to_string(simulation.numIterations);

	// Store this string in case a text entry gives an error in the future.
	lastNumIterations = numIterationsString;

	// Generate the string for the previous election 2pp
	std::string prevElection2ppString = std::to_string(simulation.prevElection2pp);

	// Store this string in case a text entry gives an error in the future.
	lastPrevElection2pp = prevElection2ppString;

	// Generate the string for the state vote standard deviation
	std::string stateSDString = std::to_string(simulation.stateSD);

	// Store this string in case a text entry gives an error in the future.
	lastStateSD = stateSDString;

	// Generate the string for the state vote daily decay
	std::string stateDecayString = std::to_string(simulation.stateDecay);

	// Store this string in case a text entry gives an error in the future.
	lastStateDecay = stateDecayString;

	const int labelYOffset = 5;

	int currentHeight = 2;

	int textBoxWidth = 150;
	int labelWidth = 200;

	// Create the controls for the simulation name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditSimulation_TextBoxID_Name, simulation.name, wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// *** Projection Combo Box *** //

	// Create the choices for the combo box.
	// Also check if the poll's pollster matches any of the choices (otherwise it is set to the first).
	wxArrayString projectionArray;
	int selectedModel = 0;
	int count = 0;
	for (auto it = project->getProjectionBegin(); it != project->getProjectionEnd(); ++it, ++count) {
		projectionArray.push_back(it->name);
		if (&*it == simulation.baseProjection) selectedModel = count;
	}

	// Create the controls for the model combo box.
	projectionStaticText = new wxStaticText(this, 0, "Base projection:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	projectionComboBox = new wxComboBox(this, PA_EditSimulation_ComboBoxID_BaseProjection, projectionArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), projectionArray, wxCB_READONLY);

	// Sets the combo box selection to the simulations's base model.
	projectionComboBox->SetSelection(selectedModel);

	currentHeight += 27;

	// Create the controls for the simulation number of iterations
	numIterationsStaticText = new wxStaticText(this, 0, "Number of Iterations:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	numIterationsTextCtrl = new wxTextCtrl(this, PA_EditSimulation_TextBoxID_NumIterations, numIterationsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the simulation previous election 2pp
	prevElection2ppStaticText = new wxStaticText(this, 0, "Previous election 2pp:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	prevElection2ppTextCtrl = new wxTextCtrl(this, PA_EditSimulation_TextBoxID_PrevElection2pp, prevElection2ppString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the simulation state vote standard deviation
	stateSDStaticText = new wxStaticText(this, 0, "State standard deviation:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	stateSDTextCtrl = new wxTextCtrl(this, PA_EditSimulation_TextBoxID_StateSD, stateSDString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the simulation state vote daily decay
	stateDecayStaticText = new wxStaticText(this, 0, "State daily vote decay:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	stateDecayTextCtrl = new wxTextCtrl(this, PA_EditSimulation_TextBoxID_StateDecay, stateDecayString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Live status combo box
	wxArrayString liveArray;
	liveArray.push_back("Projection only");
	liveArray.push_back("Manual input");
	liveArray.push_back("Automatic downloading");
	int currentLiveSelection = int(simulation.live);

	// Create the controls for choosing whether this simulation is "live"
	liveStaticText = new wxStaticText(this, 0, "Live status:", wxPoint(2, currentHeight), wxSize(100, 23));
	liveComboBox = new wxComboBox(this, PA_EditSimulation_ComboBoxID_Live, liveArray[currentLiveSelection], 
		wxPoint(190, currentHeight - 2), wxSize(200, 23), liveArray, wxCB_READONLY);

	liveComboBox->SetSelection(currentLiveSelection);

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditSimulation_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditSimulationFrame::updateTextName, this, PA_EditSimulation_TextBoxID_Name);
	Bind(wxEVT_COMBOBOX, &EditSimulationFrame::updateComboBoxBaseProjection, this, PA_EditSimulation_ComboBoxID_BaseProjection);
	Bind(wxEVT_TEXT, &EditSimulationFrame::updateTextNumIterations, this, PA_EditSimulation_TextBoxID_NumIterations);
	Bind(wxEVT_TEXT, &EditSimulationFrame::updateTextPrevElection2pp, this, PA_EditSimulation_TextBoxID_PrevElection2pp);
	Bind(wxEVT_TEXT, &EditSimulationFrame::updateTextStateSD, this, PA_EditSimulation_TextBoxID_StateSD);
	Bind(wxEVT_TEXT, &EditSimulationFrame::updateTextStateDecay, this, PA_EditSimulation_TextBoxID_StateDecay);
	Bind(wxEVT_COMBOBOX, &EditSimulationFrame::updateLive, this, PA_EditSimulation_ComboBoxID_Live);
	Bind(wxEVT_BUTTON, &EditSimulationFrame::OnOK, this, PA_EditSimulation_ButtonID_OK);
}

void EditSimulationFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	// If this is set to true the simulation has not yet been updated.
	simulation.lastUpdated = wxInvalidDateTime;

	if (isNewSimulation) {
		// Get the parent frame to add a new simulation
		parent->OnNewSimulationReady(simulation);
	}
	else {
		// Get the parent frame to replace the old simulation with the current one
		parent->OnEditSimulationReady(simulation);
	}

	// Then close this dialog.
	Close();
}

void EditSimulationFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	simulation.name = event.GetString();
}

void EditSimulationFrame::updateComboBoxBaseProjection(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	simulation.baseProjection = project->getProjectionPtr(projectionComboBox->GetCurrentSelection());
}

void EditSimulationFrame::updateTextNumIterations(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			simulation.numIterations = 0;
			return;
		}

		// convert to an int
		int i = std::stoi(str); // This may throw an error of the std::logic_error type.
		if (i > 9999999) i = 9999999; // Some kind of maximum to avoid being ridiculous
		if (i < 0) i = 0;

		simulation.numIterations = i;

		// save this valid string in case the next text entry gives an error.
		lastNumIterations = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		numIterationsTextCtrl->SetLabel(lastNumIterations);
	}
}

void EditSimulationFrame::updateTextPrevElection2pp(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			simulation.prevElection2pp = 0.0f;
			return;
		}

		// convert to an int
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f; // Some kind of maximum to avoid being ridiculous
		if (f < 0.0f) f = 0.0f;

		simulation.prevElection2pp = f;

		// save this valid string in case the next text entry gives an error.
		lastPrevElection2pp = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		prevElection2ppTextCtrl->SetLabel(lastPrevElection2pp);
	}
}

void EditSimulationFrame::updateTextStateSD(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			simulation.stateSD = 0.0f;
			return;
		}

		// convert to an int
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 10.0f) f = 10.0f; // Some kind of maximum to avoid being ridiculous
		if (f < 0.0f) f = 0.0f;

		simulation.stateSD = f;

		// save this valid string in case the next text entry gives an error.
		lastStateSD = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		stateSDTextCtrl->SetLabel(lastStateSD);
	}
}

void EditSimulationFrame::updateTextStateDecay(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			simulation.stateDecay = 0.0f;
			return;
		}

		// convert to an int
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 10.0f) f = 10.0f; // Some kind of maximum to avoid being ridiculous
		if (f < 0.0f) f = 0.0f;

		simulation.stateDecay = f;

		// save this valid string in case the next text entry gives an error.
		lastStateDecay = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		stateDecayTextCtrl->SetLabel(lastStateDecay);
	}
}

void EditSimulationFrame::updateLive(wxCommandEvent & event)
{
	simulation.live = Simulation::Mode(event.GetSelection());
}
