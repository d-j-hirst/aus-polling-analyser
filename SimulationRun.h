#pragma once

#include "Party.h"

#include <array>

class PollingProject;
class RegionCollection;
class Seat;
class SeatCollection;
class Simulation;

class SimulationRun {
public:
	friend class SimulationPreparation;

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


	// simulation functions
	void runIterations();
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

	// statistic calculations
	void calculateIndividualSeatStatistics();
	void calculateWholeResultStatistics();
	void calculatePartyWinExpectations();
	void calculateRegionPartyWinExpectations();
	void recordProbabilityBands();
	void createClassicSeatsList();
	void calculateStatistics();

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

	float liveOverallSwing = 0.0f; // swing to partyOne
	float liveOverallPercent = 0.0f;
	float classicSeatCount = 0.0f;
	// A bunch of votes from one seat is less likely to be representative than from a wide variety of seats,
	// so this factor is introduced to avoid a small number of seats from having undue influence early in the count
	float sampleRepresentativeness = 0.0f;
	int total2cpVotes = 0;
	int totalEnrolment = 0;

	std::array<int, 2> partyMajority;
	std::array<int, 2> partyMinority;
	int hungParliament;

	float pollOverallSwing;
	float pollOverallStdDev;

	// iteration-specific variables
	std::vector<std::vector<int>> regionSeatCount;
	std::vector<int> partyWins;
	float iterationOverallSwing;

	std::array<int, 2> partySupport;
};