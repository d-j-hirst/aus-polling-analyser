#pragma once

#include "ElectionData.h"
#include "Party.h"
#include "Region.h"

#include <exception>
#include <optional>

class Result;

class Seat {

public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct PartiesNotSetException : public std::runtime_error {
		PartiesNotSetException() : std::runtime_error("") {}
	};

	Seat(std::string name) :
		name(name)
	{}

	Seat()
	{}

	std::string name = "";
	std::string previousName = "";

	Party::Id incumbent = Party::InvalidId;
	Party::Id challenger = Party::InvalidId;
	Party::Id challenger2 = Party::InvalidId;
	Region::Id region = Region::InvalidId;
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
	double simulatedMarginAverage = 0.0;

	// Local modifier to the 2pp vote (towards the incumbent).
	float localModifier = 0.0f;

	// Betting odds on the incumbent and challenger - only necessary for non-classic seats
	float incumbentOdds = 2.0f;
	float challengerOdds = 2.0f;
	float challenger2Odds = 1000.0f;

	float incumbentWinPercent = 0.0f;
	float tippingPointPercent = 0.0f;

	float liveBoothSwing = 0.0f;

	float firstPartyPreferenceFlow = 0.0f;
	float preferenceFlowVariation = 0.03f;

	// Party one probability is calculated as being 1 - the other two parties,
	// which allows for conveniently adding a party as certain to win a seat without needing any other input
	int livePartyOne = Party::InvalidId;
	int livePartyTwo = Party::InvalidId;
	int livePartyThree = Party::InvalidId;
	float partyOneProb() { return 1.0f - partyTwoProb - partyThreeProb; }
	float partyTwoProb = 0.0f;
	float partyThreeProb = 0.0f;
	bool overrideBettingOdds = false;

	int incumbentWins = 0;

	double partyOneWinRate = 0.0;
	double partyTwoWinRate = 0.0;
	double partyOthersWinRate = 0.0;

	//float tempWinnerMargin = 0.0f;
	Party::Id winner = Party::InvalidId;

	std::array<int, 2> tcpTally = { 0, 0 }; // cached data for simulations
	float individualBoothGrowth; // cached data for simulations

	Party::Id getLeadingParty() const {
		return (margin > 0.0f ? incumbent : challenger);
	}

	bool hasFpResults() const {
		if (!latestResults.has_value()) return false;
		return std::find_if(latestResults->fpCandidates.begin(), latestResults->fpCandidates.end(),
			[](Results::Seat::Candidate const& cand) {return cand.totalVotes() > 0; }) != latestResults->fpCandidates.end();
	}

	bool has2cpResults() const {
		if (!latestResults) return false;
		return latestResults->total2cpVotes();
	}

	bool hasLiveResults() const {
		if (!latestResults.has_value()) return false;
		if (has2cpResults()) return true;
		if (hasFpResults()) return !isClassic2pp(true);
		return false;
	}

	bool isClassic2pp(bool live) const {
		// If either incumbent or challenger is somehow invalid then throw an exception
		if (incumbent == Party::InvalidId || challenger == Party::InvalidId) throw PartiesNotSetException();
		if (live && has2cpResults()) {
			// First possibility, we have a live classic 2cp count and can booth-match it, then it is classic overall
			if (latestResults->classic2pp && previousResults.has_value() && previousResults->classic2pp) return true;
			// otherwise it wasn't classic one of the times so we can't use standard classic procedure
			return false;
		}
		if (live && hasFpResults()) {
			// Here we don't have any 2cp results, but if the fp results are determined to not be classic
			// (e.g. because it's been noted as a maverick seat) then we can't use standard procedure
			if (!latestResults->classic2pp) return false;
		}
		// Even if there are some first-preference results consistent with a classic 2cp
		// we can still manually override this if we judge otherwise
		if (live && livePartyOne) return false;
		// At this point, we have no 2cp results, and maybe some regular fp results,
		// and might not even be running live results yet,
		// so just go by the incumbent/challenger pairs recorded pre-election.
		return incumbent + challenger == 1;
	}

	float getMajorPartyWinRate(Party::Id thisParty) const {
		return (incumbent == thisParty ? incumbentWinPercent : 100.0f - incumbentWinPercent);
	}
};