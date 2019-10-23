#include "SimulationRun.h"

#include "CountProgress.h"
#include "PollingProject.h"
#include "Simulation.h"

static std::random_device rd;
static std::mt19937 gen;

const float LongshotOddsThreshold = 2.5f;
const float seatStdDev = 2.0f; // Seat standard deviation, should remove this and use a user-input parameter instead

static const std::array<float, 3> PreferenceConsistencyBase = { 1.2f, 1.4f, 1.8f };

using Mp = Simulation::MajorParty;

void SimulationRun::run() {

	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);

	if (int(thisProjection.getProjectionLength()) == 0) return;

	gen.seed(rd());

	resetRegionSpecificOutput();

	resetSeatSpecificOutput();

	accumulateRegionStaticInfo();

	resetPpvcBiasAggregates();

	// This will also accumulate the PPVC bias aggregates
	cacheBoothData();

	determinePpvcBias();

	// this stores the manually input results for seats so that they're ready for the simulations
	// to use them if set to "Live Manual"
	project.updateLatestResultsForSeats();

	determinePreviousVoteEnrolmentRatios();

	resizeRegionSeatCountOutputs();

	countInitialRegionSeatLeads();

	calculateTotalPopulation();

	float liveOverallSwing = 0.0f; // swing to partyOne
	float liveOverallPercent = 0.0f;
	float classicSeatCount = 0.0f;
	// A bunch of votes from one seat is less likely to be representative than from a wide variety of seats,
	// so this factor is introduced to avoid a small number of seats from having undue influence early in the count
	float sampleRepresentativeness = 0.0f;
	int total2cpVotes = 0;
	int totalEnrolment = 0;
	if (sim.isLive()) {
		for (auto&[key, seat] : project.seats()) {
			if (!seat.isClassic2pp(sim.isLiveAutomatic())) continue;
			++classicSeatCount;
			Region& thisRegion = project.regions().access(seat.region);
			++thisRegion.classicSeatCount;
			if (!seat.latestResult) continue;
			bool incIsOne = seat.incumbent == 0;
			float percentCounted = seat.latestResult->getPercentCountedEstimate();
			float weightedSwing = seat.latestResult->incumbentSwing * (incIsOne ? 1.0f : -1.0f) * percentCounted;
			liveOverallSwing += weightedSwing;
			thisRegion.liveSwing += weightedSwing;
			liveOverallPercent += percentCounted;
			thisRegion.livePercentCounted += percentCounted;
			sampleRepresentativeness += std::min(2.0f, percentCounted) * 0.5f;
			total2cpVotes += seat.latestResults->total2cpVotes();
			totalEnrolment += seat.latestResults->enrolment;
		}
		if (liveOverallPercent) {
			liveOverallSwing /= liveOverallPercent;
			liveOverallPercent /= classicSeatCount;
			sampleRepresentativeness /= classicSeatCount;
			sampleRepresentativeness = std::sqrt(sampleRepresentativeness);
			for (auto& regionPair : project.regions()) {
				Region& thisRegion = regionPair.second;
				if (!thisRegion.livePercentCounted) continue;
				thisRegion.liveSwing /= thisRegion.livePercentCounted;
				thisRegion.livePercentCounted /= thisRegion.classicSeatCount;
			}
		}
	}

	sim.total2cpPercentCounted = (float(totalEnrolment) ? float(total2cpVotes) / float(totalEnrolment) : 0);

	std::array<int, 2> partyMajority = { 0, 0 };
	std::array<int, 2> partyMinority = { 0, 0 };
	int hungParliament = 0;

	sim.partySeatWinFrequency.clear();
	sim.partySeatWinFrequency.resize(project.parties().count(), std::vector<int>(project.seats().count() + 1));
	sim.othersWinFrequency.clear();
	sim.othersWinFrequency.resize(project.seats().count() + 1);
	sim.partyOneSwing = 0.0;

	int projectionSize = thisProjection.getProjectionLength();
	float pollOverallSwing = thisProjection.getMeanProjection(projectionSize - 1) - sim.settings.prevElection2pp;
	float pollOverallStdDev = thisProjection.getSdProjection(projectionSize - 1);
	for (currentIteration = 0; currentIteration < sim.settings.numIterations; ++currentIteration) {

		// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
		std::vector<std::vector<int>> regionSeatCount(project.parties().count(), std::vector<int>(project.regions().count()));

		// First, randomly determine the national swing for this particular simulation
		float simulationOverallSwing = std::normal_distribution<float>(pollOverallSwing, pollOverallStdDev)(gen);

		if (sim.isLive() && liveOverallPercent) {
			float liveSwing = liveOverallSwing;
			float liveStdDev = stdDevOverall(liveOverallPercent);
			liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
			float priorWeight = 0.5f;
			float liveWeight = 1.0f / (liveStdDev * liveStdDev) * sampleRepresentativeness;
			simulationOverallSwing = (simulationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);

			constexpr float ppvcBiasStdDev = 4.0f;
			float ppvcBiasRandom = std::normal_distribution<float>(0.0f, ppvcBiasStdDev)(gen);
			ppvcBias = ppvcBiasObserved * ppvcBiasConfidence + ppvcBiasRandom * (1.0f - ppvcBiasConfidence);
		}

		// Add random variation to the state-by-state swings and calculate the implied national 2pp
		// May be some minor floating-point errors here but they won't matter in the scheme of things
		float tempOverallSwing = 0.0f;
		for (auto& regionPair : project.regions()) {
			Region& thisRegion = regionPair.second;
			// Calculate mean of the region's swing after accounting for decay to the mean over time.
			float regionMeanSwing = pollOverallSwing +
				thisRegion.swingDeviation * pow(1.0f - sim.settings.stateDecay, thisProjection.getProjectionLength());
			// Add random noise to the region's swing level
			float swingSD = sim.settings.stateSD + thisRegion.additionalUncertainty;
			if (swingSD > 0) {
				thisRegion.simulationSwing =
					std::normal_distribution<float>(regionMeanSwing, sim.settings.stateSD + thisRegion.additionalUncertainty)(gen);
			}
			else {
				thisRegion.simulationSwing = regionMeanSwing;
			}

			if (sim.isLive() && thisRegion.livePercentCounted) {
				float liveSwing = thisRegion.liveSwing;
				float liveStdDev = stdDevSingleSeat(thisRegion.livePercentCounted);
				liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
				float priorWeight = 0.5f;
				float liveWeight = 1.0f / (liveStdDev * liveStdDev);
				priorWeight, liveWeight;
				thisRegion.simulationSwing = (thisRegion.simulationSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
			}

			tempOverallSwing += thisRegion.simulationSwing * thisRegion.population;
		}
		tempOverallSwing /= totalPopulation;

		// Adjust regional swings to keep the implied overall 2pp the same as that actually projected
		float regionSwingAdjustment = simulationOverallSwing - tempOverallSwing;
		for (auto& regionPair : project.regions()) {
			Region& thisRegion = regionPair.second;
			thisRegion.simulationSwing += regionSwingAdjustment;
		}

		sim.partyOneSwing += double(simulationOverallSwing);

		std::vector<int> partyWins(project.parties().count());

		// Now cycle through all the seats and generate a result for each
		for (auto&[key, seat] : project.seats()) {

			// First determine if this seat is "classic" (main-parties only) 2CP, which determines how we get a result and the winner
			bool isClassic2CP = seat.isClassic2pp(sim.isLive());

			if (isClassic2CP) {
				Region const& thisRegion = project.regions().access(seat.region);
				bool incIsOne = seat.incumbent == 0; // stores whether the incumbent is Party One
													 // Add or subtract the simulation regional deviation depending on which party is incumbent
				float newMargin = seat.margin + thisRegion.simulationSwing * (incIsOne ? 1.0f : -1.0f);
				// Add modifiers for known local effects (these are measured as positive if favouring the incumbent)
				newMargin += seat.localModifier;
				// Remove the average local modifier across the region
				newMargin -= thisRegion.localModifierAverage * (incIsOne ? 1.0f : -1.0f);
				// Add random noise to the new margin of this seat
				newMargin += std::normal_distribution<float>(0.0f, seatStdDev)(gen);
				// Now work out the margin of the seat from actual results if live
				SeatResult result = calculateLiveResultClassic2CP(seat, newMargin);

				float incumbentNewMargin = result.margin * (result.winner == seat.incumbent ? 1.0f : -1.0f);
				// Margin for this simulation is finalised, record it for later averaging
				seat.simulatedMarginAverage += incumbentNewMargin;
				// If the margin is greater than zero, the incumbent wins the seat.
				seat.winner = result.winner;
				// Sometimes a classic 2pp seat may also have a independent with a significant chance,
				// but not high enough to make the top two - if so this will give a certain chance to
				// override the swing-based result with a win from the challenger
				if ((!sim.isLiveAutomatic() || !seat.hasLiveResults()) && seat.challenger2Odds < 8.0f && !seat.overrideBettingOdds) {
					OddsInfo oddsInfo = calculateOddsInfo(seat);
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand >= oddsInfo.topTwoChance) seat.winner = seat.challenger2;
				}
				// If a seat is marked as classic by the AEC but betting odds say it isn't, possibly use the betting
				// odds to give a more accurate reflection
				// For e.g. 2016 Cowper had Coalition vs. Independent in betting but was classic in early count,
				// so the independent's recorded votes were considered insignificant and the seat was overly favourable
				// to the Coalition.
				if (result.significance < 1.0f) {
					if (!project.parties().oppositeMajors(seat.incumbent, seat.challenger)) {
						if (!sim.isLiveAutomatic() || !seat.winner || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > result.significance) {
							if (sim.isLiveAutomatic() && seat.livePartyOne) {
								float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
								if (uniformRand < seat.partyTwoProb) {
									seat.winner = seat.livePartyTwo;
								}
								else if (seat.livePartyThree && uniformRand < seat.partyTwoProb + seat.partyThreeProb) {
									seat.winner = seat.livePartyThree;
								}
								else {
									seat.winner = seat.livePartyOne;
								}
							}
							else {
								seat.winner = simulateWinnerFromBettingOdds(seat);
							}
						}
					}
				}
			}
			else {
				float liveSignificance = 0.0f;
				if (sim.isLiveAutomatic()) {
					SeatResult result = calculateLiveResultNonClassic2CP(seat);
					seat.winner = result.winner;
					liveSignificance = result.significance;
				}
				// If we haven't got very far into the live count, it might be unrepresentative,
				// so randomly choose between seat betting odds and the actual live count until
				// more results come in.
				if (liveSignificance < 1.0f) {
					if (!sim.isLiveAutomatic() || seat.winner == Party::InvalidId || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > liveSignificance) {
						if (sim.isLive() && seat.livePartyOne != Party::InvalidId && seat.livePartyTwo != Party::InvalidId) {
							float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
							if (uniformRand < seat.partyTwoProb) {
								seat.winner = seat.livePartyTwo;
							}
							else if (seat.livePartyThree != Party::InvalidId && uniformRand < seat.partyTwoProb + seat.partyThreeProb) {
								seat.winner = seat.livePartyThree;
							}
							else {
								seat.winner = seat.livePartyOne;
							}
						}
						else {
							seat.winner = simulateWinnerFromBettingOdds(seat);
						}
					}
				}
			}

			// If the winner is the incumbent, record this down in the seat's numbers
			seat.incumbentWins += (seat.winner == seat.incumbent ? 1 : 0);

			if (seat.winner == 0) ++seat.partyOneWinRate;
			else if (seat.winner == 1) ++seat.partyTwoWinRate;
			else ++seat.partyOthersWinRate;

			int winnerIndex = project.parties().idToIndex(seat.winner);
			if (winnerIndex != PartyCollection::InvalidIndex) {
				partyWins[winnerIndex]++;
				int regionIndex = project.regions().idToIndex(seat.region);
				++regionSeatCount[winnerIndex][regionIndex];
			}
		}

		//Assign all the wins from coalition third-parties to the respective major party
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			Party const& thisParty = project.parties().viewByIndex(partyNum);
			if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyOne) {
				partyWins[0] += partyWins[partyNum];
			}
			else if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyTwo) {
				partyWins[1] += partyWins[partyNum];
			}
		}


		//Get the number of seats supporting each major party in a minority government
		std::array<int, 2> partySupport = { partyWins[0], partyWins[1] };
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			Party const& thisParty = project.parties().viewByIndex(partyNum);
			if (thisParty.supportsParty == Party::SupportsParty::One) {
				partySupport[0] += partyWins[partyNum];
			}
			else if (thisParty.supportsParty == Party::SupportsParty::Two) {
				partySupport[1] += partyWins[partyNum];
			}
		}

		int minimumForMajority = project.seats().count() / 2 + 1;

		// Look at the overall result and classify it
		if (partyWins[0] >= minimumForMajority) ++partyMajority[Simulation::MajorParty::One];
		else if (partySupport[0] >= minimumForMajority) ++partyMinority[Mp::One];
		else if (partyWins[1] >= minimumForMajority) ++partyMajority[Mp::Two];
		else if (partySupport[1] >= minimumForMajority) ++partyMinority[Mp::Two];
		else ++hungParliament;

		int othersWins = 0;
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			++sim.partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
			if (partyIndex > 1) othersWins += partyWins[partyIndex];
			for (auto& regionPair : project.regions()) {
				Region& thisRegion = regionPair.second;
				++thisRegion.partyWins[partyIndex][regionSeatCount[partyIndex][project.regions().idToIndex(regionPair.first)]];
			}
		}
		++sim.othersWinFrequency[othersWins];

	}

	sim.incumbentWinPercent.resize(project.seats().count() + 1);

	// Go through each seat and update the incumbent wins %
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat& thisSeat = project.seats().access(project.seats().indexToId(seatIndex));
		sim.incumbentWinPercent[seatIndex] = float(thisSeat.incumbentWins) / float(sim.settings.numIterations) * 100.0f;
		thisSeat.incumbentWinPercent = sim.incumbentWinPercent[seatIndex];
		thisSeat.partyOneWinRate /= double(sim.settings.numIterations);
		thisSeat.partyTwoWinRate /= double(sim.settings.numIterations);
		thisSeat.partyOthersWinRate /= double(sim.settings.numIterations);
		thisSeat.simulatedMarginAverage /= float(sim.settings.numIterations);
	}

	sim.partyWinExpectation.resize(project.parties().count());

	for (Mp party = Mp::First; party <= Mp::Last; ++party) {
		sim.majorityPercent[party] = float(partyMajority[party]) / float(sim.settings.numIterations) * 100.0f;
		sim.minorityPercent[party] = float(partyMinority[party]) / float(sim.settings.numIterations) * 100.0f;
	}
	sim.hungPercent = float(hungParliament) / float(sim.settings.numIterations) * 100.0f;
	sim.partyOneSwing = sim.partyOneSwing / double(sim.settings.numIterations);

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.seats().count(); ++seatNum) {
			totalSeats += seatNum * sim.partySeatWinFrequency[partyIndex][seatNum];
		}
		sim.partyWinExpectation[partyIndex] = float(totalSeats) / float(sim.settings.numIterations);
	}

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
		updateProbabilityBounds(partyOneCount, numSeats, 1, sim.partyOneProbabilityBounds[0]);
		updateProbabilityBounds(partyOneCount, numSeats, 5, sim.partyOneProbabilityBounds[1]);
		updateProbabilityBounds(partyOneCount, numSeats, 20, sim.partyOneProbabilityBounds[2]);
		updateProbabilityBounds(partyOneCount, numSeats, 50, sim.partyOneProbabilityBounds[3]);
		updateProbabilityBounds(partyOneCount, numSeats, 150, sim.partyOneProbabilityBounds[4]);
		updateProbabilityBounds(partyOneCount, numSeats, 180, sim.partyOneProbabilityBounds[5]);
		updateProbabilityBounds(partyOneCount, numSeats, 195, sim.partyOneProbabilityBounds[6]);
		updateProbabilityBounds(partyOneCount, numSeats, 199, sim.partyOneProbabilityBounds[7]);

		updateProbabilityBounds(partyTwoCount, numSeats, 1, sim.partyTwoProbabilityBounds[0]);
		updateProbabilityBounds(partyTwoCount, numSeats, 5, sim.partyTwoProbabilityBounds[1]);
		updateProbabilityBounds(partyTwoCount, numSeats, 20, sim.partyTwoProbabilityBounds[2]);
		updateProbabilityBounds(partyTwoCount, numSeats, 50, sim.partyTwoProbabilityBounds[3]);
		updateProbabilityBounds(partyTwoCount, numSeats, 150, sim.partyTwoProbabilityBounds[4]);
		updateProbabilityBounds(partyTwoCount, numSeats, 180, sim.partyTwoProbabilityBounds[5]);
		updateProbabilityBounds(partyTwoCount, numSeats, 195, sim.partyTwoProbabilityBounds[6]);
		updateProbabilityBounds(partyTwoCount, numSeats, 199, sim.partyTwoProbabilityBounds[7]);

		updateProbabilityBounds(othersCount, numSeats, 1, sim.othersProbabilityBounds[0]);
		updateProbabilityBounds(othersCount, numSeats, 5, sim.othersProbabilityBounds[1]);
		updateProbabilityBounds(othersCount, numSeats, 20, sim.othersProbabilityBounds[2]);
		updateProbabilityBounds(othersCount, numSeats, 50, sim.othersProbabilityBounds[3]);
		updateProbabilityBounds(othersCount, numSeats, 150, sim.othersProbabilityBounds[4]);
		updateProbabilityBounds(othersCount, numSeats, 180, sim.othersProbabilityBounds[5]);
		updateProbabilityBounds(othersCount, numSeats, 195, sim.othersProbabilityBounds[6]);
		updateProbabilityBounds(othersCount, numSeats, 199, sim.othersProbabilityBounds[7]);
	}

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

	sim.lastUpdated = wxDateTime::Now();
}


