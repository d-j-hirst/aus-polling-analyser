#include "SimulationIteration.h"

#include "CountProgress.h"
#include "SpecialPartyCodes.h"
#include "PollingProject.h"
#include "RandomGenerator.h"
#include "Simulation.h"
#include "SimulationRun.h"

// Note: A large amount of code in this file is commented out as the "previous results"
// was updated to a new (better) format but the "latest results" was not. Further architectural
// improvement, including removing cached election results from project seat data, cannot be
// properly done unless this is fixed, and the fixing is decidedly non-trivial. In order to
// expedite the initial web release, which does not require live election updating, these have
// been disabled and code producing errors commented out and replaced with stubs,
// until the project is prepared to work on restoring the live results.

using Mp = Simulation::MajorParty;

static std::random_device rd;
static std::mt19937 gen;
static std::mutex recordMutex;

RandomGenerator rng;

// Threshold at which longshot-bias correction starts being applied for seats being approximated from betting odds
constexpr float LongshotOddsThreshold = 2.5f;

constexpr float ProminentMinorFlatBonus = 5.0f;
constexpr float ProminentMinorFlatBonusThreshold = 10.0f;
constexpr float ProminentMinorBonusMax = 35.0f;

// How strongly preferences align with ideology based on the "consistency" property of a party
constexpr std::array<float, 3> PreferenceConsistencyBase = { 1.2f, 1.4f, 1.8f };

bool isMajor(int partyIndex) {
	return partyIndex == Mp::One || partyIndex == Mp::Two;
}

SimulationIteration::SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void SimulationIteration::runIteration()
{
	loadPastSeatResults();
	initialiseIterationSpecificCounts();
	determineOverallBehaviour();
	decideMinorPartyPopulism();
	determineHomeRegions();
	// determinePpvcBias();
	determineRegionalSwings();
	determineMinorPartyContests();
	determineSeatInitialResults();

	reconcileSeatAndOverallFp();

	seatTcpVoteShare.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatFinalResult(seatIndex);
	}

	assignDirectWins();
	assignCountAsPartyWins();
	assignSupportsPartyWins();

	std::lock_guard<std::mutex> lock(recordMutex);
	recordIterationResults();
}

void SimulationIteration::initialiseIterationSpecificCounts()
{
	partyOneNewTppMargin = std::vector<float>(project.seats().count(), 0.0f);
	seatWinner = std::vector<Party::Id>(project.seats().count(), Party::InvalidId);
}

void SimulationIteration::determineOverallBehaviour()
{
	// First, randomly determine the national swing for this particular simulation
	auto projectedSample = project.projections().view(sim.settings.baseProjection).generateSupportSample(project.models());
	daysToElection = projectedSample.daysToElection;
	iterationOverallTpp = projectedSample.voteShare.at(TppCode);
	iterationOverallSwing = iterationOverallTpp - sim.settings.prevElection2pp;

	for (auto const& [sampleKey, partySample] : projectedSample.voteShare) {
		if (sampleKey == UnnamedOthersCode) {
			overallFpTarget[OthersIndex] = partySample;
			continue;
		}
		if (sampleKey == EmergingOthersCode) {
			overallFpTarget[EmergingPartyIndex] = partySample;
			continue;
		}
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				overallFpTarget[partyIndex] = partySample;
				break;
			}
		}
	}

	for (auto const& [sampleKey, preferenceFlow] : projectedSample.preferenceFlow) {
		if (sampleKey == UnnamedOthersCode) {
			previousPreferenceFlow[OthersIndex] = project.parties().getOthersPreferenceFlow();
			previousPreferenceFlow[EmergingIndIndex] = project.parties().getOthersPreferenceFlow();
			overallPreferenceFlow[OthersIndex] = preferenceFlow;
			overallPreferenceFlow[EmergingIndIndex] = preferenceFlow;
			continue;
		}
		if (sampleKey == EmergingOthersCode) {
			previousPreferenceFlow[EmergingPartyIndex] = project.parties().getOthersPreferenceFlow();
			overallPreferenceFlow[EmergingPartyIndex] = preferenceFlow;
			continue;
		}
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				previousPreferenceFlow[partyIndex] = project.parties().view(id).p1PreferenceFlow;
				overallPreferenceFlow[partyIndex] = preferenceFlow;
				break;
			}
		}
	}

	// Give any party without a sampled preference flow (e.g. Independents) a preference flow relative to generic others
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		if (!previousPreferenceFlow.contains(partyIndex)) {
			float pastPreferenceFlow = project.parties().viewByIndex(partyIndex).p1PreferenceFlow;
			previousPreferenceFlow[partyIndex] = pastPreferenceFlow;
			overallPreferenceFlow[partyIndex] = overallPreferenceFlow[OthersIndex] + pastPreferenceFlow - project.parties().getOthersPreferenceFlow();
		}
	}

	for (auto const& [partyIndex, partyFp] : overallFpTarget) {
		if (run.previousFpVoteShare.contains(partyIndex)) {
			overallFpSwing[partyIndex] = partyFp - run.previousFpVoteShare[partyIndex];
		}
		else {
			overallFpSwing[partyIndex] = 0.0f;
		}
	}

	if (sim.isLive() && run.liveOverallPercent) {
		float liveSwing = run.liveOverallSwing;
		float liveStdDev = stdDevOverall(run.liveOverallPercent);
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev) * run.sampleRepresentativeness;
		iterationOverallSwing = (iterationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
		iterationOverallTpp = iterationOverallSwing + sim.settings.prevElection2pp;
	}
}

void SimulationIteration::decideMinorPartyPopulism()
{
	for (auto partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		partyIdeologies[partyIndex] = project.parties().viewByIndex(partyIndex).ideology;
		partyConsistencies[partyIndex] = project.parties().viewByIndex(partyIndex).consistency;
	}
	for (auto const& [partyIndex, voteShare] : overallFpTarget) {
		if (partyIndex == EmergingPartyIndex) {
			float populism = std::clamp(rng.uniform(-1.0f, 2.0f), 0.0f, 1.0f);
			centristPopulistFactor[partyIndex] = populism;
			partyIdeologies[partyIndex] = (populism > 0.5f ? 4 : populism < 0.1f ? 2 : 3);
			partyConsistencies[partyIndex] = 0;
			continue;
		}
		else if (partyIndex < 0) {
			partyIdeologies[partyIndex] = 2;
			partyConsistencies[partyIndex] = 0;
			continue;
		}
		if (partyIdeologies[partyIndex] >= 3) { // center-right or strong right wing
			centristPopulistFactor[partyIndex] = 1.0f;
		}
		else {
			centristPopulistFactor[partyIndex] = 0.0f;
		}
	}
	partyIdeologies[EmergingIndIndex] = 2;
	partyConsistencies[EmergingIndIndex] = 0;
}

void SimulationIteration::determineHomeRegions()
{
	for (auto const& [id, party] : project.parties()) {
		int partyIndex = project.parties().idToIndex(id);
		if (party.homeRegion.size()) {
			auto foundRegion = project.regions().findbyName(party.homeRegion);
			if (foundRegion.second) {
				homeRegion[partyIndex] = project.regions().idToIndex(foundRegion.first);
				continue;
			}
		}
		homeRegion[partyIndex] = -1;
	}
	// 0.75f is Subjective guesstimate, too little data to calculate the
	// chance emerging parties will have a home state
	if (rng.uniform() < 0.75f) {
		homeRegion[EmergingPartyIndex] = rng.uniform_int(0, project.regions().count());
	}
	else {
		homeRegion[EmergingPartyIndex] = -1;
	}
}

void SimulationIteration::determineRegionalSwings()
{
	const int numRegions = project.regions().count();
	regionSwing.resize(numRegions);
	for (int regionIndex = 0; regionIndex < numRegions; ++regionIndex) {
		determineBaseRegionalSwing(regionIndex);
		modifyLiveRegionalSwing(regionIndex);
	}
	correctRegionalSwings();
}

void SimulationIteration::determineMinorPartyContests()
{
	for (auto& [partyIndex, voteShare] : overallFpTarget) {
		if (!(partyIndex >= 2 || partyIndex == EmergingPartyIndex)) continue;
		typedef std::pair<int, float> Priority;
		std::vector<Priority> seatPriorities;
		std::vector<float> seatMods(project.seats().count());
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			float seatMod = calculateEffectiveSeatModifier(seatIndex, partyIndex);
			seatMods[seatIndex] = seatMod;
			float priority = rng.flexibleDist(seatMod, seatMod * 0.2f, seatMod * 0.6f, 3.0f, 6.0f);
			seatPriorities.push_back({ seatIndex, priority });
			// Make sure this seat is able to be contested later on for this party
			pastSeatResults[seatPriorities[seatIndex].first].fpVotePercent.insert({ partyIndex, 0.0f });
		}


		int seatsKnown = 0;
		seatContested[partyIndex] = std::vector<bool>(project.seats().count());
		if (partyIndex >= 0) {
			fpModificationAdjustment[partyIndex] = 0.0f;
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Party const& party = project.parties().viewByIndex(partyIndex);
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				if (contains(seat.runningParties, party.abbreviation)) {
					++seatsKnown;
					seatContested[partyIndex][seatIndex] = true;
					fpModificationAdjustment[partyIndex] += seatMods[seatIndex];
				}
			}
		}

		if (!seatsKnown) {
			float estimatedSeats = 0.0f;
			float totalSeats = float(project.seats().count());
			if (partyIndex >= 0) {
				Party const& party = project.parties().viewByIndex(partyIndex);
				float HalfSeatsPrimary = 5.0f;
				float seatProp = 2.0f - 2.0f * std::pow(2.0f, -voteShare / HalfSeatsPrimary);
				// The seat total should adjust somewhat by the expected primary vote, but not entirely.
				// The entered seat total corresponds to expected seat contests at 5% primary vote
				estimatedSeats = std::clamp(party.seatTarget * seatProp, 0.0f, totalSeats);
			}
			else if (partyIndex == -3) {
				float HalfSeatsPrimary = 4.0f;
				float seatProp = 1.0f - 1.0f * std::pow(2.0f, -voteShare / HalfSeatsPrimary);
				estimatedSeats = totalSeats * seatProp;
			}
			else {
				logger << "Error: tried to determine minor party contest rate for a party category that hasn't been accounted for";
			}
			float lowerRmse = estimatedSeats * 0.3f;
			float upperRmse = std::min((totalSeats - estimatedSeats) * 1.0f, estimatedSeats);
			int actualSeats = int(floor(std::clamp(rng.flexibleDist(estimatedSeats, lowerRmse, upperRmse), std::max(7.0f, estimatedSeats * 0.4f), totalSeats)));
			std::nth_element(seatPriorities.begin(), std::next(seatPriorities.begin(), actualSeats), seatPriorities.end(),
				[](Priority const& a, Priority const& b) {return a.second > b.second; });

			fpModificationAdjustment[partyIndex] = 0.0f;
			for (int seatPlace = 0; seatPlace < actualSeats; ++seatPlace) {
				seatContested[partyIndex][seatPriorities[seatPlace].first] = true;
				fpModificationAdjustment[partyIndex] += seatMods[seatPriorities[seatPlace].first];
			}
			fpModificationAdjustment[partyIndex] = totalSeats / fpModificationAdjustment[partyIndex];
		}
		else {
			fpModificationAdjustment[partyIndex] = seatsKnown / fpModificationAdjustment[partyIndex];
		}
	}
}

