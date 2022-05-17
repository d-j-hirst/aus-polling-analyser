#include "SimulationPreparation.h"

#include "LinearRegression.h"
#include "PollingProject.h"
#include "ResultsDownloader.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

#include <filesystem>
#include <queue>
#include <random>
#include <set>

// Note: A large amount of code in this file is commented out as the "previous results"
// was updated to a new (better) format but the "latest results" was not. Further architectural
// improvement, including removing cached election results from project seat data, cannot be
// properly done unless this is fixed, and the fixing is decidedly non-trivial. In order to
// expedite the initial web release, which does not require live election updating, these have
// been disabled and code producing errors commented out and replaced with stubs,
// until the project is prepared to work on restoring the live results.

static std::random_device rd;
static std::mt19937 gen;

float cappedTransformedSwing(float previousPercent, float currentPercent, float capMultiplier) {
	float transformedSwing = transformVoteShare(currentPercent) - transformVoteShare(previousPercent);
	float cap = capMultiplier * std::abs(currentPercent - previousPercent);
	return std::clamp(transformedSwing, -cap, cap);
}

bool simPartyIsTpp (int simParty) {
	return simParty == 0 || simParty == 1 || simParty == CoalitionPartnerIndex;
};

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

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
	determineIndependentPartyIndex();

	loadTppSwingFactors();

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

	determineEffectiveSeatModifiers();
	determinePreviousSwingDeviations();

	accumulateRegionStaticInfo();

	loadSeatBettingOdds();
	loadSeatPolls();

	resetPpvcBiasAggregates();

	determinePpvcBias();

	determinePreviousVoteEnrolmentRatios();

	resizeRegionSeatCountOutputs();

	countInitialRegionSeatLeads();

	loadRegionBaseBehaviours();
	loadRegionPollBehaviours();
	loadRegionMixBehaviours();
	loadOverallRegionMixParameters();

	calculateTotalPopulation();

	if (sim.isLive()) initializeGeneralLiveData();
	if (sim.isLiveManual()) loadLiveManualResults();
	if (sim.isLiveAutomatic()) prepareLiveAutomatic();

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
	run.seatPartyWins.resize(project.seats().count());
	run.cumulativeSeatPartyFpShare.resize(project.seats().count());
	run.seatPartyFpDistribution.resize(project.seats().count());
	run.seatPartyFpZeros.resize(project.seats().count());
	run.seatTcpDistribution.resize(project.seats().count());
}

void SimulationPreparation::storeTermCode()
{
	std::string termCode = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode();
	run.yearCode = termCode.substr(0, 4);
	run.regionCode = termCode.substr(4);
}

void SimulationPreparation::determineEffectiveSeatModifiers()
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
			if (seat.sophomoreCandidate) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.sophomoreCandidateRegional * direction;
			if (seat.sophomoreParty) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.sophomorePartyRegional * direction;
			if (seat.retirement) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.retirementRegional * direction;
		}
		else {
			if (seat.sophomoreCandidate) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.sophomoreCandidateUrban * direction;
			if (seat.sophomoreParty) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.sophomorePartyUrban * direction;
			if (seat.retirement) run.seatPartyOneTppModifier[seatIndex] += run.tppSwingFactors.retirementUrban * direction;
		}
		constexpr float DisendorsementMod = 3.0f;
		constexpr float PreviousDisendorsementMod = DisendorsementMod * -0.5f;
		if (seat.disendorsement) run.seatPartyOneTppModifier[seatIndex] += DisendorsementMod * direction;
		if (seat.previousDisendorsement) run.seatPartyOneTppModifier[seatIndex] += PreviousDisendorsementMod * direction;
		run.seatPartyOneTppModifier[seatIndex] += seat.localModifier;
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

