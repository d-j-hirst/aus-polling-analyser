#include "SimulationCompletion.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

using Mp = Simulation::MajorParty;

constexpr std::array<int, NumProbabilityBoundIndices> ProbabilityBounds = { 1, 5, 20, 50, 150, 180, 195,199 };

SimulationCompletion::SimulationCompletion(PollingProject & project, Simulation & sim, SimulationRun & run, int iterations)
	: project(project), run(run), sim(sim), iterations(iterations)
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

	recordVoteTotalStats();

	recordProbabilityBands();

	recordSeatPartyWinPercentages();

	recordSeatFpVoteStats();
	recordSeatTcpVoteStats();
	recordSeatSwingFactors();

	recordTrends();

	recordModelledPolls();

	recordReportSettings();

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
	if (!run.doingBettingOddsCalibrations) logger << "Party majorities:\n";
	for (auto [partyIndex, result] : run.partyMajority) {
		sim.latestReport.majorityPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (!run.doingBettingOddsCalibrations) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.majorityPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.majorityPercent.contains(0)) sim.latestReport.majorityPercent[0] = 0;
	if (!sim.latestReport.majorityPercent.contains(1)) sim.latestReport.majorityPercent[1] = 0;
	if (!run.doingBettingOddsCalibrations) logger << "Party minorities:\n";
	for (auto [partyIndex, result] : run.partyMinority) {
		sim.latestReport.minorityPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (!run.doingBettingOddsCalibrations) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.minorityPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.minorityPercent.contains(0)) sim.latestReport.minorityPercent[0] = 0;
	if (!sim.latestReport.minorityPercent.contains(1)) sim.latestReport.minorityPercent[1] = 0;
	if (!run.doingBettingOddsCalibrations) logger << "Party most-seats:\n";
	for (auto [partyIndex, result] : run.partyMostSeats) {
		sim.latestReport.mostSeatsPercent[partyIndex] = float(result) / float(iterations) * 100.0f;
		if (!run.doingBettingOddsCalibrations) logger << " " << sim.latestReport.partyAbbr[partyIndex] << " - " << sim.latestReport.mostSeatsPercent[partyIndex] << "%\n";
	}
	if (!sim.latestReport.mostSeatsPercent.contains(0)) sim.latestReport.mostSeatsPercent[0] = 0;
	if (!sim.latestReport.mostSeatsPercent.contains(1)) sim.latestReport.mostSeatsPercent[1] = 0;
	sim.latestReport.tiedPercent = float(run.tiedParliament) / float(iterations) * 100.0f;

	if (!run.doingBettingOddsCalibrations) logger << "Tied: - " << sim.latestReport.tiedPercent << "%\n";
	sim.latestReport.partyOneSwing = sim.latestReport.partyOneSwing / double(iterations);
}

void SimulationCompletion::calculatePartyWinExpectations()
{
	for (auto [partyIndex, frequency] : sim.latestReport.partySeatWinFrequency) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.seats().count(); ++seatNum) {
			totalSeats += seatNum * frequency[seatNum];
		}
		sim.latestReport.partyWinExpectation[partyIndex] = float(totalSeats) / float(iterations);
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
			sim.latestReport.regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(iterations);
		}
	}
}