void SimulationIteration::loadPastSeatResults()
{
	// Make a copy of these because we'll be changing them
	// during the analysis (e.g. by converting minor parties
	// to "others" in some cases) and we need to avoid
	// conflicting with other threads
	pastSeatResults = run.pastSeatResults;
}

void SimulationIteration::determineBaseRegionalSwing(int regionIndex)
{
	Region const& thisRegion = project.regions().viewByIndex(regionIndex);
	float overallSwingCoeff = run.regionBaseBehaviour[regionIndex].overallSwingCoeff;
	float baseSwingDeviation = run.regionBaseBehaviour[regionIndex].baseSwingDeviation;
	float medianNaiveSwing = overallSwingCoeff * iterationOverallSwing + baseSwingDeviation;
	float swingToTransform = 0.0f;
	if (run.regionPollBehaviour.contains(regionIndex)) {
		float pollRawDeviation = thisRegion.swingDeviation;
		float pollCoeff = run.regionPollBehaviour[regionIndex].overallSwingCoeff;
		float pollIntercept = run.regionPollBehaviour[regionIndex].baseSwingDeviation;
		float pollMedianDeviation = pollCoeff * pollRawDeviation + pollIntercept;
		float naiveDeviation = medianNaiveSwing - iterationOverallSwing;
		float mixCoeff = run.regionMixParameters.mixFactorA;
		float mixTimeFactor = run.regionMixParameters.mixFactorB;
		float quartersToElection = daysToElection / 91.315f;
		float mixFactor = mixCoeff * exp(-mixTimeFactor * quartersToElection);
		float mixedDeviation = mix(naiveDeviation, pollMedianDeviation, mixFactor);
		mixedDeviation -= run.regionMixBehaviour[regionIndex].bias;
		float rmseCoeff = run.regionMixParameters.rmseA;
		float rmseTimeFactor = run.regionMixParameters.rmseB;
		float rmseAsymptote = run.regionMixParameters.rmseC;
		float generalRmse = rmseCoeff * exp(-rmseTimeFactor * quartersToElection) + rmseAsymptote;
		float regionRmseMod = run.regionMixBehaviour[regionIndex].rmse;
		float specificRmse = generalRmse * regionRmseMod;
		float kurtosisCoeff = run.regionMixParameters.kurtosisA;
		float kurtosisIntercept = run.regionMixParameters.kurtosisB;
		float kurtosis = kurtosisCoeff * quartersToElection + kurtosisIntercept;
		float randomVariation = rng.flexibleDist(0.0f, specificRmse, specificRmse, kurtosis, kurtosis);
		float totalDeviation = mixedDeviation + randomVariation;
		swingToTransform = iterationOverallSwing + totalDeviation;
	}
	else {
		// Naive swing - the swing we get without any region polling information
		float rmse = run.regionBaseBehaviour[regionIndex].rmse;
		float kurtosis = run.regionBaseBehaviour[regionIndex].kurtosis;
		float randomVariation = rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis);
		float naiveSwing = medianNaiveSwing + randomVariation;
		swingToTransform = naiveSwing;
	}

	float transformedTpp = transformVoteShare(thisRegion.lastElection2pp) + swingToTransform;
	float detransformedTpp = detransformVoteShare(transformedTpp);
	regionSwing[regionIndex] = detransformedTpp - thisRegion.lastElection2pp;
}

void SimulationIteration::modifyLiveRegionalSwing(int regionIndex)
{
	regionIndex;
	//Region const& thisRegion = project.regions().viewByIndex(regionIndex);
	//if (sim.isLive() && thisRegion.livePercentCounted) {
	//	float liveSwing = thisRegion.liveSwing;
	//	float liveStdDev = stdDevSingleSeat(thisRegion.livePercentCounted);
	//	liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	//	float priorWeight = 0.5f;
	//	float liveWeight = 1.0f / (liveStdDev * liveStdDev);
	//	regionSwing[regionIndex] = (regionSwing[regionIndex] * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	//}
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

void SimulationIteration::determineSeatInitialResults()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatTpp(seatIndex);
	}
	correctSeatTppSwings();
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatInitialFp(seatIndex);
	}
}

void SimulationIteration::determineSeatTpp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	float tppPrev = seat.tppMargin + 50.0f;
	float transformedTpp = transformVoteShare(tppPrev);
	float elasticity = run.seatParameters[seatIndex].elasticity;
	// float trend = run.seatParameters[seatIndex].trend;
	float volatility = run.seatParameters[seatIndex].volatility;
	bool useVolatility = run.seatParameters[seatIndex].loaded;
	transformedTpp += regionSwing[project.regions().idToIndex(seat.region)] * elasticity;
	// Add modifiers for known local effects
	transformedTpp += run.seatPartyOneTppModifier[seatIndex];
	// Remove the average local modifier across the region
	transformedTpp -= run.regionLocalModifierAverage[seat.region];
	transformedTpp += run.seatPreviousTppSwing[seatIndex] * run.tppSwingFactors.previousSwingModifier;
	float swingDeviation = run.tppSwingFactors.meanSwingDeviation;
	if (run.regionCode == "fed") swingDeviation += run.tppSwingFactors.federalModifier;
	if (useVolatility) swingDeviation = 0.75f * volatility + 0.25f * swingDeviation;
	float kurtosis = run.tppSwingFactors.swingKurtosis;
	//if (seat.name == "Menzies") {
	//	PA_LOG_VAR(tppPrev);
	//	PA_LOG_VAR(transformedTpp);
	//	PA_LOG_VAR(elasticity);
	//	PA_LOG_VAR(volatility);
	//	PA_LOG_VAR(useVolatility);
	//	PA_LOG_VAR(transformedTpp);
	//	PA_LOG_VAR(regionSwing);
	//	PA_LOG_VAR(seat.region);
	//	PA_LOG_VAR(project.regions().idToIndex(seat.region));
	//	PA_LOG_VAR(iterationOverallTpp);
	//	PA_LOG_VAR(regionSwing[project.regions().idToIndex(seat.region)]);
	//	static double vicSwing = 0.0;
	//	vicSwing += regionSwing[project.regions().idToIndex(seat.region)];
	//	static double vicSwingCount = double(regionSwing[project.regions().idToIndex(seat.region)]);
	//	vicSwingCount += 1.0;
	//	double avgVicSwing = vicSwing / vicSwingCount;
	//	PA_LOG_VAR(avgVicSwing);
	//	PA_LOG_VAR(run.seatPartyOneTppModifier[seatIndex]);
	//	PA_LOG_VAR(run.regionLocalModifierAverage[seat.region]);
	//	PA_LOG_VAR(run.seatPreviousTppSwing[seatIndex] * run.tppSwingFactors.previousSwingModifier);
	//	PA_LOG_VAR(swingDeviation);
	//	PA_LOG_VAR(kurtosis);
	//}
	//if (seat.name == "Hasluck") {
	//	PA_LOG_VAR(tppPrev);
	//	PA_LOG_VAR(transformedTpp);
	//	PA_LOG_VAR(elasticity);
	//	PA_LOG_VAR(volatility);
	//	PA_LOG_VAR(useVolatility);
	//	PA_LOG_VAR(transformedTpp);
	//	PA_LOG_VAR(regionSwing);
	//	PA_LOG_VAR(seat.region);
	//	PA_LOG_VAR(project.regions().idToIndex(seat.region));
	//	PA_LOG_VAR(iterationOverallTpp);
	//	PA_LOG_VAR(regionSwing[project.regions().idToIndex(seat.region)]);
	//	static double waSwing = 0.0;
	//	waSwing += regionSwing[project.regions().idToIndex(seat.region)];
	//	static double waSwingCount = double(regionSwing[project.regions().idToIndex(seat.region)]);
	//	waSwingCount += 1.0;
	//	double avgWaSwing = waSwing / waSwingCount;
	//	PA_LOG_VAR(avgWaSwing);
	//	PA_LOG_VAR(run.seatPartyOneTppModifier[seatIndex]);
	//	PA_LOG_VAR(run.regionLocalModifierAverage[seat.region]);
	//	PA_LOG_VAR(run.seatPreviousTppSwing[seatIndex] * run.tppSwingFactors.previousSwingModifier);
	//	PA_LOG_VAR(swingDeviation);
	//	PA_LOG_VAR(kurtosis);
	//}
	// Add random noise to the new margin of this seat
	transformedTpp += rng.flexibleDist(0.0f, swingDeviation, swingDeviation, kurtosis, kurtosis);
	if (sim.isLiveManual() && run.liveSeatPcCounted[seatIndex] > 0.0f) {
		float tppLive = (tppPrev + run.liveSeatTppSwing[seatIndex] > 10.0f ?
			tppPrev + run.liveSeatTppSwing[seatIndex] :
			predictorCorrectorTransformedSwing(tppPrev, run.liveSeatTppSwing[seatIndex]));
		float liveTransformedTpp = transformVoteShare(tppLive);
		float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatPcCounted[seatIndex] * 0.2f)));
		liveTransformedTpp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		float liveFactor = 1.0f - pow(2.0f, -run.liveSeatPcCounted[seatIndex] * 0.2f);
		float mixedTransformedTpp = mix(transformedTpp, liveTransformedTpp, liveFactor);
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(mixedTransformedTpp) - 50.0f;
	}
	else {
		// Margin for this simulation is finalised, record it for later averaging
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
	}
}

