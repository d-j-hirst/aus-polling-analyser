#include "EditModelFrame.h"
#include "General.h"

EditModelFrame::EditModelFrame(bool isNewModel, ModelsFrame* const parent, Model model)
	: wxDialog(NULL, 0, (isNewModel ? "New Model" : "Edit Model")),
	isNewModel(isNewModel), parent(parent), model(model)
{

	// Generate the string for the trend time score multiplier.
	std::string voteTimeMultiplierString = formatFloat(model.trendTimeScoreMultiplier, 2);

	// Generate the string for the house effect time score multiplier.
	std::string houseEffectTimeMultiplierString = formatFloat(model.houseEffectTimeScoreMultiplier, 2);

	// Generate the string for the house effect time score multiplier.
	std::string calibrationFirstPartyBiasString = formatFloat(model.calibrationFirstPartyBias, 2);

	// Generate the string for the number of iterations.
	std::string numIterationsString = std::to_string(model.numIterations);

	// Store this string in case a text entry gives an error in the future.
	lastVoteTimeMultiplier = voteTimeMultiplierString;

	// Store this string in case a text entry gives an error in the future.
	lastHouseEffectTimeMultiplier = houseEffectTimeMultiplierString;

	// Store this string in case a text entry gives an error in the future.
	lastCalibrationFirstPartyBias = calibrationFirstPartyBiasString;

	// Store this string in case a text entry gives an error in the future.
	lastNumIterations = numIterationsString;

	const int labelYOffset = 5;

	int currentHeight = 0;

	// Create the controls for the model name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(200, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditModel_TextBoxID_Name, model.name, wxPoint(200, currentHeight), wxSize(198, 23));

	currentHeight += 27;

	// Create the controls for the model number of iterations
	numIterationsStaticText = new wxStaticText(this, 0, "Number of Iterations:", wxPoint(2, currentHeight + labelYOffset), wxSize(200, 23));
	numIterationsTextCtrl = new wxTextCtrl(this, PA_EditModel_TextBoxID_NumIterations, numIterationsString,
		wxPoint(200, currentHeight), wxSize(198, 23));

	currentHeight += 27;

	// Create the controls for the model vote time multiplier.
	voteTimeMultiplierStaticText = new wxStaticText(this, 0, "Vote Time-Multiplier:", wxPoint(2, currentHeight + labelYOffset), wxSize(200, 23));
	voteTimeMultiplierTextCtrl = new wxTextCtrl(this, PA_EditModel_TextBoxID_VoteTimeMultiplier, voteTimeMultiplierString,
		wxPoint(200, currentHeight), wxSize(198, 23));

	currentHeight += 27;

	// Create the controls for the model house effect time multiplier.
	voteTimeMultiplierStaticText = new wxStaticText(this, 0, "House Effect Time-Multiplier:", wxPoint(2, currentHeight + labelYOffset), wxSize(200, 23));
	voteTimeMultiplierTextCtrl = new wxTextCtrl(this, PA_EditModel_TextBoxID_HouseEffectTimeMultiplier, houseEffectTimeMultiplierString,
		wxPoint(200, currentHeight), wxSize(198, 23));

	currentHeight += 27;

	// Create the controls for the model first-party calibration bias.
	calibrationFirstPartyBiasStaticText = new wxStaticText(this, 0, "First Party Calibration ", wxPoint(2, currentHeight + labelYOffset), wxSize(200, 23));
	calibrationFirstPartyBiasTextCtrl = new wxTextCtrl(this, PA_EditModel_TextBoxID_CalibrationFirstPartyBias, calibrationFirstPartyBiasString,
		wxPoint(200, currentHeight), wxSize(198, 23));

	currentHeight += 27;

	// Create the controls for the start date picker.
	startDateStaticText = new wxStaticText(this, 0, "Start Date:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	startDatePicker = new wxDatePickerCtrl(this, PA_EditModel_DatePickerID_StartDate, model.startDate,
		wxPoint(200, currentHeight), wxSize(120, 23), wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
	// use wxDP_SPIN instead of wxDP_DROPDOWN

	currentHeight += 27;

	// Create the controls for the end date picker.
	endDateStaticText = new wxStaticText(this, 0, "Finish Date:", wxPoint(2, currentHeight + labelYOffset), wxSize(198, 23));
	endDatePicker = new wxDatePickerCtrl(this, PA_EditModel_DatePickerID_EndDate, model.endDate,
		wxPoint(200, currentHeight), wxSize(120, 23), wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
	// use wxDP_SPIN instead of wxDP_DROPDOWN

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditModel_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditModelFrame::updateTextName, this, PA_EditModel_TextBoxID_Name);
	Bind(wxEVT_TEXT, &EditModelFrame::updateTextNumIterations, this, PA_EditModel_TextBoxID_NumIterations);
	Bind(wxEVT_TEXT, &EditModelFrame::updateTextVoteTimeMultiplier, this, PA_EditModel_TextBoxID_VoteTimeMultiplier);
	Bind(wxEVT_TEXT, &EditModelFrame::updateTextHouseEffectTimeMultiplier, this, PA_EditModel_TextBoxID_HouseEffectTimeMultiplier);
	Bind(wxEVT_TEXT, &EditModelFrame::updateTextCalibrationFirstPartyBias, this, PA_EditModel_TextBoxID_CalibrationFirstPartyBias);
	Bind(wxEVT_DATE_CHANGED, &EditModelFrame::updateStartDatePicker, this, PA_EditModel_DatePickerID_StartDate);
	Bind(wxEVT_DATE_CHANGED, &EditModelFrame::updateEndDatePicker, this, PA_EditModel_DatePickerID_EndDate);
	Bind(wxEVT_BUTTON, &EditModelFrame::OnOK, this, PA_EditModel_ButtonID_OK);
}

void EditModelFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	// If this is set to true the model has not yet been updated.
	model.lastUpdated = wxInvalidDateTime;

	if (isNewModel) {
		// Get the parent frame to add a new model
		parent->OnNewModelReady(model);
	}
	else {
		// Get the parent frame to replace the old model with the current one
		parent->OnEditModelReady(model);
	}

	// Then close this dialog.
	Close();
}

void EditModelFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	model.name = event.GetString();
}

void EditModelFrame::updateTextNumIterations(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as one, so it's ok.
		if (str.empty()) {
			model.numIterations = 1;
			return;
		}

		// convert to a float between 0 and 100.
		int i = std::stoi(str); // This may throw an error of the std::logic_error type.
		if (i < 1) i = 1;

		model.numIterations = i;

		// save this valid string in case the next text entry gives an error.
		lastNumIterations = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		numIterationsTextCtrl->SetLabel(lastNumIterations);
	}
}

void EditModelFrame::updateTextVoteTimeMultiplier(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			model.trendTimeScoreMultiplier = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		model.trendTimeScoreMultiplier = f;

		// save this valid string in case the next text entry gives an error.
		lastVoteTimeMultiplier = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		voteTimeMultiplierTextCtrl->SetLabel(lastVoteTimeMultiplier);
	}
}

void EditModelFrame::updateTextHouseEffectTimeMultiplier(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			model.houseEffectTimeScoreMultiplier = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		model.houseEffectTimeScoreMultiplier = f;

		// save this valid string in case the next text entry gives an error.
		lastHouseEffectTimeMultiplier = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		houseEffectTimeMultiplierTextCtrl->SetLabel(lastVoteTimeMultiplier);
	}
}

void EditModelFrame::updateTextCalibrationFirstPartyBias(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			model.calibrationFirstPartyBias = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		model.calibrationFirstPartyBias = f;

		// save this valid string in case the next text entry gives an error.
		lastHouseEffectTimeMultiplier = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		calibrationFirstPartyBiasTextCtrl->SetLabel(lastCalibrationFirstPartyBias);
	}
}

void EditModelFrame::updateStartDatePicker(wxDateEvent& event) {
	model.startDate = event.GetDate();
	model.startDate.SetHour(18);
}

void EditModelFrame::updateEndDatePicker(wxDateEvent& event) {
	model.endDate = event.GetDate();
	model.endDate.SetHour(18);
}