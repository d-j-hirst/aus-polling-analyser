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

	calculatePartyWinMedians();

	calculateRegionPartyWinExpectations();

	recordProbabilityBands();

	createClassicSeatsList();

	recordNames();

	recordReportSettings();
}

void SimulationCompletion::calculateIndividualSeatStatistics()
{
	sim.latestReport.incumbentWinPercent.resize(project.seats().count());

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat& thisSeat = project.seats().access(project.seats().indexToId(seatIndex));
		sim.latestReport.incumbentWinPercent[seatIndex] = float(thisSeat.incumbentWins) / float(sim.settings.numIterations) * 100.0f;
		sim.latestReport.seatIncumbentMarginAverage[seatIndex] /= float(sim.settings.numIterations) * 100.0;
		thisSeat.incumbentWinPercent = sim.latestReport.incumbentWinPercent[seatIndex];
		thisSeat.partyOneWinRate /= double(sim.settings.numIterations);
		thisSeat.partyTwoWinRate /= double(sim.settings.numIterations);
		thisSeat.partyOthersWinRate /= double(sim.settings.numIterations);
	}
}

void SimulationCompletion::calculateWholeResultStatistics()
{
	for (Mp party = Mp::First; party <= Mp::Last; ++party) {
		sim.latestReport.majorityPercent[party] = float(run.partyMajority[party]) / float(sim.settings.numIterations) * 100.0f;
		sim.latestReport.minorityPercent[party] = float(run.partyMinority[party]) / float(sim.settings.numIterations) * 100.0f;
	}
	sim.latestReport.hungPercent = float(run.hungParliament) / float(sim.settings.numIterations) * 100.0f;
	sim.latestReport.partyOneSwing = sim.latestReport.partyOneSwing / double(sim.settings.numIterations);
}

void SimulationCompletion::calculatePartyWinExpectations()
{
	sim.latestReport.partyWinExpectation.resize(project.parties().count());

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.seats().count(); ++seatNum) {
			totalSeats += seatNum * sim.latestReport.partySeatWinFrequency[partyIndex][seatNum];
		}
		sim.latestReport.partyWinExpectation[partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
	}
}

void SimulationCompletion::calculatePartyWinMedians()
{
	sim.latestReport.partyWinMedian.resize(project.parties().count());

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		int runningTotal = 0;
		for (int seatNum = 0; seatNum < int(sim.latestReport.partySeatWinFrequency[partyIndex].size()); ++seatNum) {
			runningTotal += sim.latestReport.partySeatWinFrequency[partyIndex][seatNum];
			if (runningTotal > sim.settings.numIterations / 2) {
				sim.latestReport.partyWinMedian[partyIndex] = seatNum;
				logger << partyIndex << " - median result: " << seatNum << "\n";
				break;
			}
		}
	}
}

void SimulationCompletion::calculateRegionPartyWinExpectations()
{
	sim.latestReport.regionPartyWinExpectation.resize(project.regions().count(), std::vector<float>(project.parties().count(), 0.0f));

	for (auto& regionPair : project.regions()) {
		Region& thisRegion = regionPair.second;
		int regionIndex = project.regions().idToIndex(regionPair.first);
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			int totalSeats = 0;
			for (int seatNum = 1; seatNum < int(thisRegion.partyWins[partyIndex].size()); ++seatNum) {
				totalSeats += seatNum * thisRegion.partyWins[partyIndex][seatNum];
			}
			sim.latestReport.regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
		}
	}
}

void SimulationCompletion::recordProbabilityBands()
{
	int partyOneCount = 0;
	int partyTwoCount = 0;
	int othersCount = 0;
	std::fill(sim.latestReport.partyOneProbabilityBounds.begin(), sim.latestReport.partyOneProbabilityBounds.end(), -1);
	std::fill(sim.latestReport.partyTwoProbabilityBounds.begin(), sim.latestReport.partyTwoProbabilityBounds.end(), -1);
	std::fill(sim.latestReport.othersProbabilityBounds.begin(), sim.latestReport.othersProbabilityBounds.end(), -1);
	for (int numSeats = 0; numSeats < project.seats().count(); ++numSeats) {
		partyOneCount += sim.latestReport.partySeatWinFrequency[0][numSeats];
		partyTwoCount += sim.latestReport.partySeatWinFrequency[1][numSeats];
		othersCount += sim.latestReport.othersWinFrequency[numSeats];
		for (int probBoundIndex = 0; probBoundIndex < NumProbabilityBoundIndices; ++probBoundIndex) {
			updateProbabilityBounds(partyOneCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.partyOneProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(partyTwoCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.partyTwoProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(othersCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.othersProbabilityBounds[probBoundIndex]);
		}
	}
}

void SimulationCompletion::createClassicSeatsList()
{
	// Get a list of classic seats and list the in order of Coalition win %
	sim.latestReport.classicSeatIndices.clear();
	for (auto const&[key, seat] : project.seats()) {
		if (seat.isClassic2pp(sim.isLiveAutomatic())) {
			sim.latestReport.classicSeatIndices.push_back(project.seats().idToIndex(key));
		}
	}
	std::sort(sim.latestReport.classicSeatIndices.begin(), sim.latestReport.classicSeatIndices.end(),
		[this](Seat::Id seatA, Seat::Id seatB)
	{return project.seats().view(seatA).getMajorPartyWinRate(1) > project.seats().view(seatB).getMajorPartyWinRate(1); });

}

void SimulationCompletion::recordNames()
{
	for (int index = 0; index < project.parties().count(); ++index) {
		sim.latestReport.partyName.push_back(project.parties().viewByIndex(index).name);
		sim.latestReport.partyAbbr.push_back(project.parties().viewByIndex(index).abbreviation);
		sim.latestReport.partyColour.push_back(project.parties().viewByIndex(index).colour);
	}
	for (int index = 0; index < project.regions().count(); ++index) {
		sim.latestReport.regionName.push_back(project.regions().viewByIndex(index).name);
	}
	for (int index = 0; index < project.seats().count(); ++index) {
		sim.latestReport.seatName.push_back(project.seats().viewByIndex(index).name);
		sim.latestReport.seatIncumbents.push_back(project.seats().viewByIndex(index).incumbent);
		sim.latestReport.seatMargins.push_back(project.seats().viewByIndex(index).margin);
	}
}

void SimulationCompletion::recordReportSettings()
{
	sim.latestReport.prevElection2pp = sim.settings.prevElection2pp;
}

void SimulationCompletion::updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int & bound)
{
	if (float(partyCount) > float(sim.settings.numIterations) * 0.005f * float(probThreshold) && bound == -1) bound = numSeats;
}
