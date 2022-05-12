#pragma once

#include "Party.h"
#include "SimulationRun.h"

#include <array>
#include <map>
#include <numeric>
#include <vector>

class PollingProject;
struct Region;
class Seat;
class Simulation;

class SimulationIteration {
public:
	SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run);

	void runIteration();
private:

	typedef std::array<int, 2> TcpTally;

	struct SeatResult {
		Party::Id winner = Party::InvalidId;
		Party::Id runnerUp = Party::InvalidId;
		float margin = 0.0f;
		float significance = 0.0f;
	};

	struct OddsInfo {
		float incumbentChance = 1.0f;
		float topTwoChance = 1.0f;
	};

	struct BoothAccumulation {
		enum MysteryBoothType {
			Standard,
			Ppvc,
			Team
		};
		typedef std::array<int, MysteryBoothType::Team + 1> MysteryBoothCounts;
		MysteryBoothCounts mysteryBoothCounts = { 0, 0, 0 };
		TcpTally tcpTally = { 0, 0 };

		bool hasMysteryBooths() { return std::accumulate(mysteryBoothCounts.begin(), mysteryBoothCounts.end(), 0); }
	};


	void initialiseIterationSpecificCounts();
	void determineOverallBehaviour();
	void decideMinorPartyPopulism();
	void determineHomeRegions();
	void determineRegionalSwings();
	void determineMinorPartyContests();
	void loadPastSeatResults();
	void determineBaseRegionalSwing(int regionIndex);
	void modifyLiveRegionalSwing(int regionIndex);
	void correctRegionalSwings();
	void determineSeatInitialResults();
	void correctSeatTppSwings();
	void determineSeatTpp(int seatIndex);
	void determineSeatInitialFp(int seatIndex);
	void determineSpecificPartyFp(int seatIndex, int partyIndex, float& voteShare, SimulationRun::SeatStatistics const seatStatistics);
	void determinePopulistFp(int seatIndex, int partyIndex, float& voteShare);
	void determineSeatConfirmedInds(int seatIndex);
	void determineSeatEmergingInds(int seatIndex);
	void determineSeatOthers(int seatIndex);
	void incorporateLiveSeatFps(int seatIndex);
	void prepareFpsForNormalisation(int seatIndex);
	void determineSeatEmergingParties(int seatIndex);
	void allocateMajorPartyFp(int seatIndex);
	void normaliseSeatFp(int seatIndex, int fixedParty = -10000, float fixedVote = 0.0f);
	void reconcileSeatAndOverallFp();
	void calculateNewFpVoteTotals();
	void calculatePreferenceCorrections();
	void applyCorrectionsToSeatFps();
	void correctMajorPartyFpBias();
	void determineSeatFinalResult(int seatIndex);
	void applyLiveManualOverrides(int seatIndex);
	void recordSeatResult(int seatIndex);
	void assignDirectWins();
	void assignCountAsPartyWins();
	void assignSupportsPartyWins();
	void recordMajorityResult();
	void recordPartySeatWinCounts();
	void recordSeatPartyWinner(int seatIndex);
	void recordSeatFpVotes(int seatIndex);
	void recordSeatTcpVotes(int seatIndex);
	void recordIterationResults();
	void recordVoteTotals();
	void recordSwings();

	OddsInfo calculateOddsInfo(Seat const& thisSeat);
	
	struct SeatCandidate { int vote; Party::Id partyId; float weight; };
	typedef std::vector<SeatCandidate> SeatCandidates;

	float calculateEffectiveSeatModifier(int seatIndex, int partyIndex) const;

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;

	typedef std::map<int, float> FloatByPartyIndex;

	// iteration-specific variables
	std::vector<SimulationRun::PastSeatResult> pastSeatResults;
	std::map<int, std::vector<int>> regionSeatCount; // party, then region
	std::map<int, int> partyWins;
	std::vector<float> regionSwing;
	std::vector<float> partyOneNewTppMargin;
	std::vector<Party::Id> seatWinner;
	std::vector<FloatByPartyIndex> seatFpVoteShare;
	std::vector<std::pair<std::pair<int, int>, float>> seatTcpVoteShare;
	float iterationOverallTpp = 0.0f;
	float iterationOverallSwing = 0.0f;
	int daysToElection = 0;
	FloatByPartyIndex overallFpTarget;
	FloatByPartyIndex overallFpSwing;
	FloatByPartyIndex previousPreferenceFlow;
	FloatByPartyIndex overallPreferenceFlow;
	std::map<Party::Id, int> homeRegion;
	std::map<Party::Id, std::vector<bool>> seatContested;
	FloatByPartyIndex centristPopulistFactor; // e.g. 1 = full populist, 0 = full centrist
	std::map<Party::Id, int> partyIdeologies;
	std::map<Party::Id, int> partyConsistencies;
	FloatByPartyIndex fpModificationAdjustment;
	FloatByPartyIndex tempOverallFp;
	float prefCorrection = 0.0f;
	float overallFpError = 0.0f;
	float nonMajorFpError = 0.0f;
	float othersCorrectionFactor = 0.0f;
	std::map<int, float> preferenceVariation; // in minor -> major preferences, after transformation

	std::array<int, 2> partySupport = std::array<int, 2>();
};