void SimulationRun::resetRegionSpecificOutput()
{
	for (auto&[key, thisRegion] : project.regions()) {
		thisRegion.localModifierAverage = 0.0f;
		thisRegion.seatCount = 0;

		thisRegion.liveSwing = 0.0f;
		thisRegion.livePercentCounted = 0.0f;
		thisRegion.classicSeatCount = 0;
	}
}

void SimulationRun::resetSeatSpecificOutput()
{
	for (auto&[key, seat] : project.seats()) {
		seat.incumbentWins = 0;
		seat.partyOneWinRate = 0.0f;
		seat.partyTwoWinRate = 0.0f;
		seat.partyOthersWinRate = 0.0f;
		seat.simulatedMarginAverage = 0;
		seat.latestResult = nullptr;
	}
}

void SimulationRun::accumulateRegionStaticInfo()
{
	for (auto& [key, seat] : project.seats()) {
		bool isPartyOne = (seat.incumbent == 0);
		Region& thisRegion = project.regions().access(seat.region);
		thisRegion.localModifierAverage += seat.localModifier * (isPartyOne ? 1.0f : -1.0f);
		++thisRegion.seatCount;
	}
	for (auto& [key, region] : project.regions()) {
		region.localModifierAverage /= float(region.seatCount);
	}
}

