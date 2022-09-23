#include "EditSeatFrame.h"

#include "CheckInput.h"
#include "ChoiceInput.h"
#include "FloatInput.h"
#include "General.h"
#include "TextInput.h"

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	PreviousName,
	UseFpResults,
	Incumbent,
	Challenger,
	Challenger2,
	Region,
	Margin,
	PreviousSwing,
	LocalModifier,
	TransposedTppSwing,
	IncumbentOdds,
	ChallengerOdds,
	Challenger2Odds,
	SophomoreCandidate,
	SophomoreParty,
	Retirement,
	Disendorsement,
	PreviousDisendorsement,
	IncumbentRecontestConfirmed,
	ConfirmedProminentIndependent,
	PreviousIndRunning,
	ProminentMinors,
	BettingOdds,
	Polls,
	RunningParties,
	TcpChange,
	MinorViability,
	KnownPrepolls,
	KnownPostals,
	KnownAbsentCount,
	KnownProvisionalCount,
	KnownDecPrepollCount,
	KnownPostalCount
};

EditSeatFrame::EditSeatFrame(Function function, OkCallback callback, PartyCollection const& parties,
	RegionCollection const& regions, Seat seat)
	: wxDialog(NULL, 0, (function == Function::New ? "New Seat" : "Edit Seat"), wxDefaultPosition, wxSize(500, 371)),
	okCallback(callback), parties(parties), regions(regions), seat(seat)
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
	createUseFpResultsInput(y);
	createIncumbentInput(y);
	createChallengerInput(y);
	createChallenger2Input(y);
	createRegionInput(y);
	createMarginInput(y);
	createPreviousSwingInput(y);
	createLocalModifierInput(y);
	createTransposedTppSwingInput(y);
	createIncumbentOddsInput(y);
	createChallengerOddsInput(y);
	createChallenger2OddsInput(y);
	createSophomoreCandidateInput(y);
	createSophomorePartyInput(y);
	createRetirementInput(y);
	createDisendorsementInput(y);
	createPreviousDisendorsementInput(y);
	createIncumbentRecontestConfirmedInput(y);
	createConfirmedProminentIndependentInput(y);
	createPreviousIndRunningInput(y);
	createProminentMinorsInput(y);
	createBettingOddsInput(y);
	createPollsInput(y);
	createRunningPartiesInput(y);
	createTcpChangeInput(y);
	createMinorViabilityInput(y);
	createKnownPrepollsInput(y);
	createKnownPostalsInput(y);
	createKnownAbsentCountInput(y);
	createKnownProvisionalCountInput(y);
	createKnownDecPrepollCountInput(y);
	createKnownPostalCountInput(y);

	createOkCancelButtons(y);
}

