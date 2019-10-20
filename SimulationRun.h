#pragma once

#include "Party.h"

class PollingProject;
class RegionCollection;
class Seat;
class Simulation;

class SimulationRun {
public:
	SimulationRun(Simulation& simulation) : sim(simulation) {}

	SimulationRun(SimulationRun const& otherRun) : sim(otherRun.sim) {}
	SimulationRun operator=(SimulationRun const& otherRun) { return SimulationRun(otherRun.sim); }

	void run(PollingProject& project);
private:

	struct OddsInfo {
		float incumbentChance = 1.0f;
		float topTwoChance = 1.0f;
	};

	struct SeatResult {
		Party::Id winner = Party::InvalidId;
		Party::Id runnerUp = Party::InvalidId;
		float margin = 0.0f;
		float significance = 0.0f;
	};

	void resetRegionOutput(RegionCollection& regions);

	void determinePpvcBias();

	void determinePreviousVoteEnrolmentRatios(PollingProject& project);

	void determineSeatCachedBoothData(PollingProject const& project, Seat& seat);

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateLiveResultClassic2CP(PollingProject const& project, Seat const& seat, float priorMargin);

	SeatResult calculateLiveResultNonClassic2CP(PollingProject const& project, Seat const& seat);

	SeatResult calculateLiveResultFromFirstPreferences(PollingProject const& project, Seat const& seat);

	Party::Id simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(PollingProject const & project, Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	Simulation& sim;

	int currentIteration = 0;

	float ppvcBias = 0.0f;
	float ppvcBiasNumerator = 0.0f;
	float ppvcBiasDenominator = 0.0f; // should be the total number of PPVC votes counted
	float ppvcBiasObserved = 0.0f;
	float ppvcBiasConfidence = 0.0f;
	int totalOldPpvcVotes = 0;

	float previousOrdinaryVoteEnrolmentRatio = 1.0f;
	float previousDeclarationVoteEnrolmentRatio = 1.0f;
};