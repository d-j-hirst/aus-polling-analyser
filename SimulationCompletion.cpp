#include "SimulationCompletion.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

using Mp = Simulation::MajorParty;

constexpr std::array<int, NumProbabilityBoundIndices> ProbabilityBounds = { 1, 5, 20, 50, 150, 180, 195,199 };

SimulationCompletion::SimulationCompletion(PollingProject & project, Simulation & sim, SimulationRun & run)
	: project(project), run(run), sim(sim)
{
}

void SimulationCompletion::completeRun()
{
	recordNames();

	calculateIndividualSeatStatistics();

	calculateWholeResultStatistics();

	calculatePartyWinExpectations();

	calculatePartyWinMedians();

	calculateRegionPartyWinExpectations();

	recordProbabilityBands();

	recordSeatPartyWinPercentages();

	recordSeatFpVoteStats();
	recordSeatTcpVoteStats();

	createClassicSeatsList();

	recordTrends();

	recordModelledPolls();

	recordReportSettings();
}

void SimulationCompletion::calculateIndividualSeatStatistics()
{
	sim.latestReport.seatPartyOneMarginAverage.resize(project.seats().count(), 0.0);
	sim.latestReport.partyOneWinPercent.resize(project.seats().count(), 0.0);
	sim.latestReport.partyTwoWinPercent.resize(project.seats().count(), 0.0);
	sim.latestReport.othersWinPercent.resize(project.seats().count(), 0.0);

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		sim.latestReport.seatPartyOneMarginAverage[seatIndex] = float(run.seatPartyOneMarginSum[seatIndex] / double(sim.settings.numIterations) * 100.0);
		sim.latestReport.partyOneWinPercent[seatIndex] = float(run.partyOneWinPercent[seatIndex] / double(sim.settings.numIterations) * 100.0);
		sim.latestReport.partyTwoWinPercent[seatIndex] = float(run.partyTwoWinPercent[seatIndex] / double(sim.settings.numIterations) * 100.0);
		sim.latestReport.othersWinPercent[seatIndex] = float(run.othersWinPercent[seatIndex] / double(sim.settings.numIterations) * 100.0);
	}
}

void SimulationCompletion::calculateWholeResultStatistics()
{
	logger << "Party majorities:\n";
	for (auto [partyIndex, result] : run.partyMajority) {
		sim.latestReport.majorityPercent[partyIndex] = float(result) / float(sim.settings.numIterations) * 100.0f;
		logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.majorityPercent[partyIndex] << "%\n";
	}
	logger << "Party minorities:\n";
	for (auto [partyIndex, result] : run.partyMinority) {
		sim.latestReport.minorityPercent[partyIndex] = float(result) / float(sim.settings.numIterations) * 100.0f;
		logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.minorityPercent[partyIndex] << "%\n";
	}
	logger << "Party most-seats:\n";
	for (auto [partyIndex, result] : run.partyMostSeats) {
		sim.latestReport.mostSeatsPercent[partyIndex] = float(result) / float(sim.settings.numIterations) * 100.0f;
		logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.mostSeatsPercent[partyIndex] << "%\n";
	}
	sim.latestReport.tiedPercent = float(run.tiedParliament) / float(sim.settings.numIterations) * 100.0f;

	logger << "Tied: - " << sim.latestReport.tiedPercent << "%\n";
	sim.latestReport.partyOneSwing = sim.latestReport.partyOneSwing / double(sim.settings.numIterations);
}

void SimulationCompletion::calculatePartyWinExpectations()
{
	for (auto [partyIndex, frequency] : sim.latestReport.partySeatWinFrequency) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.seats().count(); ++seatNum) {
			totalSeats += seatNum * frequency[seatNum];
		}
		sim.latestReport.partyWinExpectation[partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
	}
}

void SimulationCompletion::calculatePartyWinMedians()
{
	for (auto [partyIndex, frequency] : sim.latestReport.partySeatWinFrequency) {
		int runningTotal = 0;
		for (int seatNum = 0; seatNum < int(frequency.size()); ++seatNum) {
			runningTotal += frequency[seatNum];
			if (runningTotal > sim.settings.numIterations / 2) {
				sim.latestReport.partyWinMedian[partyIndex] = seatNum;
				break;
			}
		}
	}
}