void SimulationIteration::correctSeatTppSwings()
{
	if (sim.isLive()) return;
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		int regionId = project.regions().indexToId(regionIndex);
		// Make sure that the sum of seat TPPs is actually equal to the samples' overall TPP.
		double totalSwing = 0.0;
		double totalTurnout = 0.0;
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			Seat const& seat = project.seats().viewByIndex(seatIndex);
			if (seat.region != regionId) continue;
			double turnout = double(run.pastSeatResults[seatIndex].turnoutCount);
			double turnoutScaledSwing = double(partyOneNewTppMargin[seatIndex] - seat.tppMargin) * turnout;
			totalSwing += turnoutScaledSwing;
			totalTurnout += turnout;
		}
		// Theoretically this could cause the margin to fall outside valid bounds, but
		// practically it's not close to ever happening so save a little computation time
		// not bothering to check
		double averageSwing = totalSwing / totalTurnout;
		float swingAdjust = regionSwing[regionIndex] - float(averageSwing);
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			Seat const& seat = project.seats().viewByIndex(seatIndex);
			if (seat.region != regionId) continue;
			partyOneNewTppMargin[seatIndex] += swingAdjust;
		}
	}
}

void SimulationIteration::determineSeatInitialFp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	seatFpVoteShare.resize(project.seats().count());
	auto tempPastResults = pastSeatResults[seatIndex].fpVotePercent;
	for (auto [partyIndex, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (partyIndex != 0 && partyIndex != 1 && seat.tcpChange.size()) {
			// reduce incumbent fp by the tcp-change vs challenger (to account for redistributions)
			if (partyIndex == project.parties().idToIndex(seat.incumbent) &&
				seat.tcpChange.contains(project.parties().view(seat.challenger).abbreviation)) {
				voteShare = predictorCorrectorTransformedSwing(voteShare, seat.tcpChange.at(project.parties().view(seat.challenger).abbreviation));
			}
			else if (partyIndex == project.parties().idToIndex(seat.challenger) &&
				seat.tcpChange.contains(project.parties().view(seat.challenger).abbreviation)) {
				voteShare = predictorCorrectorTransformedSwing(voteShare, -seat.tcpChange.at(project.parties().view(seat.challenger).abbreviation));
			}
		}
		bool effectiveGreen = partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("GRN"));
		if (!overallFpSwing.contains(partyIndex)) effectiveGreen = false;
		bool effectiveIndependent = partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("IND"));
		if (!overallFpSwing.contains(partyIndex)) effectiveIndependent = true;
		if (voteShare < run.indEmergence.fpThreshold) effectiveIndependent = false;
		bool effectivePopulist = partyIndex >= Mp::Others && !effectiveGreen && !effectiveIndependent &&
			overallFpSwing.contains(partyIndex);
		if (effectiveGreen) {
			determineSpecificPartyFp(seatIndex, partyIndex, voteShare, run.greensSeatStatistics);
		}
		else if (effectiveIndependent) {
			determineSpecificPartyFp(seatIndex, partyIndex, voteShare, run.indSeatStatistics);
		}
		else if (effectivePopulist) {
			determinePopulistFp(seatIndex, partyIndex, voteShare);
		}
		else if (partyIndex >= Mp::Others) {
			// For non-major candidates that don't fit into the above categories,
			// convert their past votes into "Others" votes
			tempPastResults[OthersIndex] += tempPastResults[partyIndex];
			tempPastResults.erase(partyIndex);
			continue;
		}
		// Note: this means major party vote shares get passed on as-is
		seatFpVoteShare[seatIndex][partyIndex] = voteShare;
	}

	pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = tempPastResults[OthersIndex];
	determineSeatEmergingParties(seatIndex);
	if (seat.confirmedProminentIndependent) determineSeatConfirmedInds(seatIndex);
	determineSeatEmergingInds(seatIndex);
	determineSeatOthers(seatIndex);

	// Helps to effect minor party crowding, i.e. if too many minor parties
	// rise in their fp vote, then they're all reduced a bit more than if only one rose.
	prepareFpsForNormalisation(seatIndex);
	normaliseSeatFp(seatIndex);
	preferenceVariation.clear();
	allocateMajorPartyFp(seatIndex);

}

void SimulationIteration::determineSpecificPartyFp(int seatIndex, int partyIndex, float& voteShare, SimulationRun::SeatStatistics const seatStatistics) {
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (seat.runningParties.size() && partyIndex >= Mp::Others &&
		!contains(seat.runningParties, project.parties().viewByIndex(partyIndex).abbreviation)) {
		voteShare = 0.0f;
		return;
	}
	if (seat.runningParties.size() && partyIndex == run.indPartyIndex && !seat.previousIndRunning && !seat.incumbentRecontestConfirmed) {
		voteShare = 0.0f;
		return;
	}
	if (seat.runningParties.size() && partyIndex == OthersIndex &&
		!contains(seat.runningParties, OthersCode)) {
		voteShare = 0.0f;
		return;
	}
	float transformedFp = transformVoteShare(voteShare);
	float seatStatisticsExact = (std::clamp(transformedFp, seatStatistics.scaleLow, seatStatistics.scaleHigh)
		- seatStatistics.scaleLow) / seatStatistics.scaleStep;
	int seatStatisticsLower = int(std::floor(seatStatisticsExact));
	float seatStatisticsMix = seatStatisticsExact - float(seatStatisticsLower);
	using StatType = SimulationRun::SeatStatistics::TrendType;
	// ternary operator in second argument prevents accessing beyong the end of the array when at the upper end of the scale
	auto getMixedStat = [&](StatType statType) {
		return mix(seatStatistics.trend[int(statType)][seatStatisticsLower],
			(seatStatisticsMix ? seatStatistics.trend[int(statType)][seatStatisticsLower + 1] : 0.0f),
			seatStatisticsMix);
	};
	float recontestRateMixed = getMixedStat(StatType::RecontestRate);
	float recontestIncumbentRateMixed = getMixedStat(StatType::RecontestIncumbentRate);
	float timeToElectionFactor = std::clamp(1.78f - 0.26f * log(float(daysToElection)), 0.0f, 1.0f);
	if (seat.incumbent == partyIndex) {
		recontestRateMixed += recontestIncumbentRateMixed;
		// rough, un-empirical estimate of higher recontest rates with less time before election
		recontestRateMixed += (1.0f - recontestRateMixed) * timeToElectionFactor;
		// independents who have publically confirmed they are recontesting are given a much higher chance of
		// actually running, but not 100% as unforeseen events may occur
		if (seat.incumbentRecontestConfirmed) recontestRateMixed = 0.9f + 0.1f * recontestRateMixed;
	}
	else if (partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("IND"))) {
		// non-incumbent independents have a reverse effect: less likely to recontest as time passes
		recontestRateMixed *= 1.0f - timeToElectionFactor;
	}
	if (seat.runningParties.size() && partyIndex >= Mp::Others &&
		contains(seat.runningParties, project.parties().viewByIndex(partyIndex).abbreviation)) {
		recontestRateMixed = 1.0f;
	}
	if (seat.runningParties.size() && partyIndex == OthersIndex &&
		contains(seat.runningParties, OthersCode)) {
		recontestRateMixed = 1.0f;
	}
	// also, should some day handle retirements for minor parties that would be expected to stay competitive
	if (rng.uniform() > recontestRateMixed || (seat.retirement && partyIndex == seat.incumbent)) {
		voteShare = 0.0f;
		return;
	}
	float swingMultiplierMixed = getMixedStat(StatType::SwingCoefficient);
	float sophomoreMixed = getMixedStat(StatType::SophomoreCoefficient);
	float offsetMixed = getMixedStat(StatType::Offset);
	float lowerRmseMixed = getMixedStat(StatType::LowerRmse);
	float upperRmseMixed = getMixedStat(StatType::UpperRmse);
	float lowerKurtosisMixed = getMixedStat(StatType::LowerKurtosis);
	float upperKurtosisMixed = getMixedStat(StatType::UpperKurtosis);
	if (overallFpSwing.contains(partyIndex)) {
		transformedFp += swingMultiplierMixed * overallFpSwing[partyIndex];
	}
	transformedFp += offsetMixed;
	if (seat.sophomoreCandidate && project.parties().idToIndex(seat.incumbent) == partyIndex) {
		transformedFp += sophomoreMixed;
	}
	if (seat.prominentMinors.size() && partyIndex >= Mp::Others && contains(seat.prominentMinors, project.parties().viewByIndex(partyIndex).abbreviation)) {
		transformedFp += rng.uniform(0.0f, 15.0f);
	}
	transformedFp += rng.flexibleDist(0.0f, lowerRmseMixed, upperRmseMixed, lowerKurtosisMixed, upperKurtosisMixed);

	float regularVoteShare = detransformVoteShare(transformedFp);

	if (seat.prominentMinors.size() && partyIndex >= Mp::Others && contains(seat.prominentMinors, project.parties().viewByIndex(partyIndex).abbreviation)) {
		regularVoteShare += (1.0f - std::clamp(regularVoteShare / ProminentMinorFlatBonusThreshold, 0.0f, 1.0f)) * ProminentMinorFlatBonus;
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare, rng.uniform() * rng.uniform() * ProminentMinorBonusMax);
	}

	voteShare = regularVoteShare;
}

