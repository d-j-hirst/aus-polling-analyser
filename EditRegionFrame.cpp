#include "EditRegionFrame.h"
#include "General.h"

EditRegionFrame::EditRegionFrame(bool isNewRegion, RegionsFrame* const parent, Region region)
	: wxDialog(NULL, 0, (isNewRegion ? "New Region" : "Edit Region")),
	isNewRegion(isNewRegion), parent(parent), region(region)
{
	std::string populationString = formatFloat(region.population, 0);

	lastPopulation = populationString;

	std::string lastElection2ppString = formatFloat(region.lastElection2pp, 2);

	lastLastElection2pp = lastElection2ppString;

	std::string sample2ppString = formatFloat(region.sample2pp, 2);

	lastSample2pp = sample2ppString;

	std::string additionalUncertaintyString = formatFloat(region.additionalUncertainty, 2);

	lastAdditionalUncertainty = additionalUncertaintyString;

	float currentHeight = 2;

	int const textBoxWidth = 150;

	// Create the controls for the region name.
	nameStaticText = new wxStaticText(this, 0, "Name:", wxPoint(2, currentHeight), wxSize(100, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_EditRegion_TextBoxID_Name, region.name, wxPoint(100, currentHeight - 2), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the region population.
	populationStaticText = new wxStaticText(this, 0, "Population:", wxPoint(2, currentHeight), wxSize(100, 23));
	populationTextCtrl = new wxTextCtrl(this, PA_EditRegion_TextBoxID_Population, populationString,
		wxPoint(100, currentHeight - 2), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the region weight.
	lastElection2ppStaticText = new wxStaticText(this, 0, "Last Election 2PP:", wxPoint(2, currentHeight), wxSize(100, 23));
	lastElection2ppTextCtrl = new wxTextCtrl(this, PA_EditRegion_TextBoxID_LastElection2pp, lastElection2ppString,
		wxPoint(100, currentHeight - 2), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the region weight.
	sample2ppStaticText = new wxStaticText(this, 0, "Sample 2PP:", wxPoint(2, currentHeight), wxSize(100, 23));
	sample2ppTextCtrl = new wxTextCtrl(this, PA_EditRegion_TextBoxID_Sample2pp, sample2ppString,
		wxPoint(100, currentHeight - 2), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the controls for the region weight.
	additionalUncertaintyStaticText = new wxStaticText(this, 0, "Additional Uncertainty:", wxPoint(2, currentHeight), wxSize(100, 23));
	additionalUncertaintyTextCtrl = new wxTextCtrl(this, PA_EditRegion_TextBoxID_AdditionalUncertainty, additionalUncertaintyString,
		wxPoint(100, currentHeight - 2), wxSize(textBoxWidth, 23));

	currentHeight += 27;

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_EditRegion_ButtonID_OK, "OK", wxPoint(67, currentHeight), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, currentHeight), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_TEXT, &EditRegionFrame::updateTextName, this, PA_EditRegion_TextBoxID_Name);
	Bind(wxEVT_TEXT, &EditRegionFrame::updateTextPopulation, this, PA_EditRegion_TextBoxID_Population);
	Bind(wxEVT_TEXT, &EditRegionFrame::updateTextLastElection2pp, this, PA_EditRegion_TextBoxID_LastElection2pp);
	Bind(wxEVT_TEXT, &EditRegionFrame::updateTextSample2pp, this, PA_EditRegion_TextBoxID_Sample2pp);
	Bind(wxEVT_TEXT, &EditRegionFrame::updateTextAdditionalUncertainty, this, PA_EditRegion_TextBoxID_AdditionalUncertainty);
	Bind(wxEVT_BUTTON, &EditRegionFrame::OnOK, this, PA_EditRegion_ButtonID_OK);
}

void EditRegionFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	if (isNewRegion) {
		// Get the parent frame to add a new region
		parent->OnNewRegionReady(region);
	}
	else {
		// Get the parent frame to replace the old region with the current one
		parent->OnEditRegionReady(region);
	}

	// Then close this dialog.
	Close();
}

void EditRegionFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	region.name = event.GetString();
}

void EditRegionFrame::updateTextPopulation(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			region.population = 0;
			return;
		}

		// convert to a float between 0 and 100.
		float i = std::stoi(str); // This may throw an error of the std::logic_error type.
		if (i > 1000000000) i = 1000000000;
		if (i < 0) i = 0;

		region.population = i;

		// save this valid string in case the next text entry gives an error.
		lastPopulation = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		populationTextCtrl->SetLabel(lastPopulation);
	}
}

void EditRegionFrame::updateTextLastElection2pp(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			region.lastElection2pp = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		region.lastElection2pp = f;

		// save this valid string in case the next text entry gives an error.
		lastLastElection2pp = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		lastElection2ppTextCtrl->SetLabel(lastLastElection2pp);
	}
}

void EditRegionFrame::updateTextSample2pp(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			region.sample2pp = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		region.sample2pp = f;

		// save this valid string in case the next text entry gives an error.
		lastSample2pp = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		sample2ppTextCtrl->SetLabel(lastSample2pp);
	}
}

void EditRegionFrame::updateTextAdditionalUncertainty(wxCommandEvent& event) {

	// updates the preliminary weight data with the string from the event.
	// This code effectively acts as a pseudo-validator
	// (can't get the standard one to work properly with pre-initialized values)
	try {
		std::string str = event.GetString().ToStdString();

		// An empty string can be interpreted as zero, so it's ok.
		if (str.empty()) {
			region.sample2pp = 0.0f;
			return;
		}

		// convert to a float between 0 and 100.
		float f = std::stof(str); // This may throw an error of the std::logic_error type.
		if (f > 100.0f) f = 100.0f;
		if (f < 0.0f) f = 0.0f;

		region.additionalUncertainty = f;

		// save this valid string in case the next text entry gives an error.
		lastAdditionalUncertainty = str;
	}
	catch (std::logic_error err) {
		// Set the text to the last valid string.
		additionalUncertaintyTextCtrl->SetLabel(lastAdditionalUncertainty);
	}
}