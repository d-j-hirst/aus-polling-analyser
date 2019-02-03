#pragma once

#include "Debug.h"
#include "ElectionData.h"
#include "Party.h"

#include <optional>

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
	std::string previousName = "";

	Party const* incumbent = nullptr;
	Party const* challenger = nullptr;
	Party const* challenger2 = nullptr;
	Region* region = nullptr;
	Result const* latestResult = nullptr; // used as a temporary in simulations for storing the latest live result

	// Official seat ID from the electoral commission
	int officialId = -1;

	std::optional<Results::Seat> previousResults;
	std::optional<Results::Seat> latestResults;

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

	float liveBoothSwing = 0.0f;

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

	double partyOneWinRate = 0.0;
	double partyTwoWinRate = 0.0;
	double partyOthersWinRate = 0.0;

	//float tempWinnerMargin = 0.0f;
	Party const* winner = nullptr;

	std::array<int, 2> tcpTally = { 0, 0 }; // cached data for simulations
	float individualBoothGrowth; // cached data for simulations

	Party const* getLeadingParty() const {
		return (margin > 0.0f ? incumbent : challenger);
	}

	bool hasFpResults() const {
		if (!latestResults.has_value()) return false;
		return std::find_if(latestResults->fpCandidates.begin(), latestResults->fpCandidates.end(),
			[](Results::Candidate const& cand) {return cand.totalVotes() > 0; }) != latestResults->fpCandidates.end();
	}

	bool has2cpResults() const {
		return latestResults->total2cpVotes();
	}

	bool hasLiveResults(Party const* partyOne, Party const* partyTwo) const {
		if (!latestResults.has_value()) return false;
		if (has2cpResults()) return true;
		if (hasFpResults()) return !isClassic2pp(partyOne, partyTwo, true);
		return false;
	}

	bool isClassic2pp(Party const* partyOne, Party const* partyTwo, bool live) const {
		if (live && has2cpResults()) {
			if (latestResults->classic2pp && previousResults.has_value() && previousResults->classic2pp) return true;
			return false;
		}
		else if (live && hasFpResults()) {
			if (!latestResults->classic2pp) return false;
			return true;
		}
		else {
			if (livePartyOne) return false;
			return (incumbent == partyOne && challenger == partyTwo) ||
				(incumbent == partyTwo && challenger == partyOne);
		}
	}

	float getMajorPartyWinRate(Party const* thisParty) const {
		return (incumbent == thisParty ? incumbentWinPercent : 100.0f - incumbentWinPercent);
	}
};