void SimulationIteration::determinePopulistFp(int seatIndex, int partyIndex, float& voteShare)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (seat.runningParties.size() && partyIndex >= Mp::Others &&
		!contains(seat.runningParties, project.parties().viewByIndex(partyIndex).abbreviation)) {
		voteShare = 0.0f;
		return;
	}
	bool prominent = seat.prominentMinors.size() && partyIndex >= Mp::Others && contains(seat.prominentMinors, project.parties().viewByIndex(partyIndex).abbreviation);
	float partyFp = overallFpTarget[partyIndex];
	if (partyFp == 0.0f) {
		voteShare = 0.0f;
		return;
	}

	if (seatContested.contains(partyIndex)) {
		if (!seatContested[partyIndex][seatIndex] && !prominent) {
			voteShare = 0.0f;
			return;
		}
	}

	float seatModifier = calculateEffectiveSeatModifier(seatIndex, partyIndex);
	float adjustedModifier = seatModifier * fpModificationAdjustment[partyIndex];
	float modifiedFp1 = predictorCorrectorTransformedSwing(partyFp, partyFp * (adjustedModifier - 1.0f));
	float modifiedFp2 = partyFp * adjustedModifier;
	// Choosing the lower of these two values prevents the fp from being >= 100.0f in some scenarios
	float modifiedFp = std::min(modifiedFp1, modifiedFp2);
	float transformedFp = transformVoteShare(modifiedFp);

	float populism = centristPopulistFactor[partyIndex];
	float lowerRmse = mix(run.centristStatistics.lowerRmse, run.populistStatistics.lowerRmse, populism);
	float upperRmse = mix(run.centristStatistics.upperRmse, run.populistStatistics.upperRmse, populism);
	float lowerKurtosis = mix(run.centristStatistics.lowerKurtosis, run.populistStatistics.lowerKurtosis, populism);
	float upperKurtosis = mix(run.centristStatistics.upperKurtosis, run.populistStatistics.upperKurtosis, populism);

	transformedFp += rng.flexibleDist(0.0f, lowerRmse, upperRmse, lowerKurtosis, upperKurtosis);

	float regularVoteShare = detransformVoteShare(transformedFp);

	if (prominent) {
		regularVoteShare += (1.0f - std::clamp(regularVoteShare / ProminentMinorFlatBonusThreshold, 0.0f, 1.0f)) * ProminentMinorFlatBonus;
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare, rng.uniform() * rng.uniform() * ProminentMinorBonusMax);
	}

	//if (seat.name == "Bradfield" && partyIndex == 7) {
	//	PA_LOG_VAR(seat.name);
	//	PA_LOG_VAR(regularVoteShare);
	//	PA_LOG_VAR(transformedFp);
	//}

	voteShare = regularVoteShare;
}

void SimulationIteration::determineSeatConfirmedInds(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	bool ballotConfirmed = contains(seat.runningParties, project.parties().viewByIndex(run.indPartyIndex).abbreviation);
	if (seat.runningParties.size() && !ballotConfirmed) {
		return;
	}
	if (!seat.confirmedProminentIndependent) return;
	float indContestRate = run.indEmergence.baseRate;
	bool isFederal = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode().substr(4) == "fed";
	if (isFederal) indContestRate += run.indEmergence.fedRateMod;
	typedef SimulationRun::SeatType ST;
	bool isRural = run.seatTypes[seatIndex] == ST::Rural;
	bool isProvincial = run.seatTypes[seatIndex] == ST::Provincial;
	bool isOuterMetro = run.seatTypes[seatIndex] == ST::OuterMetro;
	if (isRural) indContestRate += run.indEmergence.ruralRateMod;
	if (isProvincial) indContestRate += run.indEmergence.provincialRateMod;
	if (isOuterMetro) indContestRate += run.indEmergence.outerMetroRateMod;
	float prevOthers = pastSeatResults[seatIndex].prevOthers;
	indContestRate += run.indEmergence.prevOthersRateMod * prevOthers;
	indContestRate = 0.9f + 0.1f * indContestRate;
	if (ballotConfirmed) indContestRate = 1.0f;
	if (rng.uniform<float>() < std::max(0.01f, indContestRate)) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - run.indEmergence.fpThreshold;
		if (isFederal) rmse *= (1.0f + run.indEmergence.fedVoteCoeff / interceptSize);
		if (isRural) rmse *= (1.0f + run.indEmergence.ruralVoteCoeff / interceptSize);
		if (isProvincial) rmse *= (1.0f + run.indEmergence.provincialVoteCoeff / interceptSize);
		if (isOuterMetro) rmse *= (1.0f + run.indEmergence.outerMetroVoteCoeff / interceptSize);
		float prevOthersCoeff = run.indEmergence.prevOthersVoteCoeff * prevOthers;
		rmse *= (1.0f + prevOthersCoeff / interceptSize);
		rmse = (rmse * 0.5f + run.indEmergence.voteRmse * 0.5f) * 1.2f;
		float transformedVoteShare = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis)) + run.indEmergence.fpThreshold;
		if (run.seatBettingOdds[seatIndex].contains(run.indPartyIndex)) {
			// Exact values of odds above $15 don't generally mean much, so cap them at this level
			constexpr float OddsCap = 15.0f;
			float cappedOdds = std::min(run.seatBettingOdds[seatIndex][run.indPartyIndex], OddsCap);
			// the last part of this line compensates for the typical bookmaker's margin
			float impliedChance = 1.0f / (cappedOdds * (2.0f / 1.85f));
			// significant adjustment downwards to adjust for longshot bias.
			// this number isn't really treated as a probability from here on so it's ok for
			// it to become negative.
			if (impliedChance < 0.4f) impliedChance -= 1.3f * (0.4f - impliedChance);
			const float pivot = transformVoteShare(32.0f); // fp vote expected for 50% chance of winning
			constexpr float range = 42.0f;
			float voteShareCenter = pivot + range * (impliedChance - 0.5f);
			constexpr float variation = 20.0f;
			float transformedBettingFp = rng.normal(voteShareCenter, variation);
			transformedVoteShare = mix(transformedVoteShare, transformedBettingFp, 0.7f);
		}
		if (run.seatPolls[seatIndex].contains(run.indPartyIndex)) {
			float weightedSum = 0.0f;
			float sumOfWeights = 0.0f;
			for (auto poll : run.seatPolls[seatIndex][run.indPartyIndex]) {
				constexpr float QualityWeightBase = 0.6f;
				float weight = myPow(QualityWeightBase, poll.second);
				weightedSum += poll.first * weight;
				sumOfWeights += weight;
			}
			float transformedPollFp = transformVoteShare(std::clamp(weightedSum / sumOfWeights, 0.1f, 99.9f));
			constexpr float MaxPollWeight = 0.8f;
			constexpr float PollWeightBase = 0.6f;
			float pollFactor = MaxPollWeight * (1.0f - std::powf(PollWeightBase, sumOfWeights));
			transformedVoteShare = mix(transformedVoteShare, transformedPollFp, pollFactor);
		}

		seatFpVoteShare[seatIndex][run.indPartyIndex] = std::max(seatFpVoteShare[seatIndex][run.indPartyIndex], detransformVoteShare(transformedVoteShare));
	}
}

void SimulationIteration::determineSeatEmergingInds(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	bool ballotConfirmed = contains(seat.runningParties, project.parties().viewByIndex(run.indPartyIndex).abbreviation);
	if (seat.runningParties.size() && !ballotConfirmed) {
		return;
	}
	float indEmergenceRate = run.indEmergence.baseRate;
	bool isFederal = project.projections().view(sim.settings.baseProjection).getBaseModel(project.models()).getTermCode().substr(4) == "fed";
	if (isFederal) indEmergenceRate += run.indEmergence.fedRateMod;
	typedef SimulationRun::SeatType ST;
	bool isRural = run.seatTypes[seatIndex] == ST::Rural;
	bool isProvincial = run.seatTypes[seatIndex] == ST::Provincial;
	bool isOuterMetro = run.seatTypes[seatIndex] == ST::OuterMetro;
	if (isRural) indEmergenceRate += run.indEmergence.ruralRateMod;
	if (isProvincial) indEmergenceRate += run.indEmergence.provincialRateMod;
	if (isOuterMetro) indEmergenceRate += run.indEmergence.outerMetroRateMod;
	float prevOthers = pastSeatResults[seatIndex].prevOthers;
	indEmergenceRate += run.indEmergence.prevOthersRateMod * prevOthers;
	// Less chance of independents emerging when there's already a strong candidate
	if (seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) indEmergenceRate *= 0.3f;
	if (rng.uniform<float>() < std::max(0.01f, indEmergenceRate)) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - run.indEmergence.fpThreshold;
		if (isFederal) rmse *= (1.0f + run.indEmergence.fedVoteCoeff / interceptSize);
		if (isRural) rmse *= (1.0f + run.indEmergence.ruralVoteCoeff / interceptSize);
		if (isProvincial) rmse *= (1.0f + run.indEmergence.provincialVoteCoeff / interceptSize);
		if (isOuterMetro) rmse *= (1.0f + run.indEmergence.outerMetroVoteCoeff / interceptSize);
		float prevOthersCoeff = run.indEmergence.prevOthersVoteCoeff * prevOthers;
		rmse *= (1.0f + prevOthersCoeff / interceptSize);
		float transformedVoteShare = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis)) + run.indEmergence.fpThreshold;
		seatFpVoteShare[seatIndex][EmergingIndIndex] = detransformVoteShare(transformedVoteShare);
	}
}

void SimulationIteration::determineSeatOthers(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (seat.runningParties.size() && !contains(seat.runningParties, OthersCode)) {
		return;
	}
	constexpr float MinPreviousOthFp = 2.0f;
	float voteShare = MinPreviousOthFp;
	if (pastSeatResults[seatIndex].fpVotePercent.contains(OthersIndex)) {
		voteShare = std::max(voteShare, pastSeatResults[seatIndex].fpVotePercent[OthersIndex]);
	}
	determineSpecificPartyFp(seatIndex, OthersIndex, voteShare, run.othSeatStatistics);
	seatFpVoteShare[seatIndex][OthersIndex] = voteShare;
}

