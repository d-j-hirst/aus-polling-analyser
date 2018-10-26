#include "EditProjectionFrame.h"
#include "ProjectionsFrame.h"
#include "PollingProject.h"
#include "General.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditProjection_Base = 650, // To avoid mixing events with other frames.
	PA_EditProjection_ButtonID_OK,
	PA_EditProjection_TextBoxID_Name,
	PA_EditProjection_ComboBoxID_BaseModel,
	PA_EditProjection_DatePickerID_EndDate,
	PA_EditProjection_TextBoxID_NumIterations,
	PA_EditProjection_TextBoxID_VoteLoss,
	PA_EditProjection_TextBoxID_DailyChange,
	PA_EditProjection_TextBoxID_InitialChange,
	PA_EditProjection_TextBoxID_NumElections,
};

EditProjectionFrame::EditProjectionFrame(bool isNewProjection, ProjectionsFrame* const parent, PollingProject const* project, Projection projection)
	: wxDialog(NULL, 0, (isNewProjection ? "New Projection" : "Edit Projection"), wxDefaultPosition, wxSize(375, 287)),
	isNewProjection(isNewProjection), parent(parent), project(project), projection(projection)
{
	// If a model has not been specified it should default to the first.
	if (this->projection.baseModel == nullptr) this->projection.baseModel = project->getModelPtr(0);

	// Generate the string for the number of iterations
	std::string numIterationsString = std::to_string(projection.numIterations);

	// Store this string in case a text entry gives an error in the future.
	lastNumIterations = numIterationsString;

	// Generate the string for the leader's daily vote loss
	std::string voteLossString = formatFloat(projection.leaderVoteLoss, 5);

	// Store this string in case a text entry gives an error in the future.
	lastVoteLoss = voteLossString;

	// Generate the string for the standard deviation of daily change.
	std::string dailyChangeString = formatFloat(projection.dailyChange, 4);

	// Store this string in case a text entry gives an error in the future.
	lastDailyChange = dailyChangeString;

	// Generate the string for the standard deviation of initial model uncertainty.
	std::string initialChangeString = formatFloat(projection.initialStdDev, 3);

	// Store this string in case a text entry gives an error in the future.
	lastInitialChange = initialChangeString;

	// Generate the string for the number of elections.
	std::string numElectionsString = std::to_string(projection.numElections);

	// Store this string in case a text entry gives an error in the future.
	lastNumElections = numElectionsString;

	const int labelYOffset = 5;

	int currentHeight = 2;

	int textBoxWidth = 150;
	int labelWidth = 200;

	// Create the controls for the projection name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_Name, projection.name, wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// *** Pollster Combo Box *** //

	// Create the choices for the combo box.
	// Also check if the poll's pollster matches any of the choices (otherwise it is set to the first).
	wxArrayString modelArray;
	int selectedModel = 0;
	int count = 0;
	for (auto it = project->getModelBegin(); it != project->getModelEnd(); ++it, ++count) {
		modelArray.push_back(it->name);
		if (&*it == projection.baseModel) selectedModel = count;
	}

	// Create the controls for the model combo box.
	modelStaticText = new wxStaticText(this, 0, "Base model:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	modelComboBox = new wxComboBox(this, PA_EditProjection_ComboBoxID_BaseModel, modelArray[0],
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), modelArray, wxCB_READONLY);

	// Sets the combo box selection to the projections's base model.
	modelComboBox->SetSelection(selectedModel);

	currentHeight += 27;

	// Create the controls for the end date picker.
	endDateStaticText = new wxStaticText(this, 0, "Finish Date:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	endDatePicker = new wxDatePickerCtrl(this, PA_EditProjection_DatePickerID_EndDate, projection.endDate,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23), wxDP_DROPDOWN | wxDP_SHOWCENTURY); // not supported under OSX/Cocoa
	// use wxDP_SPIN instead of wxDP_DROPDOWN

	currentHeight += 27;

	// Create the controls for the projection's number of iterations
	numIterationsStaticText = new wxStaticText(this, 0, "Number of Iterations:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	numIterationsTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_NumIterations, numIterationsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the projection's vote loss
	voteLossStaticText = new wxStaticText(this, 0, "Daily leader vote loss:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	voteLossTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_VoteLoss, voteLossString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the projection's daily vote change
	dailyChangeStaticText = new wxStaticText(this, 0, "SD of daily vote change:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	dailyChangeTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_DailyChange, dailyChangeString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the projection's initial vote distribution
	initialChangeStaticText = new wxStaticText(this, 0, "SD of initial vote distribution:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	initialChangeTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_InitialChange, initialChangeString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the projection's number of elections
	numElectionsStaticText = new wxStaticText(this, 0, "# elections used to calculate SD:", wxPoint(2, currentHeight + labelYOffset), wxSize(labelWidth, 23));
	numElectionsTextCtrl = new wxTextCtrl(this, PA_EditProjection_TextBoxID_NumElections, numElectionsString,
		wxPoint(labelWidth, currentHeight), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditProjection_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextName, this, PA_EditProjection_TextBoxID_Name);
	Bind(wxEVT_COMBOBOX, &EditProjectionFrame::updateComboBoxBaseModel, this, PA_EditProjection_ComboBoxID_BaseModel);
	Bind(wxEVT_DATE_CHANGED, &EditProjectionFrame::updateEndDatePicker, this, PA_EditProjection_DatePickerID_EndDate);
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextNumIterations, this, PA_EditProjection_TextBoxID_NumIterations);
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextVoteLoss, this, PA_EditProjection_TextBoxID_VoteLoss);
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextDailyChange, this, PA_EditProjection_TextBoxID_DailyChange);
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextInitialChange, this, PA_EditProjection_TextBoxID_InitialChange);
	Bind(wxEVT_TEXT, &EditProjectionFrame::updateTextNumElections, this, PA_EditProjection_TextBoxID_NumElections);
	Bind(wxEVT_BUTTON, &EditProjectionFrame::OnOK, this, PA_EditProjection_ButtonID_OK);
}

void EditProjectionFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	// If this is set to true the projection has not yet been updated.
	projection.lastUpdated = wxInvalidDateTime;

	if (isNewProjection) {
		// Get the parent frame to add a new projection
		parent->OnNewProjectionReady(projection);
	}
	else {
		// Get the parent frame to replace the old projection with the current one
		parent->OnEditProjectionReady(projection);
	}

	// Then close this dialog.
	Close();
}

void EditProjectionFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	projection.name = event.GetString();
}

void EditProjectionFrame::updateComboBoxBaseModel(wxCommandEvent& WXUNUSED(event)) {

	// updates the preliminary pollster pointer using the current selection.
	projection.baseModel = project->getModelPtr(modelComboBox->GetCurrentSelection());
}

void EditProjectionFrame::updateTextNumIterations(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			projection.numIterations = 0;
			return;
		}

		// convert to an int
		int i = std::stoi(str); // This may throw an error of the std::logic_error type.
		if (i > 99999) i = 99999; // Some kind of maximum to avoid being ridiculous
		if (i < 0) i = 0;

		projection.numIterations = i;

		// save this valid string in case the next text entry gives an error.
		lastNumIterations = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		numIterationsTextCtrl->SetLabel(lastNumIterations);
	}
}

void EditProjectionFrame::updateTextVoteLoss(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			projection.leaderVoteLoss = 0.0f;
			return;
		}

		// convert to a float between 0 and 1.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 1.0f) f = 1.0f; // Leader can't lose more that its entire lead in one day!
		if (f < 0) f = 0; // Leader can't extend its lead in this way

		projection.leaderVoteLoss = f;

		// save this valid string in case the next text entry gives an error.
		lastVoteLoss = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		voteLossTextCtrl->SetLabel(lastVoteLoss);
	}
}

void EditProjectionFrame::updateTextDailyChange(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			projection.dailyChange = 0.0f;
			return;
		}

		// convert to a float between 0 and 1.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 10.0f) f = 10.0f; // Avoid ridiculous changes
		if (f < 0) f = 0; // Negative and positive values are identical here, restrict to positive to avoid confusion.

		projection.dailyChange = f;

		// save this valid string in case the next text entry gives an error.
		lastDailyChange = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		dailyChangeTextCtrl->SetLabel(lastDailyChange);
	}
}

void EditProjectionFrame::updateTextInitialChange(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			projection.initialStdDev = 0.0f;
			return;
		}

		// convert to a float between 0 and 1.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 10.0f) f = 10.0f; // Avoid ridiculous changes
		if (f < 0) f = 0; // Negative and positive values are identical here, restrict to positive to avoid confusion.

		projection.initialStdDev = f;

		// save this valid string in case the next text entry gives an error.
		lastInitialChange = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		initialChangeTextCtrl->SetLabel(lastInitialChange);
	}
}

void EditProjectionFrame::updateTextNumElections(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			projection.numElections = 0;
			return;
		}

		// convert to a float between 0 and 1.
		int i = std::stoi(str); // This may throw an error of the std::logic_error type.
		if (i > 1000.0f) i = 1000.0f; // Avoid ridiculous changes
		if (i < 0) i = 0; // Negative and positive values are identical here, restrict to positive to avoid confusion.

		projection.numElections = i;

		// save this valid string in case the next text entry gives an error.
		lastNumElections = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		numElectionsTextCtrl->SetLabel(lastNumElections);
	}
}

void EditProjectionFrame::updateEndDatePicker(wxDateEvent& event) {
	projection.endDate = event.GetDate();
	projection.endDate.SetHour(18);
}