void SimulationRun::resetPpvcBiasAggregates()
{
	// Set up anything that needs to be prepared for seats
	ppvcBiasNumerator = 0.0f;
	ppvcBiasDenominator = 0.0f;
	totalOldPpvcVotes = 0;
}

void SimulationRun::cacheBoothData()
{
	for (auto&[key, seat] : project.seats()) {
		if (sim.isLiveAutomatic()) determineSeatCachedBoothData(seat);
	}
}

void SimulationRun::determinePpvcBias()
{
	if (!ppvcBiasDenominator) {
		// whether or not this is a live simulation, if there hasn't been any PPVC votes recorded
		// then we can set these to zero and it will be assumed there is no PPVC bias
		ppvcBias = 0.0f;
		ppvcBiasConfidence = 0.0f;
		return;
	}
	ppvcBias = ppvcBiasNumerator / ppvcBiasDenominator;
	ppvcBiasConfidence = std::clamp(ppvcBiasDenominator / float(totalOldPpvcVotes) * 5.0f, 0.0f, 1.0f);

	logger << ppvcBiasNumerator << " " << ppvcBiasDenominator << " " << ppvcBias << " " << totalOldPpvcVotes <<
		" " << ppvcBiasConfidence << " - ppvc bias measures\n";
}

void SimulationRun::determinePreviousVoteEnrolmentRatios()
{
	if (!sim.isLiveAutomatic()) return;

	int ordinaryVoteNumerator = 0;
	int declarationVoteNumerator = 0;
	int voteDenominator = 0;
	for (auto&[key, seat] : project.seats()) {
		if (seat.previousResults) {
			ordinaryVoteNumerator += seat.previousResults->ordinaryVotes();
			declarationVoteNumerator += seat.previousResults->declarationVotes();
			voteDenominator += seat.previousResults->enrolment;
		}
	}
	if (!voteDenominator) return;
	previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

void SimulationRun::resizeRegionSeatCountOutputs()
{
	// Resize regional seat counts based on the counted number of seats for each region
	for (auto& [key, region] : project.regions()) {
		region.partyLeading.clear();
		region.partyWins.clear();
		region.partyLeading.resize(project.parties().count());
		region.partyWins.resize(project.parties().count(), std::vector<int>(region.seatCount + 1));
	}
}

void SimulationRun::countInitialRegionSeatLeads()
{
	for (auto&[key, seat] : project.seats()) {
		Region& thisRegion = project.regions().access(seat.region);
		++thisRegion.partyLeading[project.parties().idToIndex(seat.getLeadingParty())];
	}
}

void SimulationRun::calculateTotalPopulation()
{
	// Some setup - calculating total population here since it's constant across all simulations
	totalPopulation = 0.0;
	for (auto&[key, region] : project.regions()) {
		totalPopulation += float(region.population);
	}
}

void SimulationRun::determineSeatCachedBoothData(Seat& seat)
{
	if (!seat.latestResults) return;
	int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	if (!firstCandidateId || !secondCandidateId) return; // maverick results mean we shouldn't try to estimate 2cp swings
	Party::Id firstSeatParty = project.getPartyByCandidate(firstCandidateId);
	Party::Id secondSeatParty = project.getPartyByCandidate(secondCandidateId);
	seat.tcpTally[0] = 0;
	seat.tcpTally[1] = 0;
	int newComparisonVotes = 0;
	int oldComparisonVotes = 0;
	float nonPpvcSwingNumerator = 0.0f;
	float nonPpvcSwingDenominator = 0.0f; // total number of votes in counted non-PPVC booths
	float ppvcSwingNumerator = 0.0f;
	float ppvcSwingDenominator = 0.0f; // total number of votes in counted PPVC booths
	int seatFirstPartyPreferences = 0;
	float seatSecondPartyPreferences = 0;
	for (auto boothId : seat.latestResults->booths) {
		Results::Booth const& booth = project.getBooth(boothId);
		Party::Id firstBoothParty = project.getPartyByCandidate(booth.tcpCandidateId[0]);
		Party::Id secondBoothParty = project.getPartyByCandidate(booth.tcpCandidateId[1]);
		bool isInSeatOrder = firstBoothParty == firstSeatParty;
		bool isPpvc = booth.isPPVC();
		if (booth.hasNewResults()) {
			seat.tcpTally[0] += float(isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
			seat.tcpTally[1] += float(isInSeatOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
		}
		if (booth.hasOldResults()) {
			if (isPpvc) totalOldPpvcVotes += booth.totalOldTcpVotes();
		}
		if (booth.hasOldAndNewResults()) {
			oldComparisonVotes += booth.totalOldTcpVotes();
			newComparisonVotes += booth.totalNewTcpVotes();
			bool directMatch = firstBoothParty == 0 && secondBoothParty == 1;
			bool oppositeMatch = secondBoothParty == 0 && firstBoothParty == 1;
			if (!isPpvc) {
				if (directMatch) {
					nonPpvcSwingNumerator += booth.rawSwing() * booth.totalNewTcpVotes();
					nonPpvcSwingDenominator += booth.totalNewTcpVotes();
				}
				else if (oppositeMatch) {
					nonPpvcSwingNumerator -= booth.rawSwing() * booth.totalNewTcpVotes();
					nonPpvcSwingDenominator += booth.totalNewTcpVotes();
				}
			}
			else {
				if (directMatch) {
					ppvcSwingNumerator += booth.rawSwing() * booth.totalNewTcpVotes();
					ppvcSwingDenominator += booth.totalNewTcpVotes();
				}
				else if (oppositeMatch) {
					ppvcSwingNumerator -= booth.rawSwing() * booth.totalNewTcpVotes();
					ppvcSwingDenominator += booth.totalNewTcpVotes();
				}
			}
		}
		if (booth.hasNewResults() && booth.totalNewFpVotes()) {
			// sometimes Fp and Tcp votes for a booth are not properly synchronised, this makes sure they're about the same
			if (abs(booth.totalNewTcpVotes() - booth.totalNewFpVotes()) < std::min(10, booth.totalNewTcpVotes() / 50 - 1)) {
				int totalDistributedVotes = 0;
				int firstPartyFpVotes = 0;
				int firstPartyTcp = isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1];
				for (auto const& candidate : booth.fpCandidates) {
					// need to use candidate IDs here since sometimes there may be two candidates
					// standing for the same "party" (e.g. independents or Coalition)
					// and we only want to match the one(s) that's actually in the 2cp
					int candidateId = candidate.candidateId;
					if (candidateId != firstCandidateId && candidateId != secondCandidateId) {
						totalDistributedVotes += candidate.fpVotes;
					}
					else if (candidateId == firstCandidateId) {
						firstPartyFpVotes = candidate.fpVotes;
					}
				}
				int boothFirstPartyPreferences = firstPartyTcp - firstPartyFpVotes;
				int boothSecondPartyPreferences = totalDistributedVotes - boothFirstPartyPreferences;
				// We can't magically detect entry errors but if we're getting a negative preference total that's
				// a pretty good sign that the Tcp has been flipped (as in Warilla THROSBY PPVC in 2016)
				// and we shouldn't use the booth
				if (boothFirstPartyPreferences >= 0 && boothSecondPartyPreferences >= 0) {
					seatFirstPartyPreferences += boothFirstPartyPreferences;
					seatSecondPartyPreferences += boothSecondPartyPreferences;
				}
			}
		}
	}
	if (nonPpvcSwingDenominator && ppvcSwingDenominator) {
		float nonPpvcSwing = nonPpvcSwingNumerator / nonPpvcSwingDenominator;
		float ppvcSwing = ppvcSwingNumerator / ppvcSwingDenominator;
		float ppvcSwingDiff = ppvcSwing - nonPpvcSwing;
		float weightedSwing = ppvcSwingDiff * ppvcSwingDenominator;
		ppvcBiasNumerator += weightedSwing;
		ppvcBiasDenominator += ppvcSwingDenominator;
	}
	if (seatFirstPartyPreferences + seatSecondPartyPreferences) {
		float totalPreferences = float(seatFirstPartyPreferences + seatSecondPartyPreferences);
		seat.firstPartyPreferenceFlow = float(seatFirstPartyPreferences) / totalPreferences;
		seat.preferenceFlowVariation = std::clamp(0.1f - totalPreferences / float(seat.latestResults->enrolment), 0.03f, 0.1f);

		if (firstSeatParty != Party::InvalidId && secondSeatParty != Party::InvalidId) {
			logger << seatFirstPartyPreferences << " " << seatSecondPartyPreferences << " " <<
				seat.firstPartyPreferenceFlow << " " << seat.preferenceFlowVariation << " preference flow to " <<
				project.parties().view(firstSeatParty).name << " vs " << project.parties().view(secondSeatParty).name << " - " << seat.name << "\n";
		}
	}

	seat.individualBoothGrowth = (oldComparisonVotes ? float(newComparisonVotes) / float(oldComparisonVotes) : 1);
}

SimulationRun::OddsInfo SimulationRun::calculateOddsInfo(Seat const& thisSeat)
{
	float incumbentOdds = (thisSeat.incumbentOdds > LongshotOddsThreshold ?
		pow(thisSeat.incumbentOdds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat.incumbentOdds);
	float challengerOdds = (thisSeat.challengerOdds > LongshotOddsThreshold ?
		pow(thisSeat.challengerOdds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat.challengerOdds);
	float challenger2Odds = (thisSeat.challenger2Odds > LongshotOddsThreshold ?
		pow(thisSeat.challenger2Odds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat.challenger2Odds);
	// Calculate incumbent chance based on adjusted odds
	float totalChance = (1.0f / incumbentOdds + 1.0f / challengerOdds + 1.0f / challenger2Odds);
	float incumbentChance = (1.0f / incumbentOdds) / totalChance;
	float topTwoChance = (1.0f / challengerOdds) / totalChance + incumbentChance;
	return OddsInfo{ incumbentChance, topTwoChance };
}

SimulationRun::SeatResult SimulationRun::calculateLiveResultClassic2CP(Seat const& seat, float priorMargin)
{
	if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
		// All swings are in terms of a swing to candidate 0 as per latest results
		Party::Id firstParty = project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
		Party::Id secondParty = project.getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);
		bool incumbentFirst = firstParty == seat.incumbent;
		float liveSwing = (incumbentFirst ? 1.0f : -1.0f) * seat.latestResult->incumbentSwing;
		std::array<int, 2> tcpTally = seat.tcpTally;

		// At this point we have tallied all the counted votes from booths (matched or otherwise)

		float liveStdDev = stdDevSingleSeat(seat.latestResult->getPercentCountedEstimate());
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 6.0f / (liveStdDev * liveStdDev);
		float priorSwing = (incumbentFirst ? 1.0f : -1.0f) * (priorMargin - seat.margin);
		float remainingVoteSwing = (priorSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);

		// To estimate the vote count for individual booths we need to adjust the previous election's total votes
		// according to how the already-counted individual booth growth as occurred
		// There may not be any old comparison votes in which case we assume no growth
		int mysteryStandardBooths = 0; // count all booths that can't be matched and haven't been counted yet
		int mysteryPPVCBooths = 0; // count all booths that can't be matched and haven't been counted yet
		int mysteryTeamBooths = 0; // count all booths that can't be matched and haven't been counted yet
		for (auto boothId : seat.latestResults->booths) {
			Results::Booth const& booth = project.getBooth(boothId);
			if (booth.hasOldResults() && !booth.hasNewResults()) {
				int estimatedTotalVotes = int(std::round(float(booth.totalOldTcpVotes()) * seat.individualBoothGrowth));
				Party::Id boothFirstParty = project.getPartyByCandidate(booth.tcpCandidateId[0]);
				// Party const* boothSecondParty = project.getPartyByCandidate(booth.tcpCandidateId[1]);
				bool isInSeatOrder = boothFirstParty == firstParty;
				float oldVotes0 = float(isInSeatOrder ? booth.tcpVote[0] : booth.tcpVote[1]);
				float oldVotes1 = float(isInSeatOrder ? booth.tcpVote[1] : booth.tcpVote[0]);
				float oldPercent0 = oldVotes0 / (oldVotes0 + oldVotes1) * 100.0f;
				float boothSwingStdDev = 2.5f + 200.0f / booth.totalOldTcpVotes(); // small booths a lot swingier
				float boothSwing = remainingVoteSwing + std::normal_distribution<float>(0.0f, boothSwingStdDev)(gen);
				if (booth.isPPVC()) {
					// votes are already in order for the seat, not the booth
					if (firstParty == 0 && secondParty == 1) boothSwing += ppvcBias;
					if (secondParty == 0 && firstParty == 1) boothSwing -= ppvcBias;
				}
				float newPercent0 = std::clamp(oldPercent0 + boothSwing, 0.0f, 100.0f);
				int newVotes0 = int(std::round(newPercent0 * float(estimatedTotalVotes) * 0.01f));
				int newVotes1 = estimatedTotalVotes - newVotes0;
				tcpTally[0] += newVotes0;
				tcpTally[1] += newVotes1;
			}
			if (!booth.hasOldResults() && !booth.hasNewResults()) {
				if (booth.isPPVC()) ++mysteryPPVCBooths;
				else if (booth.name.find(" Team") != std::string::npos) ++mysteryTeamBooths;
				else ++mysteryStandardBooths;
			}
		}

		// Now we have also tallied the estimated votes from booths that are uncounted but matched, if any

		// Need to calculate the remaining pool of uncounted and unmatched booths
		float enrolmentChange = 1.0f;
		int estimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
		if (seat.previousResults) {
			enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
			estimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
		}
		float firstTallyPercent = float(tcpTally[0]) / float(tcpTally[0] + tcpTally[1]) * 100.0f;

		if (mysteryStandardBooths + mysteryTeamBooths + mysteryPPVCBooths) {
			int estimatedRemainingOrdinaryVotes = std::max(0, estimatedTotalOrdinaryVotes - tcpTally[0] - tcpTally[1]);
			// sanity check to make sure we aren't assigning 5000 votes to a special hospital team or something
			int plausibleMaximumRemainingOrdinaryVotes = mysteryPPVCBooths * 10000 + mysteryStandardBooths * 2000 + mysteryTeamBooths * 200;
			float proportionPPVC = float(mysteryPPVCBooths * 10000) / float(plausibleMaximumRemainingOrdinaryVotes);
			estimatedRemainingOrdinaryVotes = std::min(estimatedRemainingOrdinaryVotes, plausibleMaximumRemainingOrdinaryVotes);
			estimatedTotalOrdinaryVotes = estimatedRemainingOrdinaryVotes + tcpTally[0] + tcpTally[1];

			const float MysteryVoteStdDev = 6.0f;
			float incumbentMysteryPercent = std::normal_distribution<float>(firstTallyPercent, MysteryVoteStdDev)(gen);
			if (firstParty == 0 && secondParty == 1) incumbentMysteryPercent += ppvcBias * proportionPPVC;
			if (secondParty == 1 && firstParty == 0) incumbentMysteryPercent -= ppvcBias * proportionPPVC;
			int incumbentMysteryVotes = int(std::round(incumbentMysteryPercent * 0.01f * float(estimatedRemainingOrdinaryVotes)));
			int challengerMysteryVotes = estimatedRemainingOrdinaryVotes - incumbentMysteryVotes;
			tcpTally[0] += incumbentMysteryVotes;
			tcpTally[1] += challengerMysteryVotes;
		}

		// Now estimate declaration vote totals and add these to the total tallies

		float totalOrdinaryTally = float(tcpTally[0] + tcpTally[1]);
		int estimatedTotalVotes = 0;
		if (seat.previousResults) {
			bool sameOrder = firstParty == project.getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
			estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
			int estimatedDeclarationVotes = estimatedTotalVotes - estimatedTotalOrdinaryVotes;
			int oldDeclarationVotes = seat.previousResults->total2cpVotes() - seat.previousResults->ordinaryVotes();
			float declarationVoteChange = float(estimatedDeclarationVotes) / float(oldDeclarationVotes);

			float firstNewOrdinaryPercent = float(tcpTally[0]) / totalOrdinaryTally * 100.0f;
			float firstOldOrdinaryPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].ordinaryVotes) / float(seat.previousResults->ordinaryVotes()) * 100.0f;
			float ordinaryVoteSwing = firstNewOrdinaryPercent - firstOldOrdinaryPercent;

			float absentStdDev = 5.0f; // needs more research
			float absentSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, absentStdDev)(gen);
			float firstOldAbsentPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].absentVotes) / float(seat.previousResults->absentVotes()) * 100.0f;
			float firstNewAbsentPercent = firstOldAbsentPercent + absentSwing;
			int estimatedAbsentVotes = int(std::round(float(seat.previousResults->absentVotes()) * declarationVoteChange));
			int firstCountedAbsentVotes = seat.latestResults->finalCandidates[0].absentVotes;
			int secondCountedAbsentVotes = seat.latestResults->finalCandidates[1].absentVotes;
			int estimatedRemainingAbsentVotes = std::max(0, estimatedAbsentVotes - firstCountedAbsentVotes - secondCountedAbsentVotes);
			int firstRemainingAbsentVotes = int(std::round(firstNewAbsentPercent * float(estimatedRemainingAbsentVotes) * 0.01f));
			int secondRemainingAbsentVotes = estimatedRemainingAbsentVotes - firstRemainingAbsentVotes;
			int firstAbsentVotes = firstCountedAbsentVotes + firstRemainingAbsentVotes;
			int secondAbsentVotes = secondCountedAbsentVotes + secondRemainingAbsentVotes;

			float provisionalStdDev = 5.0f; // needs more research
			float provisionalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, provisionalStdDev)(gen);
			float firstOldProvisionalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].provisionalVotes) / float(seat.previousResults->provisionalVotes()) * 100.0f;
			float firstNewProvisionalPercent = firstOldProvisionalPercent + provisionalSwing;
			int estimatedProvisionalVotes = int(std::round(float(seat.previousResults->provisionalVotes()) * declarationVoteChange));
			int firstCountedProvisionalVotes = seat.latestResults->finalCandidates[0].provisionalVotes;
			int secondCountedProvisionalVotes = seat.latestResults->finalCandidates[1].provisionalVotes;
			int estimatedRemainingProvisionalVotes = std::max(0, estimatedProvisionalVotes - firstCountedProvisionalVotes - secondCountedProvisionalVotes);
			int firstRemainingProvisionalVotes = int(std::round(firstNewProvisionalPercent * float(estimatedRemainingProvisionalVotes) * 0.01f));
			int secondRemainingProvisionalVotes = estimatedRemainingProvisionalVotes - firstRemainingProvisionalVotes;
			int firstProvisionalVotes = firstCountedProvisionalVotes + firstRemainingProvisionalVotes;
			int secondProvisionalVotes = secondCountedProvisionalVotes + secondRemainingProvisionalVotes;

			float prepollStdDev = 5.0f; // needs more research
			float prepollSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, prepollStdDev)(gen);
			float firstOldPrepollPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].prepollVotes) / float(seat.previousResults->prepollVotes()) * 100.0f;
			float firstNewPrepollPercent = firstOldPrepollPercent + prepollSwing;
			int estimatedPrepollVotes = int(std::round(float(seat.previousResults->prepollVotes()) * declarationVoteChange));
			int firstCountedPrepollVotes = seat.latestResults->finalCandidates[0].prepollVotes;
			int secondCountedPrepollVotes = seat.latestResults->finalCandidates[1].prepollVotes;
			int estimatedRemainingPrepollVotes = std::max(0, estimatedPrepollVotes - firstCountedPrepollVotes - secondCountedPrepollVotes);
			int firstRemainingPrepollVotes = int(std::round(firstNewPrepollPercent * float(estimatedRemainingPrepollVotes) * 0.01f));
			int secondRemainingPrepollVotes = estimatedRemainingPrepollVotes - firstRemainingPrepollVotes;
			int firstPrepollVotes = firstCountedPrepollVotes + firstRemainingPrepollVotes;
			int secondPrepollVotes = secondCountedPrepollVotes + secondRemainingPrepollVotes;

			float postalStdDev = 5.0f; // needs more research
			float postalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, postalStdDev)(gen);
			float firstOldPostalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].postalVotes) / float(seat.previousResults->postalVotes()) * 100.0f;
			float firstNewPostalPercent = firstOldPostalPercent + postalSwing;
			int estimatedPostalVotes = int(std::round(float(seat.previousResults->postalVotes()) * declarationVoteChange));
			int firstCountedPostalVotes = seat.latestResults->finalCandidates[0].postalVotes;
			int secondCountedPostalVotes = seat.latestResults->finalCandidates[1].postalVotes;
			int estimatedRemainingPostalVotes = std::max(0, estimatedPostalVotes - firstCountedPostalVotes - secondCountedPostalVotes);
			int firstRemainingPostalVotes = int(std::round(firstNewPostalPercent * float(estimatedRemainingPostalVotes) * 0.01f));
			int secondRemainingPostalVotes = estimatedRemainingPostalVotes - firstRemainingPostalVotes;
			int firstPostalVotes = firstCountedPostalVotes + firstRemainingPostalVotes;
			int secondPostalVotes = secondCountedPostalVotes + secondRemainingPostalVotes;

			tcpTally[0] += firstAbsentVotes + firstProvisionalVotes + firstPrepollVotes + firstPostalVotes;
			tcpTally[1] += secondAbsentVotes + secondProvisionalVotes + secondPrepollVotes + secondPostalVotes;
		}
		else {
			estimatedTotalVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
			if (seat.latestResults->ordinaryVotes() > estimatedTotalOrdinaryVotes) {
				float estimatedNonOrdinaryVotePotential = float(seat.latestResults->enrolment - estimatedTotalOrdinaryVotes);
				float estimatedProportionRemainingFormal = float(estimatedTotalVotes) / estimatedNonOrdinaryVotePotential;
				constexpr float remainingFormalStdDev = 0.05f;
				estimatedProportionRemainingFormal = std::clamp(estimatedProportionRemainingFormal + std::normal_distribution<float>(0.0f, remainingFormalStdDev)(gen), 0.0f, 1.0f);
				float actualNonOrdinaryVotePotential = float(seat.latestResults->enrolment - seat.latestResults->ordinaryVotes());
				int estimatedTotalDeclarationVotes = int(actualNonOrdinaryVotePotential * estimatedProportionRemainingFormal);

				float firstPercent = float(tcpTally[0]) / totalOrdinaryTally;
				constexpr float declarationStdDev = 0.05f;
				float declarationVoteFirstProportion = std::clamp(firstPercent + std::normal_distribution<float>(0.0f, declarationStdDev)(gen), 0.0f, 1.0f);
				int firstTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * declarationVoteFirstProportion);
				int secondTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * (1.0f - declarationVoteFirstProportion));
				tcpTally[0] += firstTcpDeclarationVotes;
				tcpTally[1] += secondTcpDeclarationVotes;
			}
		}

		float totalTally = float(tcpTally[0] + tcpTally[1]);
		float firstMargin = (float(tcpTally[0]) - totalTally * 0.5f) / totalTally * 100.0f;
		Party::Id winner = (firstMargin >= 0.0f ? firstParty : secondParty);
		Party::Id runnerUp = (firstMargin >= 0.0f ? secondParty : firstParty);

		// Third parties can potentially be a "spoiler" for a seat expected to be classic
		// This check replaces the winner by a third party if it is simulated but doesn't
		// affect the balance between the 2cp candidates.
		SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(seat);
		if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) {
			if (std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) < spoilerResult.significance) {
				return spoilerResult;
			}
		}

		float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

		return { winner, runnerUp, abs(firstMargin), float(significance) };
	}
	else if (sim.isLive() && seat.latestResult && seat.latestResult->getPercentCountedEstimate()) {
		float liveMargin = seat.latestResult->incumbentSwing + seat.margin;
		float liveStdDev = stdDevSingleSeat(seat.latestResult->getPercentCountedEstimate());
		liveMargin += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 6.0f / (liveStdDev * liveStdDev);
		float newMargin = (priorMargin * priorWeight + liveMargin * liveWeight) / (priorWeight + liveWeight);
		Party::Id winner = (newMargin >= 0.0f ? seat.incumbent : seat.challenger);
		Party::Id runnerUp = (newMargin >= 0.0f ? seat.challenger : seat.incumbent);
		float significance = std::clamp(float(seat.latestResult->percentCounted) * 0.2f, 0.0f, 1.0f);
		return { winner, runnerUp, abs(newMargin), significance };
	}

	Party::Id winner = (priorMargin >= 0.0f ? seat.incumbent : seat.challenger);
	Party::Id runnerUp = (priorMargin >= 0.0f ? seat.challenger : seat.incumbent);
	return { winner, runnerUp, abs(priorMargin), 0.0f };
}

