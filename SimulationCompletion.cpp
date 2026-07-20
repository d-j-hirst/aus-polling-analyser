#include "SimulationCompletion.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using Mp = Simulation::MajorParty;

// Values are stored in half-percent units, allowing the outer 0.5%/99.5%
// thresholds to remain integral.
constexpr std::array<int, NumProbabilityBoundIndices> ProbabilityBounds = {
	1, 5, 20, 50, 150, 180, 195, 199
};

namespace {
	template <std::size_t BucketCount>
	std::vector<float> probabilityBandsFromDistribution(
		std::array<int, BucketCount> const& distribution,
		std::vector<float> const& probabilityBands,
		int sampleCount,
		std::string const& context,
		std::optional<int> explicitZeroCount = std::nullopt,
		bool inferZeroFromFirstBucket = false)
	{
		if (sampleCount <= 0) {
			throw std::runtime_error(
				"Cannot calculate probability bands for " + context +
				" without a positive sample count.");
		}
		int const recordedCount =
			std::accumulate(distribution.begin(), distribution.end(), 0);
		if (recordedCount > sampleCount) {
			throw std::runtime_error(
				"Distribution exceeds the simulation count for " +
				context + ".");
		}

		int const missingCount = sampleCount - recordedCount;
		int const exactZeroCount = explicitZeroCount ?
			missingCount + *explicitZeroCount : missingCount;
		if (exactZeroCount > sampleCount) {
			throw std::runtime_error(
				"Exact-zero count exceeds the simulation count for " +
				context + ".");
		}

		std::vector<float> result(probabilityBands.size(), 0.0f);
		int cumulative = missingCount;
		int currentProbabilityBand = 0;
		for (int bucket = 0; bucket < int(BucketCount); ++bucket) {
			if (distribution[bucket] <= 0) continue;

			float const lowerPercentile =
				float(cumulative) / float(sampleCount) * 100.0f;
			cumulative += distribution[bucket];
			float const upperPercentile =
				float(cumulative) / float(sampleCount) * 100.0f;
			while (currentProbabilityBand < int(probabilityBands.size()) &&
				probabilityBands[currentProbabilityBand] <
					upperPercentile) {
				float const band =
					probabilityBands[currentProbabilityBand];
				float const exactFraction =
					(band - lowerPercentile) /
					(upperPercentile - lowerPercentile);
				float exactVote =
					(float(bucket) + exactFraction) *
					100.0f / float(BucketCount);
				if (explicitZeroCount &&
					band < float(exactZeroCount) /
						float(sampleCount) * 100.0f) {
					exactVote = 0.0f;
				}
				else if (inferZeroFromFirstBucket &&
					bucket == 0 && BucketCount > 1 &&
					distribution[1] < distribution[0]) {
					exactVote = 0.0f;
				}
				result[currentProbabilityBand] =
					std::clamp(exactVote, 0.0f, 100.0f);
				++currentProbabilityBand;
			}
		}
		return result;
	}
}

SimulationCompletion::SimulationCompletion(
	PollingProject& project, Simulation& sim, SimulationRun& run, int iterations)
	: project(project), run(run), sim(sim), iterations(iterations)
{
}

void SimulationCompletion::completeRun(FeedbackFunc feedback)
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
	if (run.doingLiveBaselineSimulation) {
		recordSeatTppVoteStats();
		recordRegionFpVoteStats();
		recordRegionTppVoteStats();
		recordElectionFpVoteStats();
		recordElectionTppVoteStats();
	}

	recordSeatSwingFactors();

	recordTrends();

	recordModelledPolls();

	recordReportSettings();

	if (run.isLiveAutomatic() && !run.doingBettingOddsCalibrations && !run.doingLiveBaselineSimulation) {
		exportSummary(feedback);
	}

	logger << "Simulation successfully completed.\n";
}

void SimulationCompletion::calculateIndividualSeatStatistics()
{
	sim.latestReport.seatPartyOneMarginAverage.resize(project.seats().count(), 0.0);
	sim.latestReport.partyOneWinProportion.resize(project.seats().count(), 0.0);
	sim.latestReport.partyTwoWinProportion.resize(project.seats().count(), 0.0);
	sim.latestReport.othersWinProportion.resize(project.seats().count(), 0.0);

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		sim.latestReport.seatPartyOneMarginAverage[seatIndex] = float(run.seatPartyOneMarginSum[seatIndex] / double(iterations));
		sim.latestReport.partyOneWinProportion[seatIndex] = float(run.partyOneWinPercent[seatIndex] / double(iterations));
		sim.latestReport.partyTwoWinProportion[seatIndex] = float(run.partyTwoWinPercent[seatIndex] / double(iterations));
		sim.latestReport.othersWinProportion[seatIndex] = float(run.othersWinPercent[seatIndex] / double(iterations));
	}
}

