#include "EditEventFrame.h"
#include "General.h"

EditEventFrame::EditEventFrame(bool isNewEvent, EventsFrame* const parent, Event event)
	: wxDialog(NULL, 0, (isNewEvent ? "New Event" : "Edit Event")),
	isNewEvent(isNewEvent), parent(parent), event(event)
{

	const int labelYOffset = 5;
	const int controlXOffset = 150;
	const int controlWidth = 200;
	const int labelWidth = 140;

	// Generate the string for the preference flow.
	std::string voteString = formatFloat(event.vote, 3);

	// Store this string in case a text entry gives an error in the future.
	lastVote = voteString;

	int currentHeight = 2;

	// Create the controls for the event name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditEvent_TextBoxID_Name, event.name, wxPoint(controlXOffset, 0), wxSize(controlWidth, 23));

	currentHeight += 27;

	wxArrayString eventTypeArray;
	eventTypeArray.push_back("None");
	eventTypeArray.push_back("Election");
	eventTypeArray.push_back("Discontinuity");

	// Create the controls for the pollster combo box.
	eventTypeStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	eventTypeComboBox = new wxComboBox(this, PA_EditEvent_ComboBoxID_EventType, eventTypeArray[0],
		wxPoint(controlXOffset, currentHeight), wxSize(controlWidth, 23), eventTypeArray, wxCB_READONLY);

	// Sets the combo box selection to the poll's pollster, if any.
	eventTypeComboBox->SetSelection(event.eventType);

	currentHeight += 27;

	// *** Date Picker *** //

	// Create the controls for the poll date picker.
	dateStaticText = new wxStaticText(this, 0, "Date:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	datePicker = new wxDatePickerCtrl(this, PA_EditEvent_DatePickerID_Date, event.date,
		wxPoint(controlXOffset, currentHeight), wxSize(controlWidth, 23), wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
	// use wxDP_SPIN instead of wxDP_DROPDOWN

	currentHeight += 27;

	// *** Vote *** //

	// Create the controls for the event vote.
	voteStaticText = new wxStaticText(this, 0, "Vote (for elections):", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	voteTextCtrl = new wxTextCtrl(this, PA_EditEvent_TextBoxID_Vote, voteString,
		wxPoint(controlXOffset, currentHeight), wxSize(controlWidth, 23));

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditEvent_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditEventFrame::updateTextName, this, PA_EditEvent_TextBoxID_Name);
	Bind(wxEVT_COMBOBOX, &EditEventFrame::updateComboBoxEventType, this, PA_EditEvent_ComboBoxID_EventType);
	Bind(wxEVT_DATE_CHANGED, &EditEventFrame::updateDatePicker, this, PA_EditEvent_DatePickerID_Date);
	Bind(wxEVT_TEXT, &EditEventFrame::updateTextVote, this, PA_EditEvent_TextBoxID_Vote);
	Bind(wxEVT_BUTTON, &EditEventFrame::OnOK, this, PA_EditEvent_ButtonID_OK);
}

void EditEventFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (isNewEvent) {
		// Get the parent frame to add a new event
		parent->OnNewEventReady(event);
	}
	else {
		// Get the parent frame to replace the old event with the current one
		parent->OnEditEventReady(event);
	}

	// Then close this dialog.
	Close();
}

void EditEventFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	this->event.name = event.GetString();
}

void EditEventFrame::updateComboBoxEventType(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	this->event.eventType = EventType(eventTypeComboBox->GetCurrentSelection());
}

void EditEventFrame::updateDatePicker(wxDateEvent& event) {
	this->event.date = event.GetDate();
	this->event.date.SetHour(18);
}

void EditEventFrame::updateTextVote(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			this->event.vote = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		this->event.vote = f;

		// save this valid string in case the next text entry gives an error.
		lastVote = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		voteTextCtrl->SetLabel(lastVote);
	}
}