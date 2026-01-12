#pragma once

#include <functional>
#include <string>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;
class StanModel;

class SimulationCompletion {
public:
	typedef std::function<void(std::string)> FeedbackFunc;

	SimulationCompletion(PollingProject& project, Simulation& sim, SimulationRun& run, int iterations);

	// cycleIterations needs to be given as an argument as it will
	// be different for calibration runs.
	void completeRun(FeedbackFunc feedback = [](std::string) {});
private:

	// statistic calculations
	void calculateIndividualSeatStatistics();
	void calculateWholeResultStatistics();
	void calculatePartyWinExpectations();
	void calculatePartyWinMedians();
	void calculateRegionPartyWinExpectations();
	void recordVoteTotalStats();
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
	void exportSummary(FeedbackFunc feedback);

	StanModel const& baseModel();

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	bool doLogging() const;

	int iterations;

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};