void SimulationCompletion::calculateWholeResultStatistics()
{
	if (doLogging()) logger << "Party majorities:\n";
	for (auto [partyIndex, result] : run.partyMajority) {
		sim.latestReport.majorityPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (doLogging()) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.majorityPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.majorityPercent.contains(0)) sim.latestReport.majorityPercent[0] = 0;
	if (!sim.latestReport.majorityPercent.contains(1)) sim.latestReport.majorityPercent[1] = 0;
	if (doLogging()) logger << "Party minorities:\n";
	for (auto [partyIndex, result] : run.partyMinority) {
		sim.latestReport.minorityPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (doLogging()) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.minorityPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.minorityPercent.contains(0)) sim.latestReport.minorityPercent[0] = 0;
	if (!sim.latestReport.minorityPercent.contains(1)) sim.latestReport.minorityPercent[1] = 0;
	if (doLogging()) logger << "Party most-seats:\n";
	for (auto [partyIndex, result] : run.partyMostSeats) {
		sim.latestReport.mostSeatsPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (doLogging()) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.mostSeatsPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.mostSeatsPercent.contains(0)) sim.latestReport.mostSeatsPercent[0] = 0;
	if (!sim.latestReport.mostSeatsPercent.contains(1)) sim.latestReport.mostSeatsPercent[1] = 0;
	sim.latestReport.tiedPercent = float(run.tiedParliament) / float(iterations) * 100.0f;

	if (doLogging()) logger << "Tied: - " << sim.latestReport.tiedPercent << "%\n";
	sim.latestReport.partyOneSwing = sim.latestReport.partyOneSwing / double(iterations);
}

void SimulationCompletion::calculatePartyWinExpectations()
{
	for (auto const& [partyIndex, frequency] :
		sim.latestReport.partySeatWinFrequency) {
		int64_t totalSeats = 0;
		for (int seatNum = 1; seatNum < int(frequency.size()); ++seatNum) {
			totalSeats += int64_t(seatNum) * frequency[seatNum];
		}
		sim.latestReport.partyWinExpectation[partyIndex] = float(totalSeats) / float(iterations);
	}
	if (run.natPartyIndex >= 0) {
		int64_t totalSeats = 0;
		for (int seatNum = 1;
			seatNum < int(sim.latestReport.coalitionSeatWinFrequency.size());
			++seatNum) {
			totalSeats +=
				int64_t(seatNum) *
				sim.latestReport.coalitionSeatWinFrequency[seatNum];
		}
		sim.latestReport.coalitionWinExpectation = float(totalSeats) / float(iterations);
	}
}

void SimulationCompletion::calculatePartyWinMedians()
{
	for (auto [partyIndex, frequency] : sim.latestReport.partySeatWinFrequency) {
		int runningTotal = 0;
		for (int seatNum = 0; seatNum < int(frequency.size()); ++seatNum) {
			runningTotal += frequency[seatNum];
			if (runningTotal > iterations / 2) {
				sim.latestReport.partyWinMedian[partyIndex] = seatNum;
				break;
			}
		}
	}
	if (run.natPartyIndex >= 0) {
		int runningTotal = 0;
		for (int seatNum = 0; seatNum < int(sim.latestReport.coalitionSeatWinFrequency.size()); ++seatNum) {
			runningTotal += sim.latestReport.coalitionSeatWinFrequency[seatNum];
			if (runningTotal > iterations / 2) {
				sim.latestReport.coalitionWinMedian = seatNum;
				break;
			}
		}
	}
}

void SimulationCompletion::calculateRegionPartyWinExpectations()
{
	sim.latestReport.regionPartyWinExpectation.resize(project.regions().count());
	if (run.natPartyIndex >= 0) sim.latestReport.regionCoalitionWinExpectation.resize(project.regions().count());

	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		for (auto const& [partyIndex, frequency] :
			run.regionPartyWins[regionIndex]) {
			int64_t totalSeats = 0;
			for (int seatNum = 1;
				seatNum < int(frequency.size()); ++seatNum) {
				totalSeats += int64_t(seatNum) * frequency[seatNum];
			}
			sim.latestReport.regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(iterations);
		}

		if (run.natPartyIndex >= 0) {
			sim.latestReport.regionCoalitionWinExpectation[regionIndex] =
				sim.latestReport.regionPartyWinExpectation[regionIndex][Mp::Two] +
				sim.latestReport.regionPartyWinExpectation[regionIndex][run.natPartyIndex];
		}
	}
}