void SimulationIteration::prepareFpsForNormalisation(int seatIndex)
{
	float maxPrevious = 0.0f;
	for (auto& [party, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (!isMajor(party) && voteShare > maxPrevious) maxPrevious = voteShare;
	}
	float maxCurrent = 0.0f;
	for (auto& [party, voteShare] : seatFpVoteShare[seatIndex]) {
		if (!isMajor(party) && voteShare > maxCurrent) maxCurrent = voteShare;
	}
	float diff = std::min(std::min(10.0f, seatFpVoteShare[seatIndex][0]), maxCurrent - maxPrevious);
	if (diff > 0.0f) {
		// The values for the majors (i.e. parties 0 and 1) are overwritten anyway,
		// so this only has the effect of softening the normalisation.
		// This ensures that the normalisation is only punishing to minor parties
		// when more than one rises in votes (thus crowding each other out)
		seatFpVoteShare[seatIndex][0] -= diff;
	}
}

void SimulationIteration::determineSeatEmergingParties(int seatIndex)
{
	float voteShare = 0.0f;
	determinePopulistFp(seatIndex, EmergingPartyIndex, voteShare);
	seatFpVoteShare[seatIndex][EmergingPartyIndex] = voteShare;
}

void SimulationIteration::allocateMajorPartyFp(int seatIndex)
{
	// This has four parts:
	//  1 - calculate previous-election preference allocation using overall preference flows,
	//      then determine this seat's deviation from that (per vote)
	//  2 - calculate the preference allocation for the current simulated fp votes
	//      using last-election preference flows, and adjust it according to (1)
	//  3 - Assign the remaining (major-party) vote shares to match the current simulated 2pp,
	//      with flattening as the party vote approaches 0/100%.
	//  4 - Normalise so that the total votes equal to 100%.

	Seat const& seat = project.seats().viewByIndex(seatIndex);
	// In general the ALP TPP will correspond to the entered seat margin
	// except when it's a LNP-incumbent classic 2pp seat,
	// in which case reverse it
	float partyOneCurrentTpp = 0.0f;
	partyOneCurrentTpp = 50.0f + partyOneNewTppMargin[seatIndex];
	float partyTwoCurrentTpp = 100.0f - partyOneCurrentTpp;

	constexpr float DefaultPartyTwoPrimary = 15.0f;
	float preferenceBias = 0.0f;
	float previousNonMajorFpShare = 0.0f;
	float previousPartyOneTpp = 0.0f;
	float previousPartyOneFp = pastSeatResults[seatIndex].fpVotePercent[Mp::One];
	// ternary operator for situtations where party two didn't previously run, e.g. Richmond (vic) 2018
	float previousPartyTwoFp = (pastSeatResults[seatIndex].fpVotePercent.contains(Mp::Two) ?
		pastSeatResults[seatIndex].fpVotePercent[Mp::Two] : DefaultPartyTwoPrimary);
	if (pastSeatResults[seatIndex].tcpVote.contains(Mp::One) && pastSeatResults[seatIndex].tcpVote.contains(Mp::Two)) {
		previousPartyOneTpp = pastSeatResults[seatIndex].tcpVote[Mp::One];
	}
	else {
		previousPartyOneTpp = 50.0f + seat.tppMargin;
	}

	float pastElectionPartyOnePrefEstimate = 0.0f;
	for (auto [partyIndex, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (!isMajor(partyIndex)) {
			// *** Probably want to introduce some randomness into these - they assume
			// an *exact* flow of preferences each time which is not realistic
			if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
				// Prominent independents and centrist candidates attract a lot
				// of tactical voting from the less-favoured major party, so it's not accurate
				// to give them the same share as the national vote
				// We allocate the first 5% of the vote at national rates, the next 10% at a sliding scale
				// up to the cap (see below), and the remained at the cap
				// The cap is defined as 80% at most, and also scales from 50% to 80% for the TPP side behind
				// by 0-5% at the previous two elections
				// Furthermore, due to subsequent research it is found that the ALP-preferencing share of IND fp
				// declines for >30%, according to formula given below for preferenceFlowCap
				float previousAverage = seat.tppMargin - seat.previousSwing * 0.5f;
				float preferenceFlowCap = (partyOneCurrentTpp <= 50.0f ?
					4.503f - 0.268f * voteShare + 1.344f * partyOneCurrentTpp
					: 100.f - (24.318f - 0.788f * voteShare + 1.0781f * (100.0f - partyOneCurrentTpp)));
				preferenceFlowCap = std::clamp(preferenceFlowCap, 5.0f, 95.0f);
				float upperPreferenceFlow = (previousAverage > 0.0f ? 
					std::max(50.0f + previousAverage * -6.0f, 100.0f - preferenceFlowCap) :
					std::min(50.0f + previousAverage * -6.0f, preferenceFlowCap));
				float basePreferenceFlow = previousPreferenceFlow[partyIndex];
				float transitionPreferenceFlow = mix(basePreferenceFlow, upperPreferenceFlow, std::min(voteShare - 5.0f, 10.0f) * 0.05f);
				float summedPreferenceFlow = basePreferenceFlow * std::min(voteShare, 5.0f) +
					transitionPreferenceFlow * std::clamp(voteShare - 5.0f, 0.0f, 10.0f) +
					upperPreferenceFlow * std::max(voteShare - 15.0f, 0.0f);
				float effectivePreferenceFlow = std::clamp(summedPreferenceFlow / voteShare, 1.0f, 99.0f);
				if (!preferenceVariation.contains(partyIndex)) {
					preferenceVariation[partyIndex] = rng.normal(0.0f, 15.0f);
				}
				float randomisedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
				pastElectionPartyOnePrefEstimate += voteShare * randomisedPreferenceFlow * 0.01f;
				//if (seat.name == "Shepparton") {
				//	logger << "past\n";
				//	PA_LOG_VAR(seat.name);
				//	PA_LOG_VAR(partyIndex);
				//	PA_LOG_VAR(seat.tppMargin);
				//	PA_LOG_VAR(seat.previousSwing);
				//	PA_LOG_VAR(previousAverage);
				//	PA_LOG_VAR(preferenceFlowCap);
				//	PA_LOG_VAR(partyOneCurrentTpp);
				//	PA_LOG_VAR(voteShare);
				//	PA_LOG_VAR(upperPreferenceFlow);
				//	PA_LOG_VAR(basePreferenceFlow);
				//	PA_LOG_VAR(transitionPreferenceFlow);
				//	PA_LOG_VAR(summedPreferenceFlow);
				//	PA_LOG_VAR(preferenceVariation);
				//	PA_LOG_VAR(effectivePreferenceFlow);
				//	PA_LOG_VAR(preferenceVariation[partyIndex]);
				//	PA_LOG_VAR(basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]));
				//	PA_LOG_VAR(randomisedPreferenceFlow);
				//}
			}
			else {
				pastElectionPartyOnePrefEstimate += voteShare * previousPreferenceFlow[partyIndex] * 0.01f;
			}
			previousNonMajorFpShare += voteShare;
		}
	}
	preferenceBias = previousPartyOneTpp - previousPartyOneFp - pastElectionPartyOnePrefEstimate;
	// Amount by which actual TPP is higher than estimated TPP, per 1% of the vote
	// If we don't have a previous TPP to go off, just leave it at zero - assume preferences are same as nation wide
	float preferenceBiasRate = (previousNonMajorFpShare > 0.0f ? preferenceBias / previousNonMajorFpShare : 0.0f);

	float currentPartyOnePrefs = 0.0f;
	float currentTotalPrefs = 0.0f;
	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (!isMajor(partyIndex)) {
			// *** Probably want to introduce some randomness into these - they assume
			// an *exact* flow of preferences each time which is not realistic
			if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
				// Prominent independents and centrist candidates attract a lot
				// of tactical voting from the less-favoured major party, so it's not accurate
				// to give them the same share as the national vote
				// We allocate the first 5% of the vote at national rates, the next 10% at a sliding scale
				// up to the cap (see below), and the remained at the cap
				// The cap is defined as 80% at most, and also scales from 50% to 80% for the TPP side behind
				// by 0-5% at the previous two elections
				// Furthermore, due to subsequent research it is found that the ALP-preferencing share of IND fp
				// declines for >30%, according to formula given below for preferenceFlowCap
				float previousAverage = seat.tppMargin - seat.previousSwing * 0.5f;
				// Estimated ALP preference rate from historical results based on IND fp and ALP TPP.
				float preferenceFlowCap = (partyOneCurrentTpp <= 50.0f ? 
					4.503f - 0.268f * voteShare + 1.344f * partyOneCurrentTpp
					: 100.f - (24.318f - 0.788f * voteShare + 1.0781f * (100.0f - partyOneCurrentTpp)));
				preferenceFlowCap = std::clamp(preferenceFlowCap, 5.0f, 95.0f);
				float upperPreferenceFlow = (previousAverage > 0.0f ?
					std::max(50.0f + previousAverage * -6.0f, 100.0f - preferenceFlowCap) :
					std::min(50.0f + previousAverage * -6.0f, preferenceFlowCap));
				float basePreferenceFlow = previousPreferenceFlow[partyIndex];
				float transitionPreferenceFlow = mix(basePreferenceFlow, upperPreferenceFlow, std::min(voteShare - 5.0f, 10.0f) * 0.05f);
				float summedPreferenceFlow = basePreferenceFlow * std::min(voteShare, 5.0f) +
					transitionPreferenceFlow * std::clamp(voteShare - 5.0f, 0.0f, 10.0f) +
					upperPreferenceFlow * std::max(voteShare - 15.0f, 0.0f);
				float effectivePreferenceFlow = std::clamp(summedPreferenceFlow / voteShare, 1.0f, 99.0f);
				effectivePreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
				float randomisedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
				currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f;
				//if (seat.name == "Mackellar" || seat.name == "Fowler" || seat.name == "Nicholls") {
				//	logger << "now\n";
				//	PA_LOG_VAR(seat.name);
				//	PA_LOG_VAR(partyIndex);
				//	PA_LOG_VAR(seat.tppMargin);
				//	PA_LOG_VAR(seat.previousSwing);
				//	PA_LOG_VAR(previousAverage);
				//	PA_LOG_VAR(preferenceFlowCap);
				//	PA_LOG_VAR(partyOneCurrentTpp);
				//	PA_LOG_VAR(voteShare);
				//	PA_LOG_VAR(upperPreferenceFlow);
				//	PA_LOG_VAR(basePreferenceFlow);
				//	PA_LOG_VAR(transitionPreferenceFlow);
				//	PA_LOG_VAR(summedPreferenceFlow);
				//	PA_LOG_VAR(effectivePreferenceFlow);
				//	PA_LOG_VAR(preferenceVariation[partyIndex]);
				//	PA_LOG_VAR(basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]));
				//	PA_LOG_VAR(randomisedPreferenceFlow);
				//}
			}
			else {
				currentPartyOnePrefs += voteShare * overallPreferenceFlow[partyIndex] * 0.01f;
			}
			currentTotalPrefs += voteShare;
		}
	}
	float biasAdjustedPartyOnePrefs = currentPartyOnePrefs + preferenceBiasRate * currentTotalPrefs;

	float overallAdjustedPartyOnePrefs = biasAdjustedPartyOnePrefs + prefCorrection * currentTotalPrefs;
	float overallAdjustedPartyTwoPrefs = currentTotalPrefs - overallAdjustedPartyOnePrefs;

	// this number can be below zero ...
	float partyOneEstimate = partyOneCurrentTpp - overallAdjustedPartyOnePrefs;
	// so we calculate a swing ...
	float partyOneSwing = partyOneEstimate - previousPartyOneFp;
	// and then use logistic transformation to make sure it is above zero
	float newPartyOneFp = (partyOneSwing < 0 ? predictorCorrectorTransformedSwing(previousPartyOneFp, partyOneSwing) : partyOneEstimate);
	float newPartyOneTpp = overallAdjustedPartyOnePrefs + newPartyOneFp;
	// this number can be below zero ...
	float partyTwoEstimate = partyTwoCurrentTpp - overallAdjustedPartyTwoPrefs;
	// so we calculate a swing ...
	float partyTwoSwing = partyTwoEstimate - previousPartyTwoFp;
	// and then use logistic transformation to make sure it is above zero
	float newPartyTwoFp = (partyTwoSwing < 0 ? predictorCorrectorTransformedSwing(previousPartyTwoFp, partyTwoSwing) : partyTwoEstimate);
	float newPartyTwoTpp = overallAdjustedPartyTwoPrefs + newPartyTwoFp;
	float totalTpp = newPartyOneTpp + newPartyTwoTpp;
	// Derivation of following formula (assuming ALP as first party):
	//  (alp_add + alp_sum) / (alp_add + fp_sum) = alp_tpp
	//	alp_add + alp_sum = alp_tpp * (alp_add + fp_sum)
	//	alp_add + alp_sum - alp_tpp * alp_add = alp_tpp * fp_sum
	//	alp_add(1 - alp_tpp) = alp_tpp * fp_sum - alp_sum
	//	alp_add = (alp_tpp * fp_sum - alp_sum) / (1 - alp_tpp)
	// If alp_add is below zero, then need to add LNP votes instead using equivalent formula
	float addPartyOneFp = (partyOneCurrentTpp * totalTpp * 0.01f - newPartyOneTpp) / (1.0f - partyOneCurrentTpp * 0.01f);
	if (addPartyOneFp >= 0.0f) {
		newPartyOneFp += addPartyOneFp;
	}
	else {
		float addPartyTwoFp = (partyTwoCurrentTpp * totalTpp * 0.01f - newPartyTwoTpp) / (100.0f - partyTwoCurrentTpp * 0.01f);
		newPartyTwoFp += addPartyTwoFp;
	}

	//if (seat.name == "Shepparton") {
	//	PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//	PA_LOG_VAR(partyOneCurrentTpp);
	//	PA_LOG_VAR(partyTwoCurrentTpp);
	//	PA_LOG_VAR(seatFpVoteShare[seatIndex]);
	//	PA_LOG_VAR(currentTotalPrefs);
	//	PA_LOG_VAR(previousNonMajorFpShare);
	//	PA_LOG_VAR(previousPartyOneTpp);
	//	PA_LOG_VAR(previousPreferenceFlow);
	//	PA_LOG_VAR(previousPartyOneFp);
	//	PA_LOG_VAR(pastElectionPartyOnePrefEstimate);
	//	PA_LOG_VAR(preferenceBiasRate);
	//	PA_LOG_VAR(currentPartyOnePrefs);
	//	PA_LOG_VAR(currentTotalPrefs);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
	//	PA_LOG_VAR(partyOneEstimate);
	//	PA_LOG_VAR(partyTwoEstimate);
	//	PA_LOG_VAR(partyOneSwing);
	//	PA_LOG_VAR(partyTwoSwing);
	//	PA_LOG_VAR(predictorCorrectorTransformedSwing(previousPartyOneFp, partyOneSwing));
	//	PA_LOG_VAR(previousPartyOneFp);
	//	PA_LOG_VAR(previousPartyTwoFp);
	//	PA_LOG_VAR(newPartyOneTpp);
	//	PA_LOG_VAR(newPartyTwoTpp);
	//	PA_LOG_VAR(totalTpp);
	//	PA_LOG_VAR(addPartyOneFp);
	//	PA_LOG_VAR(partyOneEstimate);
	//	PA_LOG_VAR(partyTwoEstimate);
	//	PA_LOG_VAR(newPartyOneFp);
	//	PA_LOG_VAR(newPartyTwoFp);
	//}
	seatFpVoteShare[seatIndex][Mp::One] = newPartyOneFp;
	seatFpVoteShare[seatIndex][Mp::Two] = newPartyTwoFp;


	if (seat.incumbent >= Mp::Others && seatFpVoteShare[seatIndex][seat.incumbent]) {
		// Maintain constant fp vote for non-major incumbents
		normaliseSeatFp(seatIndex, seat.incumbent, seatFpVoteShare[seatIndex][seat.incumbent]);
	}
	else {
		normaliseSeatFp(seatIndex);
	}
}

