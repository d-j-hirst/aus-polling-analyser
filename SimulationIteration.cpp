#include "SimulationIteration.h"

#include "CountProgress.h"
#include "SpecialPartyCodes.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"

using Mp = Simulation::MajorParty;

static std::random_device rd;
static std::mt19937 gen;

// Threshold at which longshot-bias correction starts being applied for seats being approximated from betting odds
constexpr float LongshotOddsThreshold = 2.5f;

// Seat standard deviation, someday remove this and use a user-input parameter instead
constexpr float seatStdDev = 2.0f;

// How strongly preferences align with ideology based on the "consistency" property of a party
constexpr std::array<float, 3> PreferenceConsistencyBase = { 1.2f, 1.4f, 1.8f };

SimulationIteration::SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void SimulationIteration::runIteration()
{
	initialiseIterationSpecificCounts(); // clean
	determineIterationOverallSwing(); // clean
	determineIterationPpvcBias(); // clean
	determineIterationRegionalSwings(); // clean

	for (auto&[key, seat] : project.seats()) {
		determineSeatResult(seat); // dirty - seat editing
		recordSeatResult(seat); // dirty - seat editing
	}

	assignCountAsPartyWins(); // clean
	assignSupportsPartyWins(); // clean
	classifyMajorityResult(); // dirty - run
	addPartySeatWinCounts(); // dirty - region editing, latestReport

	// This should eventually do all the actual recording of results
	// to sim/run storage - everything above should only edit local
	// variables, to allow efficient multithreading
	recordIterationResults();
}

void SimulationIteration::initialiseIterationSpecificCounts()
{
	// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
	regionSeatCount = std::vector<std::vector<int>>(project.parties().count(), std::vector<int>(project.regions().count()));

	partyWins = std::vector<int>(project.parties().count());
	overallFp = std::vector<float>(project.parties().count(), 0.0f);

	// First, randomly determine the national swing for this particular simulation
	auto projectedSample = project.projections().view(sim.settings.baseProjection).generateSupportSample(project.models());
	iterationOverallTpp = projectedSample.at(TppCode);
	iterationOverallSwing = iterationOverallTpp - sim.settings.prevElection2pp;

	for (auto const& [sampleKey, partySample] : projectedSample) {
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				overallFp[partyIndex] = partySample;
				break;
			}
		}
	}
}

void SimulationIteration::determineIterationOverallSwing()
{
	if (sim.isLive() && run.liveOverallPercent) {
		float liveSwing = run.liveOverallSwing;
		float liveStdDev = stdDevOverall(run.liveOverallPercent);
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev) * run.sampleRepresentativeness;
		iterationOverallSwing = (iterationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	}
}

void SimulationIteration::determineIterationPpvcBias()
{
	if (sim.isLive() && run.liveOverallPercent) {
		constexpr float ppvcBiasStdDev = 4.0f;
		float ppvcBiasRandom = std::normal_distribution<float>(0.0f, ppvcBiasStdDev)(gen);
		ppvcBias = run.ppvcBiasObserved * run.ppvcBiasConfidence + ppvcBiasRandom * (1.0f - run.ppvcBiasConfidence);
	}
}

void SimulationIteration::determineIterationRegionalSwings()
{
	const int numRegions = project.regions().count();
	regionSwing.resize(numRegions);
	for (int regionIndex = 0; regionIndex < numRegions; ++regionIndex) {
		determineBaseRegionalSwing(regionIndex);
		modifyLiveRegionalSwing(regionIndex);
	}
	correctRegionalSwings();
}

void SimulationIteration::determineBaseRegionalSwing(int regionIndex)
{
	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);
	Region const& thisRegion = project.regions().viewByIndex(regionIndex);
	// Calculate mean of the region's swing after accounting for decay to the mean over time.
	float regionMeanSwing = iterationOverallSwing +
		thisRegion.swingDeviation * pow(1.0f - sim.settings.stateDecay, thisProjection.getProjectionLength());
	// Add random noise to the region's swing level
	float swingSD = sim.settings.stateSD + thisRegion.additionalUncertainty;
	if (swingSD > 0) {
		float totalSD = sim.settings.stateSD + thisRegion.additionalUncertainty;
		regionSwing[regionIndex] = std::normal_distribution<float>(regionMeanSwing, totalSD)(gen);
	}
	else {
		regionSwing[regionIndex] = regionMeanSwing;
	}
}

