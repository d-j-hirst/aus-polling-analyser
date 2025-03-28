#pragma once

#include "ElectionData.h"

#include <map>
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
	void resetOtherOutput();
	void storeTermCode();
	void determineEffectiveSeatTppModifiers();
	void determinePreviousSwingDeviations();
	void accumulateRegionStaticInfo();
	void loadSeatBettingOdds();
	void loadSeatPolls();
	void loadSeatTppPolls();
	void loadSeatMinorViability();
	void determinePreviousVoteEnrolmentRatios();
	void resizeRegionSeatCountOutputs();
	void countInitialRegionSeatLeads();
	void calculateTotalPopulation();
	void calculateLiveAggregates();
	void updateLiveAggregateForSeat(int seatIndex);
	void finaliseLiveAggregates();
	void resetResultCounts();
	void determineSpecificPartyIndices();
	void loadPreviousPreferenceFlows();
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
	void loadRegionSwingDeviations();
	void loadTppSwingFactors();
	void loadNationalsParameters();
	void loadNationalsSeatExpectations();
	void loadIndividualSeatParameters();
	void prepareProminentMinors();
	void prepareRunningParties();
	void calculateIndEmergenceModifier();

	void initializeGeneralLiveData();

	void loadLiveManualResults();
	std::vector<int> regionSeatCount;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};