void SimulationIteration::normaliseSeatFp(int seatIndex, int fixedParty, float fixedVote)
{
	float totalVoteShare = 0.0f;
	auto previousVoteShare = seatFpVoteShare[seatIndex];
	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (partyIndex == fixedParty) continue;
		totalVoteShare += voteShare;
	}
	float totalTarget = 100.0f - fixedVote;
	float correctionFactor = totalTarget / totalVoteShare;
	for (auto& [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (partyIndex == fixedParty) continue;
		seatFpVoteShare[seatIndex][partyIndex] *= correctionFactor;
	}
}

void SimulationIteration::reconcileSeatAndOverallFp()
{
	constexpr int MaxReconciliationCycles = 5;
	for (int i = 0; i < MaxReconciliationCycles; ++i) {
		calculateNewFpVoteTotals();
		if (overallFpError < 0.3f) break;

		if (i > 2) correctMajorPartyFpBias();
		if (i > 1) calculatePreferenceCorrections();
		if (i == MaxReconciliationCycles - 1) break;
		applyCorrectionsToSeatFps();
	}
}

void SimulationIteration::calculateNewFpVoteTotals()
{
	// Vote shares in each seat are converted to equivalent previous-election vote totals to ensure that
	// they reflect the turnout differences between seats (esp. Tasmanian seats)
	std::map<int, float> partyVoteCount;
	float totalVoteCount = 0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		int seatVoteCount = 0;
		for (auto [partyIndex, voteCount] : pastSeatResults[seatIndex].fpVoteCount) {
			seatVoteCount += voteCount;
		}
		for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
			float voteCount = voteShare * float(seatVoteCount) * 0.01f;
			totalVoteCount += voteCount;
			partyVoteCount[partyIndex] += voteCount;
		}
	}
	tempOverallFp.clear();
	for (auto [partyIndex, voteCount] : partyVoteCount) {
		float fp = voteCount / totalVoteCount * 100.0f;
		if (partyIndex != OthersIndex && overallFpTarget.contains(partyIndex)) {
			tempOverallFp[partyIndex] = fp;
		}
		else if (tempOverallFp.contains(OthersIndex)) {
			tempOverallFp[OthersIndex] += fp;
		}
		else {
			tempOverallFp[OthersIndex] = fp;
		}
	}
	overallFpError = 0.0f;
	nonMajorFpError = 0.0f;
	for (auto [partyIndex, voteCount] : tempOverallFp) {
		float error = abs(overallFpTarget[partyIndex] - voteCount);
		overallFpError += error;
		if (!isMajor(partyIndex)) nonMajorFpError += error;
	}
	float tempMicroOthers = float(partyVoteCount[OthersIndex]) / float(totalVoteCount) * 100.0f;
	float indOthers = tempOverallFp[OthersIndex] - tempMicroOthers;
	othersCorrectionFactor = (overallFpTarget[OthersIndex] - indOthers) / tempMicroOthers;
}

void SimulationIteration::calculatePreferenceCorrections()
{
	float estTppSeats = 0.0f;
	float totalPrefs = 0.0f;
	for (auto [partyIndex, prefFlow] : tempOverallFp) {
		estTppSeats += overallPreferenceFlow[partyIndex] * tempOverallFp[partyIndex] * 0.01f;
		if (!isMajor(partyIndex)) totalPrefs += tempOverallFp[partyIndex];
	}
	float prefError = estTppSeats - iterationOverallTpp;
	// Since, for each sample/seat reconciliation cycle, the previous pref correction is already built
	// into the preferences for each seat, so add the current bias from the present number
	// to make sure it continues on for the next cycle
	prefCorrection += prefError / totalPrefs;
}

void SimulationIteration::applyCorrectionsToSeatFps()
{
	auto oldVoteShares = seatFpVoteShare;
	for (auto [partyIndex, vote] : tempOverallFp) {
		if (partyIndex != OthersIndex) {
			if (isMajor(partyIndex)) continue;
			if (!tempOverallFp[partyIndex]) continue; // avoid division by zero when we have non-existent emerging others
			float correctionFactor = overallFpTarget[partyIndex] / tempOverallFp[partyIndex];
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				if (seatFpVoteShare[seatIndex].contains(partyIndex)) {
					// prevent outlier seats from getting monster swings
					float swingCap = std::max(0.0f, tempOverallFp[partyIndex] * (correctionFactor - 1.0f) * 3.0f);
					float correctionSwing = std::min(swingCap, seatFpVoteShare[seatIndex][partyIndex] * (correctionFactor - 1.0f));
					float newValue = predictorCorrectorTransformedSwing(seatFpVoteShare[seatIndex][partyIndex], correctionSwing);
					seatFpVoteShare[seatIndex][partyIndex] = newValue;
				}
			}
		}
		else {
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				float allocation = seatFpVoteShare[seatIndex][OthersIndex] * (othersCorrectionFactor - 1.0f);
				FloatByPartyIndex categories;
				float totalOthers = 0.0f;
				for (auto [seatPartyIndex, seatPartyVote] : seatFpVoteShare[seatIndex]) {
					if (seatPartyIndex == OthersIndex || !overallFpTarget.contains(seatPartyIndex)) {
						categories[seatPartyIndex] = seatPartyVote;
						totalOthers += seatPartyVote;
					}
				}
				if (!totalOthers) continue;
				for (auto& [seatPartyIndex, voteShare] : categories) {
					float additionalVotes = allocation * voteShare / totalOthers;
					float newValue = predictorCorrectorTransformedSwing(seatFpVoteShare[seatIndex][seatPartyIndex], additionalVotes);
					seatFpVoteShare[seatIndex][seatPartyIndex] = newValue;
				}
			}
		}
	}
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		allocateMajorPartyFp(seatIndex);
	}
}

