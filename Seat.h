#pragma once

#include "Debug.h"

struct Region;
struct Party;

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
	Region* region = nullptr;

	// Margin by which the incumbent holds the seat (and hence the swing required for it to fall).
	float margin = 0.0f;

	// Local modifier to the 2pp vote (towards the incumbent).
	float localModifier = 0.0f;

	// Betting odds on the incumbent and challenger - only necessary for non-classic seats
	float incumbentOdds = 2.0f;
	float challengerOdds = 2.0f;

	float projectedMargin = 0.0f;
	float incumbentWinPercent = 0.0f;
	float tippingPointPercent = 0.0f;

	int incumbentWins = 0;

	//float tempWinnerMargin = 0.0f;
	Party const* winner = nullptr;

	Party const* getLeadingParty() const {
		return (margin > 0.0f ? incumbent : challenger);
	}

	bool isClassic2pp(Party const* partyOne, Party const* partyTwo) const {
		return (incumbent == partyOne && challenger == partyTwo) ||
			(incumbent == partyTwo && challenger == partyOne);
	}

	float getMajorPartyWinRate(Party const* thisParty) const {
		return (incumbent == thisParty ? incumbentWinPercent : 100.0f - incumbentWinPercent);
	}
};