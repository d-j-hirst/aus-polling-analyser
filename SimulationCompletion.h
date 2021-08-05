#pragma once

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;

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
	void recordProbabilityBands();
	void createClassicSeatsList();
	void recordNames();
	void recordReportSettings();

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound);

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;
};