#pragma once

// Assemble the forecast report from a completed run's simulated elections.
// completeRun in the .cpp file explains the summaries and the export handoff.

#include <functional>
#include <string>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;
class StanModel;

// Convert accumulated vote shares, wins and outcome counts into a report that
// describes both central estimates and uncertainty. project supplies names and
// polling-model context; run and sim.latestReport hold the recorded results;
// completion fills the remaining summaries in sim.latestReport.
// The same process summarises supporting runs, whose results are used internally
// by SimulationRun. The main report can later be selected for website export.
class SimulationCompletion {
public:
	typedef std::function<void(std::string)> FeedbackFunc;
	using ActionRequiredFunc = std::function<void(std::string)>;

	// iterations is the number of completed elections being summarised, excluding
	// discarded retry attempts. Supply this run's count: supporting calibration
	// runs may use fewer iterations than the main forecast settings request.
	SimulationCompletion(PollingProject& project, Simulation& sim, SimulationRun& run, int iterations);

	// Finish the report after all iterations succeed. Automatic live runs also
	// attempt local summary and diagnostic exports. feedback delivers messages;
	// actionRequired requests attention, such as closing a file before retrying,
	// and falls back to feedback when absent. Website submission is a later step.
	// Errors in the summaries themselves reach SimulationRun, whose failure
	// protection restores the previously retained report.
	void completeRun(
		FeedbackFunc feedback = [](std::string) {},
		ActionRequiredFunc actionRequired = {});
private:

	// statistic calculations
	void calculateIndividualSeatStatistics();
	void calculateWholeResultStatistics();
	void calculatePartyWinExpectations();
	void calculatePartyWinMedians();
	void calculateRegionPartyWinExpectations();
	void recordProbabilityBands();
	void recordNames();
	void recordSeatPartyWinPercentages();
	void recordSeatFpVoteStats();
	void recordSeatTcpVoteStats();
	void recordSeatTppVoteStats(); // only used for live baseline simulation
	void recordSeatSwingFactors();
	void recordRegionFpVoteStats();
	void recordRegionTppVoteStats();
	void recordElectionFpVoteStats();
	void recordElectionTppVoteStats();
	void recordTrends();
	void recordTcpTrend();
	void recordFpTrends();
	void recordReportSettings();
	void recordModelledPolls();
	void exportSummary(
		FeedbackFunc feedback,
		ActionRequiredFunc actionRequired);

	StanModel const& baseModel() const;

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	bool doLogging() const;

	int iterations;

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};
