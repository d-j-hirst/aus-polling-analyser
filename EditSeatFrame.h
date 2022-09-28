#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "Seat.h"
#include "PartyCollection.h"
#include "RegionCollection.h"

class CheckInput;
class ChoiceInput;
class FloatInput;
class TextInput;
class PartyCollection;
class RegionCollection;

// *** EditSeatFrame ***
// Frame that allows the user to edit an already-existing seat
// or create a new one
class EditSeatFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Seat)> OkCallback;

	EditSeatFrame(Function function, OkCallback callback, PartyCollection const& parties, RegionCollection const& regions,
		Seat seat = Seat());

private:

	void validateSeatParties();

	void createControls(int& y);

	void createNameInput(int& y);
	void createPreviousNameInput(int& y);
	void createUseFpResultsInput(int& y);
	void createIncumbentInput(int& y);
	void createChallengerInput(int& y);
	void createChallenger2Input(int& y);
	void createRegionInput(int& y);
	void createMarginInput(int& y);
	void createPreviousSwingInput(int& y);
	void createLocalModifierInput(int& y);
	void createTransposedTppSwingInput(int& y);
	void createIncumbentOddsInput(int& y);
	void createChallengerOddsInput(int& y);
	void createChallenger2OddsInput(int& y);
	void createSophomoreCandidateInput(int& y);
	void createSophomorePartyInput(int& y);
	void createRetirementInput(int& y);
	void createDisendorsementInput(int& y);
	void createPreviousDisendorsementInput(int& y);
	void createIncumbentRecontestConfirmedInput(int& y);
	void createConfirmedProminentIndependentInput(int& y);
	void createProminentMinorsInput(int& y);
	void createBettingOddsInput(int& y);
	void createPollsInput(int& y);
	void createTcpChangeInput(int& y);
	void createMinorViabilityInput(int& y);
	void createCandidateNamesInput(int& y);
	void createRunningPartiesInput(int& y);
	void createPreviousIndRunningInput(int& y);
	void createKnownPrepollsInput(int& y);
	void createKnownPostalsInput(int& y);
	void createKnownAbsentCountInput(int& y);
	void createKnownProvisionalCountInput(int& y);
	void createKnownDecPrepollCountInput(int& y);
	void createKnownPostalCountInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	wxArrayString collectPartyStrings();

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Callbacks for the controls to update the seat data.
	void updateProminentMinors(std::string prominentMinors);
	void updateBettingOdds(std::string bettingOddsInput);
	void updatePolls(std::string pollsInput);
	void updateRunningParties(std::string runningParties);
	void updateTcpChange(std::string tcpChangeInput);
	void updateMinorViability(std::string minorViabilityInput);
	void updateCandidateNames(std::string candidateNamesInput);

	// Data container for the preliminary settings for the seat to be created.
	Seat seat;

	PartyCollection const& parties;
	RegionCollection const& regions;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<TextInput> previousNameInput;
	std::unique_ptr<TextInput> useFpResultsInput;
	std::unique_ptr<ChoiceInput> incumbentInput;
	std::unique_ptr<ChoiceInput> challengerInput;
	std::unique_ptr<ChoiceInput> challenger2Input;
	std::unique_ptr<ChoiceInput> regionInput;
	std::unique_ptr<FloatInput> marginInput;
	std::unique_ptr<FloatInput> previousSwingInput;
	std::unique_ptr<FloatInput> localModifierInput;
	std::unique_ptr<FloatInput> transposedTppSwingInput;
	std::unique_ptr<FloatInput> incumbentOddsInput;
	std::unique_ptr<FloatInput> challengerOddsInput;
	std::unique_ptr<FloatInput> challenger2OddsInput;
	std::unique_ptr<CheckInput> sophomoreCandidateInput;
	std::unique_ptr<CheckInput> sophomorePartyInput;
	std::unique_ptr<CheckInput> retirementInput;
	std::unique_ptr<CheckInput> disendorsementInput;
	std::unique_ptr<CheckInput> previousDisendorsementInput;
	std::unique_ptr<CheckInput> incumbentRecontestConfirmedInput;
	std::unique_ptr<CheckInput> confirmedProminentIndependentInput;
	std::unique_ptr<CheckInput> previousIndRunningInput;
	std::unique_ptr<TextInput> prominentMinorsInput;
	std::unique_ptr<TextInput> bettingOddsInput;
	std::unique_ptr<TextInput> tcpChangeInput;
	std::unique_ptr<TextInput> minorViabilityInput;
	std::unique_ptr<TextInput> candidateNamesInput;
	std::unique_ptr<TextInput> pollsInput;
	std::unique_ptr<TextInput> runningPartiesInput;
	std::unique_ptr<FloatInput> knownPrepollsInput;
	std::unique_ptr<FloatInput> knownPostalsInput;
	std::unique_ptr<FloatInput> knownAbsentCountInput;
	std::unique_ptr<FloatInput> knownProvisionalCountInput;
	std::unique_ptr<FloatInput> knownDecPrepollCountInput;
	std::unique_ptr<FloatInput> knownPostalCountInput;

	wxButton* okButton;
	wxButton* cancelButton;

	OkCallback okCallback;
};