void SimulationIteration::correctMajorPartyFpBias()
{
	float majorFpCurrent = tempOverallFp[Mp::One] + tempOverallFp[Mp::Two];
	float majorFpTarget = overallFpTarget[Mp::One] + overallFpTarget[Mp::Two];
	// This formula calculates the adjustment needed for the current fp to reach the target fp *after normalisation*
	float adjustmentFactor = (majorFpTarget * (majorFpCurrent - 100.0f)) / (majorFpCurrent * (majorFpTarget - 100.0f));
	float totalMinors = 0.0f;
	float partyOnePrefs = 0.0f;
	for (auto [partyIndex, vote] : tempOverallFp) {
		if (isMajor(partyIndex)) continue;
		partyOnePrefs += overallPreferenceFlow[partyIndex] * vote * 0.01f;
		totalMinors += vote;
	}
	float partyOnePrefAdvantage = (partyOnePrefs * 2.0f - totalMinors) * totalMinors * 0.01f;
	float partyOneTarget = tempOverallFp[Mp::One] * adjustmentFactor - partyOnePrefAdvantage * 0.005f;
	float partyTwoTarget = tempOverallFp[Mp::Two] * adjustmentFactor + partyOnePrefAdvantage * 0.005f;
	float partyOneAdjust = partyOneTarget / tempOverallFp[Mp::One];
	float partyTwoAdjust = partyTwoTarget / tempOverallFp[Mp::Two];
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		seatFpVoteShare[seatIndex][Mp::One] = seatFpVoteShare[seatIndex][Mp::One] * partyOneAdjust;
		seatFpVoteShare[seatIndex][Mp::Two] = seatFpVoteShare[seatIndex][Mp::Two] * partyTwoAdjust;
		normaliseSeatFp(seatIndex);
	}
}

void SimulationIteration::determineSeatFinalResult(int seatIndex)
{

	typedef std::pair<int, float> PartyVotes;
	auto partyVoteLess = [](PartyVotes a, PartyVotes b) {return a.second < b.second; };

	// transfer fp vote shares to vector
	std::vector<PartyVotes> originalVoteShares; // those still in the count
	std::vector<PartyVotes> excludedVoteShares; // excluded from the count, original values
	std::vector<PartyVotes> accumulatedVoteShares;
	for (auto val : seatFpVoteShare[seatIndex]) {
		if (!val.second) continue; // don't add groups with no votes at all
		if (val.first == OthersIndex) {
			// always exclude "Others" before considering other candidates
			excludedVoteShares.push_back(val);
		}
		else {
			originalVoteShares.push_back(val);
		}
	}
	accumulatedVoteShares = originalVoteShares;

	// Set up reused functions...

	auto bothMajorParties = [](int a, int b) {
		return isMajor(a) && isMajor(b);
	};

	// Function for allocating votes from excluded parties. Used in several places in this loop only,
	// so create once and use wherever needed
	auto allocateVotes = [&](std::vector<PartyVotes>& accumulatedVoteShares, std::vector<PartyVotes> const& excludedVoteShares) {
		for (auto [sourceParty, sourceVoteShare] : excludedVoteShares) {
			// if it's a final-two situation, check if we have 
			if (int(accumulatedVoteShares.size() == 2)) {
				if (run.ncPreferenceFlow.contains(sourceParty)) {
					auto const& item = run.ncPreferenceFlow[sourceParty];
					std::pair<int, int> targetParties = { accumulatedVoteShares[0].first, accumulatedVoteShares[1].first };
					if (item.contains(targetParties)) {
						float flow = item.at(targetParties);
						float transformedFlow = transformVoteShare(flow);
						transformedFlow += rng.normal(0.0f, 10.0f);
						flow = detransformVoteShare(transformedFlow);
						accumulatedVoteShares[0].second += sourceVoteShare * 0.01f * item.at(targetParties);
						accumulatedVoteShares[1].second += sourceVoteShare * 0.01f * (100.0f - item.at(targetParties));

						continue;
					}
				}
			}
			std::vector<float> weights(accumulatedVoteShares.size());
			for (int targetIndex = 0; targetIndex < int(accumulatedVoteShares.size()); ++targetIndex) {
				auto [targetParty, targetVoteShare] = accumulatedVoteShares[targetIndex];
				int ideologyDistance = abs(partyIdeologies[sourceParty] - partyIdeologies[targetParty]);
				if (bothMajorParties(sourceParty, targetParty)) ++ideologyDistance;
				float consistencyBase = PreferenceConsistencyBase[partyConsistencies[sourceParty]];
				float thisWeight = std::pow(consistencyBase, -ideologyDistance);
				float randomFactor = rng.uniform(0.6f, 1.4f);
				thisWeight *= randomFactor;
				thisWeight *= std::sqrt(targetVoteShare);
				weights[targetIndex] = thisWeight;
			}
			float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0000001f); // avoid divide by zero warning
			for (int targetIndex = 0; targetIndex < int(accumulatedVoteShares.size()); ++targetIndex) {
				accumulatedVoteShares[targetIndex].second += sourceVoteShare * weights[targetIndex] / totalWeight;
			}
		}
	};

	// Actual method continues here

	// find top two highest fp parties in order
	while (true) {

		bool finalTwoConfirmed = (accumulatedVoteShares.size() <= 2);
		if (!finalTwoConfirmed) {
			std::nth_element(accumulatedVoteShares.begin(), std::next(accumulatedVoteShares.begin()), accumulatedVoteShares.end(),
				[](PartyVotes a, PartyVotes b) {return b.second < a.second; });
			float firstTwo = accumulatedVoteShares[0].second + accumulatedVoteShares[1].second;
			float secondVoteShare = std::min(accumulatedVoteShares[0].second, accumulatedVoteShares[1].second);
			if (100.0f - firstTwo < secondVoteShare) finalTwoConfirmed = true;
		}
		if (finalTwoConfirmed) {
			// Just use classic 2pp winner if labor/lib are the final two
			if (accumulatedVoteShares[0].first == Mp::One && accumulatedVoteShares[1].first == Mp::Two) {
				accumulatedVoteShares[0].second = partyOneNewTppMargin[seatIndex] + 50.0f;
				accumulatedVoteShares[1].second = 50.0f - partyOneNewTppMargin[seatIndex];
				break;
			}
			else if (accumulatedVoteShares[0].first == Mp::Two && accumulatedVoteShares[1].first == Mp::One) {
				accumulatedVoteShares[0].second = 50.0f - partyOneNewTppMargin[seatIndex];
				accumulatedVoteShares[1].second = partyOneNewTppMargin[seatIndex] + 50.0f;
				break;
			}
			// otherwise exclude any remaining votes and allocate 
			while (accumulatedVoteShares.size() > 2) {
				auto excludedCandidate = *std::min_element(accumulatedVoteShares.begin(), accumulatedVoteShares.end(), partyVoteLess);
				auto originalCandidate = *std::find_if(originalVoteShares.begin(), originalVoteShares.end(), [=](PartyVotes a) {return a.first == excludedCandidate.first; });
				excludedVoteShares.push_back(originalCandidate);
				std::erase(accumulatedVoteShares, excludedCandidate);
				std::erase(originalVoteShares, originalCandidate);
			}
			accumulatedVoteShares = originalVoteShares;
			allocateVotes(accumulatedVoteShares, excludedVoteShares);
			break;
		}
		// Allocate preferences from excluded groups, then exclude the lowest and allocate those too
		accumulatedVoteShares = originalVoteShares;
		allocateVotes(accumulatedVoteShares, excludedVoteShares);
		auto excludedCandidate = *std::min_element(accumulatedVoteShares.begin(), accumulatedVoteShares.end(), partyVoteLess);
		auto originalCandidate = *std::find_if(originalVoteShares.begin(), originalVoteShares.end(), [=](PartyVotes a) {return a.first == excludedCandidate.first; });
		excludedVoteShares.push_back(originalCandidate);
		std::erase(accumulatedVoteShares, excludedCandidate);
		std::erase(originalVoteShares, originalCandidate);
		accumulatedVoteShares = originalVoteShares;
		allocateVotes(accumulatedVoteShares, excludedVoteShares);
	}

	std::pair<PartyVotes, PartyVotes> topTwo = std::minmax(accumulatedVoteShares[Mp::One], accumulatedVoteShares[Mp::Two], partyVoteLess);

	// For non-standard Tcp scenarios, if it's a match to the previous tcp pair then compare with that
	// and adjsut the current results to match.
	if (!bothMajorParties(topTwo.first.first, topTwo.second.first)) {
		auto const& prevResults = pastSeatResults[seatIndex];
		if (prevResults.tcpVote.count(topTwo.first.first) && prevResults.tcpVote.count(topTwo.second.first)) {
			// Allocate the previous elections fp votes as if it were now
			std::vector<PartyVotes> pseudoAccumulated;
			std::vector<PartyVotes> pseudoExcluded;
			for (auto [party, voteShare] : prevResults.fpVotePercent) {
				if (prevResults.tcpVote.count(party)) {
					pseudoAccumulated.push_back({ party, voteShare });
				}
				else {
					pseudoExcluded.push_back({ party, voteShare });
				}

			}
			if (pseudoAccumulated[0].first != topTwo.first.first) std::swap(pseudoAccumulated[0], pseudoAccumulated[1]);
			allocateVotes(pseudoAccumulated, pseudoExcluded);
			float bias = pseudoAccumulated[0].second - prevResults.tcpVote.at(topTwo.first.first);
			float totalAllocatedPrev = std::accumulate(pseudoExcluded.begin(), pseudoExcluded.end(), 0.0f,
				[](float acc, PartyVotes const& votes) {return acc + votes.second; });
			float biasRate = bias / totalAllocatedPrev;
			float totalAllocatedNow = std::accumulate(excludedVoteShares.begin(), excludedVoteShares.end(), 0.0f,
				[](float acc, PartyVotes const& votes) {return acc + votes.second; });
			topTwo.first.second -= biasRate * totalAllocatedNow;
			topTwo.second.second += biasRate * totalAllocatedNow;
			if (topTwo.second.second < topTwo.first.second) std::swap(topTwo.first, topTwo.second);
		}
	}

	seatWinner[seatIndex] = topTwo.second.first;
	auto byParty = std::minmax(topTwo.first, topTwo.second); // default pair operator orders by first element

	seatTcpVoteShare[seatIndex] = { {byParty.first.first, byParty.second.first}, byParty.first.second };

	applyLiveManualOverrides(seatIndex);
}

