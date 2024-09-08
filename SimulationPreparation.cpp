#include "SimulationPreparation.h"

#include "LinearRegression.h"
#include "LivePreparation.h"
#include "PollingProject.h"
#include "ResultsDownloader.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"
#include "LatestResultsDataRetriever.h"

#include <filesystem>
#include <queue>
#include <random>
#include <set>

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

	storeTermCode();
	determineSpecificPartyIndices();

	loadTppSwingFactors();

	loadPreviousPreferenceFlows();
	loadNcPreferenceFlows();

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

	loadIndividualSeatParameters();

	loadPastSeatResults();

	determineEffectiveSeatTppModifiers();
	determinePreviousSwingDeviations();

	accumulateRegionStaticInfo();

	loadSeatBettingOdds();
	loadSeatMinorViability();
	loadSeatPolls();
	loadSeatTppPolls();

	determinePreviousVoteEnrolmentRatios();

	resizeRegionSeatCountOutputs();

	countInitialRegionSeatLeads();

	loadRegionBaseBehaviours();
	loadRegionPollBehaviours();
	loadRegionMixBehaviours();
	loadOverallRegionMixParameters();
	loadRegionSwingDeviations();

	calculateTotalPopulation();
	calculateIndEmergenceModifier();

	prepareProminentMinors();
	prepareRunningParties();

	if (run.isLive()) initializeGeneralLiveData();
	if (run.isLiveManual()) loadLiveManualResults();
	if (run.isLiveAutomatic()) {
		auto livePreparation = LivePreparation(project, sim, run);
		livePreparation.prepareLiveAutomatic();
	}

	calculateLiveAggregates();

	resetResultCounts();
}

void SimulationPreparation::resetLatestReport()
{
	sim.latestReport = Simulation::Report();
}

void SimulationPreparation::resetRegionSpecificOutput()
{
	run.regionLocalModifierAverage.resize(project.regions().count(), 0.0f);
	regionSeatCount.resize(project.regions().count(), 0);
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
	run.seatPartyWins.resize(project.seats().count());
	run.cumulativeSeatPartyFpShare.resize(project.seats().count());
	run.seatPartyFpDistribution.resize(project.seats().count());
	run.seatPartyFpZeros.resize(project.seats().count());
	run.seatTcpDistribution.resize(project.seats().count());

	run.seatRegionSwingSums.resize(project.seats().count(), 0.0);
	run.seatElasticitySwingSums.resize(project.seats().count(), 0.0);
	run.seatLocalEffectsSums.resize(project.seats().count(), 0.0);
	run.seatPreviousSwingEffectSums.resize(project.seats().count(), 0.0);
	run.seatFederalSwingEffectSums.resize(project.seats().count(), 0.0);
	run.seatByElectionEffectSums.resize(project.seats().count(), 0.0);
	run.seatThirdPartyExhaustEffectSums.resize(project.seats().count(), 0.0);
	run.seatPollEffectSums.resize(project.seats().count(), 0.0);
	run.seatMrpPollEffectSums.resize(project.seats().count(), 0.0);
	run.seatLocalEffects.resize(project.seats().count());
}

void SimulationPreparation::storeTermCode()
{
	std::string termCode = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode();
	run.yearCode = termCode.substr(0, 4);
	run.regionCode = termCode.substr(4);
}

