#include "SimulationPreparation.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

#include <random>

// Note: A large amount of code in this file is commented out as the "previous results"
// was updated to a new (better) format but the "latest results" was not. Further architectural
// improvement, including removing cached election results from project seat data, cannot be
// properly done unless this is fixed, and the fixing is decidedly non-trivial. In order to
// expedite the initial web release, which does not require live election updating, these have
// been disabled and code producing errors commented out and replaced with stubs,
// until the project is prepared to work on restoring the live results.

static std::random_device rd;
static std::mt19937 gen;

SimulationPreparation::SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void SimulationPreparation::prepareForIterations()
{
	gen.seed(rd());

	resetLatestReport();

	resetRegionSpecificOutput();

	resetSeatSpecificOutput();

	loadPreviousElectionBaselineVotes();

	loadSeatTypes();

	loadGreensSeatStatistics();
	loadIndSeatStatistics();
	loadIndEmergence();
	loadPopulistSeatStatistics();
	loadPopulistSeatModifiers();
	loadCentristSeatStatistics();
	loadCentristSeatModifiers();
	loadOthSeatStatistics();

	loadPastSeatResults();

	determineEffectiveSeatModifiers();

	accumulateRegionStaticInfo();

	resetPpvcBiasAggregates();

	// This will also accumulate the PPVC bias aggregates
	cacheBoothData();

	determinePpvcBias();

	loadSeatOutcomeRelations();

	determinePreviousVoteEnrolmentRatios();

	resizeRegionSeatCountOutputs();

	countInitialRegionSeatLeads();

	calculateTotalPopulation();

	calculateLiveAggregates();

	resetResultCounts();
}

void SimulationPreparation::resetLatestReport()
{
	sim.latestReport = Simulation::Report();
}

void SimulationPreparation::resetRegionSpecificOutput()
{
	run.regionLocalModifierAverage.resize(project.regions().count());
	regionSeatCount.resize(project.regions().count());
	run.regionPartyWins.resize(project.regions().count());
}

void SimulationPreparation::resetSeatSpecificOutput()
{
	run.seatPartyOneMarginSum.resize(project.seats().count(), 0.0);
	run.partyOneWinPercent.resize(project.seats().count(), 0.0);
	run.partyTwoWinPercent.resize(project.seats().count(), 0.0);
	run.othersWinPercent.resize(project.seats().count(), 0.0);

	run.seatFirstPartyPreferenceFlow.resize(project.seats().count(), 0.0f);
	run.seatPreferenceFlowVariation.resize(project.seats().count(), 0.0f);
	run.seatTcpTally.resize(project.seats().count(), { 0, 0 });
	run.seatIndividualBoothGrowth.resize(project.seats().count(), 0.0f);
	run.seatToOutcome.resize(project.seats().count(), nullptr);
	run.seatPartyWins.resize(project.seats().count());
	run.cumulativeSeatPartyFpShare.resize(project.seats().count());
	run.seatPartyFpDistribution.resize(project.seats().count());
}

void SimulationPreparation::determineEffectiveSeatModifiers()
{
	run.seatPartyOneTppModifier.resize(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		bool majorParty = (seat.incumbent <= 1);
		if (!majorParty) continue;
		float direction = (seat.incumbent ? -1.0f : 1.0f);
		if (seat.sophomoreCandidate) {
			run.seatPartyOneTppModifier[seatIndex] += 0.5f * direction;
			typedef SimulationRun::SeatType ST;
			auto type = run.seatTypes[seatIndex];
			if (type == ST::Provincial || type == ST::Rural) {
				run.seatPartyOneTppModifier[seatIndex] += 0.9f * direction;
			}
		}
		if (seat.sophomoreParty) run.seatPartyOneTppModifier[seatIndex] += 0.8f * direction;
		if (seat.retirement) run.seatPartyOneTppModifier[seatIndex] -= 1.0f * direction;
	}
}

void SimulationPreparation::accumulateRegionStaticInfo()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		run.regionLocalModifierAverage[seat.region] += run.seatPartyOneTppModifier[seatIndex];
		++regionSeatCount[seat.region];
	}
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		run.regionLocalModifierAverage[regionIndex] /= float(regionSeatCount[regionIndex]);
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
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatCachedBoothData(seatIndex);
	}
}

