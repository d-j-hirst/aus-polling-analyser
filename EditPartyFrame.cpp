#include "EditPartyFrame.h"

#include "ChoiceInput.h"
#include "ColourInput.h"
#include "FloatInput.h"
#include "General.h"
#include "TextInput.h"

#include <regex>

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 250, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	OkButton,
	Name,
	PreferenceFlow,
	ExhaustRate,
	Abbreviation,
	OfficialShortCodes,
	Colour,
	Ideology,
	Consistency,
	BoothColourMult,
	CountAsParty,
	SupportsParty,
};

EditPartyFrame::EditPartyFrame(Function function, OkCallback callback, Party party)
	: wxDialog(NULL, 0, (function == Function::New ? "New Party" : "Edit Party"), wxDefaultPosition, wxSize(400, 400)),
	party(party), callback(callback)
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
	createAbbreviationInput(y);
	createShortCodesInput(y);
	createColourInput(y);
	createIdeologyInput(y);
	createConsistencyInput(y);

	if (party.countAsParty != Party::CountAsParty::IsPartyOne && party.countAsParty != Party::CountAsParty::IsPartyTwo) {
		createBoothColourMultInput(y);
		createCountAsPartyInput(y);
		createSupportsPartyInput(y);
	}
	createOkCancelButtons(y);
}