void SimulationIteration::modifyLiveRegionalSwing(int regionIndex)
{
	Region const& thisRegion = project.regions().viewByIndex(regionIndex);
	if (sim.isLive() && thisRegion.livePercentCounted) {
		float liveSwing = thisRegion.liveSwing;
		float liveStdDev = stdDevSingleSeat(thisRegion.livePercentCounted);
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev);
		regionSwing[regionIndex] = (regionSwing[regionIndex] * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	}
}

void SimulationIteration::correctRegionalSwings()
{
	// Adjust regional swings to keep the implied overall 2pp the same as that actually projected
	float tempOverallSwing = 0.0f;
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		Region const& thisRegion = project.regions().viewByIndex(regionIndex);
		tempOverallSwing += regionSwing[regionIndex] * thisRegion.population;
	}
	tempOverallSwing /= run.totalPopulation;
	float regionSwingAdjustment = iterationOverallSwing - tempOverallSwing;
	for (float& swing : regionSwing) {
		swing += regionSwingAdjustment;
	}
}

void SimulationIteration::determineSeatResult(Seat& seat)
{
	// First determine if this seat is "classic" (main-parties only) 2CP, which determines how we get a result and the winner
	bool isClassic2CP = seat.isClassic2pp(sim.isLive());

	if (isClassic2CP) {
		determineClassicSeatResult(seat);
	}
	else {
		determineNonClassicSeatResult(seat);
	}
}

void SimulationIteration::determineClassicSeatResult(Seat& seat)
{
	Region const& thisRegion = project.regions().access(seat.region);
	bool incIsOne = seat.incumbent == 0; // stores whether the incumbent is Party One
										 // Add or subtract the simulation regional deviation depending on which party is incumbent
	float newMargin = seat.margin + regionSwing[project.regions().idToIndex(seat.region)] * (incIsOne ? 1.0f : -1.0f);
	// Add modifiers for known local effects (these are measured as positive if favouring the incumbent)
	newMargin += seat.localModifier;
	// Remove the average local modifier across the region
	newMargin -= thisRegion.localModifierAverage * (incIsOne ? 1.0f : -1.0f);
	// Add random noise to the new margin of this seat
	newMargin += std::normal_distribution<float>(0.0f, seatStdDev)(gen);
	// Now work out the margin of the seat from actual results if live
	SeatResult result = calculateResultMatched2cp(seat, newMargin);

	float incumbentNewMargin = result.margin * (result.winner == seat.incumbent ? 1.0f : -1.0f);
	// Margin for this simulation is finalised, record it for later averaging
	seat.simulatedMarginAverage += incumbentNewMargin;
	seat.winner = result.winner;
	adjustClassicSeatResultFor3rdPlaceIndependent(seat);
	adjustClassicSeatResultForBettingOdds(seat, result);
}

void SimulationIteration::adjustClassicSeatResultFor3rdPlaceIndependent(Seat& seat)
{
	// Sometimes a classic 2pp seat may also have a independent with a significant chance,
	// but not high enough to make the top two - if so this will give a certain chance to
	// override the swing-based result with a win from the challenger
	if ((!sim.isLiveAutomatic() || !seat.hasLiveResults()) && seat.challenger2Odds < 8.0f && !seat.overrideBettingOdds) {
		OddsInfo oddsInfo = calculateOddsInfo(seat);
		float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
		if (uniformRand >= oddsInfo.topTwoChance) seat.winner = seat.challenger2;
	}
}