void SimulationPreparation::determinePpvcBias()
{
	if (!run.ppvcBiasDenominator) {
		// whether or not this is a live simulation, if there hasn't been any PPVC votes recorded
		// then we can set these to zero and it will be assumed there is no PPVC bias
		// (with the usual random variation  per simulation)
		run.ppvcBiasObserved = 0.0f;
		run.ppvcBiasConfidence = 0.0f;
		return;
	}
	run.ppvcBiasObserved = run.ppvcBiasNumerator / run.ppvcBiasDenominator;
	run.ppvcBiasConfidence = std::clamp(run.ppvcBiasDenominator / float(run.totalOldPpvcVotes) * 5.0f, 0.0f, 1.0f);

	//logger << run.ppvcBiasNumerator << " " << run.ppvcBiasDenominator << " " << run.ppvcBiasObserved << " " << run.totalOldPpvcVotes <<
	//	" " << run.ppvcBiasConfidence << " - ppvc bias measures\n";
}

void SimulationPreparation::loadSeatOutcomeRelations()
{
	for (auto const& outcome : project.outcomes()) {
		auto& seatOutcome = run.seatToOutcome[project.seats().idToIndex(outcome.seat)];
		if (!seatOutcome) seatOutcome = &outcome;
		else if (seatOutcome->updateTime < outcome.updateTime) seatOutcome = &outcome;
	}
}

void SimulationPreparation::determinePreviousVoteEnrolmentRatios()
{
	if (!sim.isLiveAutomatic()) return;

	// Calculating ordinary and declaration vote totals as a proportion of total enrolment
	// Will be used to estimate turnout in seats without a previous result to extrapolate from
	int ordinaryVoteNumerator = 0;
	int declarationVoteNumerator = 0;
	int voteDenominator = 0;
	//for (auto&[key, seat] : project.seats()) {
	//	if (seat.previousResults) {
	//		ordinaryVoteNumerator += seat.previousResults->ordinaryVotes();
	//		declarationVoteNumerator += seat.previousResults->declarationVotes();
	//		voteDenominator += seat.previousResults->enrolment;
	//	}
	//}
	if (!voteDenominator) return;
	run.previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	run.previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

void SimulationPreparation::resizeRegionSeatCountOutputs()
{
	sim.latestReport.regionPartyIncuments.resize(project.regions().count());
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		sim.latestReport.regionPartyIncuments[regionIndex].resize(project.parties().count());
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			run.regionPartyWins[regionIndex][partyIndex] = std::vector<int>(regionSeatCount[regionIndex] + 1);
		}
	}
}

void SimulationPreparation::countInitialRegionSeatLeads()
{
	for (auto&[key, seat] : project.seats()) {
		++sim.latestReport.regionPartyIncuments[seat.region][project.parties().idToIndex(seat.getLeadingParty())];
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
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			updateLiveAggregateForSeat(seatIndex);
		}
		if (run.liveOverallPercent) {
			finaliseLiveAggregates();
		}
	}

	if (sim.isLiveAutomatic()) {
		sim.latestReport.total2cpPercentCounted = (float(run.totalEnrolment) ? float(run.total2cpVotes) / float(run.totalEnrolment) : 0.0f) * 100.0f;
	}
	else if (sim.isLiveManual()) {
		sim.latestReport.total2cpPercentCounted = run.liveOverallPercent;
	}
	else {
		sim.latestReport.total2cpPercentCounted = 0.0f;
	}
}

void SimulationPreparation::updateLiveAggregateForSeat(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (!seat.isClassic2pp()) return;
	++run.classicSeatCount;
	//Region& thisRegion = project.regions().access(seat.region);
	//++thisRegion.classicSeatCount;
	if (!run.seatToOutcome[seatIndex]) return;
	bool incIsOne = seat.incumbent == 0;
	float percentCounted = run.seatToOutcome[seatIndex]->getPercentCountedEstimate();
	float weightedSwing = run.seatToOutcome[seatIndex]->incumbentSwing * (incIsOne ? 1.0f : -1.0f) * percentCounted;
	run.liveOverallSwing += weightedSwing;
	//thisRegion.liveSwing += weightedSwing;
	run.liveOverallPercent += percentCounted;
	//thisRegion.livePercentCounted += percentCounted;
	run.sampleRepresentativeness += std::min(2.0f, percentCounted) * 0.5f;
	//run.total2cpVotes += seat.latestResults->total2cpVotes();
	//run.totalEnrolment += seat.latestResults->enrolment;
}

