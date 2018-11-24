#include "Simulation.h"
#include "Projection.h"
#include "Model.h"
#include "Debug.h"
#include "Region.h"
#include "Party.h"
#include "PollingProject.h"
#include "CountProgress.h"
#include <algorithm>
#include <random>

#undef min
#undef max

const float LongshotOddsThreshold = 2.5f;
const float seatStdDev = 2.0f; // Seat standard deviation, should remove this and use a user-input parameter instead

void Simulation::run(PollingProject& project) {

	if (int(baseProjection->meanProjection.size()) == 0) return;

	std::random_device rd;
	std::mt19937 gen(rd());

	// Get pointers to the major parties (for later checking if seats are classic or non-classic 2CP)
	Party const* const partyOne = project.getPartyPtr(0);
	Party const* const partyTwo = project.getPartyPtr(1);

	for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
		thisRegion->localModifierAverage = 0.0f;
		thisRegion->seatCount = 0;

		thisRegion->liveSwing = 0.0f;
		thisRegion->livePercentCounted = 0.0f;
		thisRegion->classicSeatCount = 0;
	}

	// Set up anything that needs to be prepared for seats
	for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
		thisSeat->incumbentWins = 0;
		thisSeat->simulatedMarginAverage = 0;
		thisSeat->latestResult = nullptr;
		bool isPartyOne = (thisSeat->incumbent == partyOne);
		thisSeat->region->localModifierAverage += thisSeat->localModifier * (isPartyOne ? 1.0f : -1.0f);
		++thisSeat->region->seatCount;
	}

	for (auto thisResult = project.getResultBegin(); thisResult != project.getResultEnd(); ++thisResult) {
		if (!thisResult->seat->latestResult) thisResult->seat->latestResult = &*thisResult;
	}

	// Resize regional seat counts based on the counted number of seats for each region
	for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
		thisRegion->partyLeading.clear();
		thisRegion->partyWins.clear();
		thisRegion->partyLeading.resize(project.getPartyCount());
		thisRegion->partyWins.resize(project.getPartyCount(), std::vector<int>(thisRegion->seatCount + 1));
	}

	// Record how many seats each party leads in (notionally) in each region
	for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
		++thisSeat->region->partyLeading[project.getPartyIndex(thisSeat->getLeadingParty())];
	}

	// Some setup - calculating total population here since it's constant across all simulations
	float totalPopulation = 0.0;
	for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
		totalPopulation += float(thisRegion->population);
		thisRegion->localModifierAverage /= float(thisRegion->seatCount);
	}

	float liveOverallSwing = 0.0f; // swing to partyOne
	float liveOverallPercent = 0.0f;
	float classicSeatCount = 0.0f;
	// A bunch of votes from one seat is less likely to be representative than from a wide variety of seats,
	// so this factor is introduced to avoid a small number of seats from having undue influence early in the count
	float sampleRepresentativeness = 0.0f;
	if (live) {
		for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
			if (!thisSeat->isClassic2pp(partyOne, partyTwo)) continue;
			++classicSeatCount;
			++thisSeat->region->classicSeatCount;
			if (!thisSeat->latestResult) continue;
			bool incIsOne = thisSeat->incumbent == partyOne;
			float percentCounted = thisSeat->latestResult->getPercentCountedEstimate();
			float weightedSwing = thisSeat->latestResult->incumbentSwing * (incIsOne ? 1.0f : -1.0f) * percentCounted;
			liveOverallSwing += weightedSwing;
			thisSeat->region->liveSwing += weightedSwing;
			liveOverallPercent += percentCounted;
			thisSeat->region->livePercentCounted += percentCounted;
			sampleRepresentativeness += std::min(2.0f, percentCounted) * 0.5f;
		}
		liveOverallSwing /= liveOverallPercent;
		liveOverallPercent /= classicSeatCount;
		sampleRepresentativeness /= classicSeatCount;
		sampleRepresentativeness = std::sqrt(sampleRepresentativeness);
		for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
			if (!thisRegion->livePercentCounted) continue;
			thisRegion->liveSwing /= thisRegion->livePercentCounted;
			thisRegion->livePercentCounted /= thisRegion->classicSeatCount;
		}
	}

	int partyOneMajority = 0;
	int partyOneMinority = 0;
	int hungParliament = 0;
	int partyTwoMinority = 0;
	int partyTwoMajority = 0;

	partySeatWinFrequency.clear();
	partySeatWinFrequency.resize(project.getPartyCount(), std::vector<int>(project.getSeatCount() + 1));
	othersWinFrequency.clear();
	othersWinFrequency.resize(project.getSeatCount() + 1);

	float pollOverallSwing = baseProjection->meanProjection.back() - prevElection2pp;
	float pollOverallStdDev = baseProjection->sdProjection.back();
	for (int iterationIndex = 0; iterationIndex < numIterations; ++iterationIndex) {

		// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
		std::vector<std::vector<int>> regionSeatCount(project.getPartyCount(), std::vector<int>(project.getRegionCount()));

		// First, randomly determine the national swing for this particular simulation
		float simulationOverallSwing = std::normal_distribution<float>(pollOverallSwing, pollOverallStdDev)(gen);

		if (live && liveOverallPercent) {
			float liveSwing = liveOverallSwing;
			float liveStdDev = stdDevOverall(liveOverallPercent);
			liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
			float priorWeight = 0.5f;
			float liveWeight = 1.0f / (liveStdDev * liveStdDev) * sampleRepresentativeness;
			simulationOverallSwing = (simulationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
		}

		// Add random variation to the state-by-state swings and calculate the implied national 2pp
		// May be some minor floating-point errors here but they won't matter in the scheme of things
		float tempOverallSwing = 0.0f;
		for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
			// Calculate mean of the region's swing after accounting for decay to the mean over time.
			float regionMeanSwing = pollOverallSwing + 
				thisRegion->swingDeviation * pow(1.0f - stateDecay, baseProjection->meanProjection.size());
			// Add random noise to the region's swing level
			float swingSD = this->stateSD + thisRegion->additionalUncertainty;
			if (swingSD > 0) {
				thisRegion->simulationSwing =
					std::normal_distribution<float>(regionMeanSwing, this->stateSD + thisRegion->additionalUncertainty)(gen);
			}
			else {
				thisRegion->simulationSwing = regionMeanSwing;
			}

			if (live && thisRegion->livePercentCounted) {
				float liveSwing = thisRegion->liveSwing;
				float liveStdDev = stdDevSingleSeat(thisRegion->livePercentCounted);
				liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
				float priorWeight = 0.5f;
				float liveWeight = 1.0f / (liveStdDev * liveStdDev);
				priorWeight, liveWeight;
				thisRegion->simulationSwing = (thisRegion->simulationSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
			}

			tempOverallSwing += thisRegion->simulationSwing * thisRegion->population;
		}
		tempOverallSwing /= totalPopulation;

		// Adjust regional swings to keep the implied overall 2pp the same as that actually calculated
		float regionSwingAdjustment = simulationOverallSwing - tempOverallSwing;
		for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
			thisRegion->simulationSwing += regionSwingAdjustment;
		}

		std::vector<int> partyWins(project.getPartyCount());

		// Now cycle through all the seats and generate a result for each
		for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
			// float seatFactor = 0.0f; // used for when we include election results, for now just set to zero

			// First determine if this seat is "classic" (main-parties only) 2CP, which determines how we get a result and the winner
			bool isClassic2CP = thisSeat->isClassic2pp(partyOne, partyTwo);
			if (isClassic2CP) {
				bool incIsOne = thisSeat->incumbent == partyOne; // stores whether the incumbent is Party One
				// Add or subtract the simulation regional deviation depending on which party is incumbent
				float newMargin = thisSeat->margin + thisSeat->region->simulationSwing * (incIsOne ? 1.0f : -1.0f);
				// Add modifiers for known local effects (these are measured as positive if favouring the incumbent)
				newMargin += thisSeat->localModifier;
				// Remove the average local modifier across the region
				newMargin -= thisSeat->region->localModifierAverage * (incIsOne ? 1.0f : -1.0f);
				// Add random noise to the new margin of this seat
				newMargin += std::normal_distribution<float>(0.0f, seatStdDev)(gen);
				// Now work out the margin of the seat from actual results if live
				if (live && thisSeat->latestResult && thisSeat->latestResult->getPercentCountedEstimate()) {
					float liveMargin = thisSeat->latestResult->incumbentSwing + thisSeat->margin;
					float liveStdDev = stdDevSingleSeat(thisSeat->latestResult->getPercentCountedEstimate());
					liveMargin += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
					float priorWeight = 0.5f;
					float liveWeight = 6.0f / (liveStdDev * liveStdDev);
					newMargin = (newMargin * priorWeight + liveMargin * liveWeight) / (priorWeight + liveWeight);
				}
				// Margin for this simulation is finalised, record it for later averaging
				thisSeat->simulatedMarginAverage += newMargin;
				// If the margin is greater than zero, the incumbent wins the seat.
				thisSeat->winner = (newMargin >= 0.0f ? thisSeat->incumbent : thisSeat->challenger);
				// Sometimes a classic 2pp seat may also have a independent with a significant chance,
				// but not high enough to make the top two - if so this will give a certain chance to
				// override the swing-based result with a win from the challenger
				if (thisSeat->challenger2Odds < 8.0) {
					OddsInfo oddsInfo = calculateOddsInfo(thisSeat);
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand >= oddsInfo.topTwoChance) thisSeat->winner = thisSeat->challenger2;
				}
			} else {
				if (live && thisSeat->livePartyOne) {
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand < thisSeat->partyTwoProb) {
						thisSeat->winner = thisSeat->livePartyTwo;
					}
					else if (uniformRand < thisSeat->partyTwoProb + thisSeat->partyThreeProb) {
						thisSeat->winner = thisSeat->livePartyThree;
					}
					else {
						thisSeat->winner = thisSeat->livePartyOne;
					}
				}
				else {
					// Non-standard seat; use odds adjusted for longshot bias since the presence of the
					// third-party candidate may make swing-based projections inaccurate
					OddsInfo oddsInfo = calculateOddsInfo(thisSeat);
					// Random number between 0 and 1
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					// Winner 
					if (uniformRand < oddsInfo.incumbentChance) thisSeat->winner = thisSeat->incumbent;
					else if (uniformRand < oddsInfo.topTwoChance) thisSeat->winner = thisSeat->challenger;
					else thisSeat->winner = thisSeat->challenger2;
				}
			}
			// If the winner is the incumbent, record this down in the seat's numbers
			thisSeat->incumbentWins += (thisSeat->winner == thisSeat->incumbent ? 1 : 0);

			int winnerIndex = project.getPartyIndex(thisSeat->winner);
			partyWins[winnerIndex]++;
			int regionIndex = project.getRegionIndex(thisSeat->region);
			++regionSeatCount[winnerIndex][regionIndex];
		}

		//Assign all the wins from coalition third-parties to the respective major party
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			if (project.getPartyPtr(partyNum)->countAsParty == Party::CountAsParty::CountsAsPartyOne){
				partyWins[0] += partyWins[partyNum];
			}
			else if (project.getPartyPtr(partyNum)->countAsParty == Party::CountAsParty::CountsAsPartyTwo){
				partyWins[1] += partyWins[partyNum];
			}
		}


		//Get the number of seats supporting each major party in a minority government
		std::array<int, 2> partySupport = { partyWins[0], partyWins[1] };
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			if (project.getPartyPtr(partyNum)->supportsParty == Party::SupportsParty::One) {
				partySupport[0] += partyWins[partyNum];
			}
			else if (project.getPartyPtr(partyNum)->supportsParty == Party::SupportsParty::Two) {
				partySupport[1] += partyWins[partyNum];
			}
		}

		int minimumForMajority = project.getSeatCount() / 2 + 1;

		// Look at the overall result and classify it
		if (partyWins[0] >= minimumForMajority) ++partyOneMajority;
		else if (partySupport[0] >= minimumForMajority) ++partyOneMinority;
		else if (partyWins[1] >= minimumForMajority) ++partyTwoMajority;
		else if (partySupport[1] >= minimumForMajority) ++partyTwoMinority;
		else ++hungParliament;

		int othersWins = 0;
		for (int partyIndex = 0; partyIndex < project.getPartyCount(); ++partyIndex) {
			++partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
			if (partyIndex > 1) othersWins += partyWins[partyIndex];
			for (int regionIndex = 0; regionIndex < project.getRegionCount(); ++regionIndex) {
				++project.getRegionPtr(regionIndex)->partyWins[partyIndex][regionSeatCount[partyIndex][regionIndex]];
			}
		}
		++othersWinFrequency[othersWins];

	}
	
	incumbentWinPercent.resize(project.getSeatCount() + 1);

	// Go through each seat and update the incumbent wins %
	for (int seatIndex = 0; seatIndex < project.getSeatCount(); ++seatIndex) {
		Seat* thisSeat = project.getSeatPtr(seatIndex);
		incumbentWinPercent[seatIndex] = float(thisSeat->incumbentWins) / float(numIterations) * 100.0f;
		thisSeat->incumbentWinPercent = incumbentWinPercent[seatIndex];
		thisSeat->simulatedMarginAverage /= float(numIterations);
	}

	partyWinExpectation.resize(project.getPartyCount());

	partyOneMajorityPercent = float(partyOneMajority) / float(numIterations) * 100.0f;
	partyOneMinorityPercent = float(partyOneMinority) / float(numIterations) * 100.0f;
	hungPercent = float(hungParliament) / float(numIterations) * 100.0f;
	partyTwoMinorityPercent = float(partyTwoMinority) / float(numIterations) * 100.0f;
	partyTwoMajorityPercent = float(partyTwoMajority) / float(numIterations) * 100.0f;

	for (int partyIndex = 0; partyIndex < project.getPartyCount(); ++partyIndex) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.getSeatCount(); ++seatNum) {
			totalSeats += seatNum * partySeatWinFrequency[partyIndex][seatNum];
		}
		partyWinExpectation[partyIndex] = float(totalSeats) / float(numIterations);
	}

	regionPartyWinExpectation.resize(project.getRegionCount(), std::vector<float>(project.getPartyCount(), 0.0f));

	for (int regionIndex = 0; regionIndex < project.getRegionCount(); ++regionIndex) {
		Region thisRegion = project.getRegion(regionIndex);
		for (int partyIndex = 0; partyIndex < project.getPartyCount(); ++partyIndex) {
			int totalSeats = 0;
			for (int seatNum = 1; seatNum < int(thisRegion.partyWins[partyIndex].size()); ++seatNum) {
				totalSeats += seatNum * thisRegion.partyWins[partyIndex][seatNum];
			}
			regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(numIterations);
			//PrintDebugFloat(regionPartyWinExpectation - thisRegion.partyLeading[0]);
		}
	}

	int partyOneCount = 0;
	int partyTwoCount = 0;
	int othersCount = 0;
	std::fill(partyOneProbabilityBounds.begin(), partyOneProbabilityBounds.end(), -1);
	std::fill(partyTwoProbabilityBounds.begin(), partyTwoProbabilityBounds.end(), -1);
	std::fill(othersProbabilityBounds.begin(), othersProbabilityBounds.end(), -1);
	for (int numSeats = 0; numSeats < project.getSeatCount(); ++numSeats) {
		partyOneCount += partySeatWinFrequency[0][numSeats];
		partyTwoCount += partySeatWinFrequency[1][numSeats];
		othersCount += othersWinFrequency[numSeats];
		updateProbabilityBounds(partyOneCount, numSeats, 1, partyOneProbabilityBounds[0]);
		updateProbabilityBounds(partyOneCount, numSeats, 5, partyOneProbabilityBounds[1]);
		updateProbabilityBounds(partyOneCount, numSeats, 20, partyOneProbabilityBounds[2]);
		updateProbabilityBounds(partyOneCount, numSeats, 50, partyOneProbabilityBounds[3]);
		updateProbabilityBounds(partyOneCount, numSeats, 150, partyOneProbabilityBounds[4]);
		updateProbabilityBounds(partyOneCount, numSeats, 180, partyOneProbabilityBounds[5]);
		updateProbabilityBounds(partyOneCount, numSeats, 195, partyOneProbabilityBounds[6]);
		updateProbabilityBounds(partyOneCount, numSeats, 199, partyOneProbabilityBounds[7]);

		updateProbabilityBounds(partyTwoCount, numSeats, 1, partyTwoProbabilityBounds[0]);
		updateProbabilityBounds(partyTwoCount, numSeats, 5, partyTwoProbabilityBounds[1]);
		updateProbabilityBounds(partyTwoCount, numSeats, 20, partyTwoProbabilityBounds[2]);
		updateProbabilityBounds(partyTwoCount, numSeats, 50, partyTwoProbabilityBounds[3]);
		updateProbabilityBounds(partyTwoCount, numSeats, 150, partyTwoProbabilityBounds[4]);
		updateProbabilityBounds(partyTwoCount, numSeats, 180, partyTwoProbabilityBounds[5]);
		updateProbabilityBounds(partyTwoCount, numSeats, 195, partyTwoProbabilityBounds[6]);
		updateProbabilityBounds(partyTwoCount, numSeats, 199, partyTwoProbabilityBounds[7]);

		updateProbabilityBounds(othersCount, numSeats, 1, othersProbabilityBounds[0]);
		updateProbabilityBounds(othersCount, numSeats, 5, othersProbabilityBounds[1]);
		updateProbabilityBounds(othersCount, numSeats, 20, othersProbabilityBounds[2]);
		updateProbabilityBounds(othersCount, numSeats, 50, othersProbabilityBounds[3]);
		updateProbabilityBounds(othersCount, numSeats, 150, othersProbabilityBounds[4]);
		updateProbabilityBounds(othersCount, numSeats, 180, othersProbabilityBounds[5]);
		updateProbabilityBounds(othersCount, numSeats, 195, othersProbabilityBounds[6]);
		updateProbabilityBounds(othersCount, numSeats, 199, othersProbabilityBounds[7]);
	}

	// Get a list of classic seats and list the in order of Coalition win %
	classicSeatList.clear();
	for (int seatIndex = 0; seatIndex < project.getSeatCount(); ++seatIndex) {
		Seat* seat = project.getSeatPtr(seatIndex);
		if (seat->isClassic2pp(partyOne, partyTwo)) {
			classicSeatList.push_back(ClassicSeat(seat, seatIndex));
		}
	}
	std::sort(classicSeatList.begin(), classicSeatList.end(),
		[partyTwo](ClassicSeat seatA, ClassicSeat seatB)
	{return seatA.seat->getMajorPartyWinRate(partyTwo) > seatB.seat->getMajorPartyWinRate(partyTwo); });

	lastUpdated = wxDateTime::Now();
}

