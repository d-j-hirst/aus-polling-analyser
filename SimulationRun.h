#pragma once

#include "Party.h"

class PollingProject;
class RegionCollection;
class Seat;
class SeatCollection;
class Simulation;

class SimulationRun {
public:
	SimulationRun(PollingProject& project, Simulation& simulation) : project(project), sim(simulation) {}

	SimulationRun(SimulationRun const& otherRun) : project(otherRun.project), sim(otherRun.sim) {}
	SimulationRun operator=(SimulationRun const& otherRun) { return SimulationRun(otherRun.project, otherRun.sim); }

	void run();
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

	void resetRegionSpecificOutput();

	void resetSeatSpecificOutput();

	void accumulateRegionStaticInfo();

	void resetPpvcBiasAggregates();

	void cacheBoothData();

	void determinePpvcBias();

	void determinePreviousVoteEnrolmentRatios();

	void resizeRegionSeatCountOutputs();

	void countInitialRegionSeatLeads();

	void calculateTotalPopulation();

	void determineSeatCachedBoothData(Seat& seat);

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateLiveResultClassic2CP(Seat const& seat, float priorMargin);

	SeatResult calculateLiveResultNonClassic2CP(Seat const& seat);

	SeatResult calculateLiveResultFromFirstPreferences(Seat const& seat);

	Party::Id simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	PollingProject& project;

	Simulation& sim;

	int currentIteration = 0;

	float ppvcBias = 0.0f;
	float ppvcBiasNumerator = 0.0f;
	float ppvcBiasDenominator = 0.0f; // should be the total number of PPVC votes counted
	float ppvcBiasObserved = 0.0f;
	float ppvcBiasConfidence = 0.0f;
	int totalOldPpvcVotes = 0;

	float totalPopulation = 0.0f;

	float previousOrdinaryVoteEnrolmentRatio = 1.0f;
	float previousDeclarationVoteEnrolmentRatio = 1.0f;
};