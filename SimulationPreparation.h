#pragma once

#include <utility>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;

class SimulationPreparation {
public:
	SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run);
	void prepareForIterations();
private:

	typedef std::pair<int, int> SeatPartyPreferences;

	// initialization functions
	void resetLatestReport();
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
	void calculateLiveAggregates();
	void updateLiveAggregateForSeat(Seat& seat);
	void finaliseLiveAggregates();
	void resetResultCounts();
	void determineSeatCachedBoothData(int seatIndex);
	SeatPartyPreferences aggregateVoteData(int seatIndex);
	void calculatePreferenceFlows(int seatIndex, SeatPartyPreferences majorPartyPreferences);
	void accumulatePpvcBiasMeasures(int seatIndex);

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};