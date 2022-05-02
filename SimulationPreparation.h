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
	void storeTermCode();
	void determineEffectiveSeatModifiers();
	void determinePreviousSwingDeviations();
	void accumulateRegionStaticInfo();
	void loadSeatBettingOdds();
	void loadSeatPolls();
	void resetPpvcBiasAggregates();
	void determinePpvcBias();
	void determinePreviousVoteEnrolmentRatios();
	void resizeRegionSeatCountOutputs();
	void countInitialRegionSeatLeads();
	void calculateTotalPopulation();
	void updateLiveAggregateForSeat(int seatIndex);
	void finaliseLiveAggregates();
	void resetResultCounts();
	void determineIndependentPartyIndex();
	SeatPartyPreferences aggregateVoteData(int seatIndex);
	void calculatePreferenceFlows(int seatIndex, SeatPartyPreferences majorPartyPreferences);
	void accumulatePpvcBiasMeasures(int seatIndex);
	void loadNcPreferenceFlows();
	void loadPastSeatResults();
	void loadSeatTypes();
	void loadGreensSeatStatistics();
	void loadIndSeatStatistics();
	void loadOthSeatStatistics();
	void loadIndEmergence();
	void loadPopulistSeatStatistics();
	void loadPopulistSeatModifiers();
	void loadCentristSeatStatistics();
	void loadCentristSeatModifiers();
	void loadPreviousElectionBaselineVotes();
	void loadRegionBaseBehaviours();
	void loadRegionPollBehaviours();
	void loadRegionMixBehaviours();
	void loadOverallRegionMixParameters();
	void loadTppSwingFactors();
	void loadIndividualSeatParameters();

	void loadLiveManualResults();
	void calculateLiveAggregates();

	std::string getTermCode();

	std::vector<int> regionSeatCount;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};