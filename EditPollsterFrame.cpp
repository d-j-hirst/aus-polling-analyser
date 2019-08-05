#include "EditPollsterFrame.h"

#include "CheckInput.h"
#include "ColourInput.h"
#include "FloatInput.h"
#include "General.h"
#include "TextInput.h"

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

enum ControlId
{
	Ok,
	Name,
	Weight,
	Colour,
	UseForCalibration,
	IgnoreInitially,
};

EditPollsterFrame::EditPollsterFrame(Function function, OkCallback callback, Pollster pollster)
	: wxDialog(NULL, 0, (function == Function::New ? "New Pollster" : "Edit Pollster")),
	pollster(pollster), callback(callback)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditPollsterFrame::createControls(int& y)
{
	createNameInput(y);
	createWeightInput(y);
	createColourInput(y);
	createCalibrationInput(y);
	createIgnoreInitiallyInput(y);

	createOkCancelButtons(y);
}

void EditPollsterFrame::createNameInput(int& y)
{
	auto nameCallback = [this](std::string s) -> void {pollster.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", pollster.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditPollsterFrame::createWeightInput(int& y)
{
	auto weightCallback = [this](float f) -> void {pollster.weight = f; };
	auto weightValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	weightInput.reset(new FloatInput(this, ControlId::Weight, "Weight:", pollster.weight,
		wxPoint(2, y), weightCallback, weightValidator));
	y += weightInput->Height + ControlPadding;
}

void EditPollsterFrame::createColourInput(int& y)
{
	wxColour currentColour(pollster.colour);
	auto colourCallback = [this](wxColour c) -> void {pollster.colour = c.GetRGB(); };
	colourInput.reset(new ColourInput(this, ControlId::Colour, "Colour:", currentColour, wxPoint(2, y), colourCallback));
	y += colourInput->Height + ControlPadding;
}

void EditPollsterFrame::createCalibrationInput(int & y)
{
	auto calibrationCallback = [this](int i) -> void {pollster.useForCalibration = (i != 0); };
	calibrationInput.reset(new CheckInput(this, ControlId::UseForCalibration, "Use for calibration:", pollster.useForCalibration,
		wxPoint(2, y), calibrationCallback));
	y += calibrationInput->Height + ControlPadding;
}

void EditPollsterFrame::createIgnoreInitiallyInput(int & y)
{
	auto ignoreInitiallyCallback = [this](int i) -> void {pollster.ignoreInitially = (i != 0); };
	ignoreInitiallyInput.reset(new CheckInput(this, ControlId::IgnoreInitially, "Ignore initially:", pollster.ignoreInitially,
		wxPoint(2, y), ignoreInitiallyCallback));
	y += ignoreInitiallyInput->Height + ControlPadding;
}

void EditPollsterFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditPollsterFrame::OnOK, this, ControlId::Ok);
	y += TextInput::Height + ControlPadding;
}

void EditPollsterFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditPollsterFrame::OnOK(wxCommandEvent&)
{
	// Call the function that was passed when this frame was opened.
	callback(pollster);

	// Then close this dialog.
	Close();
}