void SimulationPreparation::determineEffectiveSeatTppModifiers()
{
	run.seatPartyOneTppModifier.resize(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		bool majorParty = (seat.incumbent <= 1);
		if (!majorParty) continue;
		typedef SimulationRun::SeatType ST;
		auto type = run.seatTypes[seatIndex];
		bool isRegional = type == ST::Provincial || type == ST::Rural;
		float direction = (seat.incumbent ? -1.0f : 1.0f);
		if (isRegional) {
			if (seat.sophomoreCandidate) {
				float effectSize = run.tppSwingFactors.sophomoreCandidateRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Candidate sophomore effect", effectSize });
			}
			if (seat.sophomoreParty) {
				float effectSize = run.tppSwingFactors.sophomorePartyRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Party sophomore effect", effectSize });
			}
			if (seat.retirement) {
				float effectSize = run.tppSwingFactors.retirementRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Retirement effect", effectSize });
			}
		}
		else {
			if (seat.sophomoreCandidate) {
				float effectSize = run.tppSwingFactors.sophomoreCandidateUrban * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Candidate sophomore effect", effectSize });
			}
			if (seat.sophomoreParty) {
				float effectSize = run.tppSwingFactors.sophomorePartyUrban * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Party sophomore effect", effectSize });
			}
			if (seat.retirement) {
				float effectSize = run.tppSwingFactors.retirementRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Retirement effect", effectSize });
			}
		}
		constexpr float DisendorsementMod = 3.2f;
		constexpr float PreviousDisendorsementMod = DisendorsementMod * -1.0f;
		if (seat.disendorsement) {
			float effectSize = DisendorsementMod * direction;
			run.seatPartyOneTppModifier[seatIndex] += DisendorsementMod * direction;
			run.seatLocalEffects[seatIndex].push_back({ "Disendorsement", effectSize });
		}
		if (seat.previousDisendorsement) {
			float effectSize = PreviousDisendorsementMod * direction;
			run.seatPartyOneTppModifier[seatIndex] += PreviousDisendorsementMod * direction;
			run.seatLocalEffects[seatIndex].push_back({ "Recovery from previous disendorsement", effectSize });
		}
		run.seatPartyOneTppModifier[seatIndex] += seat.miscTppModifier;
		run.seatLocalEffects[seatIndex].push_back({ "Extraordinary circumstances (treat with extra caution)", seat.miscTppModifier });
	}
}

void SimulationPreparation::determinePreviousSwingDeviations()
{
	// Not storing region ids here or relating them to any other process using indices,
	// so using ids rather than indices is fine
	// Also, this should ideally be population-weighted, but since
	// comparisons are being made within states and within-state population
	// differences are fairly small it's a lot easier for implementation to assume all seats
	// are the same size and it won't have a noticeable effect on the outcome
	std::map<int, std::vector<float>> swingsByRegion;
	for (auto const& [id, seat] : project.seats()) {
		swingsByRegion[seat.region].push_back(seat.previousSwing);
	}
	std::map<int, float> averages;
	for (auto const& [id, swings] : swingsByRegion) {
		float sum = std::accumulate(swings.begin(), swings.end(), 0.0f);
		float average = sum / float(swings.size());
		averages[id] = average;
	}
	run.seatPreviousTppSwing.resize(project.seats().count());
	for (auto const& [id, seat] : project.seats()) {
		int index = project.seats().idToIndex(id);
		run.seatPreviousTppSwing[index] = seat.previousSwing - averages[seat.region];
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

void SimulationPreparation::loadSeatBettingOdds()
{
	run.seatBettingOdds.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto const& [partyCode, odds] : project.seats().viewByIndex(seatIndex).bettingOdds) {
			int partyIndex = project.parties().indexByShortCode(partyCode);
			run.seatBettingOdds[seatIndex][partyIndex] = odds;
		}
	}
}

void SimulationPreparation::loadSeatPolls()
{
	run.seatPolls.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto const& [partyCode, polls] : project.seats().viewByIndex(seatIndex).polls) {
			int partyIndex = project.parties().indexByShortCode(partyCode);
			run.seatPolls[seatIndex][partyIndex] = polls;
		}
	}
}

void SimulationPreparation::loadSeatTppPolls()
{
	run.seatTppPolls.resize(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto& polls = project.seats().viewByIndex(seatIndex).tppPolls;
		if (polls.size()) {
			float sum = std::accumulate(polls.begin(), polls.end(), 0.0f,
				[](float acc, std::pair<std::string, float> const& val) {return acc + val.second; });
			run.seatTppPolls[seatIndex] = sum / float(polls.size());
		}
	}
	run.seatTppMrpPolls.resize(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto& polls = project.seats().viewByIndex(seatIndex).tppMrpPolls;
		if (polls.size()) {
			float sum = std::accumulate(polls.begin(), polls.end(), 0.0f,
				[](float acc, std::pair<std::string, float> const& val) {return acc + val.second; });
			run.seatTppMrpPolls[seatIndex] = sum / float(polls.size());
		}
	}
}

