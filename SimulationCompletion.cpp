#include "SimulationCompletion.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"

using Mp = Simulation::MajorParty;

constexpr std::array<int, NumProbabilityBoundIndices> ProbabilityBounds = { 1, 5, 20, 50, 150, 180, 195,199 };

SimulationCompletion::SimulationCompletion(PollingProject & project, Simulation & sim, SimulationRun & run)
	: project(project), run(run), sim(sim)
{
}

void SimulationCompletion::completeRun()
{
	calculateIndividualSeatStatistics();

	calculateWholeResultStatistics();

	calculatePartyWinExpectations();

	calculateRegionPartyWinExpectations();

	recordProbabilityBands();

	createClassicSeatsList();
}

void SimulationCompletion::calculateIndividualSeatStatistics()
{
	sim.incumbentWinPercent.resize(project.seats().count() + 1);

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat& thisSeat = project.seats().access(project.seats().indexToId(seatIndex));
		sim.incumbentWinPercent[seatIndex] = float(thisSeat.incumbentWins) / float(sim.settings.numIterations) * 100.0f;
		thisSeat.incumbentWinPercent = sim.incumbentWinPercent[seatIndex];
		thisSeat.partyOneWinRate /= double(sim.settings.numIterations);
		thisSeat.partyTwoWinRate /= double(sim.settings.numIterations);
		thisSeat.partyOthersWinRate /= double(sim.settings.numIterations);
		thisSeat.simulatedMarginAverage /= float(sim.settings.numIterations);
	}
}

void SimulationCompletion::calculateWholeResultStatistics()
{
	for (Mp party = Mp::First; party <= Mp::Last; ++party) {
		sim.majorityPercent[party] = float(run.partyMajority[party]) / float(sim.settings.numIterations) * 100.0f;
		sim.minorityPercent[party] = float(run.partyMinority[party]) / float(sim.settings.numIterations) * 100.0f;
	}
	sim.hungPercent = float(run.hungParliament) / float(sim.settings.numIterations) * 100.0f;
	sim.partyOneSwing = sim.partyOneSwing / double(sim.settings.numIterations);
}

void SimulationCompletion::calculatePartyWinExpectations()
{
	sim.partyWinExpectation.resize(project.parties().count());

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.seats().count(); ++seatNum) {
			totalSeats += seatNum * sim.partySeatWinFrequency[partyIndex][seatNum];
		}
		sim.partyWinExpectation[partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
	}
}

void SimulationCompletion::calculateRegionPartyWinExpectations()
{
	sim.regionPartyWinExpectation.resize(project.regions().count(), std::vector<float>(project.parties().count(), 0.0f));

	for (auto& regionPair : project.regions()) {
		Region& thisRegion = regionPair.second;
		int regionIndex = project.regions().idToIndex(regionPair.first);
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			int totalSeats = 0;
			for (int seatNum = 1; seatNum < int(thisRegion.partyWins[partyIndex].size()); ++seatNum) {
				totalSeats += seatNum * thisRegion.partyWins[partyIndex][seatNum];
			}
			sim.regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
		}
	}
}

void SimulationCompletion::recordProbabilityBands()
{
	int partyOneCount = 0;
	int partyTwoCount = 0;
	int othersCount = 0;
	std::fill(sim.partyOneProbabilityBounds.begin(), sim.partyOneProbabilityBounds.end(), -1);
	std::fill(sim.partyTwoProbabilityBounds.begin(), sim.partyTwoProbabilityBounds.end(), -1);
	std::fill(sim.othersProbabilityBounds.begin(), sim.othersProbabilityBounds.end(), -1);
	for (int numSeats = 0; numSeats < project.seats().count(); ++numSeats) {
		partyOneCount += sim.partySeatWinFrequency[0][numSeats];
		partyTwoCount += sim.partySeatWinFrequency[1][numSeats];
		othersCount += sim.othersWinFrequency[numSeats];
		for (int probBoundIndex = 0; probBoundIndex < NumProbabilityBoundIndices; ++probBoundIndex) {
			updateProbabilityBounds(partyOneCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.partyOneProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(partyTwoCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.partyTwoProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(othersCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.othersProbabilityBounds[probBoundIndex]);
		}
	}
}

void SimulationCompletion::createClassicSeatsList()
{
	// Get a list of classic seats and list the in order of Coalition win %
	sim.classicSeatIds.clear();
	for (auto const&[key, seat] : project.seats()) {
		if (seat.isClassic2pp(sim.isLiveAutomatic())) {
			sim.classicSeatIds.push_back(key);
		}
	}
	std::sort(sim.classicSeatIds.begin(), sim.classicSeatIds.end(),
		[this](Seat::Id seatA, Seat::Id seatB)
	{return project.seats().view(seatA).getMajorPartyWinRate(1) > project.seats().view(seatB).getMajorPartyWinRate(1); });

}

void SimulationCompletion::updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int & bound)
{
	if (partyCount > sim.settings.numIterations / 200 * probThreshold && bound == -1) bound = numSeats;
}
