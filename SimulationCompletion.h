#pragma once

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;
class StanModel;

class SimulationCompletion {
public:
	SimulationCompletion(PollingProject& project, Simulation& sim, SimulationRun& run);

	void completeRun();
private:

	// statistic calculations
	void calculateIndividualSeatStatistics();
	void calculateWholeResultStatistics();
	void calculatePartyWinExpectations();
	void calculatePartyWinMedians();
	void calculateRegionPartyWinExpectations();
	void recordVoteTotalStats();
	void recordProbabilityBands();
	void createClassicSeatsList();
	void recordNames();
	void recordSeatPartyWinPercentages();
	void recordSeatFpVoteStats();
	void recordSeatTcpVoteStats();
	void recordSeatSwingFactors();
	void recordTrends();
	void recordTcpTrend();
	void recordFpTrends();
	void recordReportSettings();
	void recordModelledPolls();

	StanModel const& baseModel();

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};