void SimulationPreparation::loadSeatMinorViability()
{
	run.seatMinorViability.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto const& [partyCode, minorViability] : project.seats().viewByIndex(seatIndex).minorViability) {
			int partyIndex = project.parties().indexByShortCode(partyCode);
			run.seatMinorViability[seatIndex][partyIndex] = minorViability;
		}
	}
}

void SimulationPreparation::determinePreviousVoteEnrolmentRatios()
{
	if (!run.isLiveAutomatic()) return;

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
	for (auto const& [key, region] : project.regions()) {
		run.totalPopulation += float(region.population);
	}
}

void SimulationPreparation::loadLiveManualResults()
{
	for (auto outcome = project.outcomes().rbegin(); outcome != project.outcomes().rend(); ++outcome) {
		run.liveSeatTppSwing[project.seats().idToIndex(outcome->seat)] = outcome->partyOneSwing;
		run.liveSeatTcpCounted[project.seats().idToIndex(outcome->seat)] = outcome->getPercentCountedEstimate();
	}
}

void SimulationPreparation::calculateLiveAggregates()
{
	run.liveOverallTppSwing = 0.0f;
	run.liveOverallTcpPercentCounted = 0.0f;
	run.liveOverallTppBasis = 0.0f;
	run.classicSeatCount = 0.0f;
	run.sampleRepresentativeness = 0.0f;
	run.total2cpVotes = 0;
	run.totalEnrolment = 0;
	if (run.isLive()) {
		logger << "preparing live aggregates\n";
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			updateLiveAggregateForSeat(seatIndex);
		}
		finaliseLiveAggregates();
		logger << "finished preparing live aggregates\n";
	}

	if (run.isLiveAutomatic()) {
		sim.latestReport.total2cpPercentCounted = (float(run.totalEnrolment) ? float(run.total2cpVotes) / float(run.totalEnrolment) : 0.0f) * 100.0f;
	}
	else if (run.isLiveManual()) {
		sim.latestReport.total2cpPercentCounted = run.liveOverallTcpPercentCounted;
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
	int regionIndex = project.regions().idToIndex(seat.region);
	float tcpPercentCounted = run.liveSeatTcpBasis[seatIndex];
	float tppBasis = run.liveSeatTppBasis[seatIndex];
	if (!std::isnan(run.liveSeatTppSwing[seatIndex])) {
		float weight = 100.0f - 100.0f / (1.0f + tppBasis * tppBasis * 0.02f);
		float weightedSwing = run.liveSeatTppSwing[seatIndex] * weight;
		run.liveOverallTppSwing += weightedSwing;
		run.liveOverallTppBasis += weight;
		run.liveRegionSwing[regionIndex] += weightedSwing;
		run.liveRegionTppBasis[regionIndex] += weight;
	}
	float fpPercentCounted = run.liveSeatFpCounted[seatIndex];
	for (auto [party, vote] : run.liveSeatFpPercent[seatIndex]) {
		if (!std::isnan(run.liveSeatFpTransformedSwing[seatIndex][party])) {
			float weightedSwing = run.liveSeatFpTransformedSwing[seatIndex][party] * fpPercentCounted;
			run.liveOverallFpSwing[party] += weightedSwing;
			run.liveOverallFpSwingWeight[party] += fpPercentCounted;
		}
		else if (!run.pastSeatResults[seatIndex].fpVoteCount.contains(party) ||
			run.pastSeatResults[seatIndex].fpVoteCount[party] == 0)
		{
			float weightedPercent = run.liveSeatFpPercent[seatIndex][party] * fpPercentCounted;
			run.liveOverallFpNew[party] += weightedPercent;
			run.liveOverallFpNewWeight[party] += fpPercentCounted;
		}
	}
	run.liveOverallTcpPercentCounted += tcpPercentCounted;
	run.liveOverallFpPercentCounted += fpPercentCounted;
	run.liveRegionTcpPercentCounted[regionIndex] += tcpPercentCounted;
	++run.liveRegionClassicSeatCount[regionIndex];
	run.sampleRepresentativeness += std::min(2.0f, tcpPercentCounted) * 0.5f;
	//run.total2cpVotes += seat.latestResults->total2cpVotes();
	//run.totalEnrolment += seat.latestResults->enrolment;
}