void SimulationCompletion::recordVoteTotalStats()
{
	// Note: for now this method doesn't actually save anything, just prints debug info
	// It's recalculated in identical fashion in ReportUploader.cpp
	// Obviously, this should be refactored when time permits
	const std::vector<float> thresholds = { 0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f };
	//j["voteTotalThresholds"] = thresholds;
	typedef std::vector<float> VF;
	std::vector<float> tppFrequencies = std::accumulate(thresholds.begin(), thresholds.end(), VF(),
		[this](VF v, float percentile) {
			v.push_back(sim.latestReport.getTppSamplePercentile(percentile));
			return v;
		});
	if (!run.isLiveManual()) {
		std::map<int, VF> fpFrequencies;
		for (auto [partyIndex, frequencies] : sim.latestReport.partyPrimaryFrequency) {
			if (sim.latestReport.getFpSampleExpectation(partyIndex) > 0.0f) {
				VF partyThresholds = std::accumulate(thresholds.begin(), thresholds.end(), VF(),
					[this, partyIndex](VF v, float percentile) {
						v.push_back(sim.latestReport.getFpSamplePercentile(partyIndex, percentile));
						return v;
					});
				fpFrequencies[partyIndex] = partyThresholds;
			}
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
	sim.latestReport.partyColour[CoalitionPartnerIndex] = Party::createColour(0, 0, 256);
	for (int index = 0; index < project.regions().count(); ++index) {
		sim.latestReport.regionName.push_back(project.regions().viewByIndex(index).name);
	}
	for (int index = 0; index < project.seats().count(); ++index) {
		Seat const& seat = project.seats().viewByIndex(index);
		sim.latestReport.seatName.push_back(seat.name);
		int effectiveIncumbent = seat.incumbent;
		if (seat.incumbent == run.indPartyIndex && seat.retirement == true) {
			effectiveIncumbent = seat.tppMargin > 0 ? 0 : 1;
		}
		sim.latestReport.seatIncumbents.push_back(seat.incumbent);
		sim.latestReport.seatMargins.push_back(seat.tppMargin);
		if (effectiveIncumbent == 0) {
			sim.latestReport.seatIncumbentMargins.push_back(seat.tppMargin);
		}
		else if (effectiveIncumbent == 1) {
			sim.latestReport.seatIncumbentMargins.push_back(-seat.tppMargin);
		}
		else if (run.pastSeatResults[index].tcpVotePercent.contains(effectiveIncumbent)) {
			float margin = run.pastSeatResults[index].tcpVotePercent.at(effectiveIncumbent) - 50.0f;
			if (seat.tcpChange.contains(project.parties().view(seat.challenger).abbreviation)) {
				margin -= seat.tcpChange.at(project.parties().view(seat.challenger).abbreviation);
			}
			sim.latestReport.seatIncumbentMargins.push_back(margin);
		}
		else {
			// incumbent is 3rd party and gained seat in by-election, just skip for now
			sim.latestReport.seatIncumbentMargins.push_back(0);
		}
		std::map<int, std::string> candidateNames;
		for (auto [name, party] : seat.candidateNames) {
			int partyIndex = project.parties().indexByShortCode(party);
			candidateNames[partyIndex] = name;
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
			if (!run.doingBettingOddsCalibrations) {
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

		if (!run.doingBettingOddsCalibrations) {
			logger << project.seats().viewByIndex(seatIndex).name << "\n";
			logger << " Fp percent:\n";
		}
		for (auto const& [partyIndex, fpVoteShare] : sim.latestReport.seatPartyMeanFpShare[seatIndex]) {
			if (!run.doingBettingOddsCalibrations) {
				logger << "  ";
				if (partyIndex >= 0) logger << project.parties().viewByIndex(partyIndex).name;
				else if (partyIndex == -1) logger << "Others";
				else if (partyIndex == -2) logger << "Emerging Ind";
				else if (partyIndex == -3) logger << "Emerging Party";
				logger << ": " << fpVoteShare << ", " << "distribution: ";
			}
			auto const& distribution = run.seatPartyFpDistribution[seatIndex][partyIndex];
			int cumulative = iterations - std::accumulate(distribution.begin(), distribution.end(), 0);
			sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex].resize(sim.latestReport.probabilityBands.size());
			int currentProbabilityBand = 0;
			for (int a = 0; a < SimulationRun::FpBucketCount; ++a) {
				if (distribution[a] > 0) {
					float lowerPercentile = float(cumulative) / float(iterations) * 100.0f;
					cumulative += distribution[a];
					float upperPercentile = float(cumulative) / float(iterations) * 100.0f;
					while (currentProbabilityBand < int(sim.latestReport.probabilityBands.size()) && sim.latestReport.probabilityBands[currentProbabilityBand] <
						float(cumulative) / float(iterations) * 100.0f)
					{
						float band = sim.latestReport.probabilityBands[currentProbabilityBand];
						float exactFrac = (band - lowerPercentile) / (upperPercentile - lowerPercentile);
						float exactFp = float(a) + exactFrac;
						if (!a && run.seatPartyFpZeros[seatIndex].contains(partyIndex) &&
							float(currentProbabilityBand) < float(run.seatPartyFpZeros[seatIndex][partyIndex]) / float(iterations) * 100.0f) {
							exactFp = 0.0f;
						}
						sim.latestReport.seatFpProbabilityBand[seatIndex][partyIndex][currentProbabilityBand] = std::clamp(exactFp, 0.0f, 100.0f);
						++currentProbabilityBand;
					}
					if (!run.doingBettingOddsCalibrations) {
						logger << float(a) / float(SimulationRun::FpBucketCount) * 100.0f;
						logger << "-";
						logger << distribution[a];
						logger << ", ";
					}
				}
			}
			if (!run.doingBettingOddsCalibrations) {
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
		if (!run.doingBettingOddsCalibrations) {
			logger << project.seats().viewByIndex(seatIndex).name << "\n";
			logger << " Tcp percent:\n";
		}
		for (auto const& [parties, distribution] : run.seatTcpDistribution[seatIndex]) {
			int total = std::accumulate(distribution.begin(), distribution.end(), 0);
			if (total < iterations / 1000) continue;
			if (!run.doingBettingOddsCalibrations) {
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
			float scenarioPercent = float(total) / float(iterations);
			sim.latestReport.seatTcpScenarioPercent[seatIndex][parties] = scenarioPercent;
			sim.latestReport.seatTcpProbabilityBand[seatIndex][parties].resize(sim.latestReport.probabilityBands.size());

			if (!run.doingBettingOddsCalibrations) {
				logger << " - scenario frequency: " << scenarioPercent << " - distribution: ";
			}
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
					if (!run.doingBettingOddsCalibrations) {
						logger << float(a) / float(SimulationRun::FpBucketCount) * 100.0f;
						logger << "-";
						logger << distribution[a];
						logger << ", ";
					}
				}
			}
			if (!run.doingBettingOddsCalibrations) {
				logger << "\n";
				logger << "   Win rate: " << winPercent << "%\n";
				sim.latestReport.seatTcpWinPercent[seatIndex][parties] = winPercent;
				logger << "   Probability bands: " << sim.latestReport.seatTcpProbabilityBand[seatIndex][parties] << "\n";
			}
		}
		if (project.seats().access(project.seats().indexToId(seatIndex)).livePartyOne != Party::InvalidId) sim.latestReport.seatHideTcps[seatIndex] = true;
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
			"Adjustment towards seat polling;" + std::to_string(seatPollEffectAverage));
		float totalLocalEffects = 0.0f;
		for (auto const [name, impact] : run.seatLocalEffects[seatIndex]) totalLocalEffects += impact;
		for (auto const [name, impact] : run.seatLocalEffects[seatIndex]) {
			float scaledEffect = impact * seatLocalEffectsAverage / totalLocalEffects;
			sim.latestReport.swingFactors[seatIndex].push_back(
				name + ";" + std::to_string(scaledEffect));
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
	if (float(partyCount) > float(iterations) * 0.005f * float(probThreshold) && bound == -1) bound = numSeats;
}