SimulationRun::SeatResult SimulationRun::calculateLiveResultNonClassic2CP(Seat const& seat)
{
	if (sim.isLiveAutomatic() && seatPartiesMatchBetweenElections(seat)) {
		if (!currentIteration) logger << seat.name << " - matched booths\n";
		return calculateLiveResultClassic2CP(seat, seat.margin);
	}
	else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
		if (!currentIteration) logger << seat.name << " - 2cp votes\n";
		int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
		int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
		Party::Id firstParty = project.getPartyByCandidate(firstCandidateId);
		Party::Id secondParty = project.getPartyByCandidate(secondCandidateId);
		int firstTcpTally = 0;
		int secondTcpTally = 0;
		int newComparisonVotes = 0;
		int oldComparisonVotes = 0;
		float preferenceFlowGuess = std::normal_distribution<float>(seat.firstPartyPreferenceFlow, seat.preferenceFlowVariation)(gen);
		for (auto boothId : seat.latestResults->booths) {
			Results::Booth const& booth = project.getBooth(boothId);
			bool matchingOrder = project.getPartyByCandidate(booth.tcpCandidateId[0]) == project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);

			if (booth.hasNewResults()) {
				firstTcpTally += (matchingOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
				secondTcpTally += (matchingOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
			}
			else if (booth.totalNewFpVotes()) {
				int firstTcpEstimate = 0;
				for (auto const& candidate : booth.fpCandidates) {
					if (candidate.candidateId == firstCandidateId) {
						firstTcpEstimate += candidate.fpVotes;
					}
					else if (candidate.candidateId != secondCandidateId) {
						firstTcpEstimate += int(float(candidate.fpVotes) * preferenceFlowGuess);
					}
				}
				int secondTcpEstimate = booth.totalNewFpVotes() - firstTcpEstimate;
				firstTcpTally += firstTcpEstimate;
				secondTcpTally += secondTcpEstimate;
			}
			if (booth.hasOldAndNewResults()) {
				oldComparisonVotes += booth.totalOldTcpVotes();
				newComparisonVotes += booth.totalNewTcpVotes();
			}
		}

		float enrolmentChange = determineEnrolmentChange(seat, nullptr);

		// now estimate the remaining votes with considerable variance
		int estimatedTotalVotes = 0;
		if (seat.previousResults) {
			estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
		}
		else {
			estimatedTotalVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
		}
		int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
		int minTotalVotes = firstTcpTally + secondTcpTally;
		constexpr float totalVoteNumberDeviation = 0.05f;
		float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
		int clampedTotalVotes = std::clamp(int(randomizedTotalVotes), minTotalVotes, maxTotalVotes);
		int estimatedRemainingVotes = std::max(0, clampedTotalVotes - firstTcpTally - secondTcpTally);

		float firstProportionCounted = float(firstTcpTally) / float(firstTcpTally + secondTcpTally);
		constexpr float firstProportionChangeDeviation = 0.08f;
		float firstProportionChange = std::normal_distribution<float>(0.0f, firstProportionChangeDeviation)(gen);
		float firstProportionRemaining = std::clamp(firstProportionChange + firstProportionCounted, 0.0f, 1.0f);
		int firstRemainingVotes = int(firstProportionRemaining * float(estimatedRemainingVotes));
		int secondRemainingVotes = estimatedRemainingVotes - firstRemainingVotes;
		firstTcpTally += firstRemainingVotes;
		secondTcpTally += secondRemainingVotes;

		float totalTally = float(firstTcpTally + secondTcpTally);
		float margin = (float(firstTcpTally) - totalTally * 0.5f) / totalTally * 100.0f;

		Party::Id winner = (margin >= 0.0f ? firstParty : secondParty);
		Party::Id runnerUp = (margin >= 0.0f ? secondParty : firstParty);

		// Third parties can potentially be a "spoiler" for a seat expected to be classic
		// This check replaces the winner by a third party if it is simulated but doesn't
		// affect the balance between the 2cp candidates.
		SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(seat);
		if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) return spoilerResult;

		float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

		return { winner, runnerUp, margin, significance };
	}
	else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->fpCandidates.size() && seat.latestResults->totalFpVotes()) {
		if (!currentIteration) logger << seat.name << " - first preferences\n";
		return calculateLiveResultFromFirstPreferences(seat);
	}
	else {
		return { seat.incumbent, seat.challenger, seat.margin, 0.0f };
	}
}

