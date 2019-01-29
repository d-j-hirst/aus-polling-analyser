#include "Simulation.h"
#include "Projection.h"
#include "Model.h"
#include "Debug.h"
#include "Region.h"
#include "Party.h"
#include "PollingProject.h"
#include "CountProgress.h"
#include <algorithm>

#undef min
#undef max

const float LongshotOddsThreshold = 2.5f;
const float seatStdDev = 2.0f; // Seat standard deviation, should remove this and use a user-input parameter instead

static std::random_device rd;
static std::mt19937 gen;

static const std::array<float, 3> PreferenceConsistencyBase = { 1.2f, 1.4f, 1.8f };

void Simulation::run(PollingProject& project) {

	if (int(baseProjection->meanProjection.size()) == 0) return;

	gen.seed(rd());

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
		thisSeat->partyOneWinRate = 0.0f;
		thisSeat->partyTwoWinRate = 0.0f;
		thisSeat->partyOthersWinRate = 0.0f;
		thisSeat->simulatedMarginAverage = 0;
		thisSeat->latestResult = nullptr;
		bool isPartyOne = (thisSeat->incumbent == partyOne);
		thisSeat->region->localModifierAverage += thisSeat->localModifier * (isPartyOne ? 1.0f : -1.0f);
		++thisSeat->region->seatCount;
	}

	project.updateLatestResultsForSeats();

	determinePreviousVoteEnrolmentRatios(project);

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
				SeatResult result = calculateLiveResultClassic2CP(project, *thisSeat, newMargin);

				float incumbentNewMargin = result.margin * (result.winner == thisSeat->incumbent ? 1.0f : -1.0f);
				// Margin for this simulation is finalised, record it for later averaging
				thisSeat->simulatedMarginAverage += incumbentNewMargin;
				// If the margin is greater than zero, the incumbent wins the seat.
				thisSeat->winner = result.winner;
				//PrintDebugLine(thisSeat->winner->name);
				// Sometimes a classic 2pp seat may also have a independent with a significant chance,
				// but not high enough to make the top two - if so this will give a certain chance to
				// override the swing-based result with a win from the challenger
				if (thisSeat->challenger2Odds < 8.0f && !thisSeat->overrideBettingOdds) {
					OddsInfo oddsInfo = calculateOddsInfo(*thisSeat);
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand >= oddsInfo.topTwoChance) thisSeat->winner = thisSeat->challenger2;
				}
			} else {
				if (live && thisSeat->hasLiveResults()) {
					SeatResult result = calculateLiveResultNonClassic2CP(project, *thisSeat);
					thisSeat->winner = result.winner;
				}
				else {
					if (live && thisSeat->livePartyOne) {
						float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
						if (uniformRand < thisSeat->partyTwoProb) {
							thisSeat->winner = thisSeat->livePartyTwo;
						}
						else if (thisSeat->livePartyThree && uniformRand < thisSeat->partyTwoProb + thisSeat->partyThreeProb) {
							thisSeat->winner = thisSeat->livePartyThree;
						}
						else {
							thisSeat->winner = thisSeat->livePartyOne;
						}
					}
					else {
						thisSeat->winner = simulateWinnerFromBettingOdds(*thisSeat);
					}
				}
			}
			// If the winner is the incumbent, record this down in the seat's numbers
			thisSeat->incumbentWins += (thisSeat->winner == thisSeat->incumbent ? 1 : 0);

			if (thisSeat->winner == partyOne) ++thisSeat->partyOneWinRate;
			else if (thisSeat->winner == partyTwo) ++thisSeat->partyTwoWinRate;
			else ++thisSeat->partyOthersWinRate;

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
		thisSeat->partyOneWinRate /= double(numIterations);
		thisSeat->partyTwoWinRate /= double(numIterations);
		thisSeat->partyOthersWinRate /= double(numIterations);
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

void Simulation::determinePreviousVoteEnrolmentRatios(PollingProject& project)
{
	int ordinaryVoteNumerator = 0;
	int declarationVoteNumerator = 0;
	int voteDenominator = 0;
	for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
		if (thisSeat->previousResults) {
			ordinaryVoteNumerator += thisSeat->previousResults->ordinaryVotes();
			declarationVoteNumerator += thisSeat->previousResults->declarationVotes();
			voteDenominator += thisSeat->previousResults->enrolment;
		}
	}
	previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

