#include "EditSeatFrame.h"
#include "SeatsFrame.h"
#include "PollingProject.h"
#include "General.h"

EditSeatFrame::EditSeatFrame(bool isNewSeat, SeatsFrame* const parent, PollingProject* project, Seat seat)
	: wxDialog(NULL, 0, (isNewSeat ? "New Seat" : "Edit Seat"), wxDefaultPosition, wxSize(375, 371)),
	isNewSeat(isNewSeat), parent(parent), project(project), seat(seat)
{
	int partyCount = project->parties().count();
	// If a model has not been specified it should default to the first.
	if (this->seat.incumbent == nullptr) this->seat.incumbent = project->parties().getPartyPtr(0);
	if (this->seat.challenger == nullptr) this->seat.challenger = project->parties().getPartyPtr(std::min(1, partyCount - 1));
	if (this->seat.challenger2 == nullptr) this->seat.challenger2 = project->parties().getPartyPtr(partyCount - 1);
	if (this->seat.region == nullptr) this->seat.region = project->getRegionPtr(0);

	// Generate the string for the seat's incumbent's margin
	std::string marginString = formatFloat(seat.margin, 5);

	// Generate the string for the seat's incumbent's local modifier
	std::string localModifierString = formatFloat(seat.localModifier, 5);

	// Generate the string for the seat's incumbent's betting odds
	std::string incumbentOddsString = formatFloat(seat.incumbentOdds, 5);

	// Generate the string for the seat's challengers's challenger odds
	std::string challengerOddsString = formatFloat(seat.challengerOdds, 5);

	// Generate the string for the seat's incumbent's second challenger odds
	std::string challenger2OddsString = formatFloat(seat.challenger2Odds, 5);

	// Store this string in case a text entry gives an error in the future.
	lastMargin = marginString;

	lastLocalModifier = localModifierString;

	lastIncumbentOdds = incumbentOddsString;

	lastChallengerOdds = challengerOddsString;

	lastChallenger2Odds = challenger2OddsString;

	const int labelYOffset = 5;

	int currentHeight = 2;

	int textBoxWidth = 150;
	int labelWidth = 200;

	// Create the controls for the seat name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_Name, seat.name, wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the seat name.
	previousNameStaticText = new wxStaticText(this, 0, "Previous Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	previousNameTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_PreviousName, seat.previousName, wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// *** Incumbent/Challenger Combo Box *** //

	// Create the choices for the combo box.
	// Also check if the seat's party matches any of the choices (otherwise it is set to the first).
	wxArrayString partyArray;
	int selectedIncumbent = 0;
	int selectedChallenger = 0;
	int selectedChallenger2 = 0;
	int partyNum = 0;
	for (auto it = project->parties().begin(); it != project->parties().end(); ++it, ++partyNum) {
		partyArray.push_back(it->name);
		if (&*it == seat.incumbent) selectedIncumbent = partyNum;
		if (&*it == seat.challenger) selectedChallenger = partyNum;
		if (&*it == seat.challenger2) selectedChallenger2 = partyNum;
	}

	// Create the controls for the model combo box.
	incumbentStaticText = new wxStaticText(this, 0, "Incumbent:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	incumbentComboBox = new wxComboBox(this, PA_EditSeat_ComboBoxID_Incumbent, partyArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the seats's base model.
	incumbentComboBox->SetSelection(selectedIncumbent);

	currentHeight += 27;

	// Create the controls for the model combo box.
	challengerStaticText = new wxStaticText(this, 0, "Challenger:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	challengerComboBox = new wxComboBox(this, PA_EditSeat_ComboBoxID_Challenger, partyArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the seats's base model.
	challengerComboBox->SetSelection(selectedChallenger);

	currentHeight += 27;

	// Create the controls for the model combo box.
	challenger2StaticText = new wxStaticText(this, 0, "Challenger 2:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	challenger2ComboBox = new wxComboBox(this, PA_EditSeat_ComboBoxID_Challenger2, partyArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), partyArray, wxCB_READONLY);

	// Sets the combo box selection to the seats's base model.
	challenger2ComboBox->SetSelection(selectedChallenger2);

	currentHeight += 27;

	// *** Region Combo Box *** //

	// Create the choices for the combo box.
	// Also check if the seat's region matches any of the choices (otherwise it is set to the first).
	wxArrayString regionArray;
	int selectedRegion = 0;
	int regionCount = 0;
	for (auto it = project->getRegionBegin(); it != project->getRegionEnd(); ++it, ++regionCount) {
		regionArray.push_back(it->name);
		if (&*it == seat.region) selectedRegion = regionCount;
	}

	// Create the controls for the region combo box.
	regionStaticText = new wxStaticText(this, 0, "Region:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	regionComboBox = new wxComboBox(this, PA_EditSeat_ComboBoxID_Region, regionArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), regionArray, wxCB_READONLY);

	// Sets the combo box selection to the seats's base model
	regionComboBox->SetSelection(selectedRegion);

	currentHeight += 27;

	// Create the controls for the seat margin
	marginStaticText = new wxStaticText(this, 0, "Margin:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	marginTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_Margin, marginString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the seat local 2pp modifier
	localModifierStaticText = new wxStaticText(this, 0, "Local Modifier:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	localModifierTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_LocalModifier, localModifierString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the seat incumbent odds
	incumbentOddsStaticText = new wxStaticText(this, 0, "Incumbent Odds:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	incumbentOddsTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_IncumbentOdds, incumbentOddsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the seat challenger odds
	challengerOddsStaticText = new wxStaticText(this, 0, "Challenger Odds:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	challengerOddsTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_ChallengerOdds, challengerOddsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the seat second challenger odds
	challengerOddsStaticText = new wxStaticText(this, 0, "Challenger 2 Odds:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	challenger2OddsTextCtrl = new wxTextCtrl(this, PA_EditSeat_TextBoxID_Challenger2Odds, challenger2OddsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditSeat_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextName, this, PA_EditSeat_TextBoxID_Name);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextPreviousName, this, PA_EditSeat_TextBoxID_PreviousName);
	Bind(wxEVT_COMBOBOX, &EditSeatFrame::updateComboBoxIncumbent, this, PA_EditSeat_ComboBoxID_Incumbent);
	Bind(wxEVT_COMBOBOX, &EditSeatFrame::updateComboBoxChallenger, this, PA_EditSeat_ComboBoxID_Challenger);
	Bind(wxEVT_COMBOBOX, &EditSeatFrame::updateComboBoxChallenger2, this, PA_EditSeat_ComboBoxID_Challenger2);
	Bind(wxEVT_COMBOBOX, &EditSeatFrame::updateComboBoxRegion, this, PA_EditSeat_ComboBoxID_Region);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextMargin, this, PA_EditSeat_TextBoxID_Margin);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextLocalModifier, this, PA_EditSeat_TextBoxID_LocalModifier);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextIncumbentOdds, this, PA_EditSeat_TextBoxID_IncumbentOdds);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextChallengerOdds, this, PA_EditSeat_TextBoxID_ChallengerOdds);
	Bind(wxEVT_TEXT, &EditSeatFrame::updateTextChallenger2Odds, this, PA_EditSeat_TextBoxID_Challenger2Odds);
	Bind(wxEVT_BUTTON, &EditSeatFrame::OnOK, this, PA_EditSeat_ButtonID_OK);
}

void EditSeatFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (seat.challenger == seat.incumbent) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Challenger cannot be same party as incumbent. Please change one of these parties.");

		message->ShowModal();
		return;
	}

	if (isNewSeat) {
		// Get the parent frame to add a new seat
		parent->OnNewSeatReady(seat);
	}
	else {
		// Get the parent frame to replace the old seat with the current one
		parent->OnEditSeatReady(seat);
	}

	// Then close this dialog.
	Close();
}

void EditSeatFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	seat.name = event.GetString();
}

void EditSeatFrame::updateTextPreviousName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	seat.previousName = event.GetString();
}

void EditSeatFrame::updateComboBoxIncumbent(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	seat.incumbent = project->parties().getPartyPtr(incumbentComboBox->GetCurrentSelection());
}

void EditSeatFrame::updateComboBoxChallenger(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	seat.challenger = project->parties().getPartyPtr(challengerComboBox->GetCurrentSelection());
}

void EditSeatFrame::updateComboBoxChallenger2(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	seat.challenger2 = project->parties().getPartyPtr(challenger2ComboBox->GetCurrentSelection());
}

void EditSeatFrame::updateComboBoxRegion(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	seat.region = project->getRegionPtr(regionComboBox->GetCurrentSelection());
}

void EditSeatFrame::updateTextMargin(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			seat.margin = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f; // Can't have a margin greater than 100%
		if (f < -100.0) f = -100.0; // Negative margin can occur for a redistributed seat.

		seat.margin = f;

		// save this valid string in case the next text entry gives an error.
		lastMargin = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		marginTextCtrl->SetLabel(lastMargin);
	}
}

void EditSeatFrame::updateTextLocalModifier(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			seat.localModifier = 0.0f;
			return;
		}

		// convert to a float between -100 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < -100.0) f = -100.0;

		seat.localModifier = f;

		// save this valid string in case the next text entry gives an error.
		lastLocalModifier = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		localModifierTextCtrl->SetLabel(lastLocalModifier);
	}
}

void EditSeatFrame::updateTextIncumbentOdds(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			seat.incumbentOdds = 0.0f;
			return;
		}

		// convert to a float between -100 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < -100.0) f = -100.0;

		seat.incumbentOdds = f;

		// save this valid string in case the next text entry gives an error.
		lastIncumbentOdds = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		incumbentOddsTextCtrl->SetLabel(lastIncumbentOdds);
	}
}

void EditSeatFrame::updateTextChallengerOdds(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			seat.challengerOdds = 0.0f;
			return;
		}

		// convert to a float between -100 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < -100.0) f = -100.0;

		seat.challengerOdds = f;

		// save this valid string in case the next text entry gives an error.
		lastChallengerOdds = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		challengerOddsTextCtrl->SetLabel(lastChallengerOdds);
	}
}

void EditSeatFrame::updateTextChallenger2Odds(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			seat.challenger2Odds = 0.0f;
			return;
		}

		// convert to a float between -100 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < -100.0) f = -100.0;

		seat.challenger2Odds = f;

		// save this valid string in case the next text entry gives an error.
		lastChallenger2Odds = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		challenger2OddsTextCtrl->SetLabel(lastChallenger2Odds);
	}
}