void SimulationIteration::applyLiveManualOverrides(int seatIndex)
{
	if (!sim.isLiveManual()) return;
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	// this verifies there's a non-classic result entered.
	if (seat.livePartyOne != Party::InvalidId) {
		float prob = rng.uniform();
		float firstThreshold = seat.partyOneProb();
		float secondThreshold = firstThreshold + seat.partyTwoProb;
		float thirdThreshold = secondThreshold + seat.partyThreeProb;
		if (prob < firstThreshold) {
			seatWinner[seatIndex] = project.parties().idToIndex(seat.livePartyOne);
		}
		else if (prob < secondThreshold) {
			seatWinner[seatIndex] = project.parties().idToIndex(seat.livePartyTwo);
		}
		else if (prob < thirdThreshold) {
			seatWinner[seatIndex] = project.parties().idToIndex(seat.livePartyThree);
		}
	}
	bool tppOverride = seat.liveUseTpp == Seat::UseTpp::Yes && isMajor(seatWinner[seatIndex]);
	tppOverride = tppOverride || seat.liveUseTpp == Seat::UseTpp::Always;
	if (tppOverride) {
		seatWinner[seatIndex] = partyOneNewTppMargin[seatIndex] > 0.0f ? 0 : 1;
	}
}

void SimulationIteration::recordSeatResult(int seatIndex)
{
	run.seatPartyOneMarginSum[seatIndex] += partyOneNewTppMargin[seatIndex];
	if (seatWinner[seatIndex] == Mp::One) ++run.partyOneWinPercent[seatIndex];
	else if (seatWinner[seatIndex] == Mp::Two) ++run.partyTwoWinPercent[seatIndex];
	else ++run.othersWinPercent[seatIndex];
}

void SimulationIteration::assignDirectWins()
{
	// make sure that the map entry exists for each party, even if they don't win any seats
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		partyWins[partyIndex] = 0;
	}
	partyWins[EmergingIndIndex] = 0;
	partyWins[EmergingPartyIndex] = 0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		int partyIndex = seatWinner[seatIndex];
		if (!regionSeatCount.contains(partyIndex)) {
			regionSeatCount[partyIndex] = std::vector<int>(project.regions().count());
		}
		partyWins[partyIndex]++;
		int regionIndex = project.regions().idToIndex(seat.region);
		++regionSeatCount[partyIndex][regionIndex];
	}
}

void SimulationIteration::assignCountAsPartyWins()
{
	// Only count named parties, inds/emerging parties can't ever count as other parties
	for (int partyNum = Mp::Others; partyNum < project.parties().count(); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyOne) {
			partyWins[Mp::One] += partyWins[partyNum];
		}
		else if (thisParty.countAsParty == Party::CountAsParty::CountsAsPartyTwo) {
			partyWins[Mp::Two] += partyWins[partyNum];
		}
	}
}

void SimulationIteration::assignSupportsPartyWins()
{
	partySupport = { partyWins[Mp::One], partyWins[Mp::Two] };
	for (int partyNum = Mp::Others; partyNum < project.parties().count(); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.relationType == Party::RelationType::IsPartOf && thisParty.relationTarget < Mp::Others) {
			partyWins[thisParty.relationTarget] += partyWins[partyNum];
		}
		if (thisParty.relationType == Party::RelationType::Supports && thisParty.relationTarget < Mp::Others) {
			partySupport[thisParty.relationTarget] += partyWins[partyNum];
		}
	}
}

void SimulationIteration::recordMajorityResult()
{
	int minimumForMajority = project.seats().count() / 2 + 1;

	// Look at the overall result and classify it
	// Note for "supports" wins there is some logic to make sure a supporting party doesn't actually outnumber the larger party
	if (partyWins[Mp::One] >= minimumForMajority) ++run.partyMajority[Mp::One];
	else if (partyWins[Mp::Two] >= minimumForMajority) ++run.partyMajority[Mp::Two];
	else if (partySupport[Mp::One] >= minimumForMajority && partyWins[Mp::One] > partySupport[Mp::One] / 2) ++run.partyMinority[Mp::One];
	else if (partySupport[Mp::Two] >= minimumForMajority && partyWins[Mp::Two] > partySupport[Mp::Two] / 2) ++run.partyMinority[Mp::Two];
	else {
		std::vector<std::pair<int, int>> sortedPartyWins(partyWins.begin(), partyWins.end());
		std::sort(sortedPartyWins.begin(), sortedPartyWins.end(),
			[](std::pair<int, int> lhs, std::pair<int, int> rhs) {return lhs.second > rhs.second; });
		if (sortedPartyWins[0].second >= minimumForMajority) {
			++run.partyMajority[sortedPartyWins[0].first];
		}
		else if (sortedPartyWins[0].second > sortedPartyWins[1].second) {
			++run.partyMostSeats[sortedPartyWins[0].first];
		}
		else {
			++run.tiedParliament;
		}
	}
}

void SimulationIteration::recordPartySeatWinCounts()
{
	int othersWins = 0;
	for (auto [partyIndex, wins] : partyWins) {
		if (!sim.latestReport.partySeatWinFrequency.contains(partyIndex)) {
			sim.latestReport.partySeatWinFrequency[partyIndex] = std::vector<int>(project.seats().count() + 1);
		}
		++sim.latestReport.partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
		if (partyIndex > 1) othersWins += partyWins[partyIndex];
		for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
			if (!run.regionPartyWins[regionIndex].contains(partyIndex)) {
				run.regionPartyWins[regionIndex][partyIndex] = std::vector<int>(run.regionPartyWins[regionIndex][Mp::One].size());
			}
			int thisRegionSeatCount = regionSeatCount[partyIndex].size() ? regionSeatCount[partyIndex][regionIndex] : 0;
			++run.regionPartyWins[regionIndex][partyIndex][thisRegionSeatCount];
		}
	}
	++sim.latestReport.othersWinFrequency[othersWins];
}

void SimulationIteration::recordSeatPartyWinner(int seatIndex)
{
	int winner = seatWinner[seatIndex];
	if (!run.seatPartyWins[seatIndex].count(winner)) {
		run.seatPartyWins[seatIndex][winner] = 1;
	}
	else {
		++run.seatPartyWins[seatIndex][winner];
	}
}

void SimulationIteration::recordSeatFpVotes(int seatIndex)
{
	for (auto [partyIndex, fpPercent] : seatFpVoteShare[seatIndex]) {
		run.cumulativeSeatPartyFpShare[seatIndex][partyIndex] += fpPercent;
		int bucket = std::clamp(int(std::floor(fpPercent * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
		++run.seatPartyFpDistribution[seatIndex][partyIndex][bucket];
		if (!fpPercent) ++run.seatPartyFpZeros[seatIndex][partyIndex];
	}
}

void SimulationIteration::recordSeatTcpVotes(int seatIndex)
{
	auto parties = seatTcpVoteShare[seatIndex].first;
	float tcpPercent = seatTcpVoteShare[seatIndex].second;
	int bucket = std::clamp(int(std::floor(tcpPercent * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
	++run.seatTcpDistribution[seatIndex][parties][bucket];
}

void SimulationIteration::recordIterationResults()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		recordSeatResult(seatIndex);
		recordSeatPartyWinner(seatIndex);
		recordSeatFpVotes(seatIndex);
		recordSeatTcpVotes(seatIndex);
	}
	recordVoteTotals();
	recordSwings();
	recordMajorityResult();
	recordPartySeatWinCounts();
}

void SimulationIteration::recordVoteTotals()
{
	short tppBucket = short(floor(iterationOverallTpp * 10.0f));
	++sim.latestReport.tppFrequency[tppBucket];

	float othersFp = 0.0f;
	for (auto const& [partyIndex, fp]: tempOverallFp) {
		if (partyIndex >= 0) {
			short bucket = short(floor(fp * 10.0f));
			++sim.latestReport.partyPrimaryFrequency[partyIndex][bucket];
		}
		else {
			othersFp += fp;
		}
	}
	short bucket = short(floor(othersFp * 10.0f + 0.5f));
	++sim.latestReport.partyPrimaryFrequency[OthersIndex][bucket];
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

float SimulationIteration::calculateEffectiveSeatModifier(int seatIndex, int partyIndex) const
{
	float populism = centristPopulistFactor.at(partyIndex);
	float seatModifier = mix(run.seatCentristModifiers[seatIndex], run.seatPopulistModifiers[seatIndex], populism);
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	int regionIndex = project.regions().idToIndex(seat.region);
	float homeStateCoefficient = mix(run.centristStatistics.homeStateCoefficient, run.populistStatistics.homeStateCoefficient, populism);
	if (homeRegion.at(partyIndex) == regionIndex) seatModifier += homeStateCoefficient;
	float highVoteModifier = std::clamp(std::pow(2.0f, (3.0f - overallFpTarget.at(partyIndex)) * 0.1f), 0.2f, 1.0f);
	seatModifier = (seatModifier - 1.0f) * highVoteModifier + 1.0f;
	return seatModifier;
}