void SimulationIteration::adjustClassicSeatResultForBettingOdds(Seat& seat, SeatResult result)
{
	// If a seat is marked as classic by the AEC but betting odds say it isn't, possibly use the betting
	// odds to give a more accurate reflection
	// For e.g. 2016 Cowper had Coalition vs. Independent in betting but was classic in early count,
	// so the independent's recorded votes were considered insignificant and the seat was overly favourable
	// to the Coalition.
	if (result.significance < 1.0f) {
		if (!project.parties().oppositeMajors(seat.incumbent, seat.challenger)) {
			if (!sim.isLiveAutomatic() || seat.winner == Party::InvalidId || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > result.significance) {
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

void SimulationIteration::determineNonClassicSeatResult(Seat& seat)
{
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

void SimulationIteration::recordSeatResult(Seat& seat)
{
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

void SimulationIteration::assignCountAsPartyWins()
{
	for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyOne) {
			partyWins[0] += partyWins[partyNum];
		}
		else if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyTwo) {
			partyWins[1] += partyWins[partyNum];
		}
	}
}

void SimulationIteration::assignSupportsPartyWins()
{
	partySupport = { partyWins[0], partyWins[1] };
	for (int partyNum = 2; partyNum < int(partyWins.size()); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.relationType == Party::RelationType::IsPartOf && thisParty.relationTarget < 2) {
			partyWins[thisParty.relationTarget] += partyWins[partyNum];
		}
		if (thisParty.relationType == Party::RelationType::Supports && thisParty.relationTarget < 2) {
			partySupport[thisParty.relationTarget] += partyWins[partyNum];
		}
	}
}

void SimulationIteration::classifyMajorityResult()
{
	int minimumForMajority = project.seats().count() / 2 + 1;

	// Look at the overall result and classify it
	if (partyWins[0] >= minimumForMajority) ++run.partyMajority[Simulation::MajorParty::One];
	else if (partySupport[0] >= minimumForMajority) ++run.partyMinority[Mp::One];
	else if (partyWins[1] >= minimumForMajority) ++run.partyMajority[Mp::Two];
	else if (partySupport[1] >= minimumForMajority) ++run.partyMinority[Mp::Two];
	else ++run.hungParliament;
}

void SimulationIteration::addPartySeatWinCounts()
{
	int othersWins = 0;
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		++sim.latestReport.partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
		if (partyIndex > 1) othersWins += partyWins[partyIndex];
		for (auto& regionPair : project.regions()) {
			Region& thisRegion = regionPair.second;
			++thisRegion.partyWins[partyIndex][regionSeatCount[partyIndex][project.regions().idToIndex(regionPair.first)]];
		}
	}
	++sim.latestReport.othersWinFrequency[othersWins];
}

void SimulationIteration::recordIterationResults()
{
	recordVoteTotals();
	recordSwings();
}

void SimulationIteration::recordVoteTotals()
{
	short tppBucket = short(floor(iterationOverallTpp * 10.0f));
	++sim.latestReport.tppFrequency[tppBucket];

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		short bucket = short(floor(overallFp[partyIndex] * 10.0f));
		++sim.latestReport.partyPrimaryFrequency[partyIndex][bucket];
	}
}

void SimulationIteration::recordSwings()
{
	// this will be used to determine the estimated 2pp swing (for live results) later
	sim.latestReport.partyOneSwing += double(iterationOverallSwing);
}

SimulationIteration::OddsInfo SimulationIteration::calculateOddsInfo(Seat const& thisSeat)
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

