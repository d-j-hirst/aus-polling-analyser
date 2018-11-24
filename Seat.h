#pragma once

#include "Debug.h"

struct Region;
struct Party;
class Result;

class Seat {

public:
	Seat(std::string name) :
		name(name)
	{}

	Seat()
	{}

	std::string name = "";

	Party const* incumbent = nullptr;
	Party const* challenger = nullptr;
	Party const* challenger2 = nullptr;
	Region* region = nullptr;
	Result const* latestResult = nullptr;

	// Margin by which the incumbent holds the seat (and hence the swing required for it to fall).
	float margin = 0.0f;

	// Margin by which the seat is expected to be on after simulation
	// Note this variable holds the accumulated sum during simulations
	// and is only divided to form the average once simulations are complete
	float simulatedMarginAverage = 0.0f;

	// Local modifier to the 2pp vote (towards the incumbent).
	float localModifier = 0.0f;

	// Betting odds on the incumbent and challenger - only necessary for non-classic seats
	float incumbentOdds = 2.0f;
	float challengerOdds = 2.0f;
	float challenger2Odds = 1000.0f;

	float incumbentWinPercent = 0.0f;
	float tippingPointPercent = 0.0f;

	// Party one probability is calculated as being 1 - the other two parties,
	// which allows for conveniently adding a party as certain to win a seat without needing any other input
	Party const* livePartyOne = nullptr;
	Party const* livePartyTwo = nullptr;
	Party const* livePartyThree = nullptr;
	float partyOneProb() { return 1.0f - partyTwoProb - partyThreeProb; }
	float partyTwoProb = 0.0f;
	float partyThreeProb = 0.0f;
	bool overrideBettingOdds = false;

	int incumbentWins = 0;

	//float tempWinnerMargin = 0.0f;
	Party const* winner = nullptr;

	Party const* getLeadingParty() const {
		return (margin > 0.0f ? incumbent : challenger);
	}

	bool isClassic2pp(Party const* partyOne, Party const* partyTwo) const {
		if (livePartyOne) return false;
		if (overrideBettingOdds) return true;
		return (incumbent == partyOne && challenger == partyTwo) ||
			(incumbent == partyTwo && challenger == partyOne);
	}

	float getMajorPartyWinRate(Party const* thisParty) const {
		return (incumbent == thisParty ? incumbentWinPercent : 100.0f - incumbentWinPercent);
	}
};