void SimulationPreparation::finaliseLiveAggregates()
{
	run.liveOverallSwing /= run.liveOverallPercent;
	run.liveOverallPercent /= run.classicSeatCount;
	run.sampleRepresentativeness /= run.classicSeatCount;
	run.sampleRepresentativeness = std::sqrt(run.sampleRepresentativeness);
	//for (auto& regionPair : project.regions()) {
	//	Region& thisRegion = regionPair.second;
	//	if (!thisRegion.livePercentCounted) continue;
	//	thisRegion.liveSwing /= thisRegion.livePercentCounted;
	//	thisRegion.livePercentCounted /= thisRegion.classicSeatCount;
	//}
}

void SimulationPreparation::resetResultCounts()
{
	run.partyMajority = { 0, 0 };
	run.partyMinority = { 0, 0 };
	run.hungParliament = 0;
	sim.latestReport.partySeatWinFrequency.clear();
	sim.latestReport.partySeatWinFrequency.resize(project.parties().count(), std::vector<int>(project.seats().count() + 1));
	sim.latestReport.othersWinFrequency.clear();
	sim.latestReport.othersWinFrequency.resize(project.seats().count() + 1);
	sim.latestReport.partyPrimaryFrequency.clear();
	sim.latestReport.partyPrimaryFrequency.resize(project.parties().count());
	sim.latestReport.tppFrequency.clear();
	sim.latestReport.partyOneSwing = 0.0;
}

void SimulationPreparation::determineSeatCachedBoothData(int seatIndex)
{
	seatIndex;
	//Seat const& seat = project.seats().viewByIndex(seatIndex);

	//if (!seat.latestResults.has_value()) return;

	//auto seatPartyPreferences = aggregateVoteData(seatIndex);

	//calculatePreferenceFlows(seatIndex, seatPartyPreferences);

	//accumulatePpvcBiasMeasures(seatIndex);
}