Simulation::OddsInfo Simulation::calculateOddsInfo(Seat const& thisSeat)
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

Simulation::SeatResult Simulation::calculateLiveResultClassic2CP(PollingProject const& project, Seat const& seat, float priorMargin)
{
	if (live && seat.latestResult && seat.latestResult->getPercentCountedEstimate()) {
		// All swings are in terms of a swing to candidate 0 as per latest results
		Party const* firstParty = project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
		Party const* secondParty = project.getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);
		bool incumbentFirst = firstParty == seat.incumbent;
		float liveSwing = (incumbentFirst ? 1.0f : -1.0f) * seat.latestResult->incumbentSwing;
		std::array<int, 2> tcpTally = { 0, 0 };
		int newComparisonVotes = 0;
		int oldComparisonVotes = 0;
		for (auto boothId : seat.latestResults->booths) {
			Results::Booth const& booth = project.getBooth(boothId);
			bool isInSeatOrder = project.getPartyByCandidate(booth.tcpCandidateId[0]) == firstParty;
			if (booth.hasNewResults()) {
				tcpTally[0] += float(isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
				tcpTally[1] += float(isInSeatOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
			}
			if (booth.hasOldAndNewResults()) {
				oldComparisonVotes += booth.totalOldVotes();
				newComparisonVotes += booth.totalNewVotes();
			}
		}

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
		float individualBoothGrowth = (oldComparisonVotes ? float(newComparisonVotes) / float(oldComparisonVotes) : 1);
		int mysteryStandardBooths = 0; // count all booths that can't be matched and haven't been counted yet
		int mysteryPPVCBooths = 0; // count all booths that can't be matched and haven't been counted yet
		int mysteryTeamBooths = 0; // count all booths that can't be matched and haven't been counted yet
		for (auto boothId : seat.latestResults->booths) {
			Results::Booth const& booth = project.getBooth(boothId);
			if (booth.hasOldResults() && !booth.hasNewResults()) {
				int estimatedTotalVotes = int(std::round(float(booth.totalOldVotes()) * individualBoothGrowth));
				bool isInSeatOrder = project.getPartyByCandidate(booth.tcpCandidateId[0]) == firstParty;
				float oldVotes0 = float(isInSeatOrder ? booth.tcpVote[0] : booth.tcpVote[1]);
				float oldVotes1 = float(isInSeatOrder ? booth.tcpVote[1] : booth.tcpVote[0]);
				float oldPercent0 = oldVotes0 / (oldVotes0 + oldVotes1) * 100.0f;
				float boothSwingStdDev = 2.5f + 200.0f / booth.totalOldVotes(); // small booths a lot swingier
				float boothSwing = remainingVoteSwing + std::normal_distribution<float>(0.0f, boothSwingStdDev)(gen);
				float newPercent0 = std::clamp(oldPercent0 + boothSwing, 0.0f, 100.0f);
				int newVotes0 = int(std::round(newPercent0 * float(estimatedTotalVotes) * 0.01f));
				int newVotes1 = estimatedTotalVotes - newVotes0;
				tcpTally[0] += newVotes0;
				tcpTally[1] += newVotes1;
			}
			if (!booth.hasOldResults() && !booth.hasNewResults()) {
				if (booth.name.find("PPVC") != std::string::npos) ++mysteryPPVCBooths;
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
			estimatedRemainingOrdinaryVotes = std::min(estimatedRemainingOrdinaryVotes, plausibleMaximumRemainingOrdinaryVotes);
			estimatedTotalOrdinaryVotes = estimatedRemainingOrdinaryVotes + tcpTally[0] + tcpTally[1];

			const float MysteryVoteStdDev = 6.0f;
			float incumbentMysteryPercent = std::normal_distribution<float>(firstTallyPercent, MysteryVoteStdDev)(gen);
			int incumbentMysteryVotes = int(std::round(incumbentMysteryPercent * 0.01f * float(estimatedRemainingOrdinaryVotes)));
			int challengerMysteryVotes = estimatedRemainingOrdinaryVotes - incumbentMysteryVotes;
			tcpTally[0] += incumbentMysteryVotes;
			tcpTally[1] += challengerMysteryVotes;
		}

		// Now estimate declaration vote totals and add these to the total tallies

		float totalOrdinaryTally = float(tcpTally[0] + tcpTally[1]);
		if (seat.previousResults) {
			bool sameOrder = firstParty == project.getPartyByAffliation(seat.previousResults->finalCandidates[0].affiliationId);
			int estimatedTotalVotes = int(float(seat.previousResults->totalVotes()) * enrolmentChange);
			int estimatedDeclarationVotes = estimatedTotalVotes - estimatedTotalOrdinaryVotes;
			int oldDeclarationVotes = seat.previousResults->totalVotes() - seat.previousResults->ordinaryVotes();
			float declarationVoteChange = float(estimatedDeclarationVotes) / float(oldDeclarationVotes);

			float firstNewOrdinaryPercent = float(tcpTally[0]) / totalOrdinaryTally * 100.0f;
			float firstOldOrdinaryPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].ordinaryVotes) / float(seat.previousResults->ordinaryVotes()) * 100.0f;
			float ordinaryVoteSwing = firstNewOrdinaryPercent - firstOldOrdinaryPercent;

			float absentStdDev = 5.0f; // needs more research
			float absentSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, absentStdDev)(gen);
			float firstOldAbsentPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].absentVotes) / float(seat.previousResults->absentVotes()) * 100.0f;
			float firstNewAbsentPercent = firstOldAbsentPercent + absentSwing;
			int estimatedAbsentVotes = int(std::round(float(seat.previousResults->absentVotes()) * declarationVoteChange));
			int firstAbsentVotes = int(std::round(firstNewAbsentPercent * float(estimatedAbsentVotes) * 0.01f));
			int secondAbsentVotes = estimatedAbsentVotes - firstAbsentVotes;

			float provisionalStdDev = 5.0f; // needs more research
			float provisionalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, provisionalStdDev)(gen);
			float firstOldProvisionalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].provisionalVotes) / float(seat.previousResults->provisionalVotes()) * 100.0f;
			float firstNewProvisionalPercent = firstOldProvisionalPercent + provisionalSwing;
			int estimatedProvisionalVotes = int(std::round(float(seat.previousResults->provisionalVotes()) * declarationVoteChange));
			int firstProvisionalVotes = int(std::round(firstNewProvisionalPercent * float(estimatedProvisionalVotes) * 0.01f));
			int secondProvisionalVotes = estimatedProvisionalVotes - firstProvisionalVotes;

			float prepollStdDev = 5.0f; // needs more research
			float prepollSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, prepollStdDev)(gen);
			float firstOldPrepollPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].prepollVotes) / float(seat.previousResults->prepollVotes()) * 100.0f;
			float firstNewPrepollPercent = firstOldPrepollPercent + prepollSwing;
			int estimatedPrepollVotes = int(std::round(float(seat.previousResults->prepollVotes()) * declarationVoteChange));
			int firstPrepollVotes = int(std::round(firstNewPrepollPercent * float(estimatedPrepollVotes) * 0.01f));
			int secondPrepollVotes = estimatedPrepollVotes - firstPrepollVotes;

			float postalStdDev = 5.0f; // needs more research
			float postalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, postalStdDev)(gen);
			float firstOldPostalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].postalVotes) / float(seat.previousResults->postalVotes()) * 100.0f;
			float firstNewPostalPercent = firstOldPostalPercent + postalSwing;
			int estimatedPostalVotes = int(std::round(float(seat.previousResults->postalVotes()) * declarationVoteChange));
			int firstPostalVotes = int(std::round(firstNewPostalPercent * float(estimatedPostalVotes) * 0.01f));
			int secondPostalVotes = estimatedPostalVotes - firstPostalVotes;

			tcpTally[0] += firstAbsentVotes + firstProvisionalVotes + firstPrepollVotes + firstPostalVotes;
			tcpTally[1] += secondAbsentVotes + secondProvisionalVotes + secondPrepollVotes + secondPostalVotes;
		}
		else {
			int estimatedTotalDeclarationVotes = int(float(seat.latestResults->enrolment) * previousOrdinaryVoteEnrolmentRatio);
			if (seat.latestResults->ordinaryVotes() > estimatedTotalOrdinaryVotes) {
				float estimatedNonOrdinaryVotePotential = float(seat.latestResults->enrolment - estimatedTotalOrdinaryVotes);
				float estimatedPercentageRemainingFormal = float(estimatedTotalDeclarationVotes) / estimatedNonOrdinaryVotePotential;
				float actualNonOrdinaryVotePotential = float(seat.latestResults->enrolment - seat.latestResults->ordinaryVotes());
				estimatedTotalDeclarationVotes = int(actualNonOrdinaryVotePotential * estimatedPercentageRemainingFormal);

				float firstPercent = float(tcpTally[0]) / totalOrdinaryTally;
				float declarationStdDev = 0.05f;
				float declarationVoteFirstProportion = std::clamp(firstPercent + std::normal_distribution<float>(0.0f, declarationStdDev)(gen), 0.0f, 1.0f);
				int firstTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * declarationVoteFirstProportion);
				int secondTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * (1.0f - declarationVoteFirstProportion));
				tcpTally[0] += firstTcpDeclarationVotes;
				tcpTally[1] += secondTcpDeclarationVotes;
			}
		}
		float totalTally = float(tcpTally[0] + tcpTally[1]);
		float firstMargin = (float(tcpTally[0]) - totalTally * 0.5f) / totalTally * 100.0f;
		Party const* winner = (firstMargin >= 0.0f ? firstParty : secondParty);
		Party const* runnerUp = (firstMargin >= 0.0f ? secondParty : firstParty);

		return {winner, runnerUp, abs(firstMargin)};
	}

	Party const* winner = (priorMargin >= 0.0f ? seat.incumbent : seat.challenger);
	Party const* runnerUp = (priorMargin >= 0.0f ? seat.challenger : seat.incumbent);
	return { winner, runnerUp, abs(priorMargin) };
}

