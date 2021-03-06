#include "EditPartyFrame.h"

#include "CheckInput.h"
#include "ChoiceInput.h"
#include "ColourInput.h"
#include "FloatInput.h"
#include "General.h"
#include "Log.h"
#include "PartyCollection.h"
#include "TextInput.h"

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 250, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	Ok,
	Name,
	PreferenceFlow,
	ExhaustRate,
	Abbreviation,
	OfficialShortCodes,
	HomeRegion,
	SeatTarget,
	Colour,
	Ideology,
	Consistency,
	BoothColourMult,
	Relation,
	RelationType,
	IncludeInOthers,
	NcPreferenceFlow
};

EditPartyFrame::EditPartyFrame(Function function, OkCallback callback, PartyCollection const& parties, Party party)
	: wxDialog(NULL, 0, (function == Function::New ? "New Party" : "Edit Party"), wxDefaultPosition, wxSize(500, 400)),
	parties(parties), party(party), callback(callback)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditPartyFrame::createControls(int& y)
{
	createNameInput(y);
	createPreferenceFlowInput(y);
	createExhaustRateInput(y);
	createNcPreferenceFlowInput(y);
	createAbbreviationInput(y);
	createShortCodesInput(y);
	createHomeRegionInput(y);
	createSeatTargetInput(y);
	createColourInput(y);
	createIdeologyInput(y);
	createConsistencyInput(y);

	if (party.relationType != Party::RelationType::IsMajor) {
		createBoothColourMultInput(y);
		createRelationInput(y);
		createRelationTypeInput(y);
		createIncludeInOthersInput(y);
	}
	createOkCancelButtons(y);
}