void SimulationPreparation::finaliseLiveAggregates()
{
	if (run.liveOverallTcpPercentCounted) {
		run.liveOverallTppSwing /= run.liveOverallTppBasis;
		run.liveOverallTppBasis /= project.seats().count();
		run.liveOverallTcpPercentCounted /= project.seats().count();
		run.liveOverallFpPercentCounted /= project.seats().count();
		run.sampleRepresentativeness /= run.classicSeatCount;
		run.sampleRepresentativeness = std::sqrt(run.sampleRepresentativeness);
		for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
			run.liveRegionSwing[regionIndex] /= run.liveRegionTppBasis[regionIndex];
			run.liveRegionTppBasis[regionIndex] /= regionSeatCount[regionIndex];
			run.liveRegionTcpPercentCounted[regionIndex] /= regionSeatCount[regionIndex];
		}
		PA_LOG_VAR(run.liveOverallTppSwing);
		PA_LOG_VAR(run.liveOverallTcpPercentCounted);
		PA_LOG_VAR(run.sampleRepresentativeness);
	}
	for (auto& [partyIndex, vote] : run.liveOverallFpSwing) {
		vote /= run.liveOverallFpSwingWeight[partyIndex];
	}
	for (auto& [partyIndex, vote] : run.liveOverallFpNew) {
		vote /= run.liveOverallFpNewWeight[partyIndex];
	}
	PA_LOG_VAR(run.liveOverallFpSwing);
	PA_LOG_VAR(run.liveOverallFpNew);
}

void SimulationPreparation::resetResultCounts()
{
	run.partyMajority.clear();
	run.partyMinority.clear();
	run.partyMostSeats.clear();
	run.tiedParliament = 0;
	sim.latestReport.partySeatWinFrequency.clear();
	sim.latestReport.othersWinFrequency.resize(project.seats().count() + 1);
	sim.latestReport.partyPrimaryFrequency.clear();
	sim.latestReport.tppFrequency.clear();
	sim.latestReport.partyOneSwing = 0.0;
}

void SimulationPreparation::determineSpecificPartyIndices()
{
	run.indPartyIndex = project.parties().indexByShortCode("IND");
	if (run.indPartyIndex == -1) run.indPartyIndex = EmergingIndIndex;
	run.grnPartyIndex = project.parties().indexByShortCode("GRN");
	if (run.grnPartyIndex == -1) run.indPartyIndex = InvalidPartyIndex;
}

void SimulationPreparation::loadPreviousPreferenceFlows() {
	run.previousPreferenceFlow.clear();
	run.previousExhaustRate.clear();
	auto lines = extractElectionDataFromFile("analysis/Data/preference-estimates.csv", run.getTermCode());
	for (auto const& line : lines) {
		std::string party = splitString(line[2], " ")[0];
		int partyIndex = project.parties().indexByShortCode(party);
		float thisPreferenceFlow = std::stof(line[3]);
		run.previousPreferenceFlow[partyIndex] = thisPreferenceFlow;
		if (line.size() >= 5 && line[4][0] != '#') {
			float thisExhaustRate = std::stof(line[4]);
			run.previousExhaustRate[partyIndex] = thisExhaustRate * 0.01f;
		}
		else {
			run.previousExhaustRate[partyIndex] = 0.0f;
		}
	}

	run.previousPreferenceFlow[EmergingPartyIndex] = run.previousPreferenceFlow[OthersIndex];
	run.previousExhaustRate[EmergingPartyIndex] = run.previousExhaustRate[OthersIndex];
	run.previousPreferenceFlow[0] = 100.0f;
	run.previousPreferenceFlow[1] = 0.0f;
	run.previousExhaustRate[0] = 0.0f;
	run.previousExhaustRate[1] = 0.0f;
	run.previousPreferenceFlow[CoalitionPartnerIndex] = 15.0f;
	run.previousExhaustRate[CoalitionPartnerIndex] = 0.25f;

	// Ensure any other named parties without a specified preference flow
	// have the same as Others
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		if (!run.previousPreferenceFlow.contains(partyIndex)) {
			run.previousPreferenceFlow[partyIndex] = run.previousPreferenceFlow[OthersIndex];
			run.previousExhaustRate[partyIndex] = run.previousExhaustRate[OthersIndex];
		}
	}
	// This needs to be done last so the preceding step can fill out the IND
	// preference flows if they weren't already specifid
	run.previousPreferenceFlow[EmergingIndIndex] = run.previousPreferenceFlow[run.indPartyIndex];
	run.previousExhaustRate[EmergingIndIndex] = run.previousExhaustRate[run.indPartyIndex];
}

