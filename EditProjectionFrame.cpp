#include "EditProjectionFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "General.h"
#include "FloatInput.h"
#include "IntInput.h"
#include "Log.h"
#include "ModelCollection.h"
#include "TextInput.h"

#include <wx/datetime.h>

using namespace std::placeholders; // for function object parameter binding

constexpr int ControlPadding = 4;

// IDs for the controls and the menu commands
enum ControlId
{
	Base = 650, // To avoid mixing events with other frames.
	Ok,
	Name,
	BaseModel,
	EndDate,
	NumIterations,
	PossibleDates,
};

EditProjectionFrame::EditProjectionFrame(Function function, OkCallback callback, ModelCollection const& models, Projection::Settings projectionSettings)
	: wxDialog(NULL, 0, (function == Function::New ? "New Projection" : "Edit Projection"), wxDefaultPosition, wxSize(375, 287)),
	callback(callback), models(models), projectionSettings(projectionSettings)
{
	// If a model has not been specified it should default to the first.
	if (this->projectionSettings.baseModel == ModelCollection::InvalidId) this->projectionSettings.baseModel = models.indexToId(0);

	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditProjectionFrame::createControls(int & y)
{
	createNameInput(y);
	createModelInput(y);
	createEndDateInput(y);
	createNumIterationsInput(y);
	createPossibleDatesInput(y);

	createOkCancelButtons(y);
}

void EditProjectionFrame::createNameInput(int & y)
{
	auto nameCallback = [this](std::string s) -> void {projectionSettings.name = s; };
	nameInput.reset(new TextInput(this, ControlId::Name, "Name:", projectionSettings.name, wxPoint(2, y), nameCallback));
	y += nameInput->Height + ControlPadding;
}

void EditProjectionFrame::createModelInput(int & y)
{
	wxArrayString modelArray;
	int selectedModel = 0;
	int count = 0;
	for (auto const& [key, model] : models) {
		modelArray.push_back(model.getName());
		if (key == projectionSettings.baseModel) selectedModel = count;
		++count;
	}

	auto modelCallback = [this](int i) {projectionSettings.baseModel = models.indexToId(i); };
	modelInput.reset(new ChoiceInput(this, ControlId::BaseModel, "Base model: ", modelArray, selectedModel,
		wxPoint(2, y), modelCallback));
	y += modelInput->Height + ControlPadding;
}

void EditProjectionFrame::createEndDateInput(int & y)
{
	auto endDateCallback = [this](wxDateTime const& d) -> void {projectionSettings.endDate = d; };
	endDateInput.reset(new DateInput(this, ControlId::EndDate, "End Date: ", projectionSettings.endDate,
		wxPoint(2, y), endDateCallback));
	y += endDateInput->Height + ControlPadding;
}

void EditProjectionFrame::createNumIterationsInput(int & y)
{
	auto numIterationsCallback = [this](int i) -> void {projectionSettings.numIterations = i; };
	auto numIterationsValidator = [](int i) {return std::max(1, i); };
	numIterationsInput.reset(new IntInput(this, ControlId::NumIterations, "Number of Iterations:", projectionSettings.numIterations,
		wxPoint(2, y), numIterationsCallback, numIterationsValidator));
	y += numIterationsInput->Height + ControlPadding;
}

void EditProjectionFrame::createPossibleDatesInput(int& y)
{
	std::string dates = "";
	if (projectionSettings.possibleDates.size()) {
		dates += projectionSettings.possibleDates[0].first + ":" + formatFloat(projectionSettings.possibleDates[0].second, 3);
		for (size_t i = 1; i < projectionSettings.possibleDates.size(); ++i) {
			dates += "," + projectionSettings.possibleDates[i].first + ":" + formatFloat(projectionSettings.possibleDates[i].second, 3);
		}
	}

	auto possibleDatesCallback = std::bind(&EditProjectionFrame::updatePossibleDates, this, _1);
	possibleDatesInput.reset(new TextInput(this, ControlId::PossibleDates, "Possible Dates:", dates, wxPoint(2, y), possibleDatesCallback));
	y += possibleDatesInput->Height + ControlPadding;
}

void EditProjectionFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditProjectionFrame::OnOK, this, Ok);
	y += TextInput::Height + ControlPadding;
}

void EditProjectionFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditProjectionFrame::OnOK(wxCommandEvent& WXUNUSED(event))\
{
	callback(projectionSettings);
	// Then close this dialog.
	Close();
}

void EditProjectionFrame::updatePossibleDates(std::string possibleDates)
{
	try {
		std::vector<std::pair<std::string, float>> dates;
		for (auto dateOdds : splitString(possibleDates, ",")) {
			auto split = splitString(dateOdds, ":");
			if (split.size() != 2) return;
			wxDateTime tempDate;
			if (!tempDate.ParseISODate(split[0])) continue;
			dates.push_back(std::pair(split[0], std::stof(split[1])));
		}
		projectionSettings.possibleDates = dates;
		PA_LOG_VAR(projectionSettings.possibleDates);
	}
	catch (std::invalid_argument) {
		// Just don't update the variable if the input isn't properly given
	}
}