void SimulationCompletion::calculateRegionPartyWinExpectations()
{
	sim.latestReport.regionPartyWinExpectation.resize(project.regions().count());

	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			int totalSeats = 0;
			for (int seatNum = 1; seatNum < int(run.regionPartyWins[regionIndex][partyIndex].size()); ++seatNum) {
				totalSeats += seatNum * run.regionPartyWins[regionIndex][partyIndex][seatNum];
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
		if (seat.isClassic2pp()) {
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
		sim.latestReport.partyName[index] = project.parties().viewByIndex(index).name;
		sim.latestReport.partyAbbr[index] = project.parties().viewByIndex(index).abbreviation;
		sim.latestReport.partyColour[index] = project.parties().viewByIndex(index).colour;
	}
	sim.latestReport.partyName[OthersIndex] = "Others";
	sim.latestReport.partyName[EmergingIndIndex] = "Emerging Ind";
	sim.latestReport.partyName[EmergingPartyIndex] = "Emerging Party";
	sim.latestReport.partyAbbr[OthersIndex] = "OTH";
	sim.latestReport.partyAbbr[EmergingIndIndex] = "IND";
	sim.latestReport.partyAbbr[EmergingPartyIndex] = "OTH";
	sim.latestReport.partyColour[OthersIndex] = Party::createColour(128, 128, 128);
	sim.latestReport.partyColour[EmergingIndIndex] = Party::createColour(128, 128, 128);
	sim.latestReport.partyColour[EmergingPartyIndex] = Party::createColour(64, 64, 64);
	for (int index = 0; index < project.regions().count(); ++index) {
		sim.latestReport.regionName.push_back(project.regions().viewByIndex(index).name);
	}
	for (int index = 0; index < project.seats().count(); ++index) {
		sim.latestReport.seatName.push_back(project.seats().viewByIndex(index).name);
		sim.latestReport.seatIncumbents.push_back(project.seats().viewByIndex(index).incumbent);
		sim.latestReport.seatMargins.push_back(project.seats().viewByIndex(index).tppMargin);
		if (project.seats().viewByIndex(index).incumbent == 0) {
			sim.latestReport.seatIncumbentMargins.push_back(project.seats().viewByIndex(index).tppMargin);
		}
		else if (project.seats().viewByIndex(index).incumbent == 1) {
			sim.latestReport.seatIncumbentMargins.push_back(-project.seats().viewByIndex(index).tppMargin);
		}
		else {
			sim.latestReport.seatIncumbentMargins.push_back(run.pastSeatResults[index].tcpVote.at(project.seats().viewByIndex(index).incumbent) - 50.0f);
		}
	}
}

void SimulationCompletion::recordSeatPartyWinPercentages()
{
	sim.latestReport.seatPartyWinPercent.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto [partyIndex, count] : run.seatPartyWins[seatIndex]) {
			float percent = float(count) / float(sim.settings.numIterations) * 100.0f;
			sim.latestReport.seatPartyWinPercent[seatIndex][partyIndex] = percent;
			logger << sim.latestReport.seatName[seatIndex] << ", " << sim.latestReport.partyName[partyIndex] << ": " << sim.latestReport.seatPartyWinPercent[seatIndex][partyIndex] << "%\n";
		}
	}
}