void SimulationCompletion::recordProbabilityBands()
{
	int partyOneCount = 0;
	int partyTwoCount = 0;
	int coalitionCount = 0;
	int othersCount = 0;
	std::fill(sim.latestReport.partyOneProbabilityBounds.begin(), sim.latestReport.partyOneProbabilityBounds.end(), -1);
	std::fill(sim.latestReport.partyTwoProbabilityBounds.begin(), sim.latestReport.partyTwoProbabilityBounds.end(), -1);
	if (run.natPartyIndex >= 0) std::fill(sim.latestReport.coalitionProbabilityBounds.begin(), sim.latestReport.coalitionProbabilityBounds.end(), -1);
	std::fill(sim.latestReport.othersProbabilityBounds.begin(), sim.latestReport.othersProbabilityBounds.end(), -1);
	for (int numSeats = 0; numSeats <= project.seats().count(); ++numSeats) {
		partyOneCount += sim.latestReport.partySeatWinFrequency[0][numSeats];
		partyTwoCount += sim.latestReport.partySeatWinFrequency[1][numSeats];
		if (run.natPartyIndex >= 0) coalitionCount += sim.latestReport.coalitionSeatWinFrequency[numSeats];
		othersCount += sim.latestReport.othersSeatWinFrequency[numSeats];
		for (int probBoundIndex = 0; probBoundIndex < NumProbabilityBoundIndices; ++probBoundIndex) {
			updateProbabilityBounds(partyOneCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.partyOneProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(partyTwoCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.partyTwoProbabilityBounds[probBoundIndex]);
			if (run.natPartyIndex >= 0) updateProbabilityBounds(coalitionCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.coalitionProbabilityBounds[probBoundIndex]);
			updateProbabilityBounds(othersCount, numSeats,
				ProbabilityBounds[probBoundIndex], sim.latestReport.othersProbabilityBounds[probBoundIndex]);
		}
	}
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
	sim.latestReport.partyName[CoalitionPartnerIndex] = "Liberal/National Coalition";
	sim.latestReport.partyAbbr[OthersIndex] = "OTH";
	sim.latestReport.partyAbbr[EmergingIndIndex] = "IND";
	sim.latestReport.partyAbbr[EmergingPartyIndex] = "OTH";
	sim.latestReport.partyAbbr[CoalitionPartnerIndex] = "LNP";
	sim.latestReport.partyColour[OthersIndex] = Party::createColour(128, 128, 128);
	sim.latestReport.partyColour[EmergingIndIndex] = Party::createColour(128, 128, 128);
	sim.latestReport.partyColour[EmergingPartyIndex] = Party::createColour(64, 64, 64);
	sim.latestReport.partyColour[CoalitionPartnerIndex] = Party::createColour(0, 0, 255);
	for (int index = 0; index < project.regions().count(); ++index) {
		sim.latestReport.regionName.push_back(project.regions().viewByIndex(index).name);
	}
	for (int index = 0; index < project.seats().count(); ++index) {
		Seat const& seat = project.seats().viewByIndex(index);
		int const incumbentPartyIndex =
			project.parties().idToIndex(seat.incumbent);
		sim.latestReport.seatName.push_back(seat.name);
		int effectiveIncumbent = incumbentPartyIndex;
		if (incumbentPartyIndex == run.indPartyIndex && seat.retirement) {
			effectiveIncumbent = seat.tppMargin > 0 ? 0 : 1;
		}
		// Report party maps use collection indices, not persistent project IDs.
		sim.latestReport.seatIncumbents.push_back(incumbentPartyIndex);
		sim.latestReport.seatMargins.push_back(seat.tppMargin);
		if (effectiveIncumbent == 0) {
			sim.latestReport.seatIncumbentMargins.push_back(seat.tppMargin);
		}
		else if (effectiveIncumbent == 1) {
			sim.latestReport.seatIncumbentMargins.push_back(-seat.tppMargin);
		}
		else if (run.pastSeatResults[index].tcpVotePercent.contains(effectiveIncumbent)) {
			float margin = run.pastSeatResults[index].tcpVotePercent.at(effectiveIncumbent) - 50.0f;
			if (seat.challenger != Party::InvalidId) {
				auto const& challenger =
					project.parties().view(seat.challenger);
				if (seat.tcpChange.contains(challenger.abbreviation)) {
					margin -=
						seat.tcpChange.at(challenger.abbreviation);
				}
			}
			sim.latestReport.seatIncumbentMargins.push_back(margin);
		}
		else {
			// incumbent is 3rd party and gained seat in by-election, just skip for now
			sim.latestReport.seatIncumbentMargins.push_back(0);
		}
		// Store names in the report at simulation time. Saved reports must not
		// depend on the project's mutable seat data when uploaded later.
		std::map<int, std::string> candidateNames;
		for (auto const& [candidateName, partyCode] : seat.candidateNames) {
			int const partyIndex = project.parties().indexByShortCode(partyCode);
			if (partyIndex >= 0) candidateNames[partyIndex] = candidateName;
		}
		sim.latestReport.seatCandidateNames.push_back(candidateNames);
	}
}

void SimulationCompletion::recordSeatPartyWinPercentages()
{
	sim.latestReport.seatPartyWinPercent.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto [partyIndex, count] : run.seatPartyWins[seatIndex]) {
			float percent = float(count) / float(iterations) * 100.0f;
			sim.latestReport.seatPartyWinPercent[seatIndex][partyIndex] = percent;
			if (doLogging()) {
				logger << sim.latestReport.seatName[seatIndex] << ", " <<
					sim.latestReport.partyName[partyIndex] << ": " <<
					sim.latestReport.seatPartyWinPercent[seatIndex][partyIndex] << "%\n";
			}
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
			if (!cumulativePercent) continue;
			sim.latestReport.seatPartyMeanFpShare[seatIndex][partyIndex] = cumulativePercent / double(iterations);
		}

		if (doLogging()) {
			logger << project.seats().viewByIndex(seatIndex).name << "\n";
			logger << " Fp percent:\n";
		}
		for (auto const& [partyIndex, fpVoteShare] : sim.latestReport.seatPartyMeanFpShare[seatIndex]) {
			if (doLogging()) {
				logger << "  ";
				if (partyIndex >= 0) logger << project.parties().viewByIndex(partyIndex).name;
				else if (partyIndex == -1) logger << "Others";
				else if (partyIndex == -2) logger << "Emerging Ind";
				else if (partyIndex == -3) logger << "Emerging Party";
				logger << ": " << fpVoteShare << ", " << "distribution: ";
			}
			auto const& distribution = run.seatPartyFpDistribution[seatIndex][partyIndex];
			sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex] =
				probabilityBandsFromDistribution(
					distribution,
					sim.latestReport.probabilityBands,
					iterations,
					"seat FP for " +
						project.seats().viewByIndex(seatIndex).name,
					getAt(
						run.seatPartyFpZeros[seatIndex],
						partyIndex, 0));
			if (doLogging()) {
				for (int bucket = 0;
					bucket < SimulationRun::BucketCount; ++bucket) {
					if (distribution[bucket] <= 0) continue;
					logger <<
						float(bucket) /
							float(SimulationRun::BucketCount) *
							100.0f <<
						"-" << distribution[bucket] << ", ";
				}
			}
			if (doLogging()) {
				logger << "\n";
				logger << "Probability bands: " << sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex] << "\n";
			}
		}
	}
}

