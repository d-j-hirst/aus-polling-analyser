#include "EditSeatFrame.h"

#include "ChoiceInput.h"
#include "FloatInput.h"
#include "General.h"
#include "TextInput.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	PreviousName,
	Incumbent,
	Challenger,
	Challenger2,
	Region,
	Margin,
	LocalModifier,
	IncumbentOdds,
	ChallengerOdds,
	Challenger2Odds,
};

EditSeatFrame::EditSeatFrame(Function function, OkCallback callback, PartyCollection const& parties,
	RegionCollection const& regions, Seat seat)
	: wxDialog(NULL, 0, (function == Function::New ? "New Seat" : "Edit Seat"), wxDefaultPosition, wxSize(375, 371)),
	callback(callback), parties(parties), regions(regions), seat(seat)
{
	validateSeatParties();
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditSeatFrame::validateSeatParties()
{
	int partyCount = parties.count();
	// If a model has not been specified it should default to the first.
	if (this->seat.incumbent == Party::InvalidId) this->seat.incumbent = 0;
	if (this->seat.challenger == Party::InvalidId) this->seat.challenger = std::min(1, partyCount - 1);
	if (this->seat.challenger2 == Party::InvalidId) this->seat.challenger2 = partyCount - 1;
	if (this->seat.region == Region::InvalidId) this->seat.region = regions.indexToId(0);
}

void EditSeatFrame::createControls(int & y)
{
	createNameInput(y);
	createPreviousNameInput(y);
	createIncumbentInput(y);
	createChallengerInput(y);
	createChallenger2Input(y);
	createRegionInput(y);
	createMarginInput(y);
	createLocalModifierInput(y);
	createIncumbentOddsInput(y);
	createChallengerOddsInput(y);
	createChallenger2OddsInput(y);

	createOkCancelButtons(y);
}

void EditSeatFrame::createNameInput(int& y)
{
	auto nameCallback = [this](std::string s) -> void {seat.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", seat.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditSeatFrame::createPreviousNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {seat.previousName = s; };
	previousNameInput.reset(new TextInput(this, ControlId::PreviousName, "Previous Name:", seat.previousName, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditSeatFrame::createIncumbentInput(int & y)
{
	int selectedIncumbent = parties.idToIndex(seat.incumbent);

	auto incumbentCallback = [this](int i) {seat.incumbent = parties.indexToId(i); };
	incumbentInput.reset(new ChoiceInput(this, ControlId::Incumbent, "Incumbent: ", collectPartyStrings(),
		selectedIncumbent, wxPoint(2, y), incumbentCallback));
	y += incumbentInput->Height + ControlPadding;
}

void EditSeatFrame::createChallengerInput(int & y)
{
	int selectedChallenger = parties.idToIndex(seat.challenger);

	auto challengerCallback = [this](int i) {seat.challenger = parties.indexToId(i); };
	challengerInput.reset(new ChoiceInput(this, ControlId::Challenger, "Challenger: ", collectPartyStrings(),
		selectedChallenger, wxPoint(2, y), challengerCallback));
	y += challengerInput->Height + ControlPadding;
}

void EditSeatFrame::createChallenger2Input(int & y)
{
	int selectedChallenger2 = parties.idToIndex(seat.challenger2);

	auto challenger2Callback = [this](int i) {seat.challenger2 = parties.indexToId(i); };
	challenger2Input.reset(new ChoiceInput(this, ControlId::Challenger2, "Challenger 2: ", collectPartyStrings(),
		selectedChallenger2, wxPoint(2, y), challenger2Callback));
	y += challenger2Input->Height + ControlPadding;
}

void EditSeatFrame::createRegionInput(int & y)
{
	wxArrayString regionArray;
	for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
		regionArray.push_back(it->second.name);
	}
	int selectedRegion = regions.idToIndex(seat.region);

	auto regionCallback = [this](int i) {seat.region = regions.indexToId(i); };
	regionInput.reset(new ChoiceInput(this, ControlId::Region, "Region: ", regionArray,
		selectedRegion, wxPoint(2, y), regionCallback));
	y += regionInput->Height + ControlPadding;
}

void EditSeatFrame::createMarginInput(int & y)
{
	auto marginCallback = [this](float f) -> void {seat.margin = f; };
	auto marginValidator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	marginInput.reset(new FloatInput(this, ControlId::Margin, "Margin:", seat.margin,
		wxPoint(2, y), marginCallback, marginValidator));
	y += marginInput->Height + ControlPadding;
}

void EditSeatFrame::createLocalModifierInput(int & y)
{
	auto localModifierCallback = [this](float f) -> void {seat.localModifier = f; };
	auto localModifierValidator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	localModifierInput.reset(new FloatInput(this, ControlId::LocalModifier, "Local Modifier:", seat.localModifier,
		wxPoint(2, y), localModifierCallback, localModifierValidator));
	y += localModifierInput->Height + ControlPadding;
}

void EditSeatFrame::createIncumbentOddsInput(int & y)
{
	auto incumbentOddsCallback = [this](float f) -> void {seat.incumbentOdds = f; };
	auto incumbentOddsValidator = [](float f) {return std::max(f, 1.0f); };
	incumbentOddsInput.reset(new FloatInput(this, ControlId::IncumbentOdds, "Incumbent Odds:", seat.incumbentOdds,
		wxPoint(2, y), incumbentOddsCallback, incumbentOddsValidator));
	y += incumbentOddsInput->Height + ControlPadding;
}

void EditSeatFrame::createChallengerOddsInput(int & y)
{
	auto challengerOddsCallback = [this](float f) -> void {seat.challengerOdds = f; };
	auto challengerOddsValidator = [](float f) {return std::max(f, 1.0f); };
	challengerOddsInput.reset(new FloatInput(this, ControlId::ChallengerOdds, "Challenger Odds:", seat.challengerOdds,
		wxPoint(2, y), challengerOddsCallback, challengerOddsValidator));
	y += challengerOddsInput->Height + ControlPadding;
}

void EditSeatFrame::createChallenger2OddsInput(int & y)
{
	auto challenger2OddsCallback = [this](float f) -> void {seat.challenger2Odds = f; };
	auto challenger2OddsValidator = [](float f) {return std::max(f, 1.0f); };
	challenger2OddsInput.reset(new FloatInput(this, ControlId::Challenger2Odds, "Challenger 2 Odds:", seat.challenger2Odds,
		wxPoint(2, y), challenger2OddsCallback, challenger2OddsValidator));
	y += challenger2OddsInput->Height + ControlPadding;
}

void EditSeatFrame::createOkCancelButtons(int & y)
{
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditSeatFrame::OnOK, this, ControlId::Ok);
	y += FloatInput::Height + ControlPadding;
}

void EditSeatFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

wxArrayString EditSeatFrame::collectPartyStrings()
{
	wxArrayString partyArray;
	for (auto it = parties.cbegin(); it != parties.cend(); ++it) {
		partyArray.push_back(it->second.name);
	}
	return partyArray;
}

void EditSeatFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (seat.challenger == seat.incumbent) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Challenger cannot be same party as incumbent. Please change one of these parties.");

		message->ShowModal();
		return;
	}

	callback(seat);

	// Then close this dialog.
	Close();
}