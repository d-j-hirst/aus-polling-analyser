#include "EditSimulationFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "ProjectionCollection.h"
#include "TextInput.h"

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	BaseProjection,
	NumIterations,
	PrevElection2pp,
	PrevTermCodes,
	Live,
	ReportMode,
	PreviousResultsUrl,
	PreloadUrl,
	CurrentTestUrl,
	CurrentRealUrl,
	FedElectionDate
};

EditSimulationFrame::EditSimulationFrame(Function function, OkCallback okCallback, ProjectionCollection const& projections, Simulation::Settings simulation)
	: wxDialog(NULL, 0, (function == Function::New ? "New Simulation" : "Edit Simulation"), wxDefaultPosition, wxSize(500, 400)),
	okCallback(okCallback), projections(projections), simulationSettings(simulation)
{
	// If a model has not been specified it should default to the first.
	if (simulationSettings.baseProjection == Projection::InvalidId) this->simulationSettings.baseProjection = projections.indexToId(0);

	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditSimulationFrame::createControls(int & y)
{
	createNameInput(y);
	createProjectionInput(y);
	createNumIterationsInput(y);
	createPrevElection2ppInput(y);
	createPrevTermCodesInput(y);
	createLiveInput(y);
	createReportModeInput(y);
	createPreviousResultsUrlInput(y);
	createPreloadUrlInput(y);
	createCurrentTestUrlInput(y);
	createCurrentRealUrlInput(y);
	createFedElectionDateInput(y);

	createOkCancelButtons(y);
}

void EditSimulationFrame::createNameInput(int & y)
{
	auto callback = [this](std::string s) -> void {simulationSettings.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", simulationSettings.name, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createProjectionInput(int & y)
{
	wxArrayString projectionArray;
	int selectedProjection = 0;
	int count = 0;
	for (auto const& [key, projection] : projections) {
		projectionArray.push_back(projection.getSettings().name);
		if (key == simulationSettings.baseProjection) selectedProjection = count;
		++count;
	}

	auto callback = [this](int i) {simulationSettings.baseProjection = projections.indexToId(i); };
	projectionInput.reset(new ChoiceInput(this, ControlId::BaseProjection, "Base projection: ", projectionArray, selectedProjection,
		wxPoint(2, y), callback));
	y += projectionInput->Height + ControlPadding;
}

void EditSimulationFrame::createNumIterationsInput(int & y)
{
	auto callback = [this](int i) -> void {simulationSettings.numIterations = i; };
	auto validator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", simulationSettings.numIterations,
		wxPoint(2, y), callback, validator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditSimulationFrame::createPrevElection2ppInput(int & y)
{
	auto callback = [this](float f) -> void {simulationSettings.prevElection2pp = f; };
	auto validator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	prevElection2ppInput.reset(new FloatInput(this, ControlId::PrevElection2pp, "Previous election 2pp:", simulationSettings.prevElection2pp,
		wxPoint(2, y), callback, validator));
	y += prevElection2ppInput->Height + ControlPadding;
}

void EditSimulationFrame::createPrevTermCodesInput(int& y)
{
	std::string termCodes = "";
	if (simulationSettings.prevTermCodes.size()) {
		termCodes += simulationSettings.prevTermCodes[0];
		for (size_t i = 1; i < simulationSettings.prevTermCodes.size(); ++i) {
			termCodes += "," + simulationSettings.prevTermCodes[i];
		}
	}

	auto callback = std::bind(&EditSimulationFrame::updatePrevTermCodes, this, _1);
	prevTermCodesInput.reset(new TextInput(this, ControlId::PrevTermCodes, "Previous Term Codes:", termCodes, wxPoint(2, y), callback));
	y += prevTermCodesInput->Height + ControlPadding;
}

void EditSimulationFrame::createLiveInput(int & y)
{
	wxArrayString liveArray;
	liveArray.push_back("Projection only");
	liveArray.push_back("Manual input");
	liveArray.push_back("Automatic downloading");

	auto callback = [this](int i) {simulationSettings.live = Simulation::Settings::Mode(i); };
	liveInput.reset(new ChoiceInput(this, ControlId::Live, "Live status:", liveArray, int(simulationSettings.live),
		wxPoint(2, y), callback));
	y += liveInput->Height + ControlPadding;
}

void EditSimulationFrame::createReportModeInput(int& y)
{
	wxArrayString reportModeArray;
	reportModeArray.push_back("Regular Forecast");
	reportModeArray.push_back("Live Forecast");
	reportModeArray.push_back("Nowcast");

	auto callback = [this](int i) {simulationSettings.reportMode = Simulation::Settings::ReportMode(i); };
	reportModeInput.reset(new ChoiceInput(this, ControlId::ReportMode, "Report Mode:", reportModeArray, int(simulationSettings.reportMode),
		wxPoint(2, y), callback));
	y += liveInput->Height + ControlPadding;
}

void EditSimulationFrame::createPreviousResultsUrlInput(int& y)
{
	auto callback = [this](std::string s) -> void {simulationSettings.previousResultsUrl = s; };
	previousResultsUrlInput.reset(new TextInput(this, ControlId::PreviousResultsUrl, "Previous Results URL:", simulationSettings.previousResultsUrl, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createPreloadUrlInput(int& y)
{
	auto callback = [this](std::string s) -> void {simulationSettings.preloadUrl = s; };
	preloadUrlInput.reset(new TextInput(this, ControlId::PreloadUrl, "Preload URL:", simulationSettings.preloadUrl, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createCurrentTestUrlInput(int& y)
{
	auto callback = [this](std::string s) -> void {simulationSettings.currentTestUrl = s; };
	currentTestUrlInput.reset(new TextInput(this, ControlId::CurrentTestUrl, "Current Test URL:", simulationSettings.currentTestUrl, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createCurrentRealUrlInput(int& y)
{
	auto callback = [this](std::string s) -> void {simulationSettings.currentRealUrl = s; };
	currentRealUrlInput.reset(new TextInput(this, ControlId::CurrentRealUrl, "Current Real URL:", simulationSettings.currentRealUrl, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createFedElectionDateInput(int& y)
{
	auto callback = [this](wxDateTime const& d) -> void {simulationSettings.fedElectionDate = d; };
	fedElectionDateInput.reset(new DateInput(this, ControlId::FedElectionDate, "Federal Election Date: ", simulationSettings.fedElectionDate,
		wxPoint(2, y), callback));
	y += fedElectionDateInput->Height + ControlPadding;
}

void EditSimulationFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditSimulationFrame::OnOK, this, Ok);
	y += TextInput::Height + ControlPadding;
}

void EditSimulationFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditSimulationFrame::OnOK(wxCommandEvent& WXUNUSED(event))
{
	okCallback(simulationSettings);
	// Then close this dialog.
	Close();
}

void EditSimulationFrame::updatePrevTermCodes(std::string shortCodes)
{
	simulationSettings.prevTermCodes = splitString(shortCodes, ",");
}