SimulationIteration::SeatResult SimulationIteration::calculateResultMatched2cp(Seat const& seat, float priorMargin)
{
	if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
		return calculateLiveAutomaticResultMatched2cp(seat, priorMargin);
	}
	else if (sim.isLive() && seat.outcome && seat.outcome->getPercentCountedEstimate()) {
		return calculateLiveManualResultMatched2cp(seat, priorMargin);
	}
	else {
		Party::Id winner = (priorMargin >= 0.0f ? seat.incumbent : seat.challenger);
		Party::Id runnerUp = (priorMargin >= 0.0f ? seat.challenger : seat.incumbent);
		return { winner, runnerUp, abs(priorMargin), 0.0f };
	}
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveAutomaticResultMatched2cp(Seat const& seat, float priorMargin)
{
	// All swings are in terms of a swing to candidate 0 as per latest results
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);

	auto boothResults = sumMatched2cpBoothVotes(seat, priorMargin);

	// Now we have also tallied the estimated votes from booths that are uncounted but matched, if any

	// Need to calculate the remaining pool of uncounted and unmatched booths
	float enrolmentChange = 1.0f;
	int estimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	if (seat.previousResults) {
		enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
		estimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
	}

	estimateMysteryBoothVotes(seat, boothResults, estimatedTotalOrdinaryVotes);

	int estimatedTotalVotes = estimateDeclarationVotes(seat, boothResults, estimatedTotalOrdinaryVotes, enrolmentChange);

	float totalTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
	float firstMargin = (float(boothResults.tcpTally[0]) - totalTally * 0.5f) / totalTally * 100.0f;
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

SimulationIteration::SeatResult SimulationIteration::calculateLiveManualResultMatched2cp(Seat const& seat, float priorMargin)
{
	float liveMargin = seat.outcome->incumbentSwing + seat.margin;
	float liveStdDev = stdDevSingleSeat(seat.outcome->getPercentCountedEstimate());
	liveMargin += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	float priorWeight = 0.5f;
	float liveWeight = 6.0f / (liveStdDev * liveStdDev);
	float newMargin = (priorMargin * priorWeight + liveMargin * liveWeight) / (priorWeight + liveWeight);
	Party::Id winner = (newMargin >= 0.0f ? seat.incumbent : seat.challenger);
	Party::Id runnerUp = (newMargin >= 0.0f ? seat.challenger : seat.incumbent);
	float significance = std::clamp(float(seat.outcome->percentCounted) * 0.2f, 0.0f, 1.0f);
	return { winner, runnerUp, abs(newMargin), significance };
}

float SimulationIteration::calculateSeatRemainingSwing(Seat const& seat, float priorMargin)
{
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool incumbentFirst = firstParty == seat.incumbent;

	float liveSwing = (incumbentFirst ? 1.0f : -1.0f) * seat.outcome->incumbentSwing;
	float liveStdDev = stdDevSingleSeat(seat.outcome->getPercentCountedEstimate());
	liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	float priorWeight = 0.5f;
	float liveWeight = 6.0f / (liveStdDev * liveStdDev);
	float priorSwing = (incumbentFirst ? 1.0f : -1.0f) * (priorMargin - seat.margin);
	float remainingVoteSwing = (priorSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	return remainingVoteSwing;
}

SimulationIteration::BoothAccumulation SimulationIteration::sumMatched2cpBoothVotes(Seat const& seat, float priorMargin)
{
	BoothAccumulation boothAccumulation;

	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);

	// At this point we have tallied all the counted votes from booths (matched or otherwise)
	float remainingVoteSwing = calculateSeatRemainingSwing(seat, priorMargin);

	boothAccumulation.tcpTally = seat.tcpTally;

	// To estimate the vote count for individual booths we need to adjust the previous election's total votes
	// according to how the already-counted individual booth growth as occurred
	// There may not be any old comparison votes in which case we assume no growth
	for (auto boothId : seat.latestResults->booths) {
		Results::Booth const& booth = project.results().getBooth(boothId);
		if (booth.hasOldResults() && !booth.hasNewResults()) {
			int estimatedTotalVotes = int(std::round(float(booth.totalOldTcpVotes()) * seat.individualBoothGrowth));
			Party::Id boothFirstParty = project.results().getPartyByCandidate(booth.tcpCandidateId[0]);
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
			boothAccumulation.tcpTally[0] += newVotes0;
			boothAccumulation.tcpTally[1] += newVotes1;
		}
		if (!booth.hasOldResults() && !booth.hasNewResults()) {
			using Mystery = BoothAccumulation::MysteryBoothType;
			if (booth.isPPVC()) ++boothAccumulation.mysteryBoothCounts[Mystery::Ppvc];
			else if (booth.name.find(" Team") != std::string::npos) ++boothAccumulation.mysteryBoothCounts[Mystery::Team];
			else ++boothAccumulation.mysteryBoothCounts[Mystery::Standard];
		}
	}

	return boothAccumulation;
}

