#pragma once

#include "Party.h"

#include <array>
#include <numeric>

class PollingProject;
struct Region;
class Seat;
class Simulation;
class SimulationRun;

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
	void determineBaseRegionalSwing(Region& thisRegion);
	void modifyLiveRegionalSwing(Region& thisRegion);
	void correctRegionalSwings(float tempOverallSwing);
	void determineSeatResult(Seat& seat);
	void determineClassicSeatResult(Seat& seat);
	void adjustClassicSeatResultFor3rdPlaceIndependent(Seat& seat);
	void adjustClassicSeatResultForBettingOdds(Seat& seat, SeatResult result);
	void determineNonClassicSeatResult(Seat& seat);
	void recordSeatResult(Seat& seat);
	void assignCountAsPartyWins();
	void assignSupportsPartyWins();
	void classifyMajorityResult();
	void addPartySeatWinCounts();

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateResultMatched2cp(Seat const& seat, float priorMargin);
	SeatResult calculateLiveAutomaticResultMatched2cp(Seat const& seat, float priorMargin);
	SeatResult calculateLiveManualResultMatched2cp(Seat const& seat, float priorMargin);
	float calculateSeatRemainingSwing(Seat const& seat, float priorMargin);
	BoothAccumulation sumMatched2cpBoothVotes(Seat const& seat, float priorMargin);
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

	SeatResult calculateLiveResultNonClassic2CP(Seat const& seat);
	SeatResult calculateResultUnmatched2cp(Seat const& seat);
	TcpTally sumUnmatched2cpBoothVotes(Seat const& seat);
	int estimateTotalVotes(Seat const& seat);
	void modifyUnmatchedTallyForRemainingVotes(Seat const& seat, TcpTally& tcpTally, int estimatedTotalVotes);
	
	struct SeatCandidate { int vote; Party::Id partyId; float weight; };
	typedef std::vector<SeatCandidate> SeatCandidates;

	SeatResult calculateLiveResultFromFirstPreferences(Seat const& seat);
	SeatCandidates collectSeatCandidates(Seat const& seat);
	void projectSeatCandidates(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes);

	Party::Id simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};