#pragma once

#include "Party.h"

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;

class SimulationIteration {
public:
	SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run);

	void runIteration();
private:

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

	void initialiseIterationSpecificCounts();
	void determineIterationOverallSwing();
	void determineIterationPpvcBias();
	void determineIterationRegionalSwings();
	void correctRegionalSwings(float tempOverallSwing);
	void determineSeatResult(Seat& seat);
	void determineClassicSeatResult(Seat& seat);
	void adjustClassicSeatResultForBettingOdds(Seat& seat, SeatResult result);
	void determineNonClassicSeatResult(Seat& seat);
	void recordSeatResult(Seat& seat);
	void assignCountAsPartyWins();
	void assignSupportsPartyWins();
	void classifyMajorityResult();
	void addPartySeatWinCounts();

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateLiveResultClassic2CP(Seat const& seat, float priorMargin);

	SeatResult calculateLiveResultNonClassic2CP(Seat const& seat);

	SeatResult calculateLiveResultFromFirstPreferences(Seat const& seat);

	Party::Id simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};