void SimulationPreparation::resetPpvcBiasAggregates()
{
	run.ppvcBiasNumerator = 0.0f;
	run.ppvcBiasDenominator = 0.0f;
	run.totalOldPpvcVotes = 0;
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

void SimulationPreparation::prepareLiveAutomatic()
{
	downloadPreviousResults();
	parsePreviousResults();
	downloadPreload();
	parsePreload();
	downloadCurrentResults();
	parseCurrentResults();
	preparePartyCodeGroupings();
	determinePartyIdConversions();
	determineSeatIdConversions();
	calculateTppPreferenceFlows();
	calculateSeatPreferenceFlows();
	calculateBoothFpSwings();
	estimateBoothTcps();
	calculateBoothTcpSwings();
	calculateCountProgress();
	calculateSeatSwings();
	prepareLiveTppSwings();
	prepareLiveTcpSwings();
	prepareLiveFpSwings();
}

void SimulationPreparation::downloadPreviousResults()
{
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.previousResultsUrl;
	std::replace(mangledName.begin(), mangledName.end(), '/', '$');
	std::replace(mangledName.begin(), mangledName.end(), '.', '$');
	std::replace(mangledName.begin(), mangledName.end(), ':', '$');
	mangledName = "downloads/" + mangledName + ".xml";
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found previous results file at: " << mangledName << "\n";
	}
	else {
		resultsDownloader.loadZippedFile(sim.settings.previousResultsUrl, mangledName);
		logger << "Downloaded file: " << sim.settings.previousResultsUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
	xmlFilename = mangledName;
}

void SimulationPreparation::parsePreviousResults()
{
	xml.LoadFile(xmlFilename.c_str());
	previousElection = Results2::Election(xml);
}

void SimulationPreparation::downloadPreload()
{
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.preloadUrl;
	std::replace(mangledName.begin(), mangledName.end(), '/', '$');
	std::replace(mangledName.begin(), mangledName.end(), '.', '$');
	std::replace(mangledName.begin(), mangledName.end(), ':', '$');
	mangledName = "downloads/" + mangledName + ".xml";
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found preload file at: " << mangledName << "\n";
	}
	else {
		resultsDownloader.loadZippedFile(sim.settings.preloadUrl, mangledName, "preload");
		logger << "Downloaded file: " << sim.settings.preloadUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
	xmlFilename = mangledName;
}

void SimulationPreparation::parsePreload()
{
	xml.LoadFile(xmlFilename.c_str());
	currentElection = Results2::Election(xml);
}

void SimulationPreparation::downloadCurrentResults()
{
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.currentTestUrl;
	std::replace(mangledName.begin(), mangledName.end(), '/', '$');
	std::replace(mangledName.begin(), mangledName.end(), '.', '$');
	std::replace(mangledName.begin(), mangledName.end(), ':', '$');
	mangledName = "downloads/" + mangledName + ".xml";
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found currenttest file at: " << mangledName << "\n";
	}
	else {
		resultsDownloader.loadZippedFile(sim.settings.currentTestUrl, mangledName);
		logger << "Downloaded file: " << sim.settings.currentTestUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
	xmlFilename = mangledName;
}

void SimulationPreparation::parseCurrentResults()
{
	xml.LoadFile(xmlFilename.c_str());
	currentElection.update(xml);
}

void SimulationPreparation::preparePartyCodeGroupings()
{
	for (auto [id, party] : project.parties()) {
		for (auto shortCode : party.officialCodes) {
			if (id == 1 && shortCode[0] == 'N') {
				partyCodeGroupings[shortCode] = CoalitionPartnerIndex;
			}
			else {
				partyCodeGroupings[shortCode] = project.parties().idToIndex(id);
			}
		}
	}
	PA_LOG_VAR(partyCodeGroupings);
}

void SimulationPreparation::calculateTppPreferenceFlows()
{
	std::map<int, int> partyIdToPos;
	int greensParty = -1;
	int greensPos = -1;
	int pos = 0;
	for (auto const& [seatId, seat] : currentElection.seats) {
		for (int boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			for (auto [candidateId, vote] : booth.fpVotes) {
				int party = currentElection.candidates[candidateId].party;
				if (aecPartyToSimParty[party] == 2) {
					if (greensPos != -1) continue;
					partyIdToPos[party] = pos;
					greensPos = pos;
					greensParty = party;
					++pos;
				}
				if (!partyIdToPos.contains(party)) {
					partyIdToPos[party] = pos;
					++pos;
				}
			}
		}
	}
	DataSet data;
	std::map<int, int> partyIdFrequency;
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (seat.tcpVotes.size() != 2) continue;
		int numCoalition = 0;
		for (auto [candidateId, votes] : seat.fpVotes) {
			int aecPartyId = currentElection.candidates[candidateId].party;
			if (aecPartyToSimParty[aecPartyId] == 1) ++numCoalition;
			if (aecPartyToSimParty[aecPartyId] == CoalitionPartnerIndex) ++numCoalition;
		}
		if (numCoalition > 1) continue; // don't get preferences from intra-Coalition contests
		bool firstPartyFound = false;
		bool secondPartyFound = false;
		int partyOneThisSeat = -1;
		for (auto [party, votes] : seat.tcpVotes) {
			int simParty = aecPartyToSimParty[party];
			if (simParty == 0) {
				firstPartyFound = true;
				partyOneThisSeat = party;
			}
			else if (simParty == 1 || simParty == -4) {
				secondPartyFound = true;
			}
		}
		if (!firstPartyFound || !secondPartyFound) continue; // not classic 2cp
		for (int boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (!booth.totalVotesTcp()) continue;
			if (!booth.totalVotesFp()) continue;
			double totalFpVotes = double(booth.totalVotesFp());
			double totalTcpVotes = double(booth.totalVotesTcp());
			std::vector<double> fpData(partyIdToPos.size());
			for (auto [candidateId, votes] : booth.fpVotes) {
				int partyId = currentElection.candidates[candidateId].party;
				if (aecPartyToSimParty[partyId] == 2) partyId = greensParty;
				fpData[partyIdToPos.at(partyId)] = double(votes) / totalFpVotes;
				++partyIdFrequency[partyId];
			}
			double tcpData = double(booth.tcpVotes.at(partyOneThisSeat)) / totalTcpVotes;
			for (double voteIncrement = 100.0; voteIncrement < totalTcpVotes; voteIncrement += 100.0) {
				data.push_back({ fpData, tcpData });
			}
		}
	}
	// Fill with lots of dummy data to make sure that major party preferences are "forced" to what they should be
	for (auto [party, partyPos] : partyIdToPos) {
		int simParty = aecPartyToSimParty[party];
		if (simParty == 0) {
			std::vector<double> fpData(partyIdToPos.size());
			fpData[partyPos] = 1000;
			double tcpData = 1000;
			data.push_back({ fpData, tcpData });
		}
		else if (simParty == 1 || simParty == -4) {
			std::vector<double> fpData(partyIdToPos.size());
			fpData[partyPos] = 1000;
			double tcpData = 0;
			data.push_back({ fpData, tcpData });
		}
	}
	for (auto [partyId, frequency] : partyIdFrequency) {
		int simParty = aecPartyToSimParty[partyId];
		float priorPrefs = simParty >= 0 ? project.parties().viewByIndex(simParty).p1PreferenceFlow
			: project.parties().getOthersPreferenceFlow();
		std::vector<double> fpData(partyIdToPos.size());
		fpData[partyIdToPos[partyId]] = 10;
		double tcpData = priorPrefs * 0.1;
		data.push_back({ fpData, tcpData });
	}
	auto weights = runLeastSquares(data);
	for (auto const& [partyId, partyPos] : partyIdToPos) {
		updatedPreferenceFlows[partyId] = std::clamp(float(weights[partyPos]) * 100.0f, 0.0f, 100.0f);
		logger << "Party: " << currentElection.parties[partyId].name <<
			" - current preference flow to ALP: " << formatFloat(weights[partyPos] * 100.0f, 2) << "%, " <<
			partyIdFrequency[partyId] << "booths\n";
	}
	for (auto [partyId, party] : currentElection.parties) {
		if (aecPartyToSimParty[partyId] == 2) {
			updatedPreferenceFlows[partyId] = updatedPreferenceFlows[greensParty];
		}
	}
	PA_LOG_VAR(updatedPreferenceFlows);
}