std::pair<int, int> SimulationPreparation::aggregateVoteData(int seatIndex)
{
	seatIndex;
	return { 0, 0 }; // temporary, remove when restoring this section
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//if (!seat.latestResults) return { 0, 0 };
	//int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	//int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	//if (!firstCandidateId || !secondCandidateId) return { 0, 0 }; // maverick results mean we shouldn't try to estimate 2cp swings
	//Party::Id firstSeatParty = project.results().getPartyByCandidate(firstCandidateId);
	//run.seatTcpTally[seatIndex][0] = 0;
	//run.seatTcpTally[seatIndex][1] = 0;
	//int newComparisonVotes = 0;
	//int oldComparisonVotes = 0;
	//int seatFirstPartyPreferences = 0;
	//int seatSecondPartyPreferences = 0;

	//for (auto boothId : seat.latestResults->booths) {
	//	Results::Booth const& booth = project.results().getBooth(boothId);
	//	Party::Id firstBoothParty = project.results().getPartyByCandidate(booth.tcpCandidateId[0]);
	//	bool isInSeatOrder = firstBoothParty == firstSeatParty;
	//	if (booth.hasNewResults()) {
	//		run.seatTcpTally[seatIndex][0] += float(isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1]);
	//		run.seatTcpTally[seatIndex][1] += float(isInSeatOrder ? booth.newTcpVote[1] : booth.newTcpVote[0]);
	//	}
	//	if (booth.hasOldAndNewResults()) {
	//		oldComparisonVotes += booth.totalOldTcpVotes();
	//		newComparisonVotes += booth.totalNewTcpVotes();
	//	}
	//	if (booth.hasValidPreferenceData()) {
	//		int totalDistributedVotes = 0;
	//		int firstPartyFpVotes = 0;
	//		int firstPartyTcp = isInSeatOrder ? booth.newTcpVote[0] : booth.newTcpVote[1];
	//		for (auto const& candidate : booth.fpCandidates) {
	//			// need to use candidate IDs here since sometimes there may be two candidates
	//			// standing for the same "party" (e.g. independents or Coalition)
	//			// and we only want to match the one(s) that's actually in the 2cp
	//			int candidateId = candidate.candidateId;
	//			if (candidateId != firstCandidateId && candidateId != secondCandidateId) {
	//				totalDistributedVotes += candidate.fpVotes;
	//			}
	//			else if (candidateId == firstCandidateId) {
	//				firstPartyFpVotes = candidate.fpVotes;
	//			}
	//		}
	//		int boothFirstPartyPreferences = firstPartyTcp - firstPartyFpVotes;
	//		int boothSecondPartyPreferences = totalDistributedVotes - boothFirstPartyPreferences;
	//		// We can't magically detect entry errors but if we're getting a negative preference total that's
	//		// a pretty good sign that the Tcp has been flipped (as in Warilla THROSBY PPVC in 2016)
	//		// and we shouldn't use the booth
	//		if (boothFirstPartyPreferences >= 0 && boothSecondPartyPreferences >= 0) {
	//			seatFirstPartyPreferences += boothFirstPartyPreferences;
	//			seatSecondPartyPreferences += boothSecondPartyPreferences;
	//		}
	//	}
	//}

//run.seatIndividualBoothGrowth[seatIndex] = (oldComparisonVotes ? float(newComparisonVotes) / float(oldComparisonVotes) : 1);

//return std::make_pair(seatFirstPartyPreferences, seatSecondPartyPreferences);
}

void SimulationPreparation::calculatePreferenceFlows(int seatIndex, SeatPartyPreferences seatPartyPreferences)
{
	seatIndex;
	seatPartyPreferences;
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//if (!seat.latestResults.has_value()) return;
	//int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	//int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	//Party::Id firstSeatParty = project.results().getPartyByCandidate(firstCandidateId);
	//Party::Id secondSeatParty = project.results().getPartyByCandidate(secondCandidateId);
	//if (seatPartyPreferences.first + seatPartyPreferences.second) {
	//	float totalPreferences = float(seatPartyPreferences.first + seatPartyPreferences.second);
	//	run.seatFirstPartyPreferenceFlow[seatIndex] = float(seatPartyPreferences.first) / totalPreferences;
	//	run.seatPreferenceFlowVariation[seatIndex] = std::clamp(0.1f - totalPreferences / float(seat.latestResults->enrolment), 0.03f, 0.1f);

	//	if (firstSeatParty != Party::InvalidId && secondSeatParty != Party::InvalidId) {
	//		logger << seatPartyPreferences.first << " " << seatPartyPreferences.second << " " <<
	//			run.seatFirstPartyPreferenceFlow[seatIndex] << " " << run.seatPreferenceFlowVariation[seatIndex] << " preference flow to " <<
	//			project.parties().view(firstSeatParty).name << " vs " << project.parties().view(secondSeatParty).name << " - " << seat.name << "\n";
	//	}
	//}
}

