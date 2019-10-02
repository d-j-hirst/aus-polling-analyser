#include "EditPollFrame.h"

#include "ChoiceInput.h"
#include "DateInput.h"
#include "FloatInput.h"
#include "General.h"
#include "PartyCollection.h"
#include "PollsterCollection.h"

constexpr int ControlPadding = 4;

enum ControlId
{
	Ok,
	PollsterSelection,
	Date,
	Reported2pp,
	Respondent2pp,
	Calc2pp,
	Primary, // this must be last as all the primary vote text boxes will follow ...
};

EditPollFrame::EditPollFrame(Function function, OkCallback callback, PartyCollection const& parties, PollsterCollection const& pollsters, Poll poll)
	: wxDialog(NULL, 0, (function == Function::New ? "New Poll" : "Edit Poll")),
	poll(poll), callback(callback), parties(parties), pollsters(pollsters)
{
	int currentY = ControlPadding;
	createControls(currentY);
	setFinalWindowHeight(currentY);
}

void EditPollFrame::createControls(int & y)
{
	createPollsterInput(y);
	createDateInput(y);
	createReported2ppInput(y);
	createRespondent2ppInput(y);
	createPrimaryInputs(y);
	createCalc2ppLabel(y);

	createOkCancelButtons(y);
}

void EditPollFrame::createPollsterInput(int & y)
{
	wxArrayString pollsterArray;
	for (auto [key, pollster] : pollsters) {
		pollsterArray.push_back(pollster.name);
	}
	int selectedPollster = pollsters.idToIndex(poll.pollster);

	auto pollsterCallback = [this](int i) {poll.pollster = pollsters.indexToId(i); };
	pollsterInput.reset(new ChoiceInput(this, ControlId::PollsterSelection, "Pollster: ", pollsterArray, selectedPollster,
		wxPoint(2, y), pollsterCallback));
	y += pollsterInput->Height + ControlPadding;
}

void EditPollFrame::createDateInput(int & y)
{
	auto dateCallback = [this](wxDateTime d) {poll.date = d; };
	dateInput.reset(new DateInput(this, ControlId::Date, "Date: ", poll.date,
		wxPoint(2, y), dateCallback));
	y += dateInput->Height + ControlPadding;
}

void EditPollFrame::createReported2ppInput(int & y)
{
	auto reported2ppCallback = [this](float f) -> void {poll.reported2pp = f; };
	auto reported2ppValidator = [](float f) {if (f == Poll::NullValue) return f; return std::clamp(f, 0.0f, 100.0f); };
	reported2ppInput.reset(new FloatInput(this, ControlId::Reported2pp, "Reported 2PP:", poll.reported2pp,
		wxPoint(2, y), reported2ppCallback, reported2ppValidator, Poll::NullValue));
	y += reported2ppInput->Height + ControlPadding;
}

void EditPollFrame::createRespondent2ppInput(int & y)
{
	auto respondent2ppCallback = [this](float f) -> void {poll.respondent2pp = f; };
	auto respondent2ppValidator = [](float f) {if (f == Poll::NullValue) return f; return std::clamp(f, 0.0f, 100.0f); };
	respondent2ppInput.reset(new FloatInput(this, ControlId::Respondent2pp, "Respondent-allocated 2PP:", poll.respondent2pp,
		wxPoint(2, y), respondent2ppCallback, respondent2ppValidator, Poll::NullValue));
	y += respondent2ppInput->Height + ControlPadding;
}

void EditPollFrame::createPrimaryInputs(int & y)
{
	constexpr float labelWidth = 250;
	constexpr float inputWidth = 100;
	auto primaryValidator = [](float f) {if (f == Poll::NullValue) return f; return std::clamp(f, 0.0f, 100.0f); };
	for (int i = 0; i < parties.count(); i++) {
		std::string primaryString = poll.getPrimaryString(i);
		std::string partyString = parties.viewByIndex(i).name + " primary vote:";

		auto primaryCallback = [this, i](float f) -> void {poll.primary[i] = f; refreshCalculated2PP(); };
		primaryVoteInput.emplace_back(std::make_unique<FloatInput>(this, ControlId::Primary + i, partyString, poll.primary[i],
			wxPoint(2, y), primaryCallback, primaryValidator, Poll::NullValue, labelWidth, inputWidth));

		y += respondent2ppInput->Height + ControlPadding;
	}

	auto othersCallback = [this](float f) -> void {poll.primary[PartyCollection::MaxParties] = f; refreshCalculated2PP(); };
	primaryVoteInput.emplace_back(std::make_unique<FloatInput>(this, ControlId::Primary + PartyCollection::MaxParties,
		"Others primary vote:", poll.primary[PartyCollection::MaxParties],
		wxPoint(2, y), othersCallback, primaryValidator, Poll::NullValue, labelWidth, inputWidth));

	y += respondent2ppInput->Height + ControlPadding;
}

void EditPollFrame::createCalc2ppLabel(int & y)
{
	// *** need to replace this with a proper class for handling a double label.

	// Create the controls for the estimated 2pp. This can't be edited by the user.
	calc2ppStaticText = new wxStaticText(this, 0, "Calculated 2PP:", wxPoint(2, y + ControlPadding), wxSize(198, 23));
	calc2ppNumberText = new wxStaticText(this, ControlId::Calc2pp, poll.getCalc2ppString(),
		wxPoint(200, y + ControlPadding), wxSize(120, 23));

	y += 27;
}

void EditPollFrame::createOkCancelButtons(int & y)
{
	// Create the OK and cancel buttons.
	okButton = new wxButton(this, ControlId::Ok, "OK", wxPoint(67, y), wxSize(100, FloatInput::Height));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, y), wxSize(100, FloatInput::Height));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &EditPollFrame::OnOK, this, ControlId::Ok);
	y += FloatInput::Height + ControlPadding;
}

void EditPollFrame::setFinalWindowHeight(int y)
{
	SetClientSize(wxSize(GetClientSize().x, y));
}

void EditPollFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	callback(poll);

	// Then close this dialog.
	Close();
}

void EditPollFrame::refreshCalculated2PP() {
	// *** need to figure out a way to recalculate poll 2pp without invoking the entire project
	parties.recalculatePollCalc2PP(poll);
	calc2ppNumberText->SetLabel(poll.getCalc2ppString());
}