void SimulationPreparation::calculateSeatPreferenceFlows()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (!seat.tcpVotes.size()) continue;
		std::map<int, int> candidateIdToPos;
		int index = 0;
		for (auto const& [candidateId, votes] : seat.fpVotes) {
			candidateIdToPos[candidateId] = index;
			++index;
		}
		int partyIdToUse = seat.tcpVotes.begin()->first;
		DataSet data;
		for (int boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (!booth.totalVotesTcp()) continue;
			if (!booth.totalVotesFp()) continue;
			double totalFpVotes = double(booth.totalVotesFp());
			double totalTcpVotes = double(booth.totalVotesTcp());
			std::vector<double> fpData(index);
			for (auto const& [candidateId, votes] : booth.fpVotes) {
				fpData[candidateIdToPos.at(candidateId)] = double(votes) / totalFpVotes;
			}
			double tcpData = double(booth.tcpVotes.at(partyIdToUse)) / totalTcpVotes;
			data.push_back({ fpData, tcpData });
		}
		// don't bother calculating for tiny data sets as it'll be worse than
		// just using previous-election preferences
		if (data.size() < 2) continue;
		PA_LOG_VAR(seat.name);
		PA_LOG_VAR(candidateIdToPos);
		PA_LOG_VAR(partyIdToUse);
		PA_LOG_VAR(currentElection.parties[partyIdToUse].name);
		PA_LOG_VAR(data);
		std::vector<std::string> partyNames;
		for (auto const& [candidateId, votes] : seat.fpVotes) {
			partyNames.push_back(currentElection.parties[currentElection.candidates[candidateId].party].name);
		}
		PA_LOG_VAR(partyNames);
		runLeastSquares(data);
	}
}

void SimulationPreparation::estimateBoothTcps()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		std::pair<int, int> tcpParties;
		// May or may not be a tcp pair recorded, if not assume classic 2CP
		auto const& simSeat = project.seats().viewByIndex(aecSeatToSimSeat[seatId]);
		bool seatIsTpp = simPartyIsTpp(simSeat.incumbent);
		// If there is a 2CP pair recorded check if it's classic 2pp
		if (seat.tcpVotes.size()) {
			tcpParties = { seat.tcpVotes.begin()->first, std::next(seat.tcpVotes.begin())->first };
			std::pair<int, int> tcpSimParties = { aecPartyToSimParty[tcpParties.first], aecPartyToSimParty[tcpParties.second] };
			seatIsTpp = (simPartyIsTpp(tcpSimParties.first) && simPartyIsTpp(tcpSimParties.second));
			if (tcpSimParties.first != 0 && tcpSimParties.second != 0) seatIsTpp = false;
		}
		// for now don't estimate for confirmed non-classic tcp
		if (!seatIsTpp) continue;
		// Establish which AEC parties actually best represent the TPP here
		int partyOneParty = -1;
		int partyTwoParty = -1;
		if (seat.name == "Barker") {
			PA_LOG_VAR(seat.name);
			PA_LOG_VAR(seat.tcpVotes);
			PA_LOG_VAR(seat.fpVotes);
		}
		// First option - if we have an actual 2cp count then go with that
		for (auto [partyId, votes] : seat.tcpVotes) {
			if (aecPartyToSimParty[partyId] == 0) partyOneParty = partyId;
			if (aecPartyToSimParty[partyId] == 1 || aecPartyToSimParty[partyId] == -4) partyTwoParty = partyId;
			if (seat.name == "Barker") {
				PA_LOG_VAR(partyId);
				PA_LOG_VAR(aecPartyToSimParty[partyId]);
			}
		}
		// Otherwise, find coalition candidate with highest fp vote
		if (partyTwoParty == -1) {
			int partyTwoVotes = 0;
			for (auto [candidateId, votes] : seat.fpVotes) {
				int voteCount = seat.totalVotesFpCandidate(candidateId);
				if (seat.name == "Barker") {
					PA_LOG_VAR(candidateId);
					PA_LOG_VAR(votes);
					PA_LOG_VAR(currentElection.candidates[candidateId].party);
					PA_LOG_VAR(aecPartyToSimParty[currentElection.candidates[candidateId].party]);
				}
				if (aecPartyToSimParty[currentElection.candidates[candidateId].party] == 0) partyOneParty = currentElection.candidates[candidateId].party;
				if (aecPartyToSimParty[currentElection.candidates[candidateId].party] == 1 ||
					aecPartyToSimParty[currentElection.candidates[candidateId].party] == -4) {
					if (voteCount > partyTwoVotes) {
						partyTwoParty = currentElection.candidates[candidateId].party;
						partyTwoVotes = voteCount;
					}
				}
			}
		}
		if (seat.name == "Barker") {
			PA_LOG_VAR(partyOneParty);
			PA_LOG_VAR(partyTwoParty);
		}
		int coalitionMain = aecPartyToSimParty[partyTwoParty];
		int coalitionPartner = -3 - coalitionMain;
		// Got through each booth and estimate tpp based on booth votes
		for (int boothId : seat.booths) {
			auto& booth = currentElection.booths[boothId];
			if (!booth.totalVotesFp()) continue;
			float partyOnePrefs = 0.0f;
			for (auto const& [candidateId, percent] : booth.fpPercent) {
				int aecParty = currentElection.candidates[candidateId].party;
				int partyIndex = aecPartyToSimParty[aecParty];
				float prefFlow = 50.0f;
				if (partyIndex == coalitionPartner) prefFlow = 20.0f;
				else if (partyIndex == coalitionMain) prefFlow = 0.0f;
				else if (updatedPreferenceFlows.contains(aecParty)) prefFlow = updatedPreferenceFlows.at(aecParty);
				else if (partyIndex < 0) prefFlow = project.parties().getOthersPreferenceFlow();
				else prefFlow = project.parties().viewByIndex(partyIndex).p1PreferenceFlow;
				partyOnePrefs += percent * prefFlow * 0.01f;
			}
			booth.tcpEstimate[partyOneParty] = partyOnePrefs;
			booth.tcpEstimate[partyTwoParty] = 100.0f - partyOnePrefs;
		}
	}
}