void SimulationPreparation::accumulatePpvcBiasMeasures(int seatIndex) {
	seatIndex;
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//if (!seat.latestResults.has_value()) return;

	//float nonPpvcSwingNumerator = 0.0f;
	//float nonPpvcSwingDenominator = 0.0f; // total number of votes in counted non-PPVC booths
	//float ppvcSwingNumerator = 0.0f;
	//float ppvcSwingDenominator = 0.0f; // total number of votes in counted PPVC booths

	//for (auto boothId : seat.latestResults->booths) {
	//	Results::Booth const& booth = project.results().getBooth(boothId);
	//	Party::Id firstBoothParty = project.results().getPartyByCandidate(booth.tcpCandidateId[0]);
	//	Party::Id secondBoothParty = project.results().getPartyByCandidate(booth.tcpCandidateId[1]);
	//	bool isPpvc = booth.isPPVC();
	//	if (booth.hasOldResults()) {
	//		if (isPpvc) run.totalOldPpvcVotes += booth.totalOldTcpVotes();
	//	}
	//	if (booth.hasOldAndNewResults()) {
	//		bool directMatch = firstBoothParty == 0 && secondBoothParty == 1;
	//		bool oppositeMatch = secondBoothParty == 0 && firstBoothParty == 1;
	//		if (!isPpvc) {
	//			if (directMatch) {
	//				nonPpvcSwingNumerator += booth.rawSwing() * booth.totalNewTcpVotes();
	//				nonPpvcSwingDenominator += booth.totalNewTcpVotes();
	//			}
	//			else if (oppositeMatch) {
	//				nonPpvcSwingNumerator -= booth.rawSwing() * booth.totalNewTcpVotes();
	//				nonPpvcSwingDenominator += booth.totalNewTcpVotes();
	//			}
	//		}
	//		else {
	//			if (directMatch) {
	//				ppvcSwingNumerator += booth.rawSwing() * booth.totalNewTcpVotes();
	//				ppvcSwingDenominator += booth.totalNewTcpVotes();
	//			}
	//			else if (oppositeMatch) {
	//				ppvcSwingNumerator -= booth.rawSwing() * booth.totalNewTcpVotes();
	//				ppvcSwingDenominator += booth.totalNewTcpVotes();
	//			}
	//		}
	//	}
	//}

	//if (nonPpvcSwingDenominator && ppvcSwingDenominator) {
	//	float nonPpvcSwing = nonPpvcSwingNumerator / nonPpvcSwingDenominator;
	//	float ppvcSwing = ppvcSwingNumerator / ppvcSwingDenominator;
	//	float ppvcSwingDiff = ppvcSwing - nonPpvcSwing;
	//	float weightedSwing = ppvcSwingDiff * ppvcSwingDenominator;
	//	run.ppvcBiasNumerator += weightedSwing;
	//	run.ppvcBiasDenominator += ppvcSwingDenominator;
	//}
}

const std::map<std::string, std::string> simplifiedStringToPartyCode = {
	{"Labor", "ALP"},
	{"Liberal", "LNP"},
	{"National", "LNP"},
	{"Greens", "GRN"},
	{"One Nation", "ONP"},
	{"United Australia", "UAP"},
	{"Independent", "IND"},
	{"Katter's Australian", "KAP"},
	{"Centre Alliance", "CA"},
	{"Democrats", "DEM"},
	{"Centre Alliance", "SFF"}
};

void SimulationPreparation::loadPastSeatResults()
{
	if (!sim.settings.prevTermCodes.size()) throw Exception("No previous term codes given!");
	run.pastSeatResults.resize(project.seats().count());
	std::string fileName = "python/elections/results_" + sim.settings.prevTermCodes[0] + ".csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	bool fpMode = false;
	int currentSeat = -1;
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[0] == "fp") {
			fpMode = true;
		}
		else if (values[0] == "tcp") {
			fpMode = false;
		}
		else if (values[0] == "Seat") {
			try {
				currentSeat = project.seats().idToIndex(project.seats().accessByName(values[1]).first);
			}
			catch (SeatDoesntExistException) {
				// Seat might have been abolished, so no need to give an error, log it in case it's wrong
				logger << "Could not find a match for seat " + values[1] + "\n";
				currentSeat = -1;
			}
		}
		else if (values.size() >= 4) {
			if (currentSeat < 0) continue;
			std::string partyStr = values[1];
			float voteCount = std::stof(values[2]);
			float votePercent = std::stof(values[3]);
			std::string shortCodeUsed;
			if (simplifiedStringToPartyCode.count(partyStr)) {
				shortCodeUsed = simplifiedStringToPartyCode.at(partyStr);
			}
			int partyId = project.parties().indexByShortCode(shortCodeUsed);
			if (fpMode) {
				run.pastSeatResults[currentSeat].fpVoteCount[partyId] += voteCount;
				run.pastSeatResults[currentSeat].fpVotePercent[partyId] += votePercent;
			}
			else {
				run.pastSeatResults[currentSeat].tcpVote[partyId] += votePercent;
			}
		}
	} while (true);
	for (auto& [key, seat] : project.seats()) {
		if (seat.useFpResults.size()) {
			int thisSeatIndex = project.seats().idToIndex(key);
			try {
				int otherSeatIndex = project.seats().idToIndex(project.seats().accessByName(seat.useFpResults).first);
				run.pastSeatResults[thisSeatIndex] = run.pastSeatResults[otherSeatIndex];
			}
			catch (SeatDoesntExistException) {
				// Seat might have been abolished, so no need to give an error, log it in case it's wrong
				logger << "Could not match fp results for seat " + project.seats().view(thisSeatIndex).name + 
					" - no seat found matching name " + seat.useFpResults + "\n";
				currentSeat = -1;
			}
		}
	}
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto& results = run.pastSeatResults[seatIndex];
		for (auto& [party, voteShare] : results.fpVotePercent) {
			// This is a crutch that depends on the greens having index 2,
			// probably best to eventually run this with a per-party flag
			// But at the moment that's low priority
			if (party > 2 || party < 0) {
				results.prevOthers += std::min(voteShare, run.indEmergence.fpThreshold);
			}
		}
		results.prevOthers = std::max(2.0f, results.prevOthers);
	}
}

