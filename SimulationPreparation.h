#pragma once

#include "ElectionData.h"

#include "tinyxml2.h"

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
	void determinePreviousVoteEnrolmentRatios();
	void resizeRegionSeatCountOutputs();
	void countInitialRegionSeatLeads();
	void calculateTotalPopulation();
	void updateLiveAggregateForSeat(int seatIndex);
	void finaliseLiveAggregates();
	void resetResultCounts();
	void determineIndependentPartyIndex();
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

	void initializeGeneralLiveData();

	void loadLiveManualResults();
	void calculateLiveAggregates();

	void prepareLiveAutomatic();
	void downloadPreviousResults();
	void parsePreviousResults();
	void downloadPreload();
	void parsePreload();
	void downloadCurrentResults();
	void parseCurrentResults();
	void preparePartyCodeGroupings();
	void calculateBoothFpSwings();
	void calculateTppPreferenceFlows();
	void calculateSeatPreferenceFlows();
	void estimateBoothTcps();
	void calculateBoothTcpSwings();
	void calculateCountProgress();
	void determinePpvcBias();
	void calculateSeatSwings();
	void determinePpvcBiasSensitivity();
	void determineDecVoteSensitivity();
	void determineRegionalSwingBasis();
	void determinePartyIdConversions();
	void determineSeatIdConversions();
	void prepareLiveTppSwings();
	void prepareLiveTcpSwings();
	void prepareLiveFpSwings();
	void prepareOverallLiveFpSwings();

	std::string getTermCode();

	std::vector<int> regionSeatCount;
	std::string xmlFilename;
	tinyxml2::XMLDocument xml;
	Results2::Election previousElection;
	Results2::Election currentElection;

	// maps the AEC's party IDs to the simulation's party index
	std::unordered_map<int, int> aecPartyToSimParty;

	// maps the AEC's seat IDs to the simulation's seat index
	std::unordered_map<int, int> aecSeatToSimSeat;

	std::unordered_map<int, float> updatedPreferenceFlows;

	std::unordered_map<std::string, int> partyCodeGroupings;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};