void SimulationCompletion::recordSeatTcpVoteStats()
{
	sim.latestReport.probabilityBands = sim.latestReport.CurrentlyUsedProbabilityBands;
	sim.latestReport.seatTcpProbabilityBand.resize(project.seats().count());
	sim.latestReport.seatTcpScenarioPercent.resize(project.seats().count());
	sim.latestReport.seatTcpWinPercent.resize(project.seats().count());
	sim.latestReport.seatHideTcps.resize(project.seats().count(), false);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (doLogging()) {
			logger << project.seats().viewByIndex(seatIndex).name << "\n";
			logger << " Tcp percent:\n";
		}
		for (auto const& [parties, distribution] : run.seatTcpDistribution[seatIndex]) {
			int total = std::accumulate(distribution.begin(), distribution.end(), 0);
			if (total < iterations / 1000) continue;
			if (doLogging()) {
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
			}
			// Despite the legacy field name, the serialized scenario frequency
			// is a 0-1 proportion.
			float scenarioPercent = float(total) / float(iterations);
			sim.latestReport.seatTcpScenarioPercent[seatIndex][parties] = scenarioPercent;
			sim.latestReport.seatTcpProbabilityBand[seatIndex][parties] =
				probabilityBandsFromDistribution(
					distribution,
					sim.latestReport.probabilityBands,
					total,
					"seat TCP for " +
						project.seats().viewByIndex(seatIndex).name,
					std::nullopt,
					true);

			if (doLogging()) {
				logger << " - scenario frequency: " << scenarioPercent << " - distribution: ";
			}
			int const belowWinningShare = std::accumulate(
				distribution.begin(),
				distribution.begin() + SimulationRun::BucketCount / 2,
				0);
			// The distribution stores the share of parties.first.
			float const winPercent =
				100.0f -
				float(belowWinningShare) / float(total) * 100.0f;
			if (doLogging()) {
				for (int bucket = 0;
					bucket < SimulationRun::BucketCount; ++bucket) {
					if (distribution[bucket] <= 0) continue;
					logger <<
						float(bucket) /
							float(SimulationRun::BucketCount) *
							100.0f <<
						"-" << distribution[bucket] << ", ";
				}
			}
			sim.latestReport.seatTcpWinPercent[seatIndex][parties] = winPercent;
			if (doLogging()) {
				logger << "\n";
				logger << "   Win rate: " << winPercent << "%\n";
				logger << "   Probability bands: " << sim.latestReport.seatTcpProbabilityBand[seatIndex][parties] << "\n";
			}
		}
		if (project.seats().access(project.seats().indexToId(seatIndex)).livePartyOne != Party::InvalidId) sim.latestReport.seatHideTcps[seatIndex] = true;
	}
}