void EditSeatFrame::createNameInput(int& y)
{
	auto callback = [this](std::string s) -> void {seat.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", seat.name, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSeatFrame::createPreviousNameInput(int & y)
{
	auto callback = [this](std::string s) -> void {seat.previousName = s; };
	previousNameInput.reset(new TextInput(this, ControlId::PreviousName, "Previous Name:", seat.previousName, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSeatFrame::createUseFpResultsInput(int& y)
{
	auto callback = [this](std::string s) -> void {seat.useFpResults = s; };
	useFpResultsInput.reset(new TextInput(this, ControlId::UseFpResults, "Use Fp Results From:", seat.useFpResults, wxPoint(2, y), callback));
	y += nameInput->Height + ControlPadding;
}

void EditSeatFrame::createIncumbentInput(int & y)
{
	int selectedIncumbent = parties.idToIndex(seat.incumbent);

	auto callback = [this](int i) {seat.incumbent = parties.indexToId(i); };
	incumbentInput.reset(new ChoiceInput(this, ControlId::Incumbent, "Incumbent: ", collectPartyStrings(),
		selectedIncumbent, wxPoint(2, y), callback));
	y += incumbentInput->Height + ControlPadding;
}

void EditSeatFrame::createChallengerInput(int & y)
{
	int selectedChallenger = parties.idToIndex(seat.challenger);

	auto callback = [this](int i) {seat.challenger = parties.indexToId(i); };
	challengerInput.reset(new ChoiceInput(this, ControlId::Challenger, "Challenger: ", collectPartyStrings(),
		selectedChallenger, wxPoint(2, y), callback));
	y += challengerInput->Height + ControlPadding;
}

void EditSeatFrame::createChallenger2Input(int & y)
{
	int selectedChallenger2 = parties.idToIndex(seat.challenger2);

	auto callback = [this](int i) {seat.challenger2 = parties.indexToId(i); };
	challenger2Input.reset(new ChoiceInput(this, ControlId::Challenger2, "Challenger 2: ", collectPartyStrings(),
		selectedChallenger2, wxPoint(2, y), callback));
	y += challenger2Input->Height + ControlPadding;
}

void EditSeatFrame::createRegionInput(int & y)
{
	wxArrayString regionArray;
	for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
		regionArray.push_back(it->second.name);
	}
	int selectedRegion = regions.idToIndex(seat.region);

	auto callback = [this](int i) {seat.region = regions.indexToId(i); };
	regionInput.reset(new ChoiceInput(this, ControlId::Region, "Region: ", regionArray,
		selectedRegion, wxPoint(2, y), callback));
	y += regionInput->Height + ControlPadding;
}

void EditSeatFrame::createMarginInput(int & y)
{
	auto callback = [this](float f) -> void {seat.tppMargin = f; };
	auto validator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	marginInput.reset(new FloatInput(this, ControlId::Margin, "Party One TPP Margin:", seat.tppMargin,
		wxPoint(2, y), callback, validator));
	y += marginInput->Height + ControlPadding;
}

void EditSeatFrame::createPreviousSwingInput(int& y)
{
	auto callback = [this](float f) -> void {seat.previousSwing = f; };
	auto validator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	previousSwingInput.reset(new FloatInput(this, ControlId::PreviousSwing, "Previous TPP swing:", seat.previousSwing,
		wxPoint(2, y), callback, validator));
	y += previousSwingInput->Height + ControlPadding;
}

void EditSeatFrame::createLocalModifierInput(int& y)
{
	auto callback = [this](float f) -> void {seat.localModifier = f; };
	auto validator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	localModifierInput.reset(new FloatInput(this, ControlId::LocalModifier, "Local Modifier:", seat.localModifier,
		wxPoint(2, y), callback, validator));
	y += localModifierInput->Height + ControlPadding;
}

void EditSeatFrame::createTransposedTppSwingInput(int& y)
{
	auto callback = [this](float f) -> void {seat.transposedTppSwing = f; };
	auto validator = [](float f) {return std::clamp(f, -50.0f, 50.0f); };
	transposedTppSwingInput.reset(new FloatInput(this, ControlId::TransposedTppSwing, "Transposed TPP Swing:", seat.transposedTppSwing,
		wxPoint(2, y), callback, validator));
	y += transposedTppSwingInput->Height + ControlPadding;
}

void EditSeatFrame::createIncumbentOddsInput(int & y)
{
	auto callback = [this](float f) -> void {seat.incumbentOdds = f; };
	auto validator = [](float f) {return std::max(f, 1.0f); };
	incumbentOddsInput.reset(new FloatInput(this, ControlId::IncumbentOdds, "Incumbent Odds:", seat.incumbentOdds,
		wxPoint(2, y), callback, validator));
	y += incumbentOddsInput->Height + ControlPadding;
}

void EditSeatFrame::createChallengerOddsInput(int & y)
{
	auto callback = [this](float f) -> void {seat.challengerOdds = f; };
	auto validator = [](float f) {return std::max(f, 1.0f); };
	challengerOddsInput.reset(new FloatInput(this, ControlId::ChallengerOdds, "Challenger Odds:", seat.challengerOdds,
		wxPoint(2, y), callback, validator));
	y += challengerOddsInput->Height + ControlPadding;
}

void EditSeatFrame::createChallenger2OddsInput(int & y)
{
	auto callback = [this](float f) -> void {seat.challenger2Odds = f; };
	auto validator = [](float f) {return std::max(f, 1.0f); };
	challenger2OddsInput.reset(new FloatInput(this, ControlId::Challenger2Odds, "Challenger 2 Odds:", seat.challenger2Odds,
		wxPoint(2, y), callback, validator));
	y += challenger2OddsInput->Height + ControlPadding;
}

void EditSeatFrame::createSophomoreCandidateInput(int& y)
{
	auto callback = [this](int i) -> void {seat.sophomoreCandidate = (i != 0); };
	sophomoreCandidateInput.reset(new CheckInput(this, ControlId::SophomoreCandidate, "Sophomore (candidate)", seat.sophomoreCandidate,
		wxPoint(2, y), callback));
	y += challenger2OddsInput->Height + ControlPadding;
}

void EditSeatFrame::createSophomorePartyInput(int& y)
{
	auto callback = [this](int i) -> void {seat.sophomoreParty = (i != 0); };
	sophomorePartyInput.reset(new CheckInput(this, ControlId::SophomoreParty, "Sophomore (party)", seat.sophomoreParty,
		wxPoint(2, y), callback));
	y += sophomorePartyInput->Height + ControlPadding;
}

void EditSeatFrame::createRetirementInput(int& y)
{
	auto callback = [this](int i) -> void {seat.retirement = (i != 0); };
	retirementInput.reset(new CheckInput(this, ControlId::Retirement, "Retirement", seat.retirement,
		wxPoint(2, y), callback));
	y += retirementInput->Height + ControlPadding;
}

void EditSeatFrame::createDisendorsementInput(int& y)
{
	auto callback = [this](int i) -> void {seat.disendorsement = (i != 0); };
	disendorsementInput.reset(new CheckInput(this, ControlId::Disendorsement, "Disendorsement", seat.disendorsement,
		wxPoint(2, y), callback));
	y += disendorsementInput->Height + ControlPadding;
}

void EditSeatFrame::createPreviousDisendorsementInput(int& y)
{
	auto callback = [this](int i) -> void {seat.previousDisendorsement = (i != 0); };
	previousDisendorsementInput.reset(new CheckInput(this, ControlId::PreviousDisendorsement, "Previous Disendorsement", seat.previousDisendorsement,
		wxPoint(2, y), callback));
	y += previousDisendorsementInput->Height + ControlPadding;
}

void EditSeatFrame::createIncumbentRecontestConfirmedInput(int& y)
{
	auto callback = [this](int i) -> void {seat.incumbentRecontestConfirmed = (i != 0); };
	incumbentRecontestConfirmedInput.reset(new CheckInput(this, ControlId::IncumbentRecontestConfirmed, "Incumbent Recontest Confirmed", seat.incumbentRecontestConfirmed,
		wxPoint(2, y), callback));
	y += incumbentRecontestConfirmedInput->Height + ControlPadding;
}

void EditSeatFrame::createConfirmedProminentIndependentInput(int& y)
{
	auto callback = [this](int i) -> void {seat.confirmedProminentIndependent = (i != 0); };
	confirmedProminentIndependentInput.reset(new CheckInput(this, ControlId::ConfirmedProminentIndependent, "Confirmed Prominent Independent", seat.confirmedProminentIndependent,
		wxPoint(2, y), callback));
	y += confirmedProminentIndependentInput->Height + ControlPadding;
}

void EditSeatFrame::createPreviousIndRunningInput(int& y)
{
	auto callback = [this](int i) -> void {seat.previousIndRunning = (i != 0); };
	previousIndRunningInput.reset(new CheckInput(this, ControlId::PreviousIndRunning, "Previous Independent Running", seat.previousIndRunning,
		wxPoint(2, y), callback));
	y += previousIndRunningInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownPrepollsInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownPrepollPercent = f; };
	auto validator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	knownPrepollsInput.reset(new FloatInput(this, ControlId::KnownPrepolls, "Known prepoll %:", seat.knownPrepollPercent,
		wxPoint(2, y), callback, validator));
	y += knownPrepollsInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownPostalsInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownPostalPercent = f; };
	auto validator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	knownPostalsInput.reset(new FloatInput(this, ControlId::KnownPostals, "Known postals %:", seat.knownPostalPercent,
		wxPoint(2, y), callback, validator));
	y += knownPostalsInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownAbsentCountInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownAbsentCount = f; };
	auto validator = [](float f) {return std::max(f, 0.0f); };
	knownAbsentCountInput.reset(new FloatInput(this, ControlId::KnownAbsentCount, "Known absent count:", seat.knownAbsentCount,
		wxPoint(2, y), callback, validator));
	y += knownAbsentCountInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownProvisionalCountInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownProvisionalCount = f; };
	auto validator = [](float f) {return std::max(f, 0.0f); };
	knownProvisionalCountInput.reset(new FloatInput(this, ControlId::KnownProvisionalCount, "Known provisional count:", seat.knownProvisionalCount,
		wxPoint(2, y), callback, validator));
	y += knownProvisionalCountInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownDecPrepollCountInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownDecPrepollCount = f; };
	auto validator = [](float f) {return std::max(f, 0.0f); };
	knownDecPrepollCountInput.reset(new FloatInput(this, ControlId::KnownDecPrepollCount, "Known dec-prepoll count:", seat.knownDecPrepollCount,
		wxPoint(2, y), callback, validator));
	y += knownDecPrepollCountInput->Height + ControlPadding;
}