void SimulationPreparation::loadSeatTypes()
{
	run.seatTypes.resize(project.seats().count());
	std::string fileName = "python/Data/seat-types.csv";
	std::string region = getRegionCode();
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == region) {
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				if (values[0] == project.seats().viewByIndex(seatIndex).name) {
					run.seatTypes[seatIndex] = SimulationRun::SeatType(std::stoi(values[2]));
				}
			}
		}
	} while (true);
}

void SimulationPreparation::loadGreensSeatStatistics()
{
	std::string fileName = "python/Seat Statistics/statistics_GRN.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	std::getline(file, line);
	auto scaleValues = splitString(line, ",");
	run.greensSeatStatistics.scaleLow = std::stof(scaleValues[0]);
	run.greensSeatStatistics.scaleStep = std::stof(scaleValues[1]) - run.greensSeatStatistics.scaleLow;
	run.greensSeatStatistics.scaleHigh = std::stof(scaleValues.back());
	for (int trendType = 0; trendType < int(SimulationRun::SeatStatistics::TrendType::Num); ++trendType) {
		std::getline(file, line);
		auto strings = splitString(line, ",");
		for (auto const& str : strings) {
			run.greensSeatStatistics.trend[trendType].push_back(std::stof(str));
		}
	}
}

void SimulationPreparation::loadIndSeatStatistics()
{
	std::string fileName = "python/Seat Statistics/statistics_IND.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	std::getline(file, line);
	auto scaleValues = splitString(line, ",");
	run.indSeatStatistics.scaleLow = std::stof(scaleValues[0]);
	run.indSeatStatistics.scaleStep = std::stof(scaleValues[1]) - run.indSeatStatistics.scaleLow;
	run.indSeatStatistics.scaleHigh = std::stof(scaleValues.back());
	for (int trendType = 0; trendType < int(SimulationRun::SeatStatistics::TrendType::Num); ++trendType) {
		std::getline(file, line);
		auto strings = splitString(line, ",");
		for (auto const& str : strings) {
			run.indSeatStatistics.trend[trendType].push_back(std::stof(str));
		}
	}
}

void SimulationPreparation::loadOthSeatStatistics()
{
	std::string fileName = "python/Seat Statistics/statistics_OTH.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	std::getline(file, line);
	auto scaleValues = splitString(line, ",");
	run.othSeatStatistics.scaleLow = std::stof(scaleValues[0]);
	run.othSeatStatistics.scaleStep = std::stof(scaleValues[1]) - run.othSeatStatistics.scaleLow;
	run.othSeatStatistics.scaleHigh = std::stof(scaleValues.back());
	for (int trendType = 0; trendType < int(SimulationRun::SeatStatistics::TrendType::Num); ++trendType) {
		std::getline(file, line);
		auto strings = splitString(line, ",");
		for (auto const& str : strings) {
			run.othSeatStatistics.trend[trendType].push_back(std::stof(str));
		}
	}
}