SimulationRun::SeatResult SimulationRun::calculateLiveResultFromFirstPreferences(Seat const & seat)
{
	struct Candidate { int vote; Party::Id partyId; float weight; };

	std::vector<Candidate> candidates;
	for (auto const& fpCandidate : seat.latestResults->fpCandidates) {
		candidates.push_back({ fpCandidate.totalVotes(), project.getPartyByCandidate(fpCandidate.candidateId) });
	}

	// now estimate the remaining votes with considerable variance
	int countedVotes = std::accumulate(candidates.begin(), candidates.end(), 0,
		[](int i, Candidate const& c) { return i + c.vote; });
	float enrolmentChange = determineEnrolmentChange(seat, nullptr);
	int estimatedTotalVotes = 0; // use previous 2cp votes to determine estimated total votes
	if (seat.previousResults) {
		estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
	}
	else {
		estimatedTotalVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
	}
	int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	int minTotalVotes = std::min(countedVotes, maxTotalVotes);
	constexpr float totalVoteNumberDeviation = 0.05f;
	float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
	if (seat.name == "Bean" && !currentIteration) {
		logger << "found problem seat!\n";
	}
	int clampedTotalVotes = std::clamp(int(randomizedTotalVotes), minTotalVotes, maxTotalVotes);
	int estimatedRemainingVotes = std::max(0, clampedTotalVotes - countedVotes);

	float totalProjectionWeight = 0.0f;
	for (auto& candidate : candidates) {
		float countedProportion = float(candidate.vote) / float(countedVotes);
		float candidateStdDev = std::min(countedProportion * 0.2f, 0.04f);
		float remainingProportion = std::normal_distribution<float>(float(countedProportion), candidateStdDev)(gen);
		candidate.weight = std::max(0.0f, remainingProportion);
		totalProjectionWeight += candidate.weight;
	}

	for (auto& candidate : candidates) {
		candidate.vote += int(float(estimatedRemainingVotes) * candidate.weight / totalProjectionWeight);
	}

	for (int sourceIndex = candidates.size() - 1; sourceIndex > 1; --sourceIndex) {
		Candidate sourceCandidate = candidates[sourceIndex];
		if (sourceCandidate.partyId == Party::InvalidId) continue;
		Party const& sourceParty = project.parties().view(sourceCandidate.partyId);
		candidates[sourceIndex].vote = 0;
		std::vector<float> weights;
		weights.resize(candidates.size(), 0);
		for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
			Candidate const& targetCandidate = candidates[targetIndex];
			if (targetCandidate.partyId == Party::InvalidId) continue;
			Party const& targetParty = project.parties().view(targetCandidate.partyId);
			int ideologyDistance = float(std::abs(sourceParty.ideology - targetParty.ideology));
			float consistencyBase = PreferenceConsistencyBase[sourceParty.consistency];
			float thisWeight = std::pow(consistencyBase, -ideologyDistance);
			if (!sourceParty.countsAsMajor() && !targetParty.countsAsMajor()) thisWeight *= 1.6f;
			if (project.parties().oppositeMajors(sourceCandidate.partyId, targetCandidate.partyId)) thisWeight /= 2.0f;
			// note, these frequent calls to the RNG are a significant bottleneck
			thisWeight *= std::uniform_real_distribution<float>(0.5f, 1.5f)(gen);
			thisWeight *= std::sqrt(float(targetCandidate.vote)); // preferences tend to flow to more popular candidates
			weights[targetIndex] = thisWeight;
		}
		float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0f) + 0.0000001f; // avoid divide by zero warning
		for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
			// this will cause a few votes to be lost to rounding errors but since we're only running
			// these calculations when uncertainty is high that doesn't really matter.
			candidates[targetIndex].vote += int(float(sourceCandidate.vote) * weights[targetIndex] / totalWeight);
		}
		std::sort(candidates.begin(), candidates.end(),
			[](Candidate lhs, Candidate rhs) {return lhs.vote > rhs.vote; });
	}
	float totalTally = float(candidates[0].vote + candidates[1].vote);
	float margin = (float(candidates[0].vote) - totalTally * 0.5f) / totalTally * 100.0f;
	Party::Id winner = candidates[0].partyId;
	Party::Id runnerUp = candidates[1].partyId;
	float significance = std::clamp(float(seat.latestResults->totalFpVotes()) / float(estimatedTotalVotes) * 5.0f, 0.0f, 1.0f);

	return { winner, runnerUp, margin, significance };
}

