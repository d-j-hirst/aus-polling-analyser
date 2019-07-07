#include "NonClassicFrame.h"

#include "Seat.h"

// IDs for the controls and the menu commands
enum
{
	PA_NonClassic_ButtonID_OK,
	PA_NonClassic_ButtonID_Remove,
	PA_NonClassic_ComboBoxID_PartyOne,
	PA_NonClassic_ComboBoxID_PartyTwo,
	PA_NonClassic_ComboBoxID_PartyThree,
	PA_NonClassic_StaticTextID_PartyOneProb,
	PA_NonClassic_TextBoxID_PartyTwoProb,
	PA_NonClassic_TextBoxID_PartyThreeProb
};

NonClassicFrame::NonClassicFrame(ResultsFrame * const parent, PollingProject const * project, Seat* seat)
	: parent(parent), project(project), seat(seat),
	wxDialog(NULL, 0, "Non-Classic Seat Results", wxDefaultPosition, wxSize(340, 157))
{
	const int labelYOffset = 5;
	const int windowYOffset = 38;

	// *** NonClassicFrame Combo Box *** //

	// Create the choices for the combo boxes.
	// Also check if the poll's pollster matches any of the choices (otherwise it is set to the first).
	wxArrayString partyArray;
	int selectedPartyOne = 0;
	int selectedPartyTwo = 1;
	int selectedPartyThree = 0;
	int count = 0;
	for (auto it = project->parties().cbegin(); it != project->parties().cend(); ++it, ++count) {
		partyArray.push_back(it->name);
		if (&*it == seat->livePartyOne) selectedPartyOne = count;
		if (&*it == seat->livePartyTwo) selectedPartyTwo = count;
		if (&*it == seat->livePartyThree) selectedPartyThree = count;
	}

	int currentHeight = 2;

	// Create the controls for the party 1 combo box.
	partyOneStaticText = new wxStaticText(this, 0, "Party One:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyOneComboBox = new wxComboBox(this, PA_NonClassic_ComboBoxID_PartyOne, partyArray[selectedPartyOne],
		wxPoint(200, currentHeight), wxSize(120, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	partyOneComboBox->SetSelection(selectedPartyOne);

	currentHeight += 27;

	// Create the controls for the party 2 combo box.
	partyTwoStaticText = new wxStaticText(this, 0, "Party Two:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyTwoComboBox = new wxComboBox(this, PA_NonClassic_ComboBoxID_PartyTwo, partyArray[selectedPartyTwo],
		wxPoint(200, currentHeight), wxSize(120, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	partyTwoComboBox->SetSelection(selectedPartyTwo);

	currentHeight += 27;

	// Create the controls for the party 1 combo box.
	partyThreeStaticText = new wxStaticText(this, 0, "Party Three:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyThreeComboBox = new wxComboBox(this, PA_NonClassic_ComboBoxID_PartyThree, partyArray[selectedPartyThree],
		wxPoint(200, currentHeight), wxSize(120, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	partyThreeComboBox->SetSelection(selectedPartyThree);

	currentHeight += 27;

	// Create the controls for the party 1 combo box.
	partyOneProbStaticTextLabel = new wxStaticText(this, 0, "Party One Prob:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyOneProbStaticText = new wxStaticText(this, PA_NonClassic_StaticTextID_PartyOneProb, formatFloat(seat->partyOneProb(), 2),
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// Create the controls for the party 1 combo box.
	partyTwoProbStaticText = new wxStaticText(this, 0, "Party Two Prob:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyTwoProbTextCtrl = new wxTextCtrl(this, PA_NonClassic_TextBoxID_PartyTwoProb, formatFloat(seat->partyTwoProb, 2),
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// Create the controls for the party 1 combo box.
	partyThreeProbStaticText = new wxStaticText(this, 0, "Party Three Prob:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	partyThreeProbTextCtrl = new wxTextCtrl(this, PA_NonClassic_TextBoxID_PartyThreeProb, formatFloat(seat->partyThreeProb, 2),
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// *** OK and cancel buttons *** //

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_NonClassic_ButtonID_OK, "OK", wxPoint(50, currentHeight), wxSize(60, 24));
	removeButton = new wxButton(this, PA_NonClassic_ButtonID_Remove, "Remove", wxPoint(115, currentHeight), wxSize(60, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(180, currentHeight), wxSize(60, 24));

	currentHeight += 27;

	SetSize(wxSize(340, currentHeight + windowYOffset));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &NonClassicFrame::updateTextEitherParty, this, PA_NonClassic_TextBoxID_PartyTwoProb);
	Bind(wxEVT_TEXT, &NonClassicFrame::updateTextEitherParty, this, PA_NonClassic_TextBoxID_PartyThreeProb);
	Bind(wxEVT_BUTTON, &NonClassicFrame::OnOK, this, PA_NonClassic_ButtonID_OK);
	Bind(wxEVT_BUTTON, &NonClassicFrame::OnRemove, this, PA_NonClassic_ButtonID_Remove);
}

void NonClassicFrame::updateTextEitherParty(wxCommandEvent& WXUNUSED(event))
{
	try {
		float partyTwoProb = std::stof(partyTwoProbTextCtrl->GetLineText(0).ToStdString());
		float partyThreeProb = std::stof(partyThreeProbTextCtrl->GetLineText(0).ToStdString());
		float partyOneProb = 1.0f - partyTwoProb - partyThreeProb;
		partyOneProbStaticText->SetLabel(std::to_string(partyOneProb));
	}
	catch (std::invalid_argument) {
		// just do nothing, one of the text boxes doesn't have a valid number in it
	}
}

void NonClassicFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (seat) {
		try {
			Party const* partyOne = project->parties().getPartyPtr(partyOneComboBox->GetSelection());
			Party const* partyTwo = project->parties().getPartyPtr(partyTwoComboBox->GetSelection());
			Party const* partyThree = project->parties().getPartyPtr(partyThreeComboBox->GetSelection());
			float chanceTwo = std::stof(partyTwoProbTextCtrl->GetLineText(0).ToStdString());
			float chanceThree = std::stof(partyThreeProbTextCtrl->GetLineText(0).ToStdString());
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
			seat->livePartyOne = partyOne;
			seat->livePartyTwo = partyTwo;
			seat->livePartyThree = partyThree;
			seat->partyTwoProb = chanceTwo;
			seat->partyThreeProb = chanceThree;
		}
		catch (std::invalid_argument) {
			wxMessageBox("One or more text boxes does not contain a valid numeric value");
			return;
		}
	}

	// Then close this dialog.
	Close();
}

void NonClassicFrame::OnRemove(wxCommandEvent & WXUNUSED(event))
{
	seat->livePartyOne = nullptr;
	seat->livePartyTwo = nullptr;
	seat->livePartyThree = nullptr;
	seat->partyTwoProb = 0.0f;
	seat->partyThreeProb = 0.0f;
	Close();
}
