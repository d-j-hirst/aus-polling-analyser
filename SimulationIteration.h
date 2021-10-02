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
	void determineIterationOverallSwing();
	void determineIterationPpvcBias();
	void determineIterationRegionalSwings();
	void determineBaseRegionalSwing(int regionIndex);
	void modifyLiveRegionalSwing(int regionIndex);
	void correctRegionalSwings();
	void determineSeatInitialResult(int seatIndex);
	void determineSeatTpp(int seatIndex);
	void determineSeatInitialFp(int seatIndex);
	void determineSpecificPartyFp(int seatIndex, int partyIndex, float& voteShare, SimulationRun::SeatStatistics const seatStatistics);
	void determineSeatEmergingInds(int seatIndex);
	void allocateMajorPartyFp(int seatIndex);
	void normaliseSeatFp(int seatIndex);
	void calculateNewFpVoteTotals();
	void adjustClassicSeatResultFor3rdPlaceIndependent(int seatIndex);
	void adjustClassicSeatResultForBettingOdds(int seatIndex, SeatResult result);
	void determineNonClassicSeatResult(int seatIndex);
	void determineSeatFinalResult(int seatIndex);
	void recordSeatResult(int seatIndex);
	void assignDirectWins();
	void assignCountAsPartyWins();
	void assignSupportsPartyWins();
	void recordMajorityResult();
	void recordPartySeatWinCounts();
	void recordSeatPartyWinner(int seatIndex);
	void recordSeatFpVotes(int seatIndex);
	void recordIterationResults();
	void recordVoteTotals();
	void recordSwings();

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateResultMatched2cp(int seatIndex, float priorMargin);
	SeatResult calculateLiveAutomaticResultMatched2cp(int seatIndex, float priorMargin);
	SeatResult calculateLiveManualResultMatched2cp(int seatIndex, float priorMargin);
	float calculateSeatRemainingSwing(Seat const& seat, float priorMargin);
	BoothAccumulation sumMatched2cpBoothVotes(int seatIndex, float priorMargin);
	void estimateMysteryBoothVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes);
	// Resturns an estimate of the total votes in this seat
	int estimateDeclarationVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange);
	int estimateDeclarationVotesUsingPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange);
	int estimateDeclarationVotesWithoutPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes);
	float estimateDeclarationVoteProportionalChange(Seat const& seat, int estimatedTotalVotes, int estimatedTotalOrdinaryVotes);
	float calculateOrdinaryVoteSwing(Seat const& seat, BoothAccumulation const& boothResults);
	TcpTally estimateAbsentVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange);
	TcpTally estimateProvisionalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange);
	TcpTally estimatePrepollVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange);
	TcpTally estimatePostalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange);

	SeatResult calculateLiveResultNonClassic2CP(int seatIndex);
	SeatResult calculateResultUnmatched2cp(int seatIndex);
	TcpTally sumUnmatched2cpBoothVotes(int seatIndex);
	int estimateTotalVotes(Seat const& seat);
	void modifyUnmatchedTallyForRemainingVotes(Seat const& seat, TcpTally& tcpTally, int estimatedTotalVotes);
	
	struct SeatCandidate { int vote; Party::Id partyId; float weight; };
	typedef std::vector<SeatCandidate> SeatCandidates;

	SeatResult calculateLiveResultFromFirstPreferences(Seat const& seat);
	SeatCandidates collectSeatCandidates(Seat const& seat);
	void projectSeatCandidatePrimaries(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes);
	void distributePreferences(SeatCandidates& candidates);
	SeatResult determineResultFromDistribution(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes);

	Party::Id simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;

	// iteration-specific variables
	std::vector<std::vector<int>> regionSeatCount;
	std::vector<int> partyWins;
	std::vector<float> regionSwing;
	std::vector<float> incumbentNewMargin;
	std::vector<Party::Id> seatWinner;
	std::vector<std::map<Party::Id, float>> seatFpVoteShare;
	float iterationOverallTpp = 0.0f;
	float iterationOverallSwing = 0.0f;
	std::map<Party::Id, float> overallFp;
	std::map<Party::Id, float> overallFpSwing;
	float ppvcBias = 0.0f;

	std::array<int, 2> partySupport = std::array<int, 2>();
};