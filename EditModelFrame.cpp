#include "EditModelFrame.h"

#include "DatePickerInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 550, // To avoid mixing events with other frames.
	Ok,
	Name,
	NumIterations,
	VoteTimeMultiplier,
	HouseEffectTimeMultiplier,
	CalibrationFirstPartyBias,
	StartDate,
	EndDate,
};

EditModelFrame::EditModelFrame(Function function, OkCallback callback, Model model)
	: wxDialog(NULL, 0, (function == Function::New ? "New Model" : "Edit Model")),
	model(model), callback(callback)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditModelFrame::createControls(int & y)
{
	createNameInput(y);
	createNumIterationsInput(y);
	createVoteTimeMultiplierInput(y);
	createHouseEffectTimeMultiplierInput(y);
	createCalibrationFirstPartyBiasInput(y);
	createStartDateInput(y);
	createEndDateInput(y);

	createOkCancelButtons(y);
}

void EditModelFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {model.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", model.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditModelFrame::createNumIterationsInput(int & y)
{
	auto numIterationsCallback = [this](int i) -> void {model.numIterations = i; };
	auto numIterationsValidator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", model.numIterations,
		wxPoint(2, y), numIterationsCallback, numIterationsValidator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditModelFrame::createVoteTimeMultiplierInput(int & y)
{
	auto voteTimeMultiplierCallback = [this](float f) -> void {model.trendTimeScoreMultiplier = f; };
	auto voteTimeMultiplierValidator = [](float f) {return std::max(0.001f, f); };
	voteTimeMultiplierInput.reset(new FloatInput(this, ControlId::VoteTimeMultiplier, "Vote Time-Multiplier:", model.trendTimeScoreMultiplier,
		wxPoint(2, y), voteTimeMultiplierCallback, voteTimeMultiplierValidator));
	y += voteTimeMultiplierInput->Height + ControlPadding;
}

void EditModelFrame::createHouseEffectTimeMultiplierInput(int & y)
{
	auto houseEffectTimeMultiplierCallback = [this](float f) -> void {model.houseEffectTimeScoreMultiplier = f; };
	auto houseEffectTimeMultiplierValidator = [](float f) {return std::max(0.001f, f); };
	houseEffectTimeMultiplierInput.reset(new FloatInput(this, ControlId::HouseEffectTimeMultiplier, "House Effect Time-Multiplier:", model.houseEffectTimeScoreMultiplier,
		wxPoint(2, y), houseEffectTimeMultiplierCallback, houseEffectTimeMultiplierValidator));
	y += houseEffectTimeMultiplierInput->Height + ControlPadding;
}

void EditModelFrame::createCalibrationFirstPartyBiasInput(int & y)
{
	auto calibrationFirstPartyBiasCallback = [this](float f) -> void {model.calibrationFirstPartyBias = f; };
	auto calibrationFirstPartyBiasValidator = [](float f) {return std::max(0.001f, f); };
	calibrationFirstPartyBiasInput.reset(new FloatInput(this, ControlId::CalibrationFirstPartyBias, "First Party Calibration: ", model.calibrationFirstPartyBias,
		wxPoint(2, y), calibrationFirstPartyBiasCallback, calibrationFirstPartyBiasValidator));
	y += houseEffectTimeMultiplierInput->Height + ControlPadding;
}

void EditModelFrame::createStartDateInput(int & y)
{
	logger << model.startDate.FormatISODate() << "\n";
	auto startDateCallback = [this](wxDateTime const& d) -> void {model.startDate = d; };
	startDateInput.reset(new DatePickerInput(this, ControlId::StartDate, "Start Date: ", model.startDate,
		wxPoint(2, y), startDateCallback));
	y += startDateInput->Height + ControlPadding;
}

void EditModelFrame::createEndDateInput(int & y)
{
	auto endDateCallback = [this](wxDateTime const& d) -> void {model.endDate = d; };
	endDateInput.reset(new DatePickerInput(this, ControlId::EndDate, "End Date: ", model.endDate,
		wxPoint(2, y), endDateCallback));
	y += endDateInput->Height + ControlPadding;
}

void EditModelFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditModelFrame::OnOK, this, Ok);
	y += TextInput::Height + ControlPadding;
}

void EditModelFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditModelFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {
	model.lastUpdated = wxInvalidDateTime;
	callback(model);
	Close();
}