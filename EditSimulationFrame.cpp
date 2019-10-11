#include "EditSimulationFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "ProjectionCollection.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	BaseProjection,
	NumIterations,
	PrevElection2pp,
	StateSD,
	StateDecay,
	Live,
};

EditSimulationFrame::EditSimulationFrame(Function function, OkCallback callback, ProjectionCollection const& projections, Simulation simulation)
	: wxDialog(NULL, 0, (function == Function::New ? "New Simulation" : "Edit Simulation"), wxDefaultPosition, wxSize(375, 260)),
	callback(callback), projections(projections), simulation(simulation)
{
	// If a model has not been specified it should default to the first.
	if (this->simulation.baseProjection == Projection::InvalidId) this->simulation.baseProjection = projections.indexToId(0);

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
	createStateSDInput(y);
	createStateDecayInput(y);
	createLiveInput(y);

	createOkCancelButtons(y);
}

void EditSimulationFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {simulation.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", simulation.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditSimulationFrame::createProjectionInput(int & y)
{
	wxArrayString projectionArray;
	int selectedProjection = 0;
	int count = 0;
	for (auto const& [key, projection] : projections) {
		projectionArray.push_back(projection.getSettings().name);
		if (key == simulation.baseProjection) selectedProjection = count;
		++count;
	}

	auto projectionCallback = [this](int i) {simulation.baseProjection = projections.indexToId(i); };
	projectionInput.reset(new ChoiceInput(this, ControlId::BaseProjection, "Base projection: ", projectionArray, selectedProjection,
		wxPoint(2, y), projectionCallback));
	y += projectionInput->Height + ControlPadding;
}

void EditSimulationFrame::createNumIterationsInput(int & y)
{
	auto numIterationsCallback = [this](int i) -> void {simulation.numIterations = i; };
	auto numIterationsValidator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", simulation.numIterations,
		wxPoint(2, y), numIterationsCallback, numIterationsValidator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditSimulationFrame::createPrevElection2ppInput(int & y)
{
	auto prevElection2ppCallback = [this](float f) -> void {simulation.prevElection2pp = f; };
	auto prevElection2ppValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	prevElection2ppInput.reset(new FloatInput(this, ControlId::PrevElection2pp, "Previous election 2pp:", simulation.prevElection2pp,
		wxPoint(2, y), prevElection2ppCallback, prevElection2ppValidator));
	y += prevElection2ppInput->Height + ControlPadding;
}

void EditSimulationFrame::createStateSDInput(int & y)
{
	auto stateSDCallback = [this](float f) -> void {simulation.stateSD = f; };
	auto stateSDValidator = [](float f) {return std::max(f, 0.0f); };
	stateSDInput.reset(new FloatInput(this, ControlId::StateSD, "State standard deviation:", simulation.stateSD,
		wxPoint(2, y), stateSDCallback, stateSDValidator));
	y += stateSDInput->Height + ControlPadding;
}

void EditSimulationFrame::createStateDecayInput(int & y)
{
	auto stateDecayCallback = [this](float f) -> void {simulation.stateDecay = f; };
	auto stateDecayValidator = [](float f) {return std::clamp(f, 0.0f, 1.0f); };
	stateDecayInput.reset(new FloatInput(this, ControlId::StateDecay, "State daily vote decay:", simulation.stateDecay,
		wxPoint(2, y), stateDecayCallback, stateDecayValidator));
	y += stateDecayInput->Height + ControlPadding;
}

void EditSimulationFrame::createLiveInput(int & y)
{
	wxArrayString liveArray;
	liveArray.push_back("Projection only");
	liveArray.push_back("Manual input");
	liveArray.push_back("Automatic downloading");

	auto liveCallback = [this](int i) {simulation.live = Simulation::Mode(i); };
	liveInput.reset(new ChoiceInput(this, ControlId::Live, "Live status:", liveArray, int(simulation.live),
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
	// If this is set to true the projection has not yet been updated.
	simulation.lastUpdated = wxInvalidDateTime;
	callback(simulation);
	// Then close this dialog.
	Close();
}