void SimulationIteration::estimateMysteryBoothVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes)
{
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);
	float firstTallyPercent = float(boothResults.tcpTally[0]) / float(boothResults.tcpTally[0] + boothResults.tcpTally[1]) * 100.0f;

	using Mystery = BoothAccumulation::MysteryBoothType;
	if (boothResults.hasMysteryBooths()) {
		int estimatedRemainingOrdinaryVotes = std::max(0, estimatedTotalOrdinaryVotes - boothResults.tcpTally[0] - boothResults.tcpTally[1]);
		// sanity check to make sure we aren't assigning 5000 votes to a special hospital team or something
		int plausibleMaximumRemainingOrdinaryVotes = boothResults.mysteryBoothCounts[int(Mystery::Ppvc)] * 10000 +
			boothResults.mysteryBoothCounts[int(Mystery::Standard)] * 2000 + boothResults.mysteryBoothCounts[int(Mystery::Team)] * 200;
		float proportionPPVC = float(boothResults.mysteryBoothCounts[int(Mystery::Ppvc)] * 10000) / float(plausibleMaximumRemainingOrdinaryVotes);
		estimatedRemainingOrdinaryVotes = std::min(estimatedRemainingOrdinaryVotes, plausibleMaximumRemainingOrdinaryVotes);
		estimatedTotalOrdinaryVotes = estimatedRemainingOrdinaryVotes + boothResults.tcpTally[0] + boothResults.tcpTally[1];

		const float MysteryVoteStdDev = 6.0f;
		float incumbentMysteryPercent = std::normal_distribution<float>(firstTallyPercent, MysteryVoteStdDev)(gen);
		if (firstParty == 0 && secondParty == 1) incumbentMysteryPercent += ppvcBias * proportionPPVC;
		if (secondParty == 1 && firstParty == 0) incumbentMysteryPercent -= ppvcBias * proportionPPVC;
		int incumbentMysteryVotes = int(std::round(incumbentMysteryPercent * 0.01f * float(estimatedRemainingOrdinaryVotes)));
		int challengerMysteryVotes = estimatedRemainingOrdinaryVotes - incumbentMysteryVotes;
		boothResults.tcpTally[0] += incumbentMysteryVotes;
		boothResults.tcpTally[1] += challengerMysteryVotes;
	}
}

int SimulationIteration::estimateDeclarationVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange)
{
	if (seat.previousResults) {
		return estimateDeclarationVotesUsingPreviousResults(seat, boothResults, estimatedTotalOrdinaryVotes, enrolmentChange);
	}
	else {
		return estimateDeclarationVotesWithoutPreviousResults(seat, boothResults, estimatedTotalOrdinaryVotes);
	}
}

int SimulationIteration::estimateDeclarationVotesUsingPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange)
{
	int estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
	float declarationVoteChange = estimateDeclarationVoteProportionalChange(seat, estimatedTotalVotes, estimatedTotalOrdinaryVotes);
	float ordinaryVoteSwing = calculateOrdinaryVoteSwing(seat, boothResults);

	auto[firstAbsentVotes, secondAbsentVotes] = estimateAbsentVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	auto[firstProvisionalVotes, secondProvisionalVotes] = estimateProvisionalVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	auto[firstPrepollVotes, secondPrepollVotes] = estimatePrepollVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	auto[firstPostalVotes, secondPostalVotes] = estimatePostalVotes(seat, ordinaryVoteSwing, declarationVoteChange);

	boothResults.tcpTally[0] += firstAbsentVotes + firstProvisionalVotes + firstPrepollVotes + firstPostalVotes;
	boothResults.tcpTally[1] += secondAbsentVotes + secondProvisionalVotes + secondPrepollVotes + secondPostalVotes;
	return estimatedTotalVotes;
}