void SimulationPreparation::calculateBoothFpSwings()
{
	// fp swings
	for (auto& [id, currentBooth] : currentElection.booths) {
		if (!currentBooth.fpVotes.size()) continue;
		if (previousElection.booths.contains(id)) {
			auto const& previousBooth = previousElection.booths.at(id);
			int currentTotal = currentBooth.totalVotesFp();
			if (!currentTotal) continue;
			int previousTotal = previousBooth.totalVotesFp();
			if (!previousTotal) continue;
			for (auto [candidateId, votes] : currentBooth.fpVotes) {
				int currentParty = currentElection.candidates[candidateId].party;
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				bool matchFound = false;
				for (auto [prevCandidateId, prevVotes] : previousBooth.fpVotes) {
					if (currentParty == Results2::Candidate::Independent) {
						if (currentElection.candidates[candidateId].name == previousElection.candidates[prevCandidateId].name) {
							float previousPercent = float(prevVotes) * 100.0f / float(previousTotal);
							currentBooth.fpSwing[candidateId] = currentPercent - previousPercent;
							currentBooth.fpTransformedSwing[candidateId] = cappedTransformedSwing(previousPercent, currentPercent, BoothTransformCap);
							currentBooth.fpPercent[candidateId] = currentPercent;
							matchFound = true;
						}
					}
					else {
						int previousParty = previousElection.candidates[prevCandidateId].party;
						bool shortCodeMatch = previousElection.parties[previousParty].shortCode ==
							currentElection.parties[currentParty].shortCode;
						bool nameMatch = previousElection.parties[previousParty].name ==
							currentElection.parties[currentParty].name;
						bool groupingMatch = partyCodeGroupings.contains(previousElection.parties[previousParty].shortCode)
							&& partyCodeGroupings.contains(currentElection.parties[currentParty].shortCode) &&
							partyCodeGroupings[previousElection.parties[previousParty].shortCode] ==
							partyCodeGroupings[currentElection.parties[currentParty].shortCode];
						bool idMatch = previousParty == currentParty;
						bool partyMatch = shortCodeMatch || nameMatch || groupingMatch || idMatch;
						if (partyMatch) {
							float previousPercent = float(prevVotes) * 100.0f / float(previousTotal);
							currentBooth.fpSwing[candidateId] = currentPercent - previousPercent;
							currentBooth.fpTransformedSwing[candidateId] = cappedTransformedSwing(previousPercent, currentPercent, BoothTransformCap);
							currentBooth.fpPercent[candidateId] = currentPercent;
							matchFound = true;
							break;
						}
					}
				}
				if (!matchFound) {
					currentBooth.fpSwing[candidateId] = NaN;
					currentBooth.fpTransformedSwing[candidateId] = NaN;
					currentBooth.fpPercent[candidateId] = currentPercent;
				}
			}
		}
		else {
			int currentTotal = currentBooth.totalVotesFp();
			if (!currentTotal) continue;
			for (auto [candidateId, votes] : currentBooth.fpVotes) {
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				currentBooth.fpSwing[candidateId] = NaN;
				currentBooth.fpTransformedSwing[candidateId] = NaN;
				currentBooth.fpPercent[candidateId] = currentPercent;
			}
		}
	}
}

