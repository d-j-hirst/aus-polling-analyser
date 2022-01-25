#include "NonClassicFrame.h"

#include "ChoiceInput.h"
#include "FloatInput.h"
#include "General.h"
#include "PartyCollection.h"
#include "Seat.h"

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Ok,
	Remove,
	PartyOne,
	PartyTwo,
	PartyThree,
	PartyOneProb,
	PartyTwoProb,
	PartyThreeProb
};

NonClassicFrame::NonClassicFrame(PartyCollection const& parties, Seat& seat)
	: wxDialog(NULL, 0, "Non-classic seat", wxDefaultPosition, wxSize(500, 400)),
	parties(parties), seat(seat)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void NonClassicFrame::createControls(int & y)
{
	createPartyOneInput(y);
	createPartyTwoInput(y);
	createPartyThreeInput(y);
	createPartyOneProbText(y);
	createPartyTwoProbInput(y);
	createPartyThreeProbInput(y);

	createButtons(y);
}

void NonClassicFrame::createPartyOneInput(int & y)
{
	int selectedParty = parties.idToIndex(seat.livePartyOne);

	// No callback on this, as we only use the input later
	partyOneInput.reset(new ChoiceInput(this, ControlId::PartyOne, "Party One:",
		collectPartyStrings(), selectedParty, wxPoint(2, y)));
	y += partyOneInput->Height + ControlPadding;
}

void NonClassicFrame::createPartyTwoInput(int & y)
{
	int selectedParty = parties.idToIndex(seat.livePartyTwo);

	// No callback on this, as we only use the input later
	partyTwoInput.reset(new ChoiceInput(this, ControlId::PartyTwo, "Party Two:",
		collectPartyStrings(), selectedParty, wxPoint(2, y)));
	y += partyTwoInput->Height + ControlPadding;
}

void NonClassicFrame::createPartyThreeInput(int & y)
{
	int selectedParty = parties.idToIndex(seat.livePartyThree);

	// No callback on this, as we only use the input later
	partyThreeInput.reset(new ChoiceInput(this, ControlId::PartyThree, "Party Three:",
		collectPartyStrings(), selectedParty, wxPoint(2, y)));
	y += partyThreeInput->Height + ControlPadding;
}

void NonClassicFrame::createPartyOneProbText(int & y)
{
	// *** need to replace this with a proper class for handling a double label.

	// Create the controls for the estimated 2pp. This can't be edited by the user.
	partyOneProbLabel = new wxStaticText(this, 0, "Party One Prob:", wxPoint(2, y + ControlPadding), wxSize(198, 23));
	partyOneProbValue = new wxStaticText(this, ControlId::PartyOneProb, formatFloat(seat.partyOneProb(), 3),
		wxPoint(200, y + ControlPadding), wxSize(120, 23));

	y += 27;
}

void NonClassicFrame::createPartyTwoProbInput(int & y)
{
	// No callback effects on this, as we only use the input later
	auto thisCallback = [this](float) -> void {updatePartyOneProbText(); };
	auto thisValidator = [](float f) {return std::clamp(f, 0.0f, 1.0f); };
	partyTwoProbInput.reset(new FloatInput(this, ControlId::PartyTwoProb, "Party Two Prob:", seat.partyTwoProb,
		wxPoint(2, y), thisCallback, thisValidator));
	y += partyTwoProbInput->Height + ControlPadding;
}

void NonClassicFrame::createPartyThreeProbInput(int & y)
{
	// No callback effects on this, as we only use the input later
	auto thisCallback = [this](float) -> void {updatePartyOneProbText(); };
	auto thisValidator = [](float f) {return std::clamp(f, 0.0f, 1.0f); };
	partyThreeProbInput.reset(new FloatInput(this, ControlId::PartyThreeProb, "Party Three Prob:", seat.partyThreeProb,
		wxPoint(2, y), thisCallback, thisValidator));
	y += partyTwoProbInput->Height + ControlPadding;
}

void NonClassicFrame::createButtons(int & y)
{
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(50, y), wxSize(60, 24));
	removeButton = new wxButton(this, ControlId::Remove, "Remove", wxPoint(115, y), wxSize(60, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(180, y), wxSize(60, 24));

	Bind(wxEVT_BUTTON, &NonClassicFrame::OnOK, this, ControlId::Ok);
	Bind(wxEVT_BUTTON, &NonClassicFrame::OnRemove, this, ControlId::Remove);

	y += 27;
}

void NonClassicFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void NonClassicFrame::updatePartyOneProbText()
{
	try {
		float partyTwoProb = partyTwoProbInput->getValue();
		float partyThreeProb = partyThreeProbInput->getValue();
		float partyOneProb = 1.0f - partyTwoProb - partyThreeProb;
		partyOneProbValue->SetLabel(formatFloat(partyOneProb, 3));
	}
	catch (std::invalid_argument) {
		// just do nothing, one of the text boxes doesn't have a valid number in it
	}
}

void NonClassicFrame::OnOK(wxCommandEvent& WXUNUSED(event))
{
	try {
		Party::Id partyOne = parties.indexToId(partyOneInput->getSelection());
		Party::Id partyTwo = parties.indexToId(partyTwoInput->getSelection());
		Party::Id partyThree = parties.indexToId(partyThreeInput->getSelection());
		float chanceTwo = partyTwoProbInput->getValue();
		float chanceThree = partyThreeProbInput->getValue();
		float chanceOne = 1.0f - chanceTwo - chanceThree;
		if (std::min({ chanceOne, chanceTwo, chanceThree }) < 0.0f) {
			wxMessageBox("Invalid party chance: must be from 0 to 1");
			return;
		}
		if ((chanceTwo && partyTwo == partyOne)
			|| (chanceThree && (partyThree == partyOne || partyThree == partyTwo))) {
			wxMessageBox("Invalid parties chosen: All parties with non-zero chance must be different");
			return;
		}
		seat.livePartyOne = partyOne;
		seat.livePartyTwo = partyTwo;
		seat.livePartyThree = partyThree;
		seat.partyTwoProb = chanceTwo;
		seat.partyThreeProb = chanceThree;
	}
	catch (std::invalid_argument) {
		wxMessageBox("One or more text boxes does not contain a valid numeric value");
		return;
	}

	// Then close this dialog.
	Close();
}

void NonClassicFrame::OnRemove(wxCommandEvent&)
{
	seat.livePartyOne = Party::InvalidId;
	seat.livePartyTwo = Party::InvalidId;
	seat.livePartyThree = Party::InvalidId;
	seat.partyTwoProb = 0.0f;
	seat.partyThreeProb = 0.0f;
	Close();
}

wxArrayString NonClassicFrame::collectPartyStrings()
{
	wxArrayString partyArray;
	for (auto it = parties.cbegin(); it != parties.cend(); ++it) {
		partyArray.push_back(it->second.name);
	}
	return partyArray;
}
