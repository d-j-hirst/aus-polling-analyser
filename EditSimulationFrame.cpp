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
};

EditSimulationFrame::EditSimulationFrame(Function function, OkCallback callback, ProjectionCollection const& projections, Simulation::Settings simulation)
	: wxDialog(NULL, 0, (function == Function::New ? "New Simulation" : "Edit Simulation"), wxDefaultPosition, wxSize(375, 260)),
	callback(callback), projections(projections), simulationSettings(simulation)
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

	createOkCancelButtons(y);
}

void EditSimulationFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {simulationSettings.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", simulationSettings.name, wxPoint(2, y), nameCallback));
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

	auto projectionCallback = [this](int i) {simulationSettings.baseProjection = projections.indexToId(i); };
	projectionInput.reset(new ChoiceInput(this, ControlId::BaseProjection, "Base projection: ", projectionArray, selectedProjection,
		wxPoint(2, y), projectionCallback));
	y += projectionInput->Height + ControlPadding;
}

void EditSimulationFrame::createNumIterationsInput(int & y)
{
	auto numIterationsCallback = [this](int i) -> void {simulationSettings.numIterations = i; };
	auto numIterationsValidator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", simulationSettings.numIterations,
		wxPoint(2, y), numIterationsCallback, numIterationsValidator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditSimulationFrame::createPrevElection2ppInput(int & y)
{
	auto prevElection2ppCallback = [this](float f) -> void {simulationSettings.prevElection2pp = f; };
	auto prevElection2ppValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	prevElection2ppInput.reset(new FloatInput(this, ControlId::PrevElection2pp, "Previous election 2pp:", simulationSettings.prevElection2pp,
		wxPoint(2, y), prevElection2ppCallback, prevElection2ppValidator));
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

	auto prevTermCodesCallback = std::bind(&EditSimulationFrame::updatePrevTermCodes, this, _1);
	prevTermCodesInput.reset(new TextInput(this, ControlId::PrevTermCodes, "Previous Term Codes:", termCodes, wxPoint(2, y), prevTermCodesCallback));
	y += prevTermCodesInput->Height + ControlPadding;
}

void EditSimulationFrame::createLiveInput(int & y)
{
	wxArrayString liveArray;
	liveArray.push_back("Projection only");
	liveArray.push_back("Manual input");
	liveArray.push_back("Automatic downloading");

	auto liveCallback = [this](int i) {simulationSettings.live = Simulation::Settings::Mode(i); };
	liveInput.reset(new ChoiceInput(this, ControlId::Live, "Live status:", liveArray, int(simulationSettings.live),
		wxPoint(2, y), liveCallback));
	y += liveInput->Height + ControlPadding;
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
	callback(simulationSettings);
	// Then close this dialog.
	Close();
}

void EditSimulationFrame::updatePrevTermCodes(std::string shortCodes)
{
	simulationSettings.prevTermCodes = splitString(shortCodes, ",");
}