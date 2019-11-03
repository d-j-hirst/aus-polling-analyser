#include "SimulationPreparation.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"

#include <random>

static std::random_device rd;
static std::mt19937 gen;

SimulationPreparation::SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void SimulationPreparation::prepareForIterations()
{
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
	run.project.updateLatestResultsForSeats();

	determinePreviousVoteEnrolmentRatios();

	resizeRegionSeatCountOutputs();

	countInitialRegionSeatLeads();

	calculateTotalPopulation();

	calculateLiveAggregates();

	resetResultCounts();

	loadInitialStateFromProjection();
}

void SimulationPreparation::resetRegionSpecificOutput()
{
	for (auto&[key, thisRegion] : project.regions()) {
		thisRegion.localModifierAverage = 0.0f;
		thisRegion.seatCount = 0;
		thisRegion.liveSwing = 0.0f;
		thisRegion.livePercentCounted = 0.0f;
		thisRegion.classicSeatCount = 0;
	}
}

void SimulationPreparation::resetSeatSpecificOutput()
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

void SimulationPreparation::accumulateRegionStaticInfo()
{
	for (auto&[key, seat] : project.seats()) {
		bool isPartyOne = (seat.incumbent == 0);
		Region& thisRegion = project.regions().access(seat.region);
		thisRegion.localModifierAverage += seat.localModifier * (isPartyOne ? 1.0f : -1.0f);
		++thisRegion.seatCount;
	}
	// Need the region's seat count to be complete before calculating these
	for (auto&[key, region] : project.regions()) {
		region.localModifierAverage /= float(region.seatCount);
	}
}

void SimulationPreparation::resetPpvcBiasAggregates()
{
	run.ppvcBiasNumerator = 0.0f;
	run.ppvcBiasDenominator = 0.0f;
	run.totalOldPpvcVotes = 0;
}

void SimulationPreparation::cacheBoothData()
{
	if (!sim.isLiveAutomatic()) return;
	for (auto&[key, seat] : project.seats()) {
		determineSeatCachedBoothData(seat);
	}
}

void SimulationPreparation::determinePpvcBias()
{
	if (!run.ppvcBiasDenominator) {
		// whether or not this is a live simulation, if there hasn't been any PPVC votes recorded
		// then we can set these to zero and it will be assumed there is no PPVC bias
		// (with the usual random variation  per simulation)
		run.ppvcBias = 0.0f;
		run.ppvcBiasConfidence = 0.0f;
		return;
	}
	run.ppvcBias = run.ppvcBiasNumerator / run.ppvcBiasDenominator;
	run.ppvcBiasConfidence = std::clamp(run.ppvcBiasDenominator / float(run.totalOldPpvcVotes) * 5.0f, 0.0f, 1.0f);

	logger << run.ppvcBiasNumerator << " " << run.ppvcBiasDenominator << " " << run.ppvcBias << " " << run.totalOldPpvcVotes <<
		" " << run.ppvcBiasConfidence << " - ppvc bias measures\n";
}