int SimulationIteration::estimateDeclarationVotesWithoutPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes)
{
	int estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	if (seat.latestResults->ordinaryVotes() > estimatedTotalOrdinaryVotes) {
		float totalOrdinaryTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
		float estimatedNonOrdinaryVotePotential = float(seat.latestResults->enrolment - estimatedTotalOrdinaryVotes);
		float estimatedProportionRemainingFormal = float(estimatedTotalVotes) / estimatedNonOrdinaryVotePotential;
		constexpr float remainingFormalStdDev = 0.05f;
		estimatedProportionRemainingFormal = std::clamp(estimatedProportionRemainingFormal + std::normal_distribution<float>(0.0f, remainingFormalStdDev)(gen), 0.0f, 1.0f);
		float actualNonOrdinaryVotePotential = float(seat.latestResults->enrolment - seat.latestResults->ordinaryVotes());
		int estimatedTotalDeclarationVotes = int(actualNonOrdinaryVotePotential * estimatedProportionRemainingFormal);

		float firstPercent = float(boothResults.tcpTally[0]) / totalOrdinaryTally;
		constexpr float declarationStdDev = 0.05f;
		float declarationVoteFirstProportion = std::clamp(firstPercent + std::normal_distribution<float>(0.0f, declarationStdDev)(gen), 0.0f, 1.0f);
		int firstTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * declarationVoteFirstProportion);
		int secondTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * (1.0f - declarationVoteFirstProportion));
		boothResults.tcpTally[0] += firstTcpDeclarationVotes;
		boothResults.tcpTally[1] += secondTcpDeclarationVotes;
	}
	return estimatedTotalVotes;
}

float SimulationIteration::estimateDeclarationVoteProportionalChange(Seat const& seat, int estimatedTotalVotes, int estimatedTotalOrdinaryVotes)
{
	int estimatedDeclarationVotes = estimatedTotalVotes - estimatedTotalOrdinaryVotes;
	int oldDeclarationVotes = seat.previousResults->total2cpVotes() - seat.previousResults->ordinaryVotes();
	return float(estimatedDeclarationVotes) / float(oldDeclarationVotes);
}

float SimulationIteration::calculateOrdinaryVoteSwing(Seat const & seat, BoothAccumulation const & boothResults)
{
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	float totalOrdinaryTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
	float firstNewOrdinaryPercent = float(boothResults.tcpTally[0]) / totalOrdinaryTally * 100.0f;
	float firstOldOrdinaryPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].ordinaryVotes) / float(seat.previousResults->ordinaryVotes()) * 100.0f;
	return firstNewOrdinaryPercent - firstOldOrdinaryPercent;
}

SimulationIteration::TcpTally SimulationIteration::estimateAbsentVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	float absentStdDev = 5.0f; // needs more research
	float absentSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, absentStdDev)(gen);
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
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
	return { firstAbsentVotes, secondAbsentVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimateProvisionalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	float provisionalStdDev = 5.0f; // needs more research
	float provisionalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, provisionalStdDev)(gen);
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
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
	return { firstProvisionalVotes, secondProvisionalVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimatePrepollVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	float prepollStdDev = 5.0f; // needs more research
	float prepollSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, prepollStdDev)(gen);
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
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
	return { firstPrepollVotes, secondPrepollVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimatePostalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	float postalStdDev = 5.0f; // needs more research
	float postalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, postalStdDev)(gen);
	Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
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
	return { firstPostalVotes, secondPostalVotes };
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultNonClassic2CP(Seat const& seat)
{
	if (sim.isLiveAutomatic() && seatPartiesMatchBetweenElections(seat)) {
		if (!run.currentIteration) logger << seat.name << " - matched booths\n";
		return calculateResultMatched2cp(seat, seat.margin);
	}
	else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
		if (!run.currentIteration) logger << seat.name << " - 2cp votes\n";
		return calculateResultUnmatched2cp(seat);
	}
	else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->fpCandidates.size() && seat.latestResults->totalFpVotes()) {
		if (!run.currentIteration) logger << seat.name << " - first preferences\n";
		return calculateLiveResultFromFirstPreferences(seat);
	}
	else {
		return { seat.incumbent, seat.challenger, seat.margin, 0.0f };
	}
}

