#include "EditPollFrame.h"
#include "General.h"

EditPollFrame::EditPollFrame(bool isNewPoll, PollsFrame* const parent, PollingProject const* project, Poll poll)
	: wxDialog(NULL, 0, (isNewPoll ? "New Poll" : "Edit Poll"), wxDefaultPosition, wxSize(340, 157)),
	isNewPoll(isNewPoll), parent(parent), project(project), poll(poll)
{

	const int labelYOffset = 5;
	const int windowYOffset = 38;

	// *** Pollster Combo Box *** //

	// Create the choices for the combo box.
	// Also check if the poll's pollster matches any of the choices (otherwise it is set to the first).
	wxArrayString pollsterArray;
	int selectedPollster = 0;
	int count = 0;
	for (auto it = project->getPollsterBegin(); it != project->getPollsterEnd(); ++it, ++count) {
		pollsterArray.push_back(it->name);
		if (&*it == poll.pollster) selectedPollster = count;
	}

	int currentHeight = 2;

	// Create the controls for the pollster combo box.
	pollsterStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	pollsterComboBox = new wxComboBox(this, PA_EditPoll_ComboBoxID_Pollster, pollsterArray[0], 
		wxPoint(200, currentHeight), wxSize(120, 23), pollsterArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	pollsterComboBox->SetSelection(selectedPollster);

	// Sets the poll's pollster in case it didn't match any of the permitted ones.
	// The "this->" is necessary as it avoids confusion with the constructor argument.
	this->poll.pollster = project->getPollsterPtr(pollsterComboBox->GetCurrentSelection());

	currentHeight += 27;

	// *** Date Picker *** //

	// Create the controls for the poll date picker.
	dateStaticText = new wxStaticText(this, 0, "Date:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	datePicker = new wxDatePickerCtrl(this, PA_EditPoll_DatePickerID_Date, poll.date,
		wxPoint(200, currentHeight), wxSize(120, 23), wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
	                                                                          // use wxDP_SPIN instead of wxDP_DROPDOWN

	currentHeight += 27;

	// *** Reported 2PP *** //

	// Generate the string for the preference flow.
	std::string reported2ppString = poll.getReported2ppString();

	// Store this string in case a text entry gives an error in the future.
	lastReported2pp = reported2ppString;

	// Create the controls for the poll reported 2pp.
	reported2ppStaticText = new wxStaticText(this, 0, "Reported Previous-Election 2PP:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	reported2ppTextCtrl = new wxTextCtrl(this, PA_EditPoll_TextBoxID_Reported2pp, reported2ppString,
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// *** Respondent-allocated 2PP *** //

	// Generate the string for the preference flow.
	std::string respondent2ppString = poll.getRespondent2ppString();

	// Store this string in case a text entry gives an error in the future.
	lastRespondent2pp = respondent2ppString;

	// Create the controls for the poll respondent 2pp.
	respondent2ppStaticText = new wxStaticText(this, 0, "Respondent-allocated 2PP:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	respondent2ppTextCtrl = new wxTextCtrl(this, PA_EditPoll_TextBoxID_Respondent2pp, respondent2ppString,
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// *** Primary Votes *** //

	int partyCount = project->parties().getPartyCount() + 1; // includes "others".

	// Generate the string for the preference flow.
	std::vector<std::string> primaryString;
	std::vector<std::string> partyString;
	primaryString.resize(partyCount);
	partyString.resize(partyCount);
	lastPrimary.resize(partyCount);
	primaryStaticText.resize(partyCount);
	primaryTextCtrl.resize(partyCount);
	int i = 0;
	for (; i < partyCount; i++) {
		if (i < partyCount - 1) {
			primaryString[i] = poll.getPrimaryString(i);
			partyString[i] = project->parties().getParty(i).name + " primary vote:";
		}
		else {
			primaryString[i] = poll.getPrimaryString(15);
			partyString[i] = "Others primary vote:";
		}

		// Store this string in case a text entry gives an error in the future.
		lastPrimary[i] = primaryString[i];

		// Create the controls for the poll respondent 2pp.
		primaryStaticText[i] = new wxStaticText(this, 0, partyString[i], wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
		primaryTextCtrl[i] = new wxTextCtrl(this, PA_EditPoll_TextBoxID_Primary + i, primaryString[i],
			wxPoint(200, currentHeight), wxSize(120, 23));

		currentHeight += 27;
	}

	// *** Respondent-allocated 2PP *** //

	// Generate the string for the preference flow.
	std::string calc2ppString = poll.getCalc2ppString();

	// Store this string in case a text entry gives an error in the future.
	lastCalc2pp = calc2ppString;

	// Create the controls for the estimated 2pp. This can't be edited by the user.
	calc2ppStaticText = new wxStaticText(this, 0, "Calculated 2PP:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	calc2ppNumberText = new wxStaticText(this, PA_EditPoll_TextBoxID_Calc2pp, calc2ppString,
		wxPoint(200, currentHeight), wxSize(120, 23));

	currentHeight += 27;

	// *** OK and cancel buttons *** //

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditPoll_ButtonID_OK, "OK", wxPoint(50, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(180, currentHeight), wxSize(100, 24));

	currentHeight += 27;

	SetSize(wxSize(340, currentHeight + windowYOffset));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_COMBOBOX, &EditPollFrame::updateComboBoxPollster, this, PA_EditPoll_ComboBoxID_Pollster);
	Bind(wxEVT_DATE_CHANGED, &EditPollFrame::updateDatePicker, this, PA_EditPoll_DatePickerID_Date);
	Bind(wxEVT_TEXT, &EditPollFrame::updateTextReported2pp, this, PA_EditPoll_TextBoxID_Reported2pp);
	Bind(wxEVT_TEXT, &EditPollFrame::updateTextRespondent2pp, this, PA_EditPoll_TextBoxID_Respondent2pp);
	for (i = 0; i < partyCount; i++)
		Bind(wxEVT_TEXT, &EditPollFrame::updateTextPrimary, this, PA_EditPoll_TextBoxID_Primary + i);
	Bind(wxEVT_BUTTON, &EditPollFrame::OnOK, this, PA_EditPoll_ButtonID_OK);
}

void EditPollFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (isNewPoll) {
		// Get the parent frame to add a new poll
		parent->OnNewPollReady(poll);
	}
	else {
		// Get the parent frame to replace the old poll with the current one
		parent->OnEditPollReady(poll);
	}

	// Then close this dialog.
	Close();
}

void EditPollFrame::updateComboBoxPollster(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	poll.pollster = project->getPollsterPtr(pollsterComboBox->GetCurrentSelection());
}

void EditPollFrame::updateTextReported2pp(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string is given the value -1, but is read as "no value" for further calculations.
		if (str.empty()) {
			poll.reported2pp = -1.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		poll.reported2pp = f;

		// save this valid string in case the next text entry gives an error.
		lastReported2pp = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		reported2ppTextCtrl->SetLabel(lastReported2pp);
	}
}

void EditPollFrame::updateTextRespondent2pp(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string is given the value -1, but is read as "no value" for further calculations.
		if (str.empty()) {
			poll.respondent2pp = -1.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		poll.respondent2pp = f;

		// save this valid string in case the next text entry gives an error.
		lastRespondent2pp = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		respondent2ppTextCtrl->SetLabel(lastRespondent2pp);
	}
}

void EditPollFrame::updateTextPrimary(wxCommandEvent& event) {

	int whichParty = event.GetId() - PA_EditPoll_TextBoxID_Primary;
	
	// if the party number is equal to the number of parties then this is for "others".
	int partyPollIndex = (whichParty == project->parties().getPartyCount() ? 15 : whichParty);

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string is given the value -1, but is read as "no value" for further calculations.
		if (str.empty()) {
			poll.primary[partyPollIndex] = -1.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		poll.primary[partyPollIndex] = f;

		// save this valid string in case the next text entry gives an error.
		lastPrimary[whichParty] = str;

		refreshCalculated2PP();
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		primaryTextCtrl[whichParty]->SetLabel(lastRespondent2pp);
	}
}

void EditPollFrame::updateDatePicker(wxDateEvent& event) {
	poll.date = event.GetDate();
	poll.date.SetHour(18);
}

void EditPollFrame::refreshCalculated2PP() {
	project->recalculatePollCalc2PP(poll);
	calc2ppNumberText->SetLabel(poll.getCalc2ppString());
}