void SimulationCompletion::recordSeatTppVoteStats() {
	sim.latestReport.seatTppProbabilityBand.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& distribution = run.seatTppDistribution[seatIndex];
		sim.latestReport.seatTppProbabilityBand[seatIndex] =
			probabilityBandsFromDistribution(
				distribution,
				sim.latestReport.probabilityBands,
				iterations,
				"seat TPP for " +
					project.seats().viewByIndex(seatIndex).name);
	}
}

void SimulationCompletion::recordSeatSwingFactors()
{
	sim.latestReport.swingFactors.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		const float seatRegionSwingAverage = run.seatRegionSwingSums[seatIndex] / double(iterations);
		const float seatElasticitySwingAverage = run.seatElasticitySwingSums[seatIndex] / double(iterations);
		const float seatLocalEffectsAverage = run.seatLocalEffectsSums[seatIndex] / double(iterations);
		const float seatPreviousSwingEffectAverage = run.seatPreviousSwingEffectSums[seatIndex] / double(iterations);
		const float seatFederalSwingEffectAverage = run.seatFederalSwingEffectSums[seatIndex] / double(iterations);
		const float seatByElectionEffectAverage = run.seatByElectionEffectSums[seatIndex] / double(iterations);
		const float seatThirdPartyExhaustEffectAverage = run.seatThirdPartyExhaustEffectSums[seatIndex] / double(iterations);
		const float seatPollEffectAverage = run.seatPollEffectSums[seatIndex] / double(iterations);
		const float seatMrpPollEffectAverage = run.seatMrpPollEffectSums[seatIndex] / double(iterations);
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Base region swing;" + std::to_string(seatRegionSwingAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Elasticity adjustment;" + std::to_string(seatElasticitySwingAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Reversion from previous swing;" + std::to_string(seatPreviousSwingEffectAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Correlation with federal swing;" + std::to_string(seatFederalSwingEffectAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"By-election result adjustment;" + std::to_string(seatByElectionEffectAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Exhaustion from aligned non-major candidates;" + std::to_string(seatThirdPartyExhaustEffectAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Adjustment towards MRP polling;" + std::to_string(seatMrpPollEffectAverage));
		sim.latestReport.swingFactors[seatIndex].push_back(
			"Adjustment towards seat polling;" + std::to_string(seatPollEffectAverage));
		float totalLocalEffects = 0.0f;
		for (auto const& [name, impact] : run.seatLocalEffects[seatIndex]) {
			totalLocalEffects += impact;
		}
		// Opposing local effects can cancel. Do not amplify their explanatory
		// breakdown by dividing through a near-zero net effect.
		if (std::abs(totalLocalEffects) < 0.000001f) continue;
		for (auto const& [name, impact] : run.seatLocalEffects[seatIndex]) {
			float scaledEffect = impact * seatLocalEffectsAverage / totalLocalEffects;
			sim.latestReport.swingFactors[seatIndex].push_back(
				name + ";" + std::to_string(scaledEffect));
		}
	}
}

void SimulationCompletion::recordRegionFpVoteStats() {
	sim.latestReport.regionFpProbabilityBand.resize(project.regions().count());
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		for (auto const& [partyIndex, distribution] : run.regionPartyFpDistribution[regionIndex]) {
			sim.latestReport.regionFpProbabilityBand[regionIndex][partyIndex] =
				probabilityBandsFromDistribution(
					distribution,
					sim.latestReport.probabilityBands,
					iterations,
					"region FP for " +
						project.regions().viewByIndex(regionIndex).name,
					std::nullopt,
					true);
		}
	}
}