void SimulationPreparation::calculateBoothTcpSwings()
{
	// tcp swings
	for (auto& [id, currentBooth] : currentElection.booths) {
		if (!currentBooth.tcpVotes.size()) continue;
		int previousTotal = 0;
		int currentTotal = currentBooth.totalVotesTcp();
		if (currentTotal) {
			bool matched = false;
			Results2::Booth const* previousBooth = nullptr;
			if (previousElection.booths.contains(id)) {
				matched = true;
				previousBooth = &previousElection.booths.at(id);
				for (auto [affiliation, _] : currentBooth.tcpVotes) {
					if (!previousBooth->tcpVotes.contains(affiliation)) {
						matched = false;
						break;
					}
				}
				previousTotal = previousBooth->totalVotesTcp();
			}
			for (auto [affiliation, votes] : currentBooth.tcpVotes) {
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				if (matched && previousTotal && previousBooth) {
					float previousPercent = float(previousBooth->tcpVotes.at(affiliation)) * 100.0f / float(previousTotal);
					currentBooth.tcpSwing[affiliation] = currentPercent - previousPercent;
				}
				else {
					currentBooth.tcpSwing[affiliation] = NaN;
				}
				currentBooth.tcpPercent[affiliation] = currentPercent;
			}
		}
		else if (currentBooth.tcpEstimate.size()) {
			Results2::Booth const* previousBooth = nullptr;
			bool matched = false;
			if (previousElection.booths.contains(id)) {
				matched = true;
				previousBooth = &previousElection.booths.at(id);
				for (auto [affiliation, _] : currentBooth.tcpEstimate) {
					if (!previousBooth->tcpVotes.contains(affiliation)) {
						matched = false;
						break;
					}
				}
				previousTotal = previousBooth->totalVotesTcp();
			}
			for (auto [affiliation, currentPercent] : currentBooth.tcpEstimate) {
				if (matched && previousTotal && previousBooth) {
					float previousPercent = float(previousBooth->tcpVotes.at(affiliation)) * 100.0f / float(previousTotal);
					currentBooth.tcpEstimateSwing[affiliation] = currentPercent - previousPercent;
				}
				else {
					currentBooth.tcpEstimateSwing[affiliation] = NaN;
				}
			}
		}
	}
}

void SimulationPreparation::calculateCountProgress()
{
	for (auto& [seatId, seat] : currentElection.seats) {
		seat.fpProgress = float(seat.totalVotesFp()) * 100.0f / float(seat.enrolment);
		seat.tcpProgress = float(seat.totalVotesTcp()) * 100.0f / float(seat.enrolment);

		int totalFpBoothVotes = std::accumulate(seat.booths.begin(), seat.booths.end(), 0,
			[&](int acc, decltype(seat.booths)::value_type val) {
				auto currentBooth = currentElection.booths[val];
				if (!currentBooth.fpSwing.size()) return acc;
				return acc + currentElection.booths[val].totalVotesFp();
			});
		int totalFpSwingVotes = totalFpBoothVotes + seat.totalVotesFp(Results2::VoteType::Ordinary);
		seat.fpSwingProgress = float(totalFpSwingVotes) * 100.0f / float(seat.enrolment);

		int totalTcpBoothVotes = std::accumulate(seat.booths.begin(), seat.booths.end(), 0,
			[&](int acc, decltype(seat.booths)::value_type val) {
				auto currentBooth = currentElection.booths[val];
				if (!currentBooth.tcpSwing.size()) return acc;
				return acc + currentElection.booths[val].totalVotesTcp();
			});
		int totalTcpSwingVotes = totalTcpBoothVotes + seat.totalVotesTcp(Results2::VoteType::Ordinary);
		seat.tcpSwingProgress = float(totalTcpSwingVotes) * 100.0f / float(seat.enrolment);
	}
}

