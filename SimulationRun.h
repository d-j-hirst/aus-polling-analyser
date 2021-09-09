#pragma once

#include "Party.h"

#include <array>
#include <functional>
#include <string>

class PollingProject;
class RegionCollection;
class Seat;
class SeatCollection;
class Simulation;
class Outcome;

class SimulationRun {
public:
	friend class SimulationPreparation;
	friend class SimulationIteration;
	friend class SimulationCompletion;

	typedef std::function<void(std::string)> FeedbackFunc;

	SimulationRun(PollingProject& project, Simulation& simulation) : project(project), sim(simulation) {}

	SimulationRun(SimulationRun const& otherRun) : project(otherRun.project), sim(otherRun.sim) {}
	SimulationRun operator=(SimulationRun const& otherRun) { return SimulationRun(otherRun.project, otherRun.sim); }

	void run(FeedbackFunc feedback = [](std::string) {});
private:

	PollingProject& project;

	Simulation& sim;

	int currentIteration = 0;

	float ppvcBiasNumerator = 0.0f;
	float ppvcBiasDenominator = 0.0f; // should be the total number of PPVC votes counted
	float ppvcBiasObserved = 0.0f;
	float ppvcBiasConfidence = 0.0f;
	int totalOldPpvcVotes = 0;

	float totalPopulation = 0.0f;

	float previousOrdinaryVoteEnrolmentRatio = 1.0f;
	float previousDeclarationVoteEnrolmentRatio = 1.0f;

	float liveOverallSwing = 0.0f; // swing to partyOne
	float liveOverallPercent = 0.0f;
	float classicSeatCount = 0.0f;
	// A bunch of votes from one seat is less likely to be representative than from a wide variety of seats,
	// so this factor is introduced to avoid a small number of seats from having undue influence early in the count
	float sampleRepresentativeness = 0.0f;
	int total2cpVotes = 0;
	int totalEnrolment = 0;

	std::array<int, 2> partyMajority = std::array<int, 2>();
	std::array<int, 2> partyMinority = std::array<int, 2>();
	std::vector<float> seatFirstPartyPreferenceFlow;
	std::vector<float> seatPreferenceFlowVariation;
	std::vector<std::array<int, 2>> seatTcpTally;
	std::vector<float> seatIndividualBoothGrowth;
	std::vector<Outcome const*> seatToOutcome;
	int hungParliament = 0;
};