void SimulationCompletion::recordRegionTppVoteStats() {
	sim.latestReport.regionTppProbabilityBand.resize(project.regions().count());
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		auto const& distribution = run.regionTppDistribution[regionIndex];
		sim.latestReport.regionTppProbabilityBand[regionIndex] =
			probabilityBandsFromDistribution(
				distribution,
				sim.latestReport.probabilityBands,
				iterations,
				"region TPP for " +
					project.regions().viewByIndex(regionIndex).name,
				std::nullopt,
				true);
	}
}

void SimulationCompletion::recordElectionFpVoteStats() {
	for (auto const& [partyIndex, distribution] : run.electionPartyFpDistribution) {
		sim.latestReport.electionFpProbabilityBand[partyIndex] =
			probabilityBandsFromDistribution(
				distribution,
				sim.latestReport.probabilityBands,
				iterations,
				"election FP for party index " +
					std::to_string(partyIndex),
				std::nullopt,
				true);
	}
}

void SimulationCompletion::recordElectionTppVoteStats() {
	auto const& distribution = run.electionTppDistribution;
	sim.latestReport.electionTppProbabilityBand =
		probabilityBandsFromDistribution(
			distribution,
			sim.latestReport.probabilityBands,
			iterations,
			"election TPP",
			std::nullopt,
			true);
}

void SimulationCompletion::recordTrends()
{
	sim.latestReport.trendProbBands = { 1, 5, 25, 50, 75, 95, 99 };
	sim.latestReport.trendPeriod = 5;
	auto const& model = baseModel();
	auto const startDate = model.getStartDate();
	if (!startDate.IsValid()) {
		throw std::runtime_error(
			"Cannot record simulation trends because the model start date is invalid.");
	}
	sim.latestReport.trendStartDate =
		startDate.FormatISODate().ToStdString();
	recordTcpTrend();
	recordFpTrends();
}