void SimulationPreparation::calculateSeatSwings()
{
	// fp swings
	for (auto& [seatId, seat] : currentElection.seats) {
		std::unordered_map<int, double> weightedSwing;
		std::unordered_map<int, double> weightedTransformedSwing;
		std::unordered_map<int, double> weightedPercent;
		std::unordered_map<int, double> weightSwingSum;
		std::unordered_map<int, double> weightPercentSum;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			for (auto [candidate, swing] : booth.fpSwing) {
				double total = double(booth.totalVotesFp());
				weightedPercent[candidate] += double(booth.fpPercent.at(candidate)) * total;
				weightPercentSum[candidate] += total;
				if (std::isnan(swing)) continue;
				weightedSwing[candidate] += double(swing) * total;
				weightedTransformedSwing[candidate] += double(booth.fpTransformedSwing.at(candidate)) * total;
				weightSwingSum[candidate] += total;
			}
		}
		for (auto [party, percent] : weightedPercent) {
			seat.fpPercent[party] = float(percent / weightPercentSum[party]);
			if (!weightedSwing.contains(party) || !weightSwingSum[party]) {
				seat.fpSwing[party] = NaN;
				seat.fpTransformedSwing[party] = NaN;
			}
			else {
				seat.fpSwing[party] = float(weightedSwing[party] / weightSwingSum[party]);
				seat.fpTransformedSwing[party] = float(weightedTransformedSwing[party] / weightSwingSum[party]);
			}
		}
	}

	// tcp swings
	for (auto& [seatId, seat] : currentElection.seats) {
		PA_LOG_VAR(seat.name);
		std::unordered_map<int, double> weightedSwing;
		std::unordered_map<int, double> weightedPercent;
		std::unordered_map<int, double> weightSwingSum;
		std::unordered_map<int, double> weightPercentSum;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			PA_LOG_VAR(booth.name);
			if (booth.tcpPercent.size()) {
				PA_LOG_VAR(booth.tcpPercent);
				for (auto [party, percent] : booth.tcpPercent) {
					weightedPercent[party] += double(booth.tcpPercent.at(party)) * double(booth.totalVotesTcp());
					weightPercentSum[party] += double(booth.totalVotesTcp());
					if (!booth.tcpSwing.contains(party)) continue;
					if (std::isnan(booth.tcpSwing.at(party))) continue;
					weightedSwing[party] += double(booth.tcpSwing.at(party)) * double(booth.totalVotesTcp());
					weightSwingSum[party] += double(booth.totalVotesTcp());
				}
			}
			else if (booth.tcpEstimate.size()) {
				PA_LOG_VAR(booth.tcpPercent);
				PA_LOG_VAR(booth.tcpEstimate);
				PA_LOG_VAR(booth.tcpEstimateSwing);
				for (auto [party, percent] : booth.tcpEstimate) {
					weightedPercent[party] += double(booth.tcpEstimate.at(party)) * double(booth.totalVotesFp()) * 0.5;
					weightPercentSum[party] += double(booth.totalVotesFp()) * 0.5;
					if (!booth.tcpEstimateSwing.contains(party)) continue;
					if (std::isnan(booth.tcpEstimateSwing.at(party))) continue;
					weightedSwing[party] += double(booth.tcpEstimateSwing.at(party)) * double(booth.totalVotesFp()) * 0.5;
					weightSwingSum[party] += double(booth.totalVotesFp()) * 0.5;
				}
			}
			logger << "Done with booth\n";
		}
		bool coalitionPartyPresent = false;
		if (!weightedPercent.size()) {
			seat.isTpp = simPartyIsTpp(project.seats().viewByIndex(aecSeatToSimSeat[seatId]).incumbent);
		}
		for (auto [party, percent] : weightedPercent) {
			int simParty = aecPartyToSimParty[party];
			if (!simPartyIsTpp(simParty)) seat.isTpp = false;
			if (simParty == 1 || simParty == -4) {
				if (coalitionPartyPresent) seat.isTpp = false;
				else coalitionPartyPresent = true;
			}
			seat.tcpSwing[party] = float(weightedSwing[party] / weightSwingSum[party]);
			seat.tcpPercent[party] = float(weightedPercent[party] / weightPercentSum[party]);
		}
		// 0.5f factor accounts for the fact the weightSwingSum is increased twice per booth, so needs to be halved here
		seat.tcpSwingBasis = weightSwingSum.size() ? weightSwingSum.begin()->second * 100.0f / float(seat.enrolment) : 0.0f;
		logger << "Done with seat\n";
	}
	for (auto const& [id, seat] : currentElection.seats) {
		logger << "Seat: " << seat.name << "\n";
		logger << " Fp progress: " << seat.fpProgress << "\n";
		logger << " Tcp progress: " << seat.tcpProgress << "\n";
		logger << " Tcp swing progress: " << seat.tcpSwingProgress << "\n";
		logger << " Tcp swing basis: " << seat.tcpSwingBasis << "\n";
		logger << " Seat is TPP: " << seat.isTpp << "\n";
		if (seat.tcpPercent.size()) {
			logger << " Tcp votes: \n";
			for (auto [party, percent] : seat.tcpPercent) {
				logger << "  " << currentElection.parties.at(party).name <<
					": " << percent << "%, (" << formatFloat(seat.tcpSwing.at(party), 2, true) << "%)\n";
			}
		}
		if (seat.fpSwing.size()) {
			logger << " Fp votes: \n";
			for (auto [candidate, swing] : seat.fpSwing) {
				logger << "  " << currentElection.candidates.at(candidate).name <<
					" (" << currentElection.parties[currentElection.candidates.at(candidate).party].name <<
					"): " << seat.fpPercent.at(candidate) << "% (" << formatFloat(swing, 2, true) << ") (transformed " << 
					formatFloat(seat.fpTransformedSwing.at(candidate), 2, true) << ")\n";
			}
		}
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths.at(boothId);
			if (booth.fpPercent.size() || booth.tcpPercent.size()) {
				logger << " Booth: " << currentElection.booths.at(boothId).name << " - " << booth.totalVotesFp() << " fp votes\n";
			}
			if (booth.tcpPercent.size()) {
				logger << "  Tcp swings: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [party, percent] : booth.tcpPercent) {
					logger << "   Party: " << currentElection.parties.at(party).name <<
						": " << percent << "%, (" << formatFloat(booth.tcpSwing.at(party), 2, true) << "%)\n";
				}
			}
			else if (booth.tcpEstimate.size()) {
				logger << "  Tcp estimates: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [party, percent] : booth.tcpEstimate) {
					logger << "   Party: " << currentElection.parties.at(party).name <<
						": " << percent << "%, (" << formatFloat(getAt(booth.tcpEstimateSwing, party, NaN), 2, true) << "%)\n";
				}
			}
			if (booth.fpSwing.size()) {
				logger << "  Fp votes: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [candidate, swing] : booth.fpSwing) {
					logger << "   " << currentElection.candidates.at(candidate).name <<
						" (" << currentElection.parties[currentElection.candidates.at(candidate).party].name <<
						"): " << booth.fpPercent.at(candidate) << "% (" << formatFloat(swing, 2, true) << ") (transformed " <<
						formatFloat(booth.fpTransformedSwing.at(candidate), 2, true) << ")\n";
				}
			}
		}
	}
}