void EditSeatFrame::createKnownPostalCountInput(int& y)
{
	auto callback = [this](float f) -> void {seat.knownPostalCount = f; };
	auto validator = [](float f) {return std::max(f, 0.0f); };
	knownPostalCountInput.reset(new FloatInput(this, ControlId::KnownPostalCount, "Known postal count:", seat.knownPostalCount,
		wxPoint(2, y), callback, validator));
	y += knownPostalCountInput->Height + ControlPadding;
}

void EditSeatFrame::createProminentMinorsInput(int& y)
{
	std::string prominentMinors = "";
	if (seat.prominentMinors.size()) {
		prominentMinors += seat.prominentMinors[0];
		for (size_t i = 1; i < seat.prominentMinors.size(); ++i) {
			prominentMinors += "," + seat.prominentMinors[i];
		}
	}

	auto prominentMinorsCallback = std::bind(&EditSeatFrame::updateProminentMinors, this, _1);
	prominentMinorsInput.reset(new TextInput(this, ControlId::ProminentMinors, "Prominent Minors:", prominentMinors, wxPoint(2, y), prominentMinorsCallback));
	y += prominentMinorsInput->Height + ControlPadding;
}

void EditSeatFrame::createBettingOddsInput(int& y)
{
	std::string bettingOdds = "";
	bool firstDone = false;
	for (auto [shortCode, odds] : seat.bettingOdds) {
		if (firstDone) bettingOdds += ";";
		bettingOdds += shortCode + "," + formatFloat(odds, 2);
		firstDone = true;
	}

	auto bettingOddsCallback = std::bind(&EditSeatFrame::updateBettingOdds, this, _1);
	bettingOddsInput.reset(new TextInput(this, ControlId::BettingOdds, "Betting Odds:", bettingOdds, wxPoint(2, y), bettingOddsCallback));
	y += bettingOddsInput->Height + ControlPadding;
}

