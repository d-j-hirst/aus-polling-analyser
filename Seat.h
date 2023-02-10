#pragma once

#include "ElectionData.h"
#include "Party.h"
#include "PartyCollection.h"
#include "Region.h"
#include "RegionCollection.h"

#include <exception>
#include <optional>

class Outcome;

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

	// For whether to use TPP for major parties in live-manual non-classic contests
	enum class UseTpp {
		No, // Use probabilities exactly as entered (default)
		Yes, // Use TPP when the non-classic result gives a major party
		Always, // Override ANY results with the TPP.
	};

	std::string name = "";
	std::string previousName = "";
	std::string useFpResults = "";

	Party::Id incumbent = Party::InvalidId;
	Party::Id challenger = Party::InvalidId;
	Party::Id challenger2 = Party::InvalidId;
	Region::Id region = Region::InvalidId;

	// Margin by which Party One is favoured on TPP.
	float tppMargin = 0.0f;

	// Local modifier to the 2pp vote (towards the incumbent).
	float localModifier = 0.0f;

	// Previous TPP swing in this seat, to Party One.
	float previousSwing = 0.0f;

	// Betting odds on the incumbent and challenger - only necessary for non-classic seats
	float incumbentOdds = 2.0f;
	float challengerOdds = 2.0f;
	float challenger2Odds = 1000.0f;

	float incumbentWinPercent = 0.0f;
	float tippingPointPercent = 0.0f;

	float transposedTppSwing = 0.0f;

	float byElectionSwing = 0.0f;

	// Party one probability is calculated as being 1 - the other two parties,
	// which allows for conveniently adding a party as certain to win a seat without needing any other input
	int livePartyOne = Party::InvalidId;
	int livePartyTwo = Party::InvalidId;
	int livePartyThree = Party::InvalidId;
	float partyOneProb() const { return 1.0f - partyTwoProb - partyThreeProb; }
	float partyTwoProb = 0.0f;
	float partyThreeProb = 0.0f;
	bool overrideBettingOdds = false;
	UseTpp liveUseTpp = UseTpp::No;

	bool sophomoreCandidate = false;
	bool sophomoreParty = false;
	bool disendorsement = false;
	bool retirement = false;
	bool previousDisendorsement = false;
	bool incumbentRecontestConfirmed = false;
	bool confirmedProminentIndependent = false;
	bool previousIndRunning = false; // Only used for independents who didn't win last time.

	float knownPrepollPercent = 0.0f;
	float knownPostalPercent = 0.0f;

	int knownAbsentCount = 0;
	int knownProvisionalCount = 0;
	int knownDecPrepollCount = 0;
	int knownPostalCount = 0;

	std::vector<std::string> prominentMinors;
	std::vector<std::string> runningParties;

	std::map<std::string, float> bettingOdds;

	// first argument is party of challenger, incumbent being the other party is assumed
	// expressed as the tcp change of the incumbent vs this candidate
	std::map<std::string, float> tcpChange;

	// Rating of minor/independent candidates' viability based on known factors
	// 0 is default, positive number indicates increased viability
	// Each of following increases viability by 1:
	// - Previously ran as mayor/councillor
	// - For independents, backing of a significant funding/mobilisation group
	// - Candidates with major profile outside politics (*2 for being extremely well-known)
	// - Previously stood as IND or pseudo-IND for state/federal seat with >10% vote
	// These are meant to be a rought indicator and will be largely overridden by betting odds
	// and/or polls once available.
	std::map<std::string, float> minorViability;

	// outer string = party abbr, inner float = fp, inner int = credibility as for national poll
	std::map<std::string, std::vector<std::pair<float, int>>> polls;

	// Known candidate names (doesn't have do be 100% guaranteed, just confirmed as running
	// for now, or the incumbent if they haven't announced retirement etc.)
	std::map<std::string, std::string> candidateNames;

	Party::Id getLeadingParty() const {
		if (isClassic2pp()) {
			return (tppMargin > 0.0f ? 0 : 1);
		}
		else {
			return incumbent;
		}
	}

	bool hasFpResults() const {
		return false;
		//if (!latestResults.has_value()) return false;
		//return std::find_if(latestResults->fpCandidates.begin(), latestResults->fpCandidates.end(),
		//	[](Results::Seat::Candidate const& cand) {return cand.totalVotes() > 0; }) != latestResults->fpCandidates.end();
	}

	bool has2cpResults() const {
		return false;
		//if (!latestResults) return false;
		//return latestResults->total2cpVotes();
	}

	bool hasLiveResults() const {
		return false;
		//if (!latestResults.has_value()) return false;
		//if (has2cpResults()) return true;
		//if (hasFpResults()) return !isClassic2pp(true);
		//return false;
	}

	bool isClassic2pp() const {
		// If either incumbent or challenger is somehow invalid then throw an exception
		if (incumbent == Party::InvalidId || challenger == Party::InvalidId) throw PartiesNotSetException();
		return incumbent + challenger == 1;

		//if (live && has2cpResults()) {
		//	// First possibility, we have a live classic 2cp count and can booth-match it, then it is classic overall
		//	if (latestResults->classic2pp && previousResults.has_value() && previousResults->classic2pp) return true;
		//	// otherwise it wasn't classic one of the times so we can't use standard classic procedure
		//	return false;
		//}
		//if (live && hasFpResults()) {
		//	// Here we don't have any 2cp results, but if the fp results are determined to not be classic
		//	// (e.g. because it's been noted as a maverick seat) then we can't use standard procedure
		//	if (!latestResults->classic2pp) return false;
		//}
		//// Even if there are some first-preference results consistent with a classic 2cp
		//// we can still manually override this if we judge otherwise
		//if (live && livePartyOne != Party::InvalidId) return false;
		//// At this point, we have no 2cp results, and maybe some regular fp results,
		//// and might not even be running live results yet,
		//// so just go by the incumbent/challenger pairs recorded pre-election.
		//return incumbent + challenger == 1;
	}

	float getMajorPartyWinRate(Party::Id thisParty) const {
		return (incumbent == thisParty ? incumbentWinPercent : 100.0f - incumbentWinPercent);
	}

	void resetLiveData() {
		livePartyOne = Party::InvalidId;
		livePartyTwo = Party::InvalidId;
		livePartyThree = Party::InvalidId;
		partyTwoProb = 0.0f;
		partyThreeProb = 0.0f;
		overrideBettingOdds = false;
		liveUseTpp = UseTpp::No;
	}

	std::string textReport(PartyCollection const& parties, RegionCollection const& regions) const {
		std::stringstream report;
		report << "Reporting Seat: \n";
		report << " Name: " << name << "\n";
		report << " Previous Name: " << previousName << "\n";
		report << " Incumbent Party: " << parties.view(incumbent).name << "\n";
		report << " Challenger Party: " << parties.view(challenger).name << "\n";
		if (challenger2 != Party::InvalidId) report << " Challenger Party 2: " << parties.view(challenger2).name << "\n";
		report << " Last election 2pp: " << regions.view(region).name << "\n";
		report << " Margin: " << tppMargin << "\n";
		report << " Local Modifier: " << localModifier << "\n";
		report << " Incumbent Betting Odds: " << incumbentOdds << "\n";
		report << " Challenger Betting Odds: " << challengerOdds << "\n";
		report << " Challenger 2 Betting Odds: " << challenger2Odds << "\n";
		report << " Incumbent Win %: " << incumbentWinPercent << "\n";
		report << " Tipping Point %: " << tippingPointPercent << "\n";
		if (livePartyOne != Party::InvalidId) report << " Live Party One: " << parties.view(livePartyOne).name << "\n";
		if (livePartyTwo != Party::InvalidId) report << " Live Party Two: " << parties.view(livePartyTwo).name << "\n";
		if (livePartyThree != Party::InvalidId) report << " Live Party Three: " << parties.view(livePartyThree).name << "\n";
		report << " Party Two Prob: " << partyTwoProb << "\n";
		report << " Party Three Prob: " << partyThreeProb << "\n";
		report << " Override Betting Odds: " << overrideBettingOdds << "\n";
		return report.str();
	}
};