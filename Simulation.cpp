#include "Simulation.h"

#include "CountProgress.h"
#include "Log.h"
#include "Model.h"
#include "Party.h"
#include "PollingProject.h"
#include "Projection.h"
#include "Region.h"
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

	for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
		thisRegion->localModifierAverage = 0.0f;
		thisRegion->seatCount = 0;

		thisRegion->liveSwing = 0.0f;
		thisRegion->livePercentCounted = 0.0f;
		thisRegion->classicSeatCount = 0;
	}

	// Set up anything that needs to be prepared for seats
	ppvcBiasNumerator = 0.0f;
	ppvcBiasDenominator = 0.0f;
	totalOldPpvcVotes = 0;
	for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
		thisSeat->incumbentWins = 0;
		thisSeat->partyOneWinRate = 0.0f;
		thisSeat->partyTwoWinRate = 0.0f;
		thisSeat->partyOthersWinRate = 0.0f;
		thisSeat->simulatedMarginAverage = 0;
		thisSeat->latestResult = nullptr;
		bool isPartyOne = (thisSeat->incumbent == 0);
		thisSeat->region->localModifierAverage += thisSeat->localModifier * (isPartyOne ? 1.0f : -1.0f);
		++thisSeat->region->seatCount;
		if (isLiveAutomatic()) determineSeatCachedBoothData(project, *thisSeat);
	}

	determinePpvcBias();

	// this stores the manually input results for seats so that they're ready for the simulations
	// to use them if set to "Live Manual"
	project.updateLatestResultsForSeats();

	determinePreviousVoteEnrolmentRatios(project);

	// Resize regional seat counts based on the counted number of seats for each region
	for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
		thisRegion->partyLeading.clear();
		thisRegion->partyWins.clear();
		thisRegion->partyLeading.resize(project.parties().count());
		thisRegion->partyWins.resize(project.parties().count(), std::vector<int>(thisRegion->seatCount + 1));
	}

	// Record how many seats each party leads in (notionally) in each region
	for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
		++thisSeat->region->partyLeading[project.parties().idToIndex(thisSeat->getLeadingParty())];
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
	int total2cpVotes = 0;
	int totalEnrolment = 0;
	if (isLive()) {
		for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {
			if (!thisSeat->isClassic2pp(isLiveAutomatic())) continue;
			++classicSeatCount;
			++thisSeat->region->classicSeatCount;
			if (!thisSeat->latestResult) continue;
			bool incIsOne = thisSeat->incumbent == 0;
			float percentCounted = thisSeat->latestResult->getPercentCountedEstimate();
			float weightedSwing = thisSeat->latestResult->incumbentSwing * (incIsOne ? 1.0f : -1.0f) * percentCounted;
			liveOverallSwing += weightedSwing;
			thisSeat->region->liveSwing += weightedSwing;
			liveOverallPercent += percentCounted;
			thisSeat->region->livePercentCounted += percentCounted;
			sampleRepresentativeness += std::min(2.0f, percentCounted) * 0.5f;
			total2cpVotes += thisSeat->latestResults->total2cpVotes();
			totalEnrolment += thisSeat->latestResults->enrolment;
		}
		if (liveOverallPercent) {
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
	}

	total2cpPercentCounted = (float(totalEnrolment) ? float(total2cpVotes) / float(totalEnrolment) : 0);

	int partyOneMajority = 0;
	int partyOneMinority = 0;
	int hungParliament = 0;
	int partyTwoMinority = 0;
	int partyTwoMajority = 0;

	partySeatWinFrequency.clear();
	partySeatWinFrequency.resize(project.parties().count(), std::vector<int>(project.getSeatCount() + 1));
	othersWinFrequency.clear();
	othersWinFrequency.resize(project.getSeatCount() + 1);
	partyOneSwing = 0.0;

	float pollOverallSwing = baseProjection->meanProjection.back() - prevElection2pp;
	float pollOverallStdDev = baseProjection->sdProjection.back();
	for (currentIteration = 0; currentIteration < numIterations; ++currentIteration) {

		// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
		std::vector<std::vector<int>> regionSeatCount(project.parties().count(), std::vector<int>(project.getRegionCount()));

		// First, randomly determine the national swing for this particular simulation
		float simulationOverallSwing = std::normal_distribution<float>(pollOverallSwing, pollOverallStdDev)(gen);

		if (isLive() && liveOverallPercent) {
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

			if (isLive() && thisRegion->livePercentCounted) {
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

		// Adjust regional swings to keep the implied overall 2pp the same as that actually projected
		float regionSwingAdjustment = simulationOverallSwing - tempOverallSwing;
		for (auto thisRegion = project.getRegionBegin(); thisRegion != project.getRegionEnd(); ++thisRegion) {
			thisRegion->simulationSwing += regionSwingAdjustment;
		}

		partyOneSwing += double(simulationOverallSwing);

		std::vector<int> partyWins(project.parties().count());

		// Now cycle through all the seats and generate a result for each
		for (auto thisSeat = project.getSeatBegin(); thisSeat != project.getSeatEnd(); ++thisSeat) {

			// First determine if this seat is "classic" (main-parties only) 2CP, which determines how we get a result and the winner
			bool isClassic2CP = thisSeat->isClassic2pp(isLive());

			if (isClassic2CP) {
				bool incIsOne = thisSeat->incumbent == 0; // stores whether the incumbent is Party One
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
				// Sometimes a classic 2pp seat may also have a independent with a significant chance,
				// but not high enough to make the top two - if so this will give a certain chance to
				// override the swing-based result with a win from the challenger
				if ((!isLiveAutomatic() || !thisSeat->hasLiveResults()) && thisSeat->challenger2Odds < 8.0f && !thisSeat->overrideBettingOdds) {
					OddsInfo oddsInfo = calculateOddsInfo(*thisSeat);
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand >= oddsInfo.topTwoChance) thisSeat->winner = thisSeat->challenger2;
				}
				// If a seat is marked as classic by the AEC but betting odds say it isn't, possibly use the betting
				// odds to give a more accurate reflection
				// For e.g. 2016 Cowper had Coalition vs. Independent in betting but was classic in early count,
				// so the independent's recorded votes were considered insignificant and the seat was overly favourable
				// to the Coalition.
				if (result.significance < 1.0f) {
					if (!project.parties().oppositeMajors(thisSeat->incumbent, thisSeat->challenger)) {
						if (!isLiveAutomatic() || !thisSeat->winner || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > result.significance) {
							if (isLiveAutomatic() && thisSeat->livePartyOne) {
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
				}
			} else {
				float liveSignificance = 0.0f;
				if (isLiveAutomatic()) {
					SeatResult result = calculateLiveResultNonClassic2CP(project, *thisSeat);
					thisSeat->winner = result.winner;
					liveSignificance = result.significance;
				}
				// If we haven't got very far into the live count, it might be unrepresentative,
				// so randomly choose between seat betting odds and the actual live count until
				// more results come in.
				if (liveSignificance < 1.0f) {
					if (!isLiveAutomatic() || !thisSeat->winner || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > liveSignificance) {
						if (isLive() && thisSeat->livePartyOne) {
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
			}

			// If the winner is the incumbent, record this down in the seat's numbers
			thisSeat->incumbentWins += (thisSeat->winner == thisSeat->incumbent ? 1 : 0);

			if (thisSeat->winner == 0) ++thisSeat->partyOneWinRate;
			else if (thisSeat->winner == 1) ++thisSeat->partyTwoWinRate;
			else ++thisSeat->partyOthersWinRate;

			int winnerIndex = project.parties().idToIndex(thisSeat->winner);
			partyWins[winnerIndex]++;
			int regionIndex = project.getRegionIndex(thisSeat->region);
			++regionSeatCount[winnerIndex][regionIndex];
		}

		//Assign all the wins from coalition third-parties to the respective major party
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			Party const& thisParty = project.parties().view(project.parties().indexToId(partyNum));
			if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyOne){
				partyWins[0] += partyWins[partyNum];
			}
			else if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyTwo){
				partyWins[1] += partyWins[partyNum];
			}
		}


		//Get the number of seats supporting each major party in a minority government
		std::array<int, 2> partySupport = { partyWins[0], partyWins[1] };
		for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
			Party const& thisParty = project.parties().view(project.parties().indexToId(partyNum));
			if (thisParty.supportsParty == Party::SupportsParty::One) {
				partySupport[0] += partyWins[partyNum];
			}
			else if (thisParty.supportsParty == Party::SupportsParty::Two) {
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
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
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

	partyWinExpectation.resize(project.parties().count());

	partyOneMajorityPercent = float(partyOneMajority) / float(numIterations) * 100.0f;
	partyOneMinorityPercent = float(partyOneMinority) / float(numIterations) * 100.0f;
	hungPercent = float(hungParliament) / float(numIterations) * 100.0f;
	partyTwoMinorityPercent = float(partyTwoMinority) / float(numIterations) * 100.0f;
	partyTwoMajorityPercent = float(partyTwoMajority) / float(numIterations) * 100.0f;
	partyOneSwing = partyOneSwing / double(numIterations);

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		int totalSeats = 0;
		for (int seatNum = 1; seatNum < project.getSeatCount(); ++seatNum) {
			totalSeats += seatNum * partySeatWinFrequency[partyIndex][seatNum];
		}
		partyWinExpectation[partyIndex] = float(totalSeats) / float(numIterations);
	}

	regionPartyWinExpectation.resize(project.getRegionCount(), std::vector<float>(project.parties().count(), 0.0f));

	for (int regionIndex = 0; regionIndex < project.getRegionCount(); ++regionIndex) {
		Region thisRegion = project.getRegion(regionIndex);
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			int totalSeats = 0;
			for (int seatNum = 1; seatNum < int(thisRegion.partyWins[partyIndex].size()); ++seatNum) {
				totalSeats += seatNum * thisRegion.partyWins[partyIndex][seatNum];
			}
			regionPartyWinExpectation[regionIndex][partyIndex] = float(totalSeats) / float(numIterations);
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
		if (seat->isClassic2pp(isLiveAutomatic())) {
			classicSeatList.push_back(ClassicSeat(seat, seatIndex));
		}
	}
	std::sort(classicSeatList.begin(), classicSeatList.end(),
		[](ClassicSeat seatA, ClassicSeat seatB)
	{return seatA.seat->getMajorPartyWinRate(1) > seatB.seat->getMajorPartyWinRate(1); });

	lastUpdated = wxDateTime::Now();
}

float Simulation::getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex) const {
	return (classicSeatList[classicSeatIndex].seat->incumbent == partyIndex ?
		incumbentWinPercent[classicSeatList[classicSeatIndex].seatIndex] :
		100.0f - incumbentWinPercent[classicSeatList[classicSeatIndex].seatIndex]);
}

int Simulation::findBestSeatDisplayCenter(Party::Id partySorted, int numSeatsDisplayed) {
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

void Simulation::determinePpvcBias()
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

void Simulation::determinePreviousVoteEnrolmentRatios(PollingProject& project)
{
	if (!isLiveAutomatic()) return;

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
	if (!voteDenominator) return;
	previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

void Simulation::determineSeatCachedBoothData(PollingProject const& project, Seat& seat)
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

		logger << seatFirstPartyPreferences << " " << seatSecondPartyPreferences << " " <<
			seat.firstPartyPreferenceFlow << " " << seat.preferenceFlowVariation << " preference flow to " <<
			project.parties().view(firstSeatParty).name << " vs " << project.parties().view(secondSeatParty).name << " - " << seat.name << "\n";
	}

	seat.individualBoothGrowth = (oldComparisonVotes ? float(newComparisonVotes) / float(oldComparisonVotes) : 1);
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
	if (isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
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
		SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(project, seat);
		if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) {
			if (std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) < spoilerResult.significance) {
				return spoilerResult;
			}
		}

		float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

		return {winner, runnerUp, abs(firstMargin), float(significance)};
	} else if (isLive() && seat.latestResult && seat.latestResult->getPercentCountedEstimate()) {
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

Simulation::SeatResult Simulation::calculateLiveResultNonClassic2CP(PollingProject const& project, Seat const& seat)
{
	if (isLiveAutomatic() && seatPartiesMatchBetweenElections(project, seat)) {
		if (!currentIteration) logger << seat.name << " - matched booths\n";
		return calculateLiveResultClassic2CP(project, seat, seat.margin);
	}
	else if (isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
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
		SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(project, seat);
		if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) return spoilerResult;

		float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

		return { winner, runnerUp, margin, significance };
	}
	else if (isLiveAutomatic() && seat.latestResults && seat.latestResults->fpCandidates.size() && seat.latestResults->totalFpVotes()) {
		if (!currentIteration) logger << seat.name << " - first preferences\n";
		return calculateLiveResultFromFirstPreferences(project, seat);
	}
	else {
		return { seat.incumbent, seat.challenger, seat.margin, 0.0f };
	}
}

Simulation::SeatResult Simulation::calculateLiveResultFromFirstPreferences(PollingProject const & project, Seat const & seat)
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
		Party const& sourceParty = project.parties().view(sourceCandidate.partyId);
		candidates[sourceIndex].vote = 0;
		std::vector<float> weights;
		weights.resize(candidates.size(), 0);
		for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
			Candidate const& targetCandidate = candidates[targetIndex];
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

Party::Id Simulation::simulateWinnerFromBettingOdds(Seat const& thisSeat)
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

bool Simulation::seatPartiesMatchBetweenElections(PollingProject const & project, Seat const& seat)
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

float Simulation::determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes)
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
