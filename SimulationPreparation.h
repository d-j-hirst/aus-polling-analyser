#pragma once

// Preparation of the shared inputs and result storage for one simulation run.
// The sequence is explained in prepareForIterations in the .cpp file.

#include <map>
#include <stdexcept>
#include <vector>
#include <utility>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;
namespace SeatVolatility { struct Table; }

// Assemble the information that SimulationIteration needs to calculate possible
// elections. The project supplies configured parties, seats, regions (geographic
// groups of seats) and local evidence. sim selects the forecast and holds its
// report; run receives historical results, model estimates and working totals.
// For a live forecast, preparation also obtains reported votes and updates the
// project's records of the count so far through LivePreparation.
//
// Preparing these shared inputs does not choose one final election result.
// Individual iterations still draw different possible vote shares and local
// variations using the uncertainty described by the loaded model estimates.
class SimulationPreparation {
public:
	class Exception : public std::runtime_error {
	public:
		Exception(std::string what) : std::runtime_error(what) {}
	};

	// Keep references to the existing project, forecast and working run. Loading
	// and calculation begin only when prepareForIterations is called.
	SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run);
	// Check the inputs, prepare shared electoral information and clear the totals
	// that this run's iterations will fill. A required input that cannot be used
	// raises an error for SimulationRun to report before those iterations begin.
	void prepareForIterations();
private:

	typedef std::pair<int, int> SeatPartyPreferences;

	// initialization functions
	void resetLatestReport();
	void resetRegionSpecificOutput();
	void resetSeatSpecificOutput();
	void resetOtherOutput();
	void storeTermCode();
	void validateIterationInputs() const;
	void validatePastSeatResults() const;
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
	void calculateRegionalProportion();
	void loadGreensSeatStatistics();
	void loadIndSeatStatistics();
	void loadOthSeatStatistics();
	void loadGreensIndependentCorrelation();
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
	void validateCoalitionSeatSettings() const;
	void loadIndividualSeatParameters();
	SeatVolatility::Table readIndividualSeatParameters() const;
	void applyIndividualSeatParameters(SeatVolatility::Table const& table);
	void prepareProminentMinors();
	void prepareRunningParties();
	void calculateIndEmergenceModifier();
	std::string workspaceFile(std::string const& relativePath) const;

	void initializeGeneralLiveData();

	void loadLiveManualResults();
	std::vector<int> regionSeatCount;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};
