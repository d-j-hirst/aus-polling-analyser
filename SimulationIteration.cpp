#include "SimulationIteration.h"

#include "CountProgress.h"
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
	initialiseIterationSpecificCounts();
	determineIterationOverallSwing();
	determineIterationPpvcBias();
	determineIterationOverallSwing();
	determineIterationRegionalSwings();

	for (auto&[key, seat] : project.seats()) {
		determineSeatResult(seat);
		recordSeatResult(seat);
	}

	assignCountAsPartyWins();
	assignSupportsPartyWins();
	classifyMajorityResult();
	addPartySeatWinCounts();
}

void SimulationIteration::initialiseIterationSpecificCounts()
{
	// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
	run.regionSeatCount = std::vector<std::vector<int>>(project.parties().count(), std::vector<int>(project.regions().count()));

	run.partyWins = std::vector<int>(project.parties().count());

	// First, randomly determine the national swing for this particular simulation
	run.iterationOverallSwing = std::normal_distribution<float>(run.pollOverallSwing, run.pollOverallStdDev)(gen);
}

void SimulationIteration::determineIterationOverallSwing()
{
	if (sim.isLive() && run.liveOverallPercent) {
		float liveSwing = run.liveOverallSwing;
		float liveStdDev = stdDevOverall(run.liveOverallPercent);
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev) * run.sampleRepresentativeness;
		run.iterationOverallSwing = (run.iterationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	}

	// this will be used to determine the estimated 2pp swing (for live results) later
	sim.partyOneSwing += double(run.iterationOverallSwing);
}

void SimulationIteration::determineIterationPpvcBias()
{
	if (sim.isLive() && run.liveOverallPercent) {
		constexpr float ppvcBiasStdDev = 4.0f;
		float ppvcBiasRandom = std::normal_distribution<float>(0.0f, ppvcBiasStdDev)(gen);
		run.ppvcBias = run.ppvcBiasObserved * run.ppvcBiasConfidence + ppvcBiasRandom * (1.0f - run.ppvcBiasConfidence);
	}
}

void SimulationIteration::determineIterationRegionalSwings()
{
	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);
	// Add random variation to the state-by-state swings and calculate the implied national 2pp
	// May be some minor floating-point errors here but they won't matter in the scheme of things
	float tempOverallSwing = 0.0f;
	for (auto& regionPair : project.regions()) {
		Region& thisRegion = regionPair.second;
		// Calculate mean of the region's swing after accounting for decay to the mean over time.
		float regionMeanSwing = run.pollOverallSwing +
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
	tempOverallSwing /= run.totalPopulation;

	correctRegionalSwings(tempOverallSwing);
}

void SimulationIteration::correctRegionalSwings(float tempOverallSwing)
{
	// Adjust regional swings to keep the implied overall 2pp the same as that actually projected
	float regionSwingAdjustment = run.iterationOverallSwing - tempOverallSwing;
	for (auto& regionPair : project.regions()) {
		Region& thisRegion = regionPair.second;
		thisRegion.simulationSwing += regionSwingAdjustment;
	}
}

void SimulationIteration::determineSeatResult(Seat & seat)
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

void SimulationIteration::determineClassicSeatResult(Seat & seat)
{
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
	seat.winner = result.winner;
	adjustClassicSeatResultForBettingOdds(seat, result);
}

void SimulationIteration::adjustClassicSeatResultForBettingOdds(Seat & seat, SeatResult result)
{
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

void SimulationIteration::determineNonClassicSeatResult(Seat & seat)
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

void SimulationIteration::recordSeatResult(Seat & seat)
{
	// If the winner is the incumbent, record this down in the seat's numbers
	seat.incumbentWins += (seat.winner == seat.incumbent ? 1 : 0);

	if (seat.winner == 0) ++seat.partyOneWinRate;
	else if (seat.winner == 1) ++seat.partyTwoWinRate;
	else ++seat.partyOthersWinRate;

	int winnerIndex = project.parties().idToIndex(seat.winner);
	if (winnerIndex != PartyCollection::InvalidIndex) {
		run.partyWins[winnerIndex]++;
		int regionIndex = project.regions().idToIndex(seat.region);
		++run.regionSeatCount[winnerIndex][regionIndex];
	}
}

void SimulationIteration::assignCountAsPartyWins()
{
	for (int partyNum = 2; partyNum < int(run.partyWins.size()); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyOne) {
			run.partyWins[0] += run.partyWins[partyNum];
		}
		else if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyTwo) {
			run.partyWins[1] += run.partyWins[partyNum];
		}
	}
}