void SimulationPreparation::loadIndEmergence()
{
	std::string fileName = "python/Seat Statistics/statistics_emerging_IND.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	auto extractNum = [&]() {std::string line; std::getline(file, line); return std::stof(line); };
	run.indEmergence.fpThreshold = extractNum();
	run.indEmergence.baseRate = extractNum();
	run.indEmergence.fedRateMod = extractNum();
	run.indEmergence.ruralRateMod = extractNum();
	run.indEmergence.provincialRateMod = extractNum();
	run.indEmergence.outerMetroRateMod = extractNum();
	run.indEmergence.prevOthersRateMod = extractNum();
	run.indEmergence.voteRmse = extractNum();
	run.indEmergence.voteKurtosis = extractNum();
	run.indEmergence.fedVoteCoeff = extractNum();
	run.indEmergence.ruralVoteCoeff = extractNum();
	run.indEmergence.provincialVoteCoeff = extractNum();
	run.indEmergence.outerMetroVoteCoeff = extractNum();
	run.indEmergence.prevOthersVoteCoeff = extractNum();
	run.indEmergence.voteIntercept = extractNum();
}

void SimulationPreparation::loadPopulistSeatStatistics()
{
	std::string fileName = "python/Seat Statistics/statistics_populist.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	auto extractNum = [&]() {std::string line; std::getline(file, line); return std::stof(line); };
	run.populistStatistics.lowerRmse = extractNum();
	run.populistStatistics.upperRmse = extractNum();
	run.populistStatistics.lowerKurtosis = extractNum();
	run.populistStatistics.upperKurtosis = extractNum();
	run.populistStatistics.homeStateCoefficient = extractNum();
}

void SimulationPreparation::loadPopulistSeatModifiers()
{
	run.seatPopulistModifiers.resize(project.seats().count());
	std::string fileName = "python/Seat Statistics/modifiers_populist.csv";
	std::string region = getRegionCode();
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == region) {
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				if (values[0] == project.seats().viewByIndex(seatIndex).name) {
					run.seatPopulistModifiers[seatIndex] = std::stof(values[2]);
				}
			}
		}
	} while (true);
}

void SimulationPreparation::loadCentristSeatStatistics()
{
	std::string fileName = "python/Seat Statistics/statistics_centrist.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	auto extractNum = [&]() {std::string line; std::getline(file, line); return std::stof(line); };
	run.centristStatistics.lowerRmse = extractNum();
	run.centristStatistics.upperRmse = extractNum();
	run.centristStatistics.lowerKurtosis = extractNum();
	run.centristStatistics.upperKurtosis = extractNum();
	run.centristStatistics.homeStateCoefficient = extractNum();
}

void SimulationPreparation::loadCentristSeatModifiers()
{
	run.seatCentristModifiers.resize(project.seats().count());
	std::string fileName = "python/Seat Statistics/modifiers_centrist.csv";
	std::string region = getRegionCode();
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == region) {
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				if (values[0] == project.seats().viewByIndex(seatIndex).name) {
					run.seatCentristModifiers[seatIndex] = std::stof(values[2]);
				}
			}
		}
	} while (true);
}

void SimulationPreparation::loadPreviousElectionBaselineVotes()
{
	std::string fileName = "python/Data/prior-results.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string yearCode = getYearCode();
	std::string regionCode = getRegionCode();
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[0] == yearCode && values[1] == regionCode) {
			std::string partyCode = splitString(values[2], " ")[0];
			// exclusive others is what we want to store, overall others isn't used
			if (partyCode == "OTH") continue;
			int partyIndex = project.parties().indexByShortCode(partyCode);
			// ignore parties that were significant last election but not expected to be so for this election
			if (partyIndex == OthersIndex && partyCode != UnnamedOthersCode) continue;
			run.previousFpVoteShare[partyIndex] = std::stof(values[3]);
		}
	} while (true);
}

std::string SimulationPreparation::getYearCode()
{
	std::string termCode = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode();
	return termCode.substr(0, 4);
}

std::string SimulationPreparation::getRegionCode()
{
	std::string termCode = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode();
	return termCode.substr(4);
}
