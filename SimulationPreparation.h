#pragma once

#include <stdexcept>
#include <vector>
#include <utility>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;

class SimulationPreparation {
public:
	class Exception : public std::runtime_error {
	public:
		Exception(std::string what) : std::runtime_error(what) {}
	};

	SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run);
	void prepareForIterations();
private:

	typedef std::pair<int, int> SeatPartyPreferences;

	// initialization functions
	void resetLatestReport();
	void resetRegionSpecificOutput();
	void resetSeatSpecificOutput();
	void determineEffectiveSeatModifiers();
	void accumulateRegionStaticInfo();
	void resetPpvcBiasAggregates();
	void cacheBoothData();
	void determinePpvcBias();
	void loadSeatOutcomeRelations();
	void determinePreviousVoteEnrolmentRatios();
	void resizeRegionSeatCountOutputs();
	void countInitialRegionSeatLeads();
	void calculateTotalPopulation();
	void calculateLiveAggregates();
	void updateLiveAggregateForSeat(int seatIndex);
	void finaliseLiveAggregates();
	void resetResultCounts();
	void determineSeatCachedBoothData(int seatIndex);
	SeatPartyPreferences aggregateVoteData(int seatIndex);
	void calculatePreferenceFlows(int seatIndex, SeatPartyPreferences majorPartyPreferences);
	void accumulatePpvcBiasMeasures(int seatIndex);
	void loadPastSeatResults();
	void loadSeatTypes();
	void loadGreensSeatStatistics();
	void loadIndSeatStatistics();
	void loadPreviousElectionBaselineVotes();

	std::string getYearCode();
	std::string getRegionCode();

	std::vector<int> regionSeatCount;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};