void EditPartyFrame::createNameInput(int& y)
{
	auto nameCallback = std::bind(&EditPartyFrame::updateName, this, _1);
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", party.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditPartyFrame::createPreferenceFlowInput(int& y)
{
	auto preferenceFlowCallback = std::bind(&EditPartyFrame::updatePreferenceFlow, this, _1);
	auto preferenceFlowValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	preferenceFlowInput.reset(new FloatInput(this, ControlId::PreferenceFlow, "Preferences to party 1:", party.preferenceShare,
		wxPoint(2, y), preferenceFlowCallback, preferenceFlowValidator));
	y += preferenceFlowInput->Height + ControlPadding;
}

void EditPartyFrame::createExhaustRateInput(int& y)
{
	auto exhaustRateCallback = std::bind(&EditPartyFrame::updateExhaustRate, this, _1);
	auto exhaustRateValidator = [](float f) {return std::clamp(f, 0.0f, 100.0f); };
	exhaustRateInput.reset(new FloatInput(this, ControlId::ExhaustRate, "Exhaust Rate:", party.exhaustRate,
		wxPoint(2, y), exhaustRateCallback, exhaustRateValidator));
	y += exhaustRateInput->Height + ControlPadding;
}

void EditPartyFrame::createAbbreviationInput(int& y)
{
	auto abbreviationCallback = std::bind(&EditPartyFrame::updateAbbreviation, this, _1);
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

void EditPartyFrame::createColourInput(int& y)
{
	wxColour currentColour(party.colour.r, party.colour.g, party.colour.b);
	auto colourCallback = std::bind(&EditPartyFrame::updateColour, this, _1);
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

	auto ideologyCallback = std::bind(&EditPartyFrame::updateIdeology, this, _1);
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

	auto consistencyCallback = std::bind(&EditPartyFrame::updateConsistency, this, _1);
	consistencyInput.reset(new ChoiceInput(this, ControlId::Consistency, "Consistency:", consistencyArray, currentConsistencySelection,
		wxPoint(2, y), consistencyCallback));
	y += consistencyInput->Height + ControlPadding;
}

void EditPartyFrame::createBoothColourMultInput(int& y)
{
	auto boothColourMultCallback = std::bind(&EditPartyFrame::updateBoothColourMult, this, _1);
	auto boothColourMultValidator = [](float f) {return std::max(f, 0.0f); };
	boothColourMultInput.reset(new FloatInput(this, ControlId::BoothColourMult, "Booth Colour Multiplier:", party.boothColourMult,
		wxPoint(2, y), boothColourMultCallback, boothColourMultValidator));
	y += boothColourMultInput->Height + ControlPadding;
}

void EditPartyFrame::createCountAsPartyInput(int& y)
{
	wxArrayString countAsPartyArray;
	countAsPartyArray.push_back("None");
	countAsPartyArray.push_back("Counts As Party One");
	countAsPartyArray.push_back("Counts As Party Two");
	int countAsPartySelection = 0;
	switch (party.countAsParty) {
	case Party::CountAsParty::CountsAsPartyOne: countAsPartySelection = 1; break;
	case Party::CountAsParty::CountsAsPartyTwo: countAsPartySelection = 2; break;
	}

	auto countAsPartyCallback = std::bind(&EditPartyFrame::updateCountAsParty, this, _1);
	countAsPartyInput.reset(new ChoiceInput(this, ControlId::CountAsParty, "Counts as party:", countAsPartyArray, countAsPartySelection,
		wxPoint(2, y), countAsPartyCallback));
	y += countAsPartyInput->Height + ControlPadding;
}

void EditPartyFrame::createSupportsPartyInput(int& y)
{
	wxArrayString supportsPartyArray;
	supportsPartyArray.push_back("None");
	supportsPartyArray.push_back("Supports Party One");
	supportsPartyArray.push_back("Supports Party Two");
	int supportsPartySelection = 0;
	switch (party.supportsParty) {
	case Party::SupportsParty::One: supportsPartySelection = 1; break;
	case Party::SupportsParty::Two: supportsPartySelection = 2; break;
	}

	auto supportsPartyCallback = std::bind(&EditPartyFrame::updateSupportsParty, this, _1);
	supportsPartyInput.reset(new ChoiceInput(this, ControlId::SupportsParty, "Supports party:", supportsPartyArray, supportsPartySelection,
		wxPoint(2, y), supportsPartyCallback));
	y += supportsPartyInput->Height + ControlPadding;
}

void EditPartyFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::OkButton, "OK", wxPoint(67, y), wxSize(100, TextInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, TextInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditPartyFrame::OnOK, this, OkButton);
	y += TextInput::Height + ControlPadding;
}

void EditPartyFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditPartyFrame::OnOK(wxCommandEvent& WXUNUSED(event))
{
	// Call the function that was passed when this frame was opened.
	callback(party);

	// Then close this dialog.
	Close();
}

void EditPartyFrame::updateName(std::string name) 
{
	party.name = name;
}

void EditPartyFrame::updatePreferenceFlow(float preferenceFlow) 
{
	party.preferenceShare = preferenceFlow;
}

void EditPartyFrame::updateExhaustRate(float exhaustRate) 
{
	party.exhaustRate = exhaustRate;
}

void EditPartyFrame::updateAbbreviation(std::string abbreviation) 
{
	party.abbreviation = abbreviation;
}

void EditPartyFrame::updateShortCodes(std::string shortCodes) 
{
	// updates the party short codes data with the string from the event.
	std::regex partyCodeRegex("([^,]+)(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?");
	std::smatch matchResults;
	std::regex_match(shortCodes, matchResults, partyCodeRegex);
	if (matchResults.size()) {
		party.officialCodes.clear();
		for (int matchIndex = 1; matchIndex < 56; matchIndex += 2) {
			if (!matchResults[matchIndex].matched) break;
			party.officialCodes.push_back(matchResults[matchIndex].str());
		}
	}
}

void EditPartyFrame::updateColour(wxColour colour)
{
	party.colour.r = colour.Red();
	party.colour.g = colour.Green();
	party.colour.b = colour.Blue();
}

void EditPartyFrame::updateIdeology(int ideology)
{
	party.ideology = ideology;
}

void EditPartyFrame::updateConsistency(int consistency)
{
	party.consistency = consistency;
}

void EditPartyFrame::updateBoothColourMult(float boothColourMult)
{
	party.boothColourMult = boothColourMult;
}

void EditPartyFrame::updateCountAsParty(int countAsParty)
{
	switch (countAsParty) {
	case 0: party.countAsParty = Party::CountAsParty::None; break;
	case 1: party.countAsParty = Party::CountAsParty::CountsAsPartyOne; break;
	case 2: party.countAsParty = Party::CountAsParty::CountsAsPartyTwo; break;
	}
}

void EditPartyFrame::updateSupportsParty(int supportsParty)
{
	switch (supportsParty) {
	case 0: party.supportsParty = Party::SupportsParty::None; break;
	case 1: party.supportsParty = Party::SupportsParty::One; break;
	case 2: party.supportsParty = Party::SupportsParty::Two; break;
	}
}