void SimulationPreparation::loadNcPreferenceFlows()
{
	for (auto const& [partyIndex, party] : project.parties()) {
		for (auto prefFlow : party.ncPreferenceFlow) {
			int firstParty = project.parties().indexByShortCode(prefFlow.first.first);
			int secondParty = project.parties().indexByShortCode(prefFlow.first.second);
			run.ncPreferenceFlow[partyIndex][{firstParty, secondParty}] = prefFlow.second;
			run.ncPreferenceFlow[partyIndex][{secondParty, firstParty}] = 100.0f - prefFlow.second;
			if (prefFlow.first.first == "IND") {
				run.ncPreferenceFlow[partyIndex][{EmergingIndIndex, secondParty}] = prefFlow.second;
				run.ncPreferenceFlow[partyIndex][{secondParty, EmergingIndIndex}] = 100.0f - prefFlow.second;
			}
			else if (prefFlow.first.second == "IND") {
				run.ncPreferenceFlow[partyIndex][{firstParty, EmergingIndIndex}] = prefFlow.second;
				run.ncPreferenceFlow[partyIndex][{EmergingIndIndex, firstParty}] = 100.0f - prefFlow.second;
			}
		}
	}
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
	std::string fileName = "analysis/elections/results_" + sim.settings.prevTermCodes[0] + ".csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	bool fpMode = false;
	int currentSeat = -1;
	bool indSeen = false;
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
				currentSeat = project.seats().idToIndex(project.seats().accessByName(values[1], true).first);
				indSeen = false;
			}
			catch (SeatDoesntExistException) {
				// Seat might have been abolished, so no need to give an error, log it in case it's wrong
				if (!run.doingBettingOddsCalibrations) logger << "Could not find a match for seat " + values[1] + "\n";
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
				if (shortCodeUsed == "IND") {
					if (indSeen) {
						partyId = -1;
					}
					else {
						indSeen = true;
					}
				}
				run.pastSeatResults[currentSeat].fpVoteCount[partyId] += voteCount;
				run.pastSeatResults[currentSeat].fpVotePercent[partyId] += votePercent;
			}
			else {
				run.pastSeatResults[currentSeat].tcpVoteCount[partyId] += voteCount;
				run.pastSeatResults[currentSeat].tcpVotePercent[partyId] += votePercent;
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
				results.prevOthers += std::min(voteShare, detransformVoteShare(run.indEmergence.fpThreshold));
			}
		}
		results.prevOthers = std::max(2.0f, results.prevOthers);
		for (auto& [party, voteCount] : results.fpVoteCount) {
			results.turnoutCount += voteCount;
		}
		run.totalPreviousTurnout += results.turnoutCount;
	}

	// SFF -> IND seats
	if (run.getTermCode() == "2023nsw") {
		// Due to the circumstances of these candidates leaving SFF and becoming independents,
		// a special adjustment needs to be made (there is no appropriate historical precedent.)
		// The SFF vote is treated as if it were independent.
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Barwon") {
				run.pastSeatResults[seatIndex].fpVoteCount[run.indPartyIndex] = 15218;
				run.pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex] = 32.96f;
				run.pastSeatResults[seatIndex].fpVoteCount[OthersIndex] = 5873;
				run.pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = 12.72f;
				run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] = 56.6f;
			}
			else if (project.seats().viewByIndex(seatIndex).name == "Murray") {
				run.pastSeatResults[seatIndex].fpVoteCount[run.indPartyIndex] = 18305;
				run.pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex] = 38.75f;
				run.pastSeatResults[seatIndex].fpVoteCount[OthersIndex] = 6919;
				run.pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = 14.65f;
				run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] = 53.54f;
			}
			else if (project.seats().viewByIndex(seatIndex).name == "Orange") {
				run.pastSeatResults[seatIndex].fpVoteCount[run.indPartyIndex] = 24718;
				run.pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex] = 49.15f;
				run.pastSeatResults[seatIndex].fpVoteCount[OthersIndex] = 4849;
				run.pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = 9.64f;
				run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] = 65.18f;
			}
		}
	} else if (run.getTermCode() == "2024qld") {
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Mirani") {
				run.pastSeatResults[seatIndex].fpVoteCount[run.indPartyIndex] = 9320;
				run.pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex] = 31.66f;
				run.pastSeatResults[seatIndex].fpVoteCount[OthersIndex] = 1871;
				run.pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = 6.36f;
				run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] = 58.98f;
			}
		}
	}
}