Simulation::SeatResult Simulation::calculateLiveResultNonClassic2CP(PollingProject const & project, Seat const & seat)
{
	if (live && seat.latestResults && seat.latestResults->totalVotes()) {
		int firstTcpTally = 0;
		int secondTcpTally = 0;
		int newComparisonVotes = 0;
		int oldComparisonVotes = 0;
		for (auto boothId : seat.latestResults->booths) {
			Results::Booth const& booth = project.getBooth(boothId);
			bool matchingOrder = project.getPartyByAffliation(booth.tcpAffiliationId[0]) == project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);

			if (booth.hasNewResults()) {
				firstTcpTally += (matchingOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
				secondTcpTally += (matchingOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
			}
			if (booth.hasOldAndNewResults()) {
				oldComparisonVotes += booth.totalOldVotes();
				newComparisonVotes += booth.totalNewVotes();
			}
		}

		float totalTally = float(firstTcpTally + secondTcpTally);
		float margin = (float(firstTcpTally) - totalTally * 0.5f) / totalTally * 100.0f;
		Party const* winner = (margin >= 0.0f ? project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId)
			: project.getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId));
		Party const* runnerUp = (margin >= 0.0f ? project.getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId)
			: project.getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId));

		return { winner, runnerUp, margin };
	}
	else if (live && seat.latestResults && seat.latestResults->fpCandidates.size()) {
		struct Candidate { int vote; Party const* party; } ;

		std::vector<Candidate> candidates;
		for (auto const& fpCandidate : seat.latestResults->fpCandidates) {
			candidates.push_back({ fpCandidate.totalVotes(), project.getPartyByCandidate(fpCandidate.candidateId) });
		}
		for (int sourceIndex = candidates.size() - 1; sourceIndex > 1; --sourceIndex) {
			Candidate sourceCandidate = candidates[sourceIndex];
			candidates[sourceIndex].vote = 0;
			std::vector<float> weights;
			weights.resize(candidates.size(), 0);
			for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
				Candidate const& targetCandidate = candidates[targetIndex];
				int ideologyDistance = float(std::abs(sourceCandidate.party->ideology - targetCandidate.party->ideology));
				float consistencyBase = PreferenceConsistencyBase[sourceCandidate.party->consistency];
				float thisWeight = std::pow(consistencyBase, -ideologyDistance);
				if (!sourceCandidate.party->countsAsMajor() && !targetCandidate.party->countsAsMajor()) thisWeight *= 1.6f;
				if (Party::oppositeMajors(*sourceCandidate.party, *targetCandidate.party)) thisWeight /= 2.0f;
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
		Party const* winner = candidates[0].party;
		Party const* runnerUp = candidates[1].party;

		return { winner, runnerUp, margin };
	}

	return SeatResult();
}

Party const * Simulation::simulateWinnerFromBettingOdds(Seat const& thisSeat)
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