void EditSeatFrame::createPollsInput(int& y)
{
	std::string pollStr = "";
	bool firstDone = false;
	for (auto [shortCode, partyPolls] : seat.polls) {
		for (auto poll : partyPolls) {
			if (firstDone) pollStr += ";";
			pollStr += shortCode + "," + formatFloat(poll.first, 2) + "," + std::to_string(poll.second);
			firstDone = true;
		}
	}

	auto pollsCallback = std::bind(&EditSeatFrame::updatePolls, this, _1);
	pollsInput.reset(new TextInput(this, ControlId::Polls, "Seat Polls:", pollStr, wxPoint(2, y), pollsCallback));
	y += pollsInput->Height + ControlPadding;
}

void EditSeatFrame::createRunningPartiesInput(int& y)
{
	std::string runningParties = "";
	if (seat.runningParties.size()) {
		runningParties += seat.runningParties[0];
		for (size_t i = 1; i < seat.runningParties.size(); ++i) {
			runningParties += "," + seat.runningParties[i];
		}
	}

	auto runningPartiesCallback = std::bind(&EditSeatFrame::updateRunningParties, this, _1);
	runningPartiesInput.reset(new TextInput(this, ControlId::RunningParties, "Running Parties:", runningParties, wxPoint(2, y), runningPartiesCallback));
	y += runningPartiesInput->Height + ControlPadding;
}