SimulationIteration::SeatResult SimulationIteration::calculateResultUnmatched2cp(Seat const& seat)
{
	TcpTally tcpTally = sumUnmatched2cpBoothVotes(seat);

	int estimatedTotalVotes = estimateTotalVotes(seat);

	modifyUnmatchedTallyForRemainingVotes(seat, tcpTally, estimatedTotalVotes);

	int totalVotes = tcpTally[0] + tcpTally[1];

	float margin = (float(tcpTally[0]) - float(totalVotes) * 0.5f) / float(totalVotes) * 100.0f;

	int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	Party::Id firstParty = project.results().getPartyByCandidate(firstCandidateId);
	Party::Id secondParty = project.results().getPartyByCandidate(secondCandidateId);

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

SimulationIteration::TcpTally SimulationIteration::sumUnmatched2cpBoothVotes(Seat const& seat)
{
	int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	TcpTally tcpTally = { 0, 0 };
	float preferenceFlowGuess = std::normal_distribution<float>(seat.firstPartyPreferenceFlow, seat.preferenceFlowVariation)(gen);
	for (auto boothId : seat.latestResults->booths) {
		Results::Booth const& booth = project.results().getBooth(boothId);
		bool matchingOrder = project.results().getPartyByCandidate(booth.tcpCandidateId[0]) == 
			project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);

		if (booth.hasNewResults()) {
			for (int i = 0; i <= 1; ++i) tcpTally[i] += (matchingOrder ? booth.newTcpVote[i] : booth.newTcpVote[1 - i]);
		}
		else if (booth.totalNewFpVotes()) {
			TcpTally tcpEstimate = { 0, 0 };
			for (auto const& candidate : booth.fpCandidates) {
				if (candidate.candidateId == firstCandidateId) {
					tcpEstimate[0] += candidate.fpVotes;
				}
				else if (candidate.candidateId != secondCandidateId) {
					tcpEstimate[0] += int(float(candidate.fpVotes) * preferenceFlowGuess);
				}
			}
			tcpEstimate[1] = booth.totalNewFpVotes() - tcpEstimate[0];
			for (int i = 0; i <= 1; ++i) tcpTally[i] += tcpEstimate[i];
		}
	}
	return tcpTally;
}

int SimulationIteration::estimateTotalVotes(Seat const& seat)
{
	// now estimate the remaining votes with considerable variance
	float enrolmentChange = determineEnrolmentChange(seat, nullptr);
	int estimatedTotalVotes = 0;
	if (seat.previousResults) {
		estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
	}
	else {
		estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	}
	return estimatedTotalVotes;
}