Party::Id SimulationRun::simulateWinnerFromBettingOdds(Seat const& thisSeat)
{
	// Non-standard seat; use odds adjusted for longshot bias since the presence of the
	// third-party candidate may make swing-based projections inaccurate
	OddsInfo oddsInfo = calculateOddsInfo(thisSeat);
	// Random number between 0 and 1
	float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
	// Winner 
	if (uniformRand < oddsInfo.incumbentChance) return thisSeat.incumbent;
	else if (uniformRand < oddsInfo.topTwoChance || !thisSeat.challenger2) return thisSeat.challenger;
	else return thisSeat.challenger2;
}

bool SimulationRun::seatPartiesMatchBetweenElections(Seat const& seat)
{
	if (!seat.latestResults) return false;
	if (!seat.previousResults) return false;
	Party::Id oldPartyOne = project.getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	Party::Id oldPartyTwo = project.getPartyByAffiliation(seat.previousResults->finalCandidates[1].affiliationId);
	Party::Id newPartyOne = project.getPartyByAffiliation(seat.latestResults->finalCandidates[0].affiliationId);
	Party::Id newPartyTwo = project.getPartyByAffiliation(seat.latestResults->finalCandidates[1].affiliationId);
	if (oldPartyOne == newPartyOne && oldPartyTwo == newPartyTwo) return true;
	if (oldPartyOne == newPartyTwo && oldPartyTwo == newPartyOne) return true;
	return false;
}

float SimulationRun::determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes)
{
	// Need to calculate the remaining pool of uncounted and unmatched booths
	float enrolmentChange = 1.0f;
	int tempEstimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
	if (seat.previousResults) {
		enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
		tempEstimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
	}
	if (estimatedTotalOrdinaryVotes) *estimatedTotalOrdinaryVotes = tempEstimatedTotalOrdinaryVotes;
	return enrolmentChange;
}

void SimulationRun::updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int & bound)
{
	if (partyCount > sim.settings.numIterations / 200 * probThreshold && bound == -1) bound = numSeats;
}
