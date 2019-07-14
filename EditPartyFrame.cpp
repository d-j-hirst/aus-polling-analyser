#include "EditPartyFrame.h"
#include "General.h"

#include <regex>

using namespace std::placeholders; // for function object parameter binding

// IDs for the controls and the menu commands
enum
{
	PA_EditParty_ButtonID_OK,
	PA_EditParty_TextBoxID_Name,
	PA_EditParty_TextBoxID_PreferenceFlow,
	PA_EditParty_TextBoxID_ExhaustRate,
	PA_EditParty_TextBoxID_Abbreviation,
	PA_EditParty_TextBoxID_OfficialShortCodes,
	PA_EditParty_ColourPickerID_Colour,
	PA_EditParty_ComboBoxID_Ideology,
	PA_EditParty_ComboBoxID_Consistency,
	PA_EditParty_TextBoxID_BoothColourMult,
	PA_EditParty_ComboBoxID_CountAsParty,
	PA_EditParty_ComboBoxID_SupportsParty,
};

EditPartyFrame::EditPartyFrame(Function function, OkCallback callback, Party party)
	: wxDialog(NULL, 0, (function == Function::New ? "New Party" : "Edit Party"), wxDefaultPosition, wxSize(400, 400)),
	party(party), callback(callback)
{
	// Generate the string for the preference flow.
	std::string preferenceFlowString = formatFloat(party.preferenceShare, 2);

	// Store this string in case a text entry gives an error in the future.
	lastPreferenceFlow = preferenceFlowString;

	// Generate the string for the exhaust rate.
	std::string exhaustRateString = formatFloat(party.exhaustRate, 2);

	// Store this string in case a text entry gives an error in the future.
	lastExhaustRate = exhaustRateString;

	int currentHeight = 2;

	auto nameFunc = std::bind(&EditPartyFrame::updateTextName, this, _1);
	nameTextInput.reset(new TextInput(this, PA_EditParty_TextBoxID_Name, "Name:", party.name, wxPoint(2, currentHeight), nameFunc));

	currentHeight += 27;

	// Create the controls for the preference flow.
	preferenceFlowStaticText = new wxStaticText(this, 0, "Preference Flow:", wxPoint(2, currentHeight), wxSize(150, 23));
	preferenceFlowTextCtrl = new wxTextCtrl(this, PA_EditParty_TextBoxID_PreferenceFlow, preferenceFlowString,
		wxPoint(150, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	// Create the controls for the exhaust rate.
	exhaustRateStaticText = new wxStaticText(this, 0, "Exhaust Rate:", wxPoint(2, currentHeight), wxSize(150, 23));
	exhaustRateTextCtrl = new wxTextCtrl(this, PA_EditParty_TextBoxID_ExhaustRate, exhaustRateString,
		wxPoint(150, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	// Create the controls for the party name abbreviation.
	abbreviationStaticText = new wxStaticText(this, 0, "Abbreviation:", wxPoint(2, currentHeight), wxSize(150, 23));
	abbreviationTextCtrl = new wxTextCtrl(this, PA_EditParty_TextBoxID_Abbreviation, party.abbreviation, wxPoint(150, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	std::string shortCodes = "";
	if (party.officialCodes.size()) {
		shortCodes += party.officialCodes[0];
		for (size_t i = 1; i < party.officialCodes.size(); ++i) {
			shortCodes += "," + party.officialCodes[i];
		}
	}

	// Create the controls for the party's official short codes
	officialShortCodesStaticText = new wxStaticText(this, 0, "Official Short Codes:", wxPoint(2, currentHeight), wxSize(150, 23));
	officialShortCodesTextCtrl = new wxTextCtrl(this, PA_EditParty_TextBoxID_OfficialShortCodes, shortCodes, wxPoint(150, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	wxColour currentColour(party.colour.r, party.colour.g, party.colour.b);

	// Create the controls for the party's colour
	colourPickerText = new wxStaticText(this, 0, "Colour:", wxPoint(2, currentHeight), wxSize(150, 23));
	colourPicker = new wxColourPickerCtrl(this, PA_EditParty_ColourPickerID_Colour, currentColour, wxPoint(150, currentHeight - 2), wxSize(200, 23));

	currentHeight += 27;

	// Ideology combo box
	wxArrayString ideologyArray;
	ideologyArray.push_back("Strong Left");
	ideologyArray.push_back("Moderate Left");
	ideologyArray.push_back("Centrist");
	ideologyArray.push_back("Moderate Right");
	ideologyArray.push_back("Strong Right");
	int currentIdeologySelection = party.ideology;

	// Create the controls for the ideology combo box.
	ideologyStaticText = new wxStaticText(this, 0, "Ideology:", wxPoint(2, currentHeight), wxSize(198, 23));
	ideologyComboBox = new wxComboBox(this, PA_EditParty_ComboBoxID_Ideology, ideologyArray[currentIdeologySelection],
		wxPoint(200, currentHeight), wxSize(120, 23), ideologyArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	ideologyComboBox->SetSelection(currentIdeologySelection);

	currentHeight += 27;

	// Consistency combo box
	wxArrayString consistencyArray;
	consistencyArray.push_back("Low");
	consistencyArray.push_back("Moderate");
	consistencyArray.push_back("High");
	int currentConsistencySelection = party.consistency;

	// Create the controls for the ideology combo box.
	consistencyStaticText = new wxStaticText(this, 0, "Preference Consistency:", wxPoint(2, currentHeight), wxSize(198, 23));
	consistencyComboBox = new wxComboBox(this, PA_EditParty_ComboBoxID_Consistency, consistencyArray[currentConsistencySelection],
		wxPoint(200, currentHeight), wxSize(120, 23), consistencyArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	consistencyComboBox->SetSelection(currentConsistencySelection);

	currentHeight += 27;

	if (party.countAsParty != Party::CountAsParty::IsPartyOne && party.countAsParty != Party::CountAsParty::IsPartyTwo) {

		// Create the controls for the party name abbreviation.
		boothColourMultStaticText = new wxStaticText(this, 0, "Booth Colour Multiplier:", wxPoint(2, currentHeight), wxSize(150, 23));
		boothColourMultTextCtrl = new wxTextCtrl(this, PA_EditParty_TextBoxID_BoothColourMult, formatFloat(party.boothColourMult, 3), wxPoint(150, currentHeight - 2), wxSize(200, 23));

		currentHeight += 27;

		// *** Count-As-Party Combo Box *** //

		// Firstly do counts-as-party (i.e. formal coalition party) status

		// Create the choices for the combo box.
		// Also check if the party's count-as-party matches one of the options
		wxArrayString countAsPartyArray;
		countAsPartyArray.push_back("None");
		countAsPartyArray.push_back("Counts As Party One");
		countAsPartyArray.push_back("Counts As Party Two");
		int currentSelection = 0;
		switch (party.countAsParty) {
		case Party::CountAsParty::CountsAsPartyOne: currentSelection = 1; break;
		case Party::CountAsParty::CountsAsPartyTwo: currentSelection = 2; break;
		}

		// Create the controls for the count-as-party combo box.
		countAsPartyStaticText = new wxStaticText(this, 0, "Counts as party:", wxPoint(2, currentHeight), wxSize(198, 23));
		countAsPartyComboBox = new wxComboBox(this, PA_EditParty_ComboBoxID_CountAsParty, countAsPartyArray[currentSelection],
			wxPoint(200, currentHeight), wxSize(120, 23), countAsPartyArray, wxCB_READONLY);

		// Sets the combo box selection to the poll's pollster, if any.
		countAsPartyComboBox->SetSelection(currentSelection);

		currentHeight += 27;

		// Now do supports-party (likely minority government support) status

		// Create the choices for the combo box.
		// Also check if the party's count-as-party matches one of the options
		wxArrayString supportsPartyArray;
		supportsPartyArray.push_back("None");
		supportsPartyArray.push_back("Supports Party One");
		supportsPartyArray.push_back("Supports Party Two");
		currentSelection = 0;
		switch (party.supportsParty) {
		case Party::SupportsParty::One: currentSelection = 1; break;
		case Party::SupportsParty::Two: currentSelection = 2; break;
		}

		// Create the controls for the count-as-party combo box.
		supportsPartyStaticText = new wxStaticText(this, 0, "Supports party:", wxPoint(2, currentHeight), wxSize(198, 23));
		supportsPartyComboBox = new wxComboBox(this, PA_EditParty_ComboBoxID_SupportsParty, supportsPartyArray[currentSelection],
			wxPoint(200, currentHeight), wxSize(120, 23), supportsPartyArray, wxCB_READONLY);

		// Sets the combo box selection to the poll's pollster, if any.
		supportsPartyComboBox->SetSelection(currentSelection);

		currentHeight += 27;
	}

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditParty_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditPartyFrame::updateTextPreferenceFlow, this, PA_EditParty_TextBoxID_PreferenceFlow);
	Bind(wxEVT_TEXT, &EditPartyFrame::updateTextExhaustRate, this, PA_EditParty_TextBoxID_ExhaustRate);
	Bind(wxEVT_TEXT, &EditPartyFrame::updateTextAbbreviation, this, PA_EditParty_TextBoxID_Abbreviation);
	Bind(wxEVT_TEXT, &EditPartyFrame::updateTextOfficialShortCodes, this, PA_EditParty_TextBoxID_OfficialShortCodes);
	Bind(wxEVT_COLOURPICKER_CHANGED, &EditPartyFrame::updateColourPicker, this, PA_EditParty_ColourPickerID_Colour);
	Bind(wxEVT_COMBOBOX, &EditPartyFrame::updateComboBoxIdeology, this, PA_EditParty_ComboBoxID_Ideology);
	Bind(wxEVT_COMBOBOX, &EditPartyFrame::updateComboBoxConsistency, this, PA_EditParty_ComboBoxID_Consistency);
	Bind(wxEVT_TEXT, &EditPartyFrame::updateBoothColourMult, this, PA_EditParty_TextBoxID_BoothColourMult);
	Bind(wxEVT_COMBOBOX, &EditPartyFrame::updateComboBoxCountAsParty, this, PA_EditParty_ComboBoxID_CountAsParty);
	Bind(wxEVT_COMBOBOX, &EditPartyFrame::updateComboBoxSupportsParty, this, PA_EditParty_ComboBoxID_SupportsParty);
	Bind(wxEVT_BUTTON, &EditPartyFrame::OnOK, this, PA_EditParty_ButtonID_OK);
}

void EditPartyFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {
	// Call the function that was passed when this frame was opened.
	callback(party);

	// Then close this dialog.
	Close();
}

void EditPartyFrame::updateTextName(std::string name) {

	// updates the preliminary project data with the string from the event.
	party.name = name;
}

void EditPartyFrame::updateTextPreferenceFlow(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			party.preferenceShare = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		party.preferenceShare = f;

		// save this valid string in case the next text entry gives an error.
		lastPreferenceFlow = str;
	} catch (std::logic_error err) {
		// Set the text to the last valid string.
		preferenceFlowTextCtrl->SetLabel(lastPreferenceFlow);
	}
}

void EditPartyFrame::updateTextExhaustRate(wxCommandEvent & event)
{
	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			party.exhaustRate = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		party.exhaustRate = f;

		// save this valid string in case the next text entry gives an error.
		lastExhaustRate = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		exhaustRateTextCtrl->SetLabel(lastExhaustRate);
	}
}

void EditPartyFrame::updateTextAbbreviation(wxCommandEvent& event) {
	// updates the preliminary project data with the string from the event.
	party.abbreviation = event.GetString();
}

void EditPartyFrame::updateTextOfficialShortCodes(wxCommandEvent& event) {
	// updates the party short codes data with the string from the event.
	std::string thisString = event.GetString();
	std::regex partyCodeRegex("([^,]+)(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?(,([^,]+))?");
	std::smatch matchResults;
	std::regex_match(thisString, matchResults, partyCodeRegex);
	if (matchResults.size()) {
		party.officialCodes.clear();
		for (int matchIndex = 1; matchIndex < 56; matchIndex += 2) {
			if (!matchResults[matchIndex].matched) break;
			party.officialCodes.push_back(matchResults[matchIndex].str());
		}
	}
}

void EditPartyFrame::updateColourPicker(wxColourPickerEvent& event)
{
	party.colour.r = event.GetColour().Red();
	party.colour.g = event.GetColour().Green();
	party.colour.b = event.GetColour().Blue();
}

void EditPartyFrame::updateComboBoxIdeology(wxCommandEvent& WXUNUSED(event))
{
	party.ideology = ideologyComboBox->GetCurrentSelection();
}

void EditPartyFrame::updateComboBoxConsistency(wxCommandEvent& WXUNUSED(event))
{
	party.consistency = consistencyComboBox->GetCurrentSelection();
}

void EditPartyFrame::updateBoothColourMult(wxCommandEvent & event)
{
	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			party.boothColourMult = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		party.boothColourMult = f;

		// save this valid string in case the next text entry gives an error.
		lastBoothColourMult = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		boothColourMultTextCtrl->SetLabel(lastBoothColourMult);
	}
}

void EditPartyFrame::updateComboBoxCountAsParty(wxCommandEvent& WXUNUSED(event)) {

	int selection = countAsPartyComboBox->GetCurrentSelection();
	switch (selection) {
	case 0: party.countAsParty = Party::CountAsParty::None; break;
	case 1: party.countAsParty = Party::CountAsParty::CountsAsPartyOne; break;
	case 2: party.countAsParty = Party::CountAsParty::CountsAsPartyTwo; break;
	}
}

void EditPartyFrame::updateComboBoxSupportsParty(wxCommandEvent& WXUNUSED(event)) {

	int selection = supportsPartyComboBox->GetCurrentSelection();
	switch (selection) {
	case 0: party.supportsParty = Party::SupportsParty::None; break;
	case 1: party.supportsParty = Party::SupportsParty::One; break;
	case 2: party.supportsParty = Party::SupportsParty::Two; break;
	}
}