void SimulationIteration::modifyUnmatchedTallyForRemainingVotes(Seat const& seat, TcpTally& tcpTally, int estimatedTotalVotes)
{
	int totalBoothVotes = tcpTally[0] + tcpTally[1];
	int minTotalVotes = totalBoothVotes;
	int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	constexpr float totalVoteNumberDeviation = 0.05f;
	float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
	int clampedTotalVotes = std::clamp(int(randomizedTotalVotes), minTotalVotes, maxTotalVotes);
	int estimatedRemainingVotes = std::max(0, clampedTotalVotes - totalBoothVotes);

	float firstProportionCounted = float(tcpTally[0]) / float(totalBoothVotes);
	constexpr float firstProportionChangeDeviation = 0.08f;
	float firstProportionChange = std::normal_distribution<float>(0.0f, firstProportionChangeDeviation)(gen);
	float firstProportionRemaining = std::clamp(firstProportionChange + firstProportionCounted, 0.0f, 1.0f);
	int firstRemainingVotes = int(firstProportionRemaining * float(estimatedRemainingVotes));
	int secondRemainingVotes = estimatedRemainingVotes - firstRemainingVotes;
	tcpTally[0] += firstRemainingVotes;
	tcpTally[1] += secondRemainingVotes;
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultFromFirstPreferences(Seat const& seat)
{
	SeatCandidates candidates = collectSeatCandidates(seat);
	int estimatedTotalVotes = estimateTotalVotes(seat);
	projectSeatCandidatePrimaries(seat, candidates, estimatedTotalVotes);
	distributePreferences(candidates);
	return determineResultFromDistribution(seat, candidates, estimatedTotalVotes);
}

SimulationIteration::SeatCandidates SimulationIteration::collectSeatCandidates(Seat const& seat)
{
	SeatCandidates candidates;
	for (auto const& fpCandidate : seat.latestResults->fpCandidates) {
		candidates.push_back({ fpCandidate.totalVotes(), project.results().getPartyByCandidate(fpCandidate.candidateId) });
	}
	return candidates;
}

void SimulationIteration::projectSeatCandidatePrimaries(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes)
{
	// now estimate the remaining votes with considerable variance
	int countedVotes = std::accumulate(candidates.begin(), candidates.end(), 0,
		[](int i, SeatCandidate const& c) { return i + c.vote; });

	int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	int minTotalVotes = std::min(countedVotes, maxTotalVotes);
	constexpr float totalVoteNumberDeviation = 0.05f;
	float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
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
}

void SimulationIteration::distributePreferences(SeatCandidates& candidates)
{
	for (int sourceIndex = candidates.size() - 1; sourceIndex > 1; --sourceIndex) {
		SeatCandidate sourceCandidate = candidates[sourceIndex];
		if (sourceCandidate.partyId == Party::InvalidId) continue;
		Party const& sourceParty = project.parties().view(sourceCandidate.partyId);
		candidates[sourceIndex].vote = 0;
		std::vector<float> weights;
		weights.resize(candidates.size(), 0);
		for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
			SeatCandidate const& targetCandidate = candidates[targetIndex];
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
			[](SeatCandidate lhs, SeatCandidate rhs) {return lhs.vote > rhs.vote; });
	}
}

SimulationIteration::SeatResult SimulationIteration::determineResultFromDistribution(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes)
{
	float totalTally = float(candidates[0].vote + candidates[1].vote);
	float margin = (float(candidates[0].vote) - totalTally * 0.5f) / totalTally * 100.0f;
	Party::Id winner = candidates[0].partyId;
	Party::Id runnerUp = candidates[1].partyId;
	float significance = std::clamp(float(seat.latestResults->totalFpVotes()) / float(estimatedTotalVotes) * 5.0f, 0.0f, 1.0f);

	return { winner, runnerUp, margin, significance };
}

Party::Id SimulationIteration::simulateWinnerFromBettingOdds(Seat const& thisSeat)
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


bool SimulationIteration::seatPartiesMatchBetweenElections(Seat const& seat)
{
	if (!seat.latestResults) return false;
	if (!seat.previousResults) return false;
	Party::Id oldPartyOne = project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	Party::Id oldPartyTwo = project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[1].affiliationId);
	Party::Id newPartyOne = project.results().getPartyByAffiliation(seat.latestResults->finalCandidates[0].affiliationId);
	Party::Id newPartyTwo = project.results().getPartyByAffiliation(seat.latestResults->finalCandidates[1].affiliationId);
	if (oldPartyOne == newPartyOne && oldPartyTwo == newPartyTwo) return true;
	if (oldPartyOne == newPartyTwo && oldPartyTwo == newPartyOne) return true;
	return false;
}

float SimulationIteration::determineEnrolmentChange(Seat const& seat, int* estimatedTotalOrdinaryVotes)
{
	// Need to calculate the remaining pool of uncounted and unmatched booths
	float enrolmentChange = 1.0f;
	int tempEstimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	if (seat.previousResults) {
		enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
		tempEstimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
	}
	if (estimatedTotalOrdinaryVotes) *estimatedTotalOrdinaryVotes = tempEstimatedTotalOrdinaryVotes;
	return enrolmentChange;
}