float Simulation::getClassicSeatMajorPartyWinRate(int classicSeatIndex, Party const* thisParty) const {
	return (classicSeatList[classicSeatIndex].seat->incumbent == thisParty ?
		incumbentWinPercent[classicSeatList[classicSeatIndex].seatIndex] :
		100.0f - incumbentWinPercent[classicSeatList[classicSeatIndex].seatIndex]);
}

int Simulation::findBestSeatDisplayCenter(Party* partySorted, int numSeatsDisplayed) {
	// aim here is to find the range of seats with the greatest difference between most and least likely for "partySorted" to wion
	float bestProbRange = 50.0f;
	int bestCenter = int(classicSeatList.size()) / 2;
	for (int lastSeatIndex = numSeatsDisplayed - 1; lastSeatIndex < int(classicSeatList.size()); ++lastSeatIndex) {
		float lastSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex, partySorted);
		float firstSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex - numSeatsDisplayed + 1, partySorted);
		float probRange = abs(50.0f - (lastSeatProb + firstSeatProb) / 2);
		if (probRange < bestProbRange) {
			bestProbRange = probRange;
			bestCenter = lastSeatIndex - numSeatsDisplayed / 2;
		}
	}
	return bestCenter;
}

Simulation::OddsInfo Simulation::calculateOddsInfo(std::list<Seat>::iterator thisSeat)
{
	float incumbentOdds = (thisSeat->incumbentOdds > LongshotOddsThreshold ?
		pow(thisSeat->incumbentOdds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat->incumbentOdds);
	float challengerOdds = (thisSeat->challengerOdds > LongshotOddsThreshold ?
		pow(thisSeat->challengerOdds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat->challengerOdds);
	float challenger2Odds = (thisSeat->challenger2Odds > LongshotOddsThreshold ?
		pow(thisSeat->challenger2Odds, 3) / pow(LongshotOddsThreshold, 2) : thisSeat->challenger2Odds);
	// Calculate incumbent chance based on adjusted odds
	float totalChance = (1.0f / incumbentOdds + 1.0f / challengerOdds + 1.0f / challenger2Odds);
	float incumbentChance = (1.0f / incumbentOdds) / totalChance;
	float topTwoChance = (1.0f / challengerOdds) / totalChance + incumbentChance;
	return OddsInfo{ incumbentChance, topTwoChance };
}
