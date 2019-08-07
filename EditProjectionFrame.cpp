#include "EditProjectionFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "ModelCollection.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	BaseModel,
	EndDate,
	NumIterations,
	VoteLoss,
	DailyChange,
	InitialChange,
	NumElections,
};

EditProjectionFrame::EditProjectionFrame(Function function, OkCallback callback, ModelCollection const& models, Projection projection)
	: wxDialog(NULL, 0, (function == Function::New ? "New Projection" : "Edit Projection"), wxDefaultPosition, wxSize(375, 287)),
	callback(callback), models(models), projection(projection)
{
	// If a model has not been specified it should default to the first.
	if (this->projection.baseModel == Model::InvalidId) this->projection.baseModel = models.indexToId(0);

	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditProjectionFrame::createControls(int & y)
{
	createNameInput(y);
	createModelInput(y);
	createEndDateInput(y);
	createNumIterationsInput(y);
	createVoteLossInput(y);
	createDailyChangeInput(y);
	createInitialChangeInput(y);
	createNumElectionsInput(y);

	createOkCancelButtons(y);
}

void EditProjectionFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {projection.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", projection.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditProjectionFrame::createModelInput(int & y)
{
	wxArrayString modelArray;
	int selectedModel = 0;
	int count = 0;
	for (auto modelIt = models.cbegin(); modelIt != models.cend(); ++modelIt, ++count) {
		modelArray.push_back(modelIt->second.name);
		if (modelIt->first == projection.baseModel) selectedModel = count;
	}

	auto modelCallback = [this](int i) {projection.baseModel = models.indexToId(i); };
	modelInput.reset(new ChoiceInput(this, ControlId::BaseModel, "Base model: ", modelArray, selectedModel,
		wxPoint(2, y), modelCallback));
	y += modelInput->Height + ControlPadding;
}

void EditProjectionFrame::createEndDateInput(int & y)
{
	auto endDateCallback = [this](wxDateTime const& d) -> void {projection.endDate = d; };
	endDateInput.reset(new DateInput(this, ControlId::EndDate, "End Date: ", projection.endDate,
		wxPoint(2, y), endDateCallback));
	y += endDateInput->Height + ControlPadding;
}

void EditProjectionFrame::createNumIterationsInput(int & y)
{
	auto numIterationsCallback = [this](int i) -> void {projection.numIterations = i; };
	auto numIterationsValidator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", projection.numIterations,
		wxPoint(2, y), numIterationsCallback, numIterationsValidator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditProjectionFrame::createVoteLossInput(int & y)
{
	auto voteLossCallback = [this](float f) -> void {projection.leaderVoteDecay = f; };
	auto voteLossValidator = [](float f) {return std::clamp(f, 0.0f, 1.0f); };
	voteLossInput.reset(new FloatInput(this, ControlId::VoteLoss, "Leading party vote decay:", projection.leaderVoteDecay,
		wxPoint(2, y), voteLossCallback, voteLossValidator));
	y += voteLossInput->Height + ControlPadding;
}

void EditProjectionFrame::createDailyChangeInput(int & y)
{
	auto dailyChangeCallback = [this](float f) -> void {projection.dailyChange = f; };
	auto dailyChangeValidator = [](float f) {return std::max(f, 0.0f); };
	dailyChangeInput.reset(new FloatInput(this, ControlId::DailyChange, "SD of daily vote change:", projection.dailyChange,
		wxPoint(2, y), dailyChangeCallback, dailyChangeValidator));
	y += dailyChangeInput->Height + ControlPadding;
}

void EditProjectionFrame::createInitialChangeInput(int & y)
{
	auto initialChangeCallback = [this](float f) -> void {projection.initialStdDev = f; };
	auto initialChangeValidator = [](float f) {return std::max(f, 0.0f); };
	initialChangeInput.reset(new FloatInput(this, ControlId::InitialChange, "SD of initial vote change:", projection.initialStdDev,
		wxPoint(2, y), initialChangeCallback, initialChangeValidator));
	y += initialChangeInput->Height + ControlPadding;
}

void EditProjectionFrame::createNumElectionsInput(int & y)
{
	auto numElectionsCallback = [this](int i) -> void {projection.numElections = i; };
	auto numElectionsValidator = [](int i) {return std::max(1, i); };
	numElectionsInput.reset(new IntInput(this, ControlId::NumElections, "Number of Elections:", projection.numElections,
		wxPoint(2, y), numElectionsCallback, numElectionsValidator));
	y += numElectionsInput->Height + ControlPadding;
}

void EditProjectionFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditProjectionFrame::OnOK, this, Ok);
	y += TextInput::Height + ControlPadding;
}

void EditProjectionFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditProjectionFrame::OnOK(wxCommandEvent& WXUNUSED(event))\
{
	// If this is set to true the projection has not yet been updated.
	projection.lastUpdated = wxInvalidDateTime;
	callback(projection);
	// Then close this dialog.
	Close();
}