void SimulationPreparation::determinePartyIdConversions()
{
	for (auto const& [_, aecParty] : currentElection.parties) {
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			auto const& simParty = project.parties().viewByIndex(partyIndex);
			if (contains(simParty.officialCodes, aecParty.shortCode)) {
				if (partyIndex == 1 && aecParty.shortCode[0] == 'N') {
					aecPartyToSimParty[aecParty.id] = CoalitionPartnerIndex;
				}
				else {
					aecPartyToSimParty[aecParty.id] = partyIndex;
				}
				break;
			}
		}
		if (!aecPartyToSimParty.contains(aecParty.id)) {
			logger << "No party conversion found for " << aecParty.name << " (" << aecParty.shortCode << ") - check this is ok\n";
			aecPartyToSimParty[aecParty.id] = -1;
		}
	}
}

void SimulationPreparation::determineSeatIdConversions()
{
	for (auto const& [_, aecSeat] : currentElection.seats) {
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			auto const& simSeat = project.seats().viewByIndex(seatIndex);
			if (simSeat.name == aecSeat.name) {
				aecSeatToSimSeat[aecSeat.id] = seatIndex;
				break;
			}
		}
		if (!aecSeatToSimSeat.contains(aecSeat.id)) {
			logger << "No seat conversion found for " << aecSeat.name << " - needs to be fixed\n";
			aecSeatToSimSeat[aecSeat.id] = -1;
		}
	}
}

void SimulationPreparation::prepareLiveTppSwings()
{
	for (auto const& [id, seat] : currentElection.seats) {
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatTppSwing[seatIndex] = NaN;
		if (seat.tcpSwing.size() != 2) continue;
		if (!seat.isTpp) continue;
		for (auto [party, swing] : seat.tcpSwing) {
			if (aecPartyToSimParty[party] == 0) {
				run.liveSeatTppSwing[seatIndex] = swing;
				run.liveSeatTcpCounted[seatIndex] = seat.tcpSwingProgress;
				run.liveSeatTcpBasis[seatIndex] = seat.tcpSwingBasis;
				int seatId = project.seats().indexToId(seatIndex);
				project.outcomes().add(Outcome(seatId, swing, seat.tcpSwingProgress, 0, 0));
				break;
			}
		}
	}
}

void SimulationPreparation::prepareLiveTcpSwings()
{
	for (auto const& [id, seat] : currentElection.seats) {
		if (seat.tcpPercent.size() != 2) continue;
		if (seat.isTpp) continue;
		auto firstCandidate = seat.tcpSwing.begin();
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatTcpParties[seatIndex] = { aecPartyToSimParty[firstCandidate->first], aecPartyToSimParty[std::next(firstCandidate)->first] };
		run.liveSeatTcpSwing[seatIndex] = firstCandidate->second;
		run.liveSeatTcpPercent[seatIndex] = seat.tcpPercent.at(firstCandidate->first);
		run.liveSeatTcpCounted[seatIndex] = seat.tcpSwingProgress;
		run.liveSeatTcpBasis[seatIndex] = seat.tcpSwingBasis;
	}
	PA_LOG_VAR(run.liveSeatTcpParties);
	PA_LOG_VAR(run.liveSeatTcpSwing);
	PA_LOG_VAR(run.liveSeatTcpPercent);
	PA_LOG_VAR(run.liveSeatTcpCounted);
}