void SimulationPreparation::determinePreviousVoteEnrolmentRatios()
{
	if (!sim.isLiveAutomatic()) return;

	// Calculating ordinary and declaration vote totals as a proportion of total enrolment
	// Will be used to estimate turnout in seats without a previous result to extrapolate from
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
	run.previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	run.previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

void SimulationPreparation::resizeRegionSeatCountOutputs()
{
	for (auto&[key, region] : project.regions()) {
		region.partyLeading.clear();
		region.partyWins.clear();
		region.partyLeading.resize(project.parties().count());
		region.partyWins.resize(project.parties().count(), std::vector<int>(region.seatCount + 1));
	}
}

void SimulationPreparation::countInitialRegionSeatLeads()
{
	for (auto&[key, seat] : project.seats()) {
		Region& thisRegion = project.regions().access(seat.region);
		++thisRegion.partyLeading[project.parties().idToIndex(seat.getLeadingParty())];
	}
}

void SimulationPreparation::calculateTotalPopulation()
{
	// Total population is needed for adjusting regional swings after
	// random variation is applied via simulation
	run.totalPopulation = 0.0;
	for (auto&[key, region] : project.regions()) {
		run.totalPopulation += float(region.population);
	}
}

void SimulationPreparation::calculateLiveAggregates()
{
	run.liveOverallSwing = 0.0f;
	run.liveOverallPercent = 0.0f;
	run.classicSeatCount = 0.0f;
	run.sampleRepresentativeness = 0.0f;
	run.total2cpVotes = 0;
	run.totalEnrolment = 0;
	if (sim.isLive()) {
		for (auto&[key, seat] : project.seats()) {
			updateLiveAggregateForSeat(seat);
		}
		if (run.liveOverallPercent) {
			finaliseLiveAggregates();
		}
	}

	sim.total2cpPercentCounted = (float(run.totalEnrolment) ? float(run.total2cpVotes) / float(run.totalEnrolment) : 0.0f);
}

void SimulationPreparation::updateLiveAggregateForSeat(Seat & seat)
{
	if (!seat.isClassic2pp(sim.isLiveAutomatic())) return;
	++run.classicSeatCount;
	Region& thisRegion = project.regions().access(seat.region);
	++thisRegion.classicSeatCount;
	if (!seat.latestResult) return;
	bool incIsOne = seat.incumbent == 0;
	float percentCounted = seat.latestResult->getPercentCountedEstimate();
	float weightedSwing = seat.latestResult->incumbentSwing * (incIsOne ? 1.0f : -1.0f) * percentCounted;
	run.liveOverallSwing += weightedSwing;
	thisRegion.liveSwing += weightedSwing;
	run.liveOverallPercent += percentCounted;
	thisRegion.livePercentCounted += percentCounted;
	run.sampleRepresentativeness += std::min(2.0f, percentCounted) * 0.5f;
	run.total2cpVotes += seat.latestResults->total2cpVotes();
	run.totalEnrolment += seat.latestResults->enrolment;
}

void SimulationPreparation::finaliseLiveAggregates()
{
	run.liveOverallSwing /= run.liveOverallPercent;
	run.liveOverallPercent /= run.classicSeatCount;
	run.sampleRepresentativeness /= run.classicSeatCount;
	run.sampleRepresentativeness = std::sqrt(run.sampleRepresentativeness);
	for (auto& regionPair : project.regions()) {
		Region& thisRegion = regionPair.second;
		if (!thisRegion.livePercentCounted) continue;
		thisRegion.liveSwing /= thisRegion.livePercentCounted;
		thisRegion.livePercentCounted /= thisRegion.classicSeatCount;
	}
}

void SimulationPreparation::resetResultCounts()
{
	run.partyMajority = { 0, 0 };
	run.partyMinority = { 0, 0 };
	run.hungParliament = 0;
	sim.partySeatWinFrequency.clear();
	sim.partySeatWinFrequency.resize(project.parties().count(), std::vector<int>(project.seats().count() + 1));
	sim.othersWinFrequency.clear();
	sim.othersWinFrequency.resize(project.seats().count() + 1);
	sim.partyOneSwing = 0.0;
}

void SimulationPreparation::loadInitialStateFromProjection()
{
	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);
	int projectionSize = thisProjection.getProjectionLength();
	run.pollOverallSwing = thisProjection.getMeanProjection(projectionSize - 1) - sim.settings.prevElection2pp;
	run.pollOverallStdDev = thisProjection.getSdProjection(projectionSize - 1);
}


void SimulationPreparation::determineSeatCachedBoothData(Seat& seat)
{
	if (!seat.latestResults.has_value()) return;

	auto seatPartyPreferences = aggregateVoteData(seat);

	calculatePreferenceFlows(seat, seatPartyPreferences);

	accumulatePpvcBiasMeasures(seat);
}