void SimulationPreparation::loadSeatTypes()
{
	run.seatTypes.resize(project.seats().count());
	std::string fileName = "analysis/Data/seat-types.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == run.regionCode) {
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
	std::string fileName = "analysis/Seat Statistics/statistics_GRN.csv";
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
	std::string fileName = "analysis/Seat Statistics/statistics_IND.csv";
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
	std::string fileName = "analysis/Seat Statistics/statistics_OTH.csv";
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
	std::string fileName = "analysis/Seat Statistics/statistics_emerging_IND.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	auto extractNum = [&]() {std::string line; std::getline(file, line); return std::stof(line); };
	run.indEmergence.fpThreshold = transformVoteShare(extractNum());
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
	std::string fileName = "analysis/Seat Statistics/statistics_populist.csv";
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
	std::string fileName = "analysis/Seat Statistics/modifiers_populist.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == run.regionCode) {
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
	std::string fileName = "analysis/Seat Statistics/statistics_centrist.csv";
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
	std::string fileName = "analysis/Seat Statistics/modifiers_centrist.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[1] == run.regionCode) {
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
	std::string fileName = "analysis/Data/prior-results.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values[0] == run.yearCode && values[1] == run.regionCode) {
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

void SimulationPreparation::loadRegionBaseBehaviours()
{
	std::string fileName = "analysis/Regional/" + run.getTermCode() + "-regions-base.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		if (values[0] == "all") continue;
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		run.regionBaseBehaviour[regionIndex].overallSwingCoeff = std::stof(values[1]);
		run.regionBaseBehaviour[regionIndex].baseSwingDeviation = std::stof(values[2]);
		run.regionBaseBehaviour[regionIndex].rmse = std::stof(values[3]);
		run.regionBaseBehaviour[regionIndex].kurtosis = std::stof(values[4]);
	} while (true);
}

void SimulationPreparation::loadRegionPollBehaviours()
{
	std::string fileName = "analysis/Regional/" + run.getTermCode() + "-regions-polled.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		if (values[0] == "all") {
			run.generalPollBehaviour.overallSwingCoeff = std::stof(values[1]);
			run.generalPollBehaviour.baseSwingDeviation = std::stof(values[2]);
			continue;
		}
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		run.regionPollBehaviour[regionIndex].overallSwingCoeff = std::stof(values[1]);
		run.regionPollBehaviour[regionIndex].baseSwingDeviation = std::stof(values[2]);
	} while (true);
}