void SimulationPreparation::prepareLiveFpSwings()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatFpCounted[seatIndex] = seat.fpSwingProgress;
		float coalitionMainPercent = 0.0f;
		float coalitionPartnerPercent = 0.0f;
		std::priority_queue<std::tuple<float, float, float>> indFps; // percent, swing, transformed
		for (auto [candidate, swing] : seat.fpSwing) {
			int partyIndex = aecPartyToSimParty[currentElection.candidates[candidate].party];
			// in this section handle only parties that are unambiguously representable
			if (partyIndex == OthersIndex) {
				run.liveSeatFpSwing[seatIndex][partyIndex] += seat.fpSwing.at(candidate);
				run.liveSeatFpTransformedSwing[seatIndex][partyIndex] += seat.fpTransformedSwing.at(candidate);
				run.liveSeatFpPercent[seatIndex][partyIndex] += seat.fpPercent.at(candidate);
			}
			else if (partyIndex == run.indPartyIndex) {
				indFps.push({seat.fpPercent.at(candidate), swing, seat.fpTransformedSwing.at(candidate) });
			}
			else {
				if (partyIndex == CoalitionPartnerIndex) coalitionPartnerPercent = seat.fpPercent.at(candidate);
				if (partyIndex == 1) coalitionMainPercent = seat.fpPercent.at(candidate);
				run.liveSeatFpSwing[seatIndex][partyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][partyIndex] += seat.fpTransformedSwing.at(candidate);
				run.liveSeatFpPercent[seatIndex][partyIndex] += seat.fpPercent.at(candidate);
			}
		}
		if (indFps.size()) {
			auto highestInd = indFps.top();
			float voteShare = std::get<0>(highestInd);
			float swing = std::get<1>(highestInd);
			float transformedSwing = std::get<2>(highestInd);
			if (swing < voteShare - 0.1f) { // implies candidate is matched
				run.liveSeatFpSwing[seatIndex][run.indPartyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][run.indPartyIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][run.indPartyIndex] = voteShare;
			}
			else if (voteShare > 8.0f) {
				run.liveSeatFpSwing[seatIndex][run.indPartyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][run.indPartyIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][run.indPartyIndex] = voteShare;
			}
			else {
				run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
				run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
				run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			}
			indFps.pop();
		}
		if (indFps.size()) {
			auto secondInd = indFps.top();
			float voteShare = std::get<0>(secondInd);
			float swing = std::get<1>(secondInd);
			float transformedSwing = std::get<2>(secondInd);
			if (voteShare > 8.0f) {
				run.liveSeatFpSwing[seatIndex][EmergingIndIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][EmergingIndIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][EmergingIndIndex] = voteShare;
			}
			else {
				run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
				run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
				run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			}
			indFps.pop();
		}
		while (indFps.size()) {
			auto otherInd = indFps.top();
			float voteShare = std::get<0>(otherInd);
			float swing = std::get<1>(otherInd);
			float transformedSwing = std::get<2>(otherInd);
			run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
			run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
			run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			indFps.pop();
		}
		// All this code ensures that (a) the main Coalition candidate (with highest current primary vote)
		// in the seat is in index 1 and represented as a swing (or vote share only if didn't contest last time)
		// (b) the coalition partner (with lower current primary vote), if any,
		// is in index CoalitionPartnerIndex (-4) and represented as a raw vote share.
		if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex) &&
			!run.liveSeatFpSwing[seatIndex].contains(1)) {
			run.liveSeatFpSwing[seatIndex][1] = run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpTransformedSwing[seatIndex][1] = run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpPercent[seatIndex][1] = run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpSwing[seatIndex].erase(CoalitionPartnerIndex);
			run.liveSeatFpTransformedSwing[seatIndex].erase(CoalitionPartnerIndex);
			run.liveSeatFpPercent[seatIndex].erase(CoalitionPartnerIndex);
		}
		if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex) &&
			run.liveSeatFpSwing[seatIndex].contains(1) && 
			coalitionPartnerPercent > coalitionMainPercent) {
			run.liveSeatFpSwing[seatIndex][1] = run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpTransformedSwing[seatIndex][1] = run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpPercent[seatIndex][1] = run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex] = coalitionMainPercent;
		}
		else if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex)) {
			run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex] = coalitionPartnerPercent;
		}
		PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
		PA_LOG_VAR(run.liveSeatFpSwing[seatIndex]);
		PA_LOG_VAR(run.liveSeatFpTransformedSwing[seatIndex]);
		PA_LOG_VAR(run.liveSeatFpPercent[seatIndex]);
	}
}

void SimulationPreparation::updateLiveAggregateForSeat(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (!seat.isClassic2pp()) return;
	++run.classicSeatCount;
	int regionIndex = project.regions().idToIndex(seat.region);
	float percentCounted = run.liveSeatTcpCounted[seatIndex];
	if (!std::isnan(run.liveSeatTppSwing[seatIndex])) {
		float weightedSwing = run.liveSeatTppSwing[seatIndex] * percentCounted;
		run.liveOverallSwing += weightedSwing;
		run.liveRegionSwing[regionIndex] += weightedSwing;
	}
	run.liveOverallPercent += percentCounted;
	run.liveRegionPercentCounted[regionIndex] += percentCounted;
	++run.liveRegionClassicSeatCount[regionIndex];
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
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		run.liveRegionSwing[regionIndex] /= run.liveRegionPercentCounted[regionIndex];
		run.liveRegionPercentCounted[regionIndex] /= run.liveRegionClassicSeatCount[regionIndex];
	}
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

void SimulationPreparation::determineIndependentPartyIndex()
{
	run.indPartyIndex = project.parties().indexByShortCode("IND");
	if (run.indPartyIndex == -1) run.indPartyIndex = EmergingIndIndex;
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
				results.prevOthers += std::min(voteShare, detransformVoteShare(run.indEmergence.fpThreshold));
			}
		}
		results.prevOthers = std::max(2.0f, results.prevOthers);
		for (auto& [party, voteCount] : results.fpVoteCount) {
			results.turnoutCount += voteCount;
		}
		run.totalPreviousTurnout += results.turnoutCount;
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
	std::string fileName = "analysis/Regional/" + getTermCode() + "-regions-base.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
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
	std::string fileName = "analysis/Regional/" + getTermCode() + "-regions-polled.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
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
	std::string fileName = "analysis/Regional/" + getTermCode() + "-mix-regions.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
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
	std::string fileName = "analysis/Regional/" + getTermCode() + "-mix-parameters.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
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
		std::string subRegion = values[2];
		int regionId = project.regions().findbyAnalysisCode(subRegion).first;
		if (regionId == Region::InvalidId) continue;
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

void SimulationPreparation::initializeGeneralLiveData()
{
	run.liveSeatTppSwing.resize(project.seats().count());
	run.liveSeatTcpCounted.resize(project.seats().count());
	run.liveSeatFpSwing.resize(project.seats().count());
	run.liveSeatFpTransformedSwing.resize(project.seats().count());
	run.liveSeatFpPercent.resize(project.seats().count());
	run.liveSeatFpCounted.resize(project.seats().count());
	run.liveSeatTcpParties.resize(project.seats().count());
	run.liveSeatTcpSwing.resize(project.seats().count());
	run.liveSeatTcpPercent.resize(project.seats().count());
	run.liveSeatTcpBasis.resize(project.seats().count());
	run.liveRegionSwing.resize(project.regions().count());
	run.liveRegionPercentCounted.resize(project.regions().count());
	run.liveRegionClassicSeatCount.resize(project.regions().count());
}

std::string SimulationPreparation::getTermCode()
{
	return run.yearCode + run.regionCode;
}