std::pair<int, int> SimulationPreparation::aggregateVoteData(Seat& seat)
{
	if (!seat.latestResults) return { 0, 0 };
	int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	if (!firstCandidateId || !secondCandidateId) return { 0, 0 }; // maverick results mean we shouldn't try to estimate 2cp swings
	Party::Id firstSeatParty = project.getPartyByCandidate(firstCandidateId);
	seat.tcpTally[0] = 0;
	seat.tcpTally[1] = 0;
	int newComparisonVotes = 0;
	int oldComparisonVotes = 0;
	int seatFirstPartyPreferences = 0;
	int seatSecondPartyPreferences = 0;

	for (auto boothId : seat.latestResults->booths) {
		Results::Booth const& booth = project.getBooth(boothId);
		Party::Id firstBoothParty = project.getPartyByCandidate(booth.tcpCandidateId[0]);
		bool isInSeatOrder = firstBoothParty == firstSeatParty;
		if (booth.hasNewResults()) {
			seat.tcpTally[0] += float(isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
			seat.tcpTally[1] += float(isInSeatOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
		}
		if (booth.hasOldAndNewResults()) {
			oldComparisonVotes += booth.totalOldTcpVotes();
			newComparisonVotes += booth.totalNewTcpVotes();
		}
		if (booth.hasValidPreferenceData()) {
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

	seat.individualBoothGrowth = (oldComparisonVotes ? float(newComparisonVotes) / float(oldComparisonVotes) : 1);

	return std::make_pair(seatFirstPartyPreferences, seatSecondPartyPreferences);
}

void SimulationPreparation::calculatePreferenceFlows(Seat & seat, SeatPartyPreferences seatPartyPreferences)
{
	if (!seat.latestResults.has_value()) return;
	int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	Party::Id firstSeatParty = project.getPartyByCandidate(firstCandidateId);
	Party::Id secondSeatParty = project.getPartyByCandidate(secondCandidateId);
	if (seatPartyPreferences.first + seatPartyPreferences.second) {
		float totalPreferences = float(seatPartyPreferences.first + seatPartyPreferences.second);
		seat.firstPartyPreferenceFlow = float(seatPartyPreferences.first) / totalPreferences;
		seat.preferenceFlowVariation = std::clamp(0.1f - totalPreferences / float(seat.latestResults->enrolment), 0.03f, 0.1f);

		if (firstSeatParty != Party::InvalidId && secondSeatParty != Party::InvalidId) {
			logger << seatPartyPreferences.first << " " << seatPartyPreferences.second << " " <<
				seat.firstPartyPreferenceFlow << " " << seat.preferenceFlowVariation << " preference flow to " <<
				project.parties().view(firstSeatParty).name << " vs " << project.parties().view(secondSeatParty).name << " - " << seat.name << "\n";
		}
	}
}

void SimulationPreparation::accumulatePpvcBiasMeasures(Seat& seat) {
	if (!seat.latestResults.has_value()) return;

	float nonPpvcSwingNumerator = 0.0f;
	float nonPpvcSwingDenominator = 0.0f; // total number of votes in counted non-PPVC booths
	float ppvcSwingNumerator = 0.0f;
	float ppvcSwingDenominator = 0.0f; // total number of votes in counted PPVC booths

	for (auto boothId : seat.latestResults->booths) {
		Results::Booth const& booth = project.getBooth(boothId);
		Party::Id firstBoothParty = project.getPartyByCandidate(booth.tcpCandidateId[0]);
		Party::Id secondBoothParty = project.getPartyByCandidate(booth.tcpCandidateId[1]);
		bool isPpvc = booth.isPPVC();
		if (booth.hasOldResults()) {
			if (isPpvc) run.totalOldPpvcVotes += booth.totalOldTcpVotes();
		}
		if (booth.hasOldAndNewResults()) {
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
	}

	if (nonPpvcSwingDenominator && ppvcSwingDenominator) {
		float nonPpvcSwing = nonPpvcSwingNumerator / nonPpvcSwingDenominator;
		float ppvcSwing = ppvcSwingNumerator / ppvcSwingDenominator;
		float ppvcSwingDiff = ppvcSwing - nonPpvcSwing;
		float weightedSwing = ppvcSwingDiff * ppvcSwingDenominator;
		run.ppvcBiasNumerator += weightedSwing;
		run.ppvcBiasDenominator += ppvcSwingDenominator;
	}
}