void SimulationPreparation::loadRegionMixBehaviours()
{
	std::string fileName = "analysis/Regional/" + run.getTermCode() + "-mix-regions.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		run.regionMixBehaviour[regionIndex].bias = std::stof(values[1]);
		run.regionMixBehaviour[regionIndex].rmse = std::stof(values[2]);
	} while (true);
}

void SimulationPreparation::loadOverallRegionMixParameters()
{
	std::string fileName = "analysis/Regional/" + run.getTermCode() + "-mix-parameters.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		if (values[0] == "mix_factor") {
			run.regionMixParameters.mixFactorA = std::stof(values[1]);
			run.regionMixParameters.mixFactorB = std::stof(values[2]);
		}
		else if (values[0] == "rmse") {
			run.regionMixParameters.rmseA = std::stof(values[1]);
			run.regionMixParameters.rmseB = std::stof(values[2]);
			run.regionMixParameters.rmseC = std::stof(values[3]);
		}
		else if (values[0] == "kurtosis") {
			run.regionMixParameters.kurtosisA = std::stof(values[1]);
			run.regionMixParameters.kurtosisB = std::stof(values[2]);
		}

	} while (true);
}

void SimulationPreparation::loadRegionSwingDeviations()
{
	std::string fileName = "analysis/Regional/" + run.getTermCode() + "-swing-deviations.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		int index = 0;
		for (auto value : values) {
			run.regionSwingDeviations[index] = std::stof(value);
			if (index == 5) { // TAS, ACT and NT are all bundled together
				run.regionSwingDeviations[6] = std::stof(value);
				run.regionSwingDeviations[7] = std::stof(value);
				break;
			}
			++index;
		}

	} while (true);
	PA_LOG_VAR(run.regionSwingDeviations);
}

void SimulationPreparation::loadTppSwingFactors()
{
	std::string fileName = "analysis/Seat Statistics/tpp-swing-factors.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		if (values[0] == "mean-swing-deviation") {
			run.tppSwingFactors.meanSwingDeviation = std::stof(values[1]);
		}
		if (values[0] == "swing-kurtosis") {
			run.tppSwingFactors.swingKurtosis = std::stof(values[1]);
		}
		else if (values[0] == "federal-modifier") {
			run.tppSwingFactors.federalModifier = std::stof(values[1]);
		}
		else if (values[0] == "retirement-urban") {
			run.tppSwingFactors.retirementUrban = std::stof(values[1]);
		}
		else if (values[0] == "retirement-regional") {
			run.tppSwingFactors.retirementRegional = std::stof(values[1]);
		}
		else if (values[0] == "sophomore-candidate-urban") {
			run.tppSwingFactors.sophomoreCandidateUrban = std::stof(values[1]);
		}
		else if (values[0] == "sophomore-candidate-regional") {
			run.tppSwingFactors.sophomoreCandidateRegional = std::stof(values[1]);
		}
		else if (values[0] == "sophomore-party-urban") {
			run.tppSwingFactors.sophomorePartyUrban = std::stof(values[1]);
		}
		else if (values[0] == "sophomore-party-regional") {
			run.tppSwingFactors.sophomorePartyRegional = std::stof(values[1]);
		}
		else if (values[0] == "previous-swing-modifier") {
			run.tppSwingFactors.previousSwingModifier = std::stof(values[1]);
		}
		else if (values[0] == "by-election-modifier") {
			run.tppSwingFactors.byElectionSwingModifier = std::stof(values[1]);
		}
	} while (true);
}

void SimulationPreparation::loadIndividualSeatParameters()
{
	run.seatParameters.resize(project.seats().count());
	std::string fileName = "analysis/Seat Statistics/individual-seat-factors.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() < 6) break;
		std::string electionRegion = values[1];
		if (electionRegion != run.regionCode) continue;
		std::string seatName = values[0];
		try {
			int seatId = project.seats().accessByName(seatName, true).first;
			int seatIndex = project.seats().idToIndex(seatId);
			if (run.seatParameters[seatIndex].loaded) continue;
			run.seatParameters[seatIndex].loaded = true;
			run.seatParameters[seatIndex].elasticity = std::stof(values[3]);
			run.seatParameters[seatIndex].trend = std::stof(values[4]);
			run.seatParameters[seatIndex].volatility = std::stof(values[5]);
		}
		catch (SeatDoesntExistException) {
			// Expected that some seats won't exist, ignore them
		}

	} while (true);
}