void SimulationIteration::assignSupportsPartyWins()
{
	run.partySupport = { run.partyWins[0], run.partyWins[1] };
	for (int partyNum = 2; partyNum < int(run.partyWins.size()); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.supportsParty == Party::SupportsParty::One) {
			run.partySupport[0] += run.partyWins[partyNum];
		}
		else if (thisParty.supportsParty == Party::SupportsParty::Two) {
			run.partySupport[1] += run.partyWins[partyNum];
		}
	}
}

void SimulationIteration::classifyMajorityResult()
{
	int minimumForMajority = project.seats().count() / 2 + 1;

	// Look at the overall result and classify it
	if (run.partyWins[0] >= minimumForMajority) ++run.partyMajority[Simulation::MajorParty::One];
	else if (run.partySupport[0] >= minimumForMajority) ++run.partyMinority[Mp::One];
	else if (run.partyWins[1] >= minimumForMajority) ++run.partyMajority[Mp::Two];
	else if (run.partySupport[1] >= minimumForMajority) ++run.partyMinority[Mp::Two];
	else ++run.hungParliament;
}

void SimulationIteration::addPartySeatWinCounts()
{
	int othersWins = 0;
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		++sim.partySeatWinFrequency[partyIndex][run.partyWins[partyIndex]];
		if (partyIndex > 1) othersWins += run.partyWins[partyIndex];
		for (auto& regionPair : project.regions()) {
			Region& thisRegion = regionPair.second;
			++thisRegion.partyWins[partyIndex][run.regionSeatCount[partyIndex][project.regions().idToIndex(regionPair.first)]];
		}
	}
	++sim.othersWinFrequency[othersWins];
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

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultClassic2CP(Seat const& seat, float priorMargin)
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
					if (firstParty == 0 && secondParty == 1) boothSwing += run.ppvcBias;
					if (secondParty == 0 && firstParty == 1) boothSwing -= run.ppvcBias;
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
		int estimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
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
			if (firstParty == 0 && secondParty == 1) incumbentMysteryPercent += run.ppvcBias * proportionPPVC;
			if (secondParty == 1 && firstParty == 0) incumbentMysteryPercent -= run.ppvcBias * proportionPPVC;
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
			estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
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

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultNonClassic2CP(Seat const& seat)
{
	if (sim.isLiveAutomatic() && seatPartiesMatchBetweenElections(seat)) {
		if (!run.currentIteration) logger << seat.name << " - matched booths\n";
		return calculateLiveResultClassic2CP(seat, seat.margin);
	}
	else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
		if (!run.currentIteration) logger << seat.name << " - 2cp votes\n";
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
			estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
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
		if (!run.currentIteration) logger << seat.name << " - first preferences\n";
		return calculateLiveResultFromFirstPreferences(seat);
	}
	else {
		return { seat.incumbent, seat.challenger, seat.margin, 0.0f };
	}
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultFromFirstPreferences(Seat const & seat)
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
		estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	}
	int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	int minTotalVotes = std::min(countedVotes, maxTotalVotes);
	constexpr float totalVoteNumberDeviation = 0.05f;
	float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
	if (seat.name == "Bean" && !run.currentIteration) {
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
	Party::Id oldPartyOne = project.getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	Party::Id oldPartyTwo = project.getPartyByAffiliation(seat.previousResults->finalCandidates[1].affiliationId);
	Party::Id newPartyOne = project.getPartyByAffiliation(seat.latestResults->finalCandidates[0].affiliationId);
	Party::Id newPartyTwo = project.getPartyByAffiliation(seat.latestResults->finalCandidates[1].affiliationId);
	if (oldPartyOne == newPartyOne && oldPartyTwo == newPartyTwo) return true;
	if (oldPartyOne == newPartyTwo && oldPartyTwo == newPartyOne) return true;
	return false;
}

float SimulationIteration::determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes)
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