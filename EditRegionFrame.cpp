#include "EditRegionFrame.h"

#include "FloatInput.h"
#include "General.h"
#include "IntInput.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Ok,
	Name,
	Population,
	LastElection2pp,
	Sample2pp,
	AnalysisCode,
	HomeRegionMod
};

EditRegionFrame::EditRegionFrame(Function function, OkCallback callback, Region region)
	: wxDialog(NULL, 0, (function == Function::New ? "New Region" : "Edit Region"), wxDefaultPosition, wxSize(500, 400)),
	callback(callback), region(region)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditRegionFrame::createControls(int & y)
{
	createNameInput(y);
	createPopulationInput(y);
	createLastElection2ppInput(y);
	createSample2ppInput(y);
	createAnalysisCodeInput(y);
	createHomeRegionModInput(y);

	createOkCancelButtons(y);
}

void EditRegionFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {region.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", region.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditRegionFrame::createPopulationInput(int & y)
{
	auto populationCallback = [this](int i) -> void {region.population = i; };
	auto populationValidator = [](int i) {return std::max(1, i); };
	populationInput.reset(new IntInput(this, ControlId::Population, "Population:", region.population,
		wxPoint(2, y), populationCallback, populationValidator));
	y += populationInput->Height + ControlPadding;
}

void EditRegionFrame::createLastElection2ppInput(int & y)
{
	auto lastElection2ppCallback = [this](float f) -> void {region.lastElection2pp = f; };
	auto lastElection2ppValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	lastElection2ppInput.reset(new FloatInput(this, ControlId::LastElection2pp, "Last election 2pp vote:", region.lastElection2pp,
		wxPoint(2, y), lastElection2ppCallback, lastElection2ppValidator));
	y += lastElection2ppInput->Height + ControlPadding;
}

void EditRegionFrame::createSample2ppInput(int & y)
{
	auto sample2ppCallback = [this](float f) -> void {region.sample2pp = f; };
	auto sample2ppValidator = [](float f) {return std::clamp(f, -100.0f, 100.0f); };
	sample2ppInput.reset(new FloatInput(this, ControlId::Sample2pp, "Polling average 2pp vote:", region.sample2pp,
		wxPoint(2, y), sample2ppCallback, sample2ppValidator));
	y += sample2ppInput->Height + ControlPadding;
}

void EditRegionFrame::createAnalysisCodeInput(int& y)
{
	auto analysisCodeCallback = [this](std::string s) -> void {region.analysisCode = s; };
	analysisCodeInput.reset(new TextInput(this, ControlId::AnalysisCode, "Analysis Code:", region.analysisCode, wxPoint(2, y), analysisCodeCallback));
	y += analysisCodeInput->Height + ControlPadding;
}

void EditRegionFrame::createHomeRegionModInput(int& y)
{
	auto homeRegionModCallback = [this](float f) -> void {region.homeRegionMod = f; };
	auto homeRegionModValidator = [](float f) {return std::max(f, 0.0f); };
	homeRegionModInput.reset(new FloatInput(this, ControlId::HomeRegionMod, "Home Region Modifier:", region.homeRegionMod,
		wxPoint(2, y), homeRegionModCallback, homeRegionModValidator));
	y += homeRegionModInput->Height + ControlPadding;
}

void EditRegionFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditRegionFrame::OnOK, this, ControlId::Ok);
	y += TextInput::Height + ControlPadding;
}

void EditRegionFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditRegionFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {
	// Call the function that was passed when this frame was opened.
	callback(region);

	// Then close this dialog.
	Close();
}