void EditPartyFrame::createNameInput(int& y)
{
	auto nameCallback = [this](std::string s) -> void {party.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", party.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditPartyFrame::createPreferenceFlowInput(int& y)
{
	auto preferenceFlowCallback = [this](float f) -> void {party.p1PreferenceFlow = f; };
	auto preferenceFlowValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	preferenceFlowInput.reset(new FloatInput(this, ControlId::PreferenceFlow, "Preferences to party 1:", party.p1PreferenceFlow,
		wxPoint(2, y), preferenceFlowCallback, preferenceFlowValidator));
	y += preferenceFlowInput->Height + ControlPadding;
}

void EditPartyFrame::createExhaustRateInput(int& y)
{
	auto exhaustRateCallback = [this](float f) -> void {party.exhaustRate = f; };
	auto exhaustRateValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	exhaustRateInput.reset(new FloatInput(this, ControlId::ExhaustRate, "Exhaust Rate:", party.exhaustRate,
		wxPoint(2, y), exhaustRateCallback, exhaustRateValidator));
	y += exhaustRateInput->Height + ControlPadding;
}

void EditPartyFrame::createNcPreferenceFlowInput(int& y)
{
	std::string preferenceString = "";
	if (party.ncPreferenceFlow.size()) {
		auto stringifyPreferenceFlow = [](Party::NcPreferenceFlow const& a) {
			return a.first.first + "," + a.first.second + "," + formatFloat(a.second, 3);
		};
		preferenceString += stringifyPreferenceFlow(party.ncPreferenceFlow[0]);
		for (size_t i = 1; i < party.ncPreferenceFlow.size(); ++i) {
			preferenceString += ";" + stringifyPreferenceFlow(party.ncPreferenceFlow[i]);
		}
	}

	auto ncPreferenceFlowCallback = std::bind(&EditPartyFrame::updateNcPreferenceFlow, this, _1);
	ncPreferenceFlowInput.reset(new TextInput(this, ControlId::NcPreferenceFlow, "Non-Classic Pref. Flow:", preferenceString, wxPoint(2, y), ncPreferenceFlowCallback));
	y += ncPreferenceFlowInput->Height + ControlPadding;
}

void EditPartyFrame::createAbbreviationInput(int& y)
{
	auto abbreviationCallback = [this](std::string s) -> void {party.abbreviation = s; };
	abbreviationInput.reset(new TextInput(this, ControlId::Abbreviation, "Abbreviation:", party.abbreviation, wxPoint(2, y), abbreviationCallback));
	y += abbreviationInput->Height + ControlPadding;
}

void EditPartyFrame::createShortCodesInput(int& y)
{
	std::string shortCodes = "";
	if (party.officialCodes.size()) {
		shortCodes += party.officialCodes[0];
		for (size_t i = 1; i < party.officialCodes.size(); ++i) {
			shortCodes += "," + party.officialCodes[i];
		}
	}

	auto shortCodesCallback = std::bind(&EditPartyFrame::updateShortCodes, this, _1);
	shortCodesInput.reset(new TextInput(this, ControlId::OfficialShortCodes, "Official Short Codes:", shortCodes, wxPoint(2, y), shortCodesCallback));
	y += shortCodesInput->Height + ControlPadding;
}

void EditPartyFrame::createHomeRegionInput(int& y)
{
	auto homeRegionCallback = [this](std::string s) -> void {party.homeRegion = s; };
	homeRegionInput.reset(new TextInput(this, ControlId::HomeRegion, "Home region:", party.homeRegion, wxPoint(2, y), homeRegionCallback));
	y += homeRegionInput->Height + ControlPadding;
}

void EditPartyFrame::createSeatTargetInput(int& y)
{
	auto seatTargetCallback = [this](float f) -> void {party.seatTarget = f; };
	auto seatTargetValidator = [](float f) {return std::clamp(f, 0.0f, 10000.0f); };
	seatTargetInput.reset(new FloatInput(this, ControlId::SeatTarget, "Seat Target:", party.seatTarget,
		wxPoint(2, y), seatTargetCallback, seatTargetValidator));
	y += seatTargetInput->Height + ControlPadding;
}

void EditPartyFrame::createColourInput(int& y)
{
	wxColour currentColour(party.colour.r, party.colour.g, party.colour.b);
	auto colourCallback = [this](wxColour c) -> void {party.colour = { c.Red(), c.Green(), c.Blue() }; };
	colourInput.reset(new ColourInput(this, ControlId::Colour, "Colour:", currentColour, wxPoint(2, y), colourCallback));
	y += colourInput->Height + ControlPadding;
}

void EditPartyFrame::createIdeologyInput(int& y)
{
	// Ideology combo box
	wxArrayString ideologyArray;
	ideologyArray.push_back("Strong Left");
	ideologyArray.push_back("Moderate Left");
	ideologyArray.push_back("Centrist");
	ideologyArray.push_back("Moderate Right");
	ideologyArray.push_back("Strong Right");
	int currentIdeologySelection = party.ideology;

	auto ideologyCallback = [this](int i) -> void {party.ideology = i; };
	ideologyInput.reset(new ChoiceInput(this, ControlId::Ideology, "Ideology:", ideologyArray, currentIdeologySelection,
		wxPoint(2, y), ideologyCallback));
	y += ideologyInput->Height + ControlPadding;
}

void EditPartyFrame::createConsistencyInput(int& y)
{
	// Consistency combo box
	wxArrayString consistencyArray;
	consistencyArray.push_back("Low");
	consistencyArray.push_back("Moderate");
	consistencyArray.push_back("High");
	int currentConsistencySelection = party.consistency;

	auto consistencyCallback = [this](int i) -> void {party.consistency = i; };
	consistencyInput.reset(new ChoiceInput(this, ControlId::Consistency, "Consistency:", consistencyArray, currentConsistencySelection,
		wxPoint(2, y), consistencyCallback));
	y += consistencyInput->Height + ControlPadding;
}

void EditPartyFrame::createBoothColourMultInput(int& y)
{
	auto boothColourMultCallback = [this](float f) -> void {party.boothColourMult = f; };
	auto boothColourMultValidator = [](float f) {return std::max(f, 0.0f); };
	boothColourMultInput.reset(new FloatInput(this, ControlId::BoothColourMult, "Booth Colour Multiplier:", party.boothColourMult,
		wxPoint(2, y), boothColourMultCallback, boothColourMultValidator));
	y += boothColourMultInput->Height + ControlPadding;
}

void EditPartyFrame::createRelationInput(int& y)
{
	wxArrayString relationArray;
	for (auto const& [key, otherParty] : parties) {
		relationArray.Add(otherParty.name);
	}

	auto relationCallback = std::bind(&EditPartyFrame::updateRelation, this, _1);
	relationInput.reset(new ChoiceInput(this, ControlId::Relation, "Related party:", relationArray, parties.idToIndex(party.relationTarget),
		wxPoint(2, y), relationCallback));
	y += relationInput->Height + ControlPadding;
}

void EditPartyFrame::createRelationTypeInput(int& y)
{
	wxArrayString relationTypeArray;
	relationTypeArray.push_back("None");
	relationTypeArray.push_back("Supports related party");
	relationTypeArray.push_back("In coalition with related party");
	relationTypeArray.push_back("Is part of related party");
	int relationTypeSelection = static_cast<int>(party.relationType);

	auto relationTypeCallback = std::bind(&EditPartyFrame::updateRelationType, this, _1);
	relationTypeInput.reset(new ChoiceInput(this, ControlId::RelationType, "Relation type:", relationTypeArray, relationTypeSelection,
		wxPoint(2, y), relationTypeCallback));
	y += relationTypeInput->Height + ControlPadding;
}

void EditPartyFrame::createIncludeInOthersInput(int& y)
{
	auto includeInOthersCallback = [this](int i) -> void {party.includeInOthers = (i != 0); };
	includeInOthersInput.reset(new CheckInput(this, ControlId::IncludeInOthers, "Include in \"others:\"", party.includeInOthers,
		wxPoint(2, y), includeInOthersCallback));
	y += includeInOthersInput->Height + ControlPadding;
}

void EditPartyFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditPartyFrame::OnOK, this, ControlId::Ok);
	y += TextInput::Height + ControlPadding;
}

void EditPartyFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditPartyFrame::OnOK(wxCommandEvent&)
{
	// Call the function that was passed when this frame was opened.
	callback(party);

	// Then close this dialog.
	Close();
}

void EditPartyFrame::updateNcPreferenceFlow(std::string allPreferenceFlows)
{
	try {
		party.ncPreferenceFlow.clear();
		auto preferenceFlows = splitString(allPreferenceFlows, ";");
		for (auto preferenceFlow : preferenceFlows) {
			auto parts = splitString(preferenceFlow, ",");
			if (parts.size() != 3) continue;
			Party::NcPreferenceFlow parsedFlow = { {parts[0], parts[1]}, std::stof(parts[2]) };
			party.ncPreferenceFlow.push_back(parsedFlow);
		}
	}
	catch (std::invalid_argument) {
		// Just don't update the variable if the input isn't properly given
	}
}

void EditPartyFrame::updateShortCodes(std::string shortCodes)
{
	party.officialCodes = splitString(shortCodes, ",");
}

void EditPartyFrame::updateRelation(int relation)
{
	party.relationTarget = relation;
}

void EditPartyFrame::updateRelationType(int relationType)
{
	party.relationType = Party::RelationType(relationType);
}