void SimulationCompletion::recordSeatFpVoteStats()
{
	sim.latestReport.probabilityBands = sim.latestReport.CurrentlyUsedProbabilityBands;
	sim.latestReport.seatPartyMeanFpShare.resize(project.seats().count());
	sim.latestReport.seatFpProbabilityBand.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto [partyIndex, cumulativePercent] : run.cumulativeSeatPartyFpShare[seatIndex]) {
			sim.latestReport.seatPartyMeanFpShare[seatIndex][partyIndex] = cumulativePercent / double(sim.settings.numIterations);
		}

		logger << project.seats().viewByIndex(seatIndex).name << "\n";
		logger << " Fp percent:\n";
		for (auto const& [partyIndex, fpVoteShare] : sim.latestReport.seatPartyMeanFpShare[seatIndex]) {
			logger << "  ";
			if (partyIndex >= 0) logger << project.parties().viewByIndex(partyIndex).name;
			else if (partyIndex == -1) logger << "Others";
			else if (partyIndex == -2) logger << "Emerging Ind";
			else if (partyIndex == -3) logger << "Emerging Party";
			logger << ": " << fpVoteShare << ", " << "distribution: ";
			auto const& distribution = run.seatPartyFpDistribution[seatIndex][partyIndex];
			int cumulative = sim.settings.numIterations - std::accumulate(distribution.begin(), distribution.end(), 0);
			sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex].resize(sim.latestReport.probabilityBands.size());
			int currentProbabilityBand = 0;
			for (int a = 0; a < SimulationRun::FpBucketCount; ++a) {
				if (distribution[a] > 0) {
					float lowerPercentile = float(cumulative) / float(sim.settings.numIterations) * 100.0f;
					cumulative += distribution[a];
					float upperPercentile = float(cumulative) / float(sim.settings.numIterations) * 100.0f;
					while (currentProbabilityBand < int(sim.latestReport.probabilityBands.size()) && sim.latestReport.probabilityBands[currentProbabilityBand] <
						float(cumulative) / float(sim.settings.numIterations) * 100.0f)
					{
						float band = sim.latestReport.probabilityBands[currentProbabilityBand];
						float exactFrac = (band - lowerPercentile) / (upperPercentile - lowerPercentile);
						float exactFp = float(a) + exactFrac;
						if (!a && distribution[1] < distribution[0]) exactFp = 0.0f;
						sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex][currentProbabilityBand] = std::clamp(exactFp, 0.0f, 100.0f);
						++currentProbabilityBand;
					}
					logger << float(a) / float(SimulationRun::FpBucketCount) * 100.0f;
					logger << "-";
					logger << distribution[a];
					logger << ", ";
				}
			}
			logger << "\n";
			logger << "Probability bands: " << sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex] << "\n";
		}
	}
}

void SimulationCompletion::recordSeatTcpVoteStats()
{
	sim.latestReport.probabilityBands = sim.latestReport.CurrentlyUsedProbabilityBands;
	sim.latestReport.seatTcpProbabilityBand.resize(project.seats().count());
	sim.latestReport.seatTcpScenarioPercent.resize(project.seats().count());
	sim.latestReport.seatTcpWinPercent.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		logger << project.seats().viewByIndex(seatIndex).name << "\n";
		logger << " Tcp percent:\n";
		for (auto const& [parties, distribution] : run.seatTcpDistribution[seatIndex]) {
			int total = std::accumulate(distribution.begin(), distribution.end(), 0);
			if (total < sim.settings.numIterations / 1000) continue;
			logger << "  ";
			if (parties.first >= 0) logger << project.parties().viewByIndex(parties.first).name;
			else if (parties.first == -1) logger << "Others";
			else if (parties.first == -2) logger << "Emerging Ind";
			else if (parties.first == -3) logger << "Emerging Party";
			logger << " vs ";
			if (parties.second >= 0) logger << project.parties().viewByIndex(parties.second).name;
			else if (parties.second == -1) logger << "Others";
			else if (parties.second == -2) logger << "Emerging Ind";
			else if (parties.second == -3) logger << "Emerging Party";
			float scenarioPercent = float(total) / float(sim.settings.numIterations);
			sim.latestReport.seatTcpScenarioPercent[seatIndex][parties] = scenarioPercent;
			sim.latestReport.seatTcpProbabilityBand[seatIndex][parties].resize(sim.latestReport.probabilityBands.size());
			logger << " - scenario frequency: " << scenarioPercent << " - distribution: ";
			int cumulative = 0;
			int currentProbabilityBand = 0;
			float winPercent = 0.0f;
			bool winPercentAssigned = false;
			for (int a = 0; a < SimulationRun::FpBucketCount; ++a) {
				if (distribution[a] > 0) {
					float lowerPercentile = float(cumulative) / float(total) * 100.0f;
					if (a >= SimulationRun::FpBucketCount / 2 && !winPercentAssigned) {
						winPercent = 100.0f - lowerPercentile;
						winPercentAssigned = true;
					}
					cumulative += distribution[a];
					float upperPercentile = float(cumulative) / float(total) * 100.0f;
					while (currentProbabilityBand < int(sim.latestReport.probabilityBands.size()) && sim.latestReport.probabilityBands[currentProbabilityBand] <
						float(cumulative) / float(total) * 100.0f)
					{
						float band = sim.latestReport.probabilityBands[currentProbabilityBand];
						float exactFrac = (band - lowerPercentile) / (upperPercentile - lowerPercentile);
						float exactFp = float(a) + exactFrac;
						if (!a && distribution[1] < distribution[0]) exactFp = 0.0f;
						sim.latestReport.seatTcpProbabilityBand[seatIndex][parties][currentProbabilityBand] = std::clamp(exactFp, 0.0f, 100.0f);
						++currentProbabilityBand;
					}
					logger << float(a) / float(SimulationRun::FpBucketCount) * 100.0f;
					logger << "-";
					logger << distribution[a];
					logger << ", ";
				}
			}
			logger << "\n";
			logger << "   Win rate: " << winPercent << "%\n";
			sim.latestReport.seatTcpWinPercent[seatIndex][parties] = winPercent;
			logger << "   Probability bands: " << sim.latestReport.seatTcpProbabilityBand[seatIndex][parties] << "\n";
		}
	}
}

void SimulationCompletion::recordTrends()
{
	sim.latestReport.trendProbBands = { 1, 5, 25, 50, 75, 95, 99 };
	sim.latestReport.trendPeriod = 5;
	auto const& model = baseModel();
	auto const startDate = model.getStartDate();
	sim.latestReport.trendStartDate = std::to_string(startDate.GetYear()) + "-" +
		(int(startDate.GetMonth()) < 9 ? "0" : "") +
		std::to_string(int(startDate.GetMonth()) + 1) + "-" + std::to_string(startDate.GetDay());
	recordTcpTrend();
	recordFpTrends();
}

void SimulationCompletion::recordTcpTrend()
{
	auto const& model = baseModel();
	auto const& series = model.viewTPPSeries();
	for (int i = 0; ; i = std::min(i + sim.latestReport.trendPeriod, int(series.timePoint.size()) - 1)) {
		sim.latestReport.tppTrend.push_back({});
		for (int j : sim.latestReport.trendProbBands) {
			sim.latestReport.tppTrend.back().push_back(series.timePoint[i].values[j]);
		}
		if (i == int(series.timePoint.size()) - 1) {
			sim.latestReport.finalTrendValue = i;
			break;
		}
	}
}

void SimulationCompletion::recordFpTrends()
{
	auto const& model = baseModel();
	for (auto [index, abbr] : sim.latestReport.partyAbbr) {
		if (index == EmergingPartyIndex) abbr = EmergingOthersCode;
		if (index == OthersIndex) abbr = UnnamedOthersCode;
		if (model.viewAdjustedSeries(abbr)) {
			auto const& series = *model.viewAdjustedSeries(abbr);
			for (int i = 0; ; i = std::min(i + sim.latestReport.trendPeriod, int(series.timePoint.size()) - 1)) {
				sim.latestReport.fpTrend[index].push_back({});
				for (int j : sim.latestReport.trendProbBands) {
					sim.latestReport.fpTrend[index].back().push_back(series.timePoint[i].values[j]);
				}
				if (i == int(series.timePoint.size()) - 1) break;
			}
		}
	}
}

void SimulationCompletion::recordReportSettings()
{
	sim.latestReport.prevElection2pp = sim.settings.prevElection2pp;
}

void SimulationCompletion::recordModelledPolls()
{
	sim.latestReport.modelledPolls = baseModel().viewModelledPolls();
}

StanModel const& SimulationCompletion::baseModel()
{
	return project.projections().view(sim.settings.baseProjection).getBaseModel(project.models());
}

void SimulationCompletion::updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int & bound)
{
	if (float(partyCount) > float(sim.settings.numIterations) * 0.005f * float(probThreshold) && bound == -1) bound = numSeats;
}