void SimulationCompletion::recordTcpTrend()
{
	auto const& model = baseModel();
	auto const& series = model.viewTPPSeries();
	if (series.timePoint.empty()) {
		throw std::runtime_error(
			"Cannot record the TPP trend because its model series is empty.");
	}
	for (int i = 0; ; i = std::min(i + sim.latestReport.trendPeriod, int(series.timePoint.size()) - 1)) {
		sim.latestReport.tppTrend.push_back({});
		for (int j : sim.latestReport.trendProbBands) {
			if (j < 0 || j >= int(series.timePoint[i].values.size())) {
				throw std::runtime_error(
					"Cannot record the TPP trend because a model time point "
					"does not contain every requested probability band.");
			}
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
		if (abbr == "ON") abbr = "ONP";
		auto const adjustedSeries = model.viewAdjustedSeries(abbr);
		if (adjustedSeries) {
			auto const& series = *adjustedSeries;
			if (series.timePoint.empty()) {
				throw std::runtime_error(
					"Cannot record the " + abbr +
					" FP trend because its model series is empty.");
			}
			for (int i = 0; ; i = std::min(i + sim.latestReport.trendPeriod, int(series.timePoint.size()) - 1)) {
				sim.latestReport.fpTrend[index].push_back({});
				for (int j : sim.latestReport.trendProbBands) {
					if (j < 0 || j >= int(series.timePoint[i].values.size())) {
						throw std::runtime_error(
							"Cannot record the " + abbr +
							" FP trend because a model time point does not "
							"contain every requested probability band.");
					}
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

void SimulationCompletion::exportSummary(FeedbackFunc feedback)
{
	// This remains a purpose-built operational export for automatic live runs,
	// rather than a general report format. Its selected party columns preserve
	// the layouts expected by the existing downstream workflow.
	std::ofstream summaryFile;
	constexpr int MaxSummaryFileOpenAttempts = 3;
	for (int attempt = 1;
		attempt <= MaxSummaryFileOpenAttempts && !summaryFile.is_open();
		++attempt) {
		summaryFile.open("live_summary.csv");
		if (!summaryFile.is_open() &&
			attempt < MaxSummaryFileOpenAttempts) {
			feedback(
				"Could not write live_summary.csv. Close the file if it is "
				"open, then dismiss this message to retry (" +
				std::to_string(attempt) + " of " +
				std::to_string(MaxSummaryFileOpenAttempts) + ").");
			summaryFile.clear();
		}
	}
	if (!summaryFile.is_open()) {
		feedback(
			"Could not write live_summary.csv after three attempts. "
			"The simulation will continue without updating that file.");
		return;
	}
	summaryFile << "Simulation Summary\n";
	summaryFile << "Iterations\n" << iterations << "\n";
	summaryFile << "Party Names\n";
	std::set useParties = { -3, -2, 0, 1, 2, 4, 5, 6, 7, 10 };
	if (run.getTermCode() == "2022fed") useParties = { -3, -2, 0, 1, 2, 4, 5, 6, 7, 9 };
	PA_LOG_VAR(sim.latestReport.partyName);
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << name << ",";
	}
	summaryFile << "\nALP majority % simulations\n";
	summaryFile << sim.latestReport.majorityPercent[0];
	summaryFile << "\nALP minority % simulations\n";
	summaryFile << sim.latestReport.minorityPercent[0];
	summaryFile << "\nLNP minority % simulations\n";
	summaryFile << sim.latestReport.minorityPercent[1];
	summaryFile << "\nLNP majority % simulations\n";
	summaryFile << sim.latestReport.majorityPercent[1];
	summaryFile << "\nParty Win Expectations\n";
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		if (!sim.latestReport.partyWinExpectation.contains(partyIndex)) {
			summaryFile << "0,";
			continue;
		}
		summaryFile << sim.latestReport.partyWinExpectation.at(partyIndex) << ",";
	}
	summaryFile << "\nCoalition Win Expectation\n";
	summaryFile << sim.latestReport.coalitionWinExpectation << "\n";
	summaryFile << "Party Win 0.1%s\n";
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << sim.latestReport.getPartySeatsPercentile(partyIndex, 0.1f) << ",";
	}
	summaryFile << "\nCoalition Win 0.1%\n";
	summaryFile << sim.latestReport.getCoalitionSeatsPercentile(0.1f);
	summaryFile << "\nParty Win 5%s\n";
	for (auto [partyIndex, median] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << sim.latestReport.getPartySeatsPercentile(partyIndex, 5.0f) << ",";
	}
	summaryFile << "\nCoalition Win 5%\n";
	summaryFile << sim.latestReport.getCoalitionSeatsPercentile(5.0f);
	summaryFile << "\nParty Win Medians\n";
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		if (!sim.latestReport.partyWinMedian.contains(partyIndex)) {
			summaryFile << "0,";
			continue;
		}
		summaryFile << sim.latestReport.partyWinMedian.at(partyIndex) << ",";
	}
	summaryFile << "\nCoalition Win Median\n";
	summaryFile << sim.latestReport.coalitionWinMedian;
	summaryFile << "\nParty Win 95%s\n";
	for (auto [partyIndex, median] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << sim.latestReport.getPartySeatsPercentile(partyIndex, 95.0f) << ",";
	}
	summaryFile << "\nCoalition Win 95%\n";
	summaryFile << sim.latestReport.getCoalitionSeatsPercentile(95.0f);
	summaryFile << "\nParty Win 99.9%s\n";
	for (auto [partyIndex, median] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << sim.latestReport.getPartySeatsPercentile(partyIndex, 99.9f) << ",";
	}
	summaryFile << "\nCoalition Win 99.9%\n";
	summaryFile << sim.latestReport.getCoalitionSeatsPercentile(99.9f);
	summaryFile << "\nParty Vote Shares\n";
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << sim.latestReport.getFpSampleExpectation(partyIndex) << ",";
	}
	summaryFile << "\n2PP Vote Share (Labor) - mean\n";
	summaryFile << sim.latestReport.getTppSampleExpectation() << ",";
	summaryFile << "\n2PP Vote Share (Labor) - 0.1%\n";
	summaryFile << sim.latestReport.getTppSamplePercentile(0.1f) << ",";
	summaryFile << "\n2PP Vote Share (Labor) - 5%\n";
	summaryFile << sim.latestReport.getTppSamplePercentile(5.0f) << ",";
	summaryFile << "\n2PP Vote Share (Labor) - median\n";
	summaryFile << sim.latestReport.getTppSampleMedian() << ",";
	summaryFile << "\n2PP Vote Share (Labor) - 95%\n";
	summaryFile << sim.latestReport.getTppSamplePercentile(95.0f) << ",";
	summaryFile << "\n2PP Vote Share (Labor) - 99.9%\n";
	summaryFile << sim.latestReport.getTppSamplePercentile(99.9f) << ",";
	summaryFile << "\nSeat Names\n";
	for (auto const& name : sim.latestReport.seatName) {
		summaryFile << name << ",";
	}
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << "\nSeat Win Percentages - " << name << "\n";
		for (auto index = 0; index < int(sim.latestReport.seatName.size()); ++index) {
			summaryFile << sim.latestReport.seatPartyWinPercent[index][partyIndex] << ",";
		}
	}
	for (auto [partyIndex, name] : sim.latestReport.partyName) {
		if (!useParties.contains(partyIndex)) continue;
		summaryFile << "\nSeat FP Estimate - " << name << "\n";
		for (auto index = 0; index < int(sim.latestReport.seatName.size()); ++index) {
			if (sim.latestReport.seatPartyMeanFpShare[index].contains(partyIndex))
				summaryFile << sim.latestReport.seatPartyMeanFpShare[index][partyIndex] << ",";
			else
				summaryFile << "0,";
		}
	}
	summaryFile << "\nFP percent counted\n";
	for (auto index = 0; index < int(sim.latestReport.seatName.size()); ++index) {
		summaryFile << run.liveElection->getSeatFpCompletion(sim.latestReport.seatName[index]) << ",";
	}
	summaryFile << "\nTPP swing basis\n";
	for (auto index = 0; index < int(sim.latestReport.seatName.size()); ++index) {
		summaryFile << run.liveElection->getSeatTppCompletion(sim.latestReport.seatName[index]) << ",";
	}
	summaryFile << "\nTCP swing basis\n";
	for (auto index = 0; index < int(sim.latestReport.seatName.size()); ++index) {
		summaryFile << run.liveElection->getSeatTcpCompletion(sim.latestReport.seatName[index]) << ",";
	}
	auto internals = run.liveElection->getInternals();
	auto const writeValuesForKeys = [&summaryFile](
		auto const& keys, auto const& values) {
		for (auto const& [key, unused] : keys) {
			auto const value = values.find(key);
			if (value != values.end()) {
				summaryFile << value->second;
			}
			summaryFile << ",";
		}
	};
	summaryFile << "\nBooth type biases\n";
	for (auto const& [boothType, bias] : internals.boothTypeBiases) {
		summaryFile << Results2::Booth::boothTypeName(boothType) << ",";
	}
	summaryFile << "\n";
	writeValuesForKeys(
		internals.boothTypeBiases, internals.boothTypeBiases);
	summaryFile << "\nBooth type bias StdDev\n";
	writeValuesForKeys(
		internals.boothTypeBiases, internals.boothTypeBiasStdDev);
	summaryFile << "\nBooth type bias raw\n";
	writeValuesForKeys(
		internals.boothTypeBiases, internals.boothTypeBiasesRaw);
	summaryFile << "\nBooth type source count\n";
	writeValuesForKeys(
		internals.boothTypeBiases, internals.boothTypeSourceCount);
	summaryFile << "\nBooth type vote count\n";
	writeValuesForKeys(
		internals.boothTypeBiases, internals.boothTypeVoteCount);
	summaryFile << "\nVote type biases\n";
	for (auto const& [voteType, bias] : internals.voteTypeBiases) {
		summaryFile << Results2::voteTypeName(voteType) << ",";
	}
	summaryFile << "\n";
	writeValuesForKeys(
		internals.voteTypeBiases, internals.voteTypeBiases);
	summaryFile << "\nVote type bias StdDev\n";
	writeValuesForKeys(
		internals.voteTypeBiases, internals.voteTypeBiasStdDev);
	summaryFile << "\nVote type bias raw\n";
	writeValuesForKeys(
		internals.voteTypeBiases, internals.voteTypeBiasesRaw);
	summaryFile << "\nVote type source count\n";
	writeValuesForKeys(
		internals.voteTypeBiases, internals.voteTypeSourceCount);
	summaryFile << "\nVote type vote count\n";
	writeValuesForKeys(
		internals.voteTypeBiases, internals.voteTypeVoteCount);
	summaryFile << "\nInternal projected 2PP\n";
	summaryFile << internals.projected2pp;
	summaryFile << "\nRaw 2PP deviation\n";
	summaryFile << internals.raw2ppDeviation;
}

StanModel const& SimulationCompletion::baseModel() const
{
	return project.projections().view(sim.settings.baseProjection).getBaseModel(project.models());
}

void SimulationCompletion::updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int & bound)
{
	// ProbabilityBounds uses half-percent units, so compare counts exactly
	// rather than introducing floating-point ambiguity at a percentile boundary.
	if (int64_t(partyCount) * 200 >
			int64_t(iterations) * probThreshold &&
		bound == -1) {
		bound = numSeats;
	}
}

bool SimulationCompletion::doLogging() const {
	return !run.doingBettingOddsCalibrations && !run.doingLiveBaselineSimulation;
}