void SimulationPreparation::prepareProminentMinors()
{
	std::size_t seatCount = project.seats().count();
	const std::string indAbbrev = project.parties().viewByIndex(run.indPartyIndex).abbreviation;
	run.seatProminentMinors.resize(seatCount);
	for (int seatIndex = 0; seatIndex < int(seatCount); ++seatIndex) {
		for (auto const& code : project.seats().viewByIndex(seatIndex).prominentMinors) {
			run.seatProminentMinors[seatIndex].push_back(project.parties().indexByShortCode(code));
		}
	}
}

void SimulationPreparation::prepareRunningParties()
{
	std::size_t seatCount = project.seats().count();
	const std::string indAbbrev = project.parties().viewByIndex(run.indPartyIndex).abbreviation;
	run.runningParties.resize(seatCount);
	run.indCount.resize(seatCount);
	run.othCount.resize(seatCount);
	run.runningParties.resize(seatCount);
	for (int seatIndex = 0; seatIndex < int(seatCount); ++seatIndex) {
		for (auto const& code : project.seats().viewByIndex(seatIndex).runningParties) {
			auto asteriskSplit = splitString(code, "*");
			auto partyCode = asteriskSplit[0];
			run.runningParties[seatIndex].push_back(partyCode);
			if (partyCode == indAbbrev) {
				run.indCount[seatIndex] = asteriskSplit.size();
			} 
			else if (partyCode == OthersCode) {
				run.othCount[seatIndex] = asteriskSplit.size();
			}
		}
	}
}

void SimulationPreparation::calculateIndEmergenceModifier()
{
	// More independents should be expected to emerge if more
	// are already confirmed relative to the usual number.
	int numConfirmed = std::count_if(project.seats().begin(), project.seats().end(),
		[](const decltype(project.seats().begin())::value_type& seatPair) {return seatPair.second.confirmedProminentIndependent; });
	int daysToElection = project.projections().view(sim.settings.baseProjection).generateSupportSample(project.models()).daysToElection;
	float expectedConfirmed = std::max(float(daysToElection) * -0.02f + 3.5f, 0.0f) * project.seats().count() / 100.0f;
	run.indEmergenceModifier = (float(numConfirmed) + 1.0f) / (expectedConfirmed + 1.0f);
}

void SimulationPreparation::initializeGeneralLiveData()
{
	run.liveSeatTppSwing.resize(project.seats().count(), 0.0f);
	run.liveSeatTcpCounted.resize(project.seats().count(), 0.0f);
	run.liveSeatFpSwing.resize(project.seats().count());
	run.liveSeatFpTransformedSwing.resize(project.seats().count());
	run.liveSeatFpPercent.resize(project.seats().count());
	run.liveSeatFpCounted.resize(project.seats().count(), 0.0f);
	run.liveSeatTcpParties.resize(project.seats().count(), { -1000, -1000 });
	run.liveSeatTcpSwing.resize(project.seats().count(), 0.0f);
	run.liveSeatTcpPercent.resize(project.seats().count(), 0.0f);
	run.liveSeatTcpBasis.resize(project.seats().count(), 0.0f);
	run.liveSeatTppBasis.resize(project.seats().count(), 0.0f);
	run.liveSeatPpvcSensitivity.resize(project.seats().count(), 0.0f);
	run.liveSeatDecVoteSensitivity.resize(project.seats().count(), 0.0f);
	run.liveEstDecVoteRemaining.resize(project.seats().count(), 0.0f);
	run.liveRegionSwing.resize(project.regions().count(), 0.0f);
	run.liveRegionTcpPercentCounted.resize(project.regions().count(), 0.0f);
	run.liveRegionTppBasis.resize(project.regions().count(), 0.0f);
	run.liveRegionClassicSeatCount.resize(project.regions().count(), 0.0f);
}