void EditSeatFrame::createTcpChangeInput(int& y)
{
	std::string tcpChange = "";
	bool firstDone = false;
	for (auto [shortCode, change] : seat.tcpChange) {
		if (firstDone) tcpChange += ";";
		tcpChange += shortCode + "," + formatFloat(change, 2);
		firstDone = true;
	}

	auto tcpChangeCallback = std::bind(&EditSeatFrame::updateTcpChange, this, _1);
	tcpChangeInput.reset(new TextInput(this, ControlId::TcpChange, "Tcp change:", tcpChange, wxPoint(2, y), tcpChangeCallback));
	y += tcpChangeInput->Height + ControlPadding;
}

void EditSeatFrame::createMinorViabilityInput(int& y)
{
	std::string minorViability = "";
	bool firstDone = false;
	for (auto [shortCode, change] : seat.minorViability) {
		if (firstDone) minorViability += ";";
		minorViability += shortCode + "," + formatFloat(change, 2);
		firstDone = true;
	}

	auto callback = std::bind(&EditSeatFrame::updateMinorViability, this, _1);
	minorViabilityInput.reset(new TextInput(this, ControlId::MinorViability, "Minor candidate viability:", minorViability, wxPoint(2, y), callback));
	y += minorViabilityInput->Height + ControlPadding;
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

	okCallback(seat);

	// Then close this dialog.
	Close();
}

void EditSeatFrame::updateProminentMinors(std::string prominentMinors)
{
	seat.prominentMinors = splitString(prominentMinors, ",");
}

// Helper function for a recurring process
void partyFloatUpdate(std::string input, std::map<std::string, float>& storedValue) {
	storedValue.clear();
	auto vec = splitString(input, ";");
	for (auto odds : vec) {
		auto item = splitString(odds, ",");
		if (item.size() < 2) continue;
		std::string party = item[0];
		try {
			float val = std::stof(item[1]);
			storedValue[party] = val;
		}
		catch (std::invalid_argument) {
			continue;
		}
	}
}

void EditSeatFrame::updateBettingOdds(std::string bettingOdds)
{
	partyFloatUpdate(bettingOdds, seat.bettingOdds);
}

void EditSeatFrame::updatePolls(std::string pollStr)
{
	seat.polls.clear();
	auto pollsVec = splitString(pollStr, ";");
	for (auto poll : pollsVec) {
		auto singlePoll = splitString(poll, ",");
		if (singlePoll.size() < 3) continue;
		std::string party = singlePoll[0];
		try {
			float fpVal = std::stof(singlePoll[1]);
			int credibility = std::stoi(singlePoll[2]);
			seat.polls[party].push_back({ fpVal, credibility });
		}
		catch (std::invalid_argument) {
			continue;
		}
	}
}

void EditSeatFrame::updateRunningParties(std::string runningParties)
{
	seat.runningParties = splitString(runningParties, ",");
}

void EditSeatFrame::updateTcpChange(std::string tcpChange)
{
	partyFloatUpdate(tcpChange, seat.tcpChange);
}

void EditSeatFrame::updateMinorViability(std::string minorViability)
{
	partyFloatUpdate(minorViability, seat.minorViability);
}
