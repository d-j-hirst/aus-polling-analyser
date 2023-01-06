#include "SimulationIteration.h"

#include "CountProgress.h"
#include "SpecialPartyCodes.h"
#include "PollingProject.h"
#include "RandomGenerator.h"
#include "Simulation.h"
#include "SimulationRun.h"

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

const std::set<std::pair<std::string, std::string>> IgnoreExhaust = {
	{"2022vic", "St Albans"},
	{"2022vic", "Sydenham"},
};

bool isMajor(int partyIndex) {
	return partyIndex == Mp::One || partyIndex == Mp::Two;
}

SimulationIteration::SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

bool SimulationIteration::checkForNans(std::string const& loc) {
	static bool alreadyLogged = false;
	auto report = [&](int seatIndex, std::string type) {
		std::lock_guard<std::mutex> lock(recordMutex);
		if (alreadyLogged) return;
		alreadyLogged = true;
		logger << "Warning: A " << type << " vote share for seat " << project.seats().viewByIndex(seatIndex).name << "was Nan!\n";
		logger << "At simulation location " << loc << "\n";
		logger << "Simulation iteration aborted to prevent a freeze, trying to redo.\n";
		PA_LOG_VAR(run.liveOverallTppSwing);
		PA_LOG_VAR(iterationOverallTpp);
		PA_LOG_VAR(run.liveOverallFpSwing);
		PA_LOG_VAR(run.liveSeatTppSwing);
		PA_LOG_VAR(run.liveSeatTcpSwing);
		PA_LOG_VAR(run.liveSeatFpSwing);
		PA_LOG_VAR(overallFpTarget);
		PA_LOG_VAR(overallFpSwing);
		PA_LOG_VAR(regionSwing);
		PA_LOG_VAR(partyOneNewTppMargin);
		PA_LOG_VAR(seatFpVoteShare);
		return;
	};
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (int(seatFpVoteShare.size()) > seatIndex) {
			for (auto [party, voteShare] : seatFpVoteShare[seatIndex]) {
				if (std::isnan(voteShare)) {
					report(seatIndex, "fp");
					return true;
				}
			}
		}
		if (std::isnan(partyOneNewTppMargin[seatIndex])) {
			report(seatIndex, "tcp");
			return true;
		}
	}
	return false;
}

void SimulationIteration::runIteration()
{
	bool gotValidResult = false;
	while (!gotValidResult) {
		loadPastSeatResults();
		initialiseIterationSpecificCounts();
		determineFedStateCorrelation();
		determineOverallTpp();
		decideMinorPartyPopulism();
		determineHomeRegions();
		determineMinorPartyContests();
		incorporateLiveOverallFps();
		determineIndDistributionParameters();
		determinePpvcBias();
		determineDecVoteBias();
		determineRegionalSwings();
		determineSeatInitialResults();

		if (checkForNans("Before reconciling")) {
			seatFpVoteShare.clear();
			partyOneNewTppMargin.clear();
			continue;
		}

		reconcileSeatAndOverallFp();

		if (checkForNans("After reconciling")) {
			seatFpVoteShare.clear();
			partyOneNewTppMargin.clear();
			continue;
		}

		seatTcpVoteShare.resize(project.seats().count());
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			determineSeatFinalResult(seatIndex);
		}

		assignDirectWins();
		assignCountAsPartyWins();
		assignSupportsPartyWins();
		gotValidResult = true;
	}

	std::lock_guard<std::mutex> lock(recordMutex);
	recordIterationResults();
}

void SimulationIteration::initialiseIterationSpecificCounts()
{
	partyOneNewTppMargin = std::vector<float>(project.seats().count(), 0.0f);
	seatWinner = std::vector<Party::Id>(project.seats().count(), Party::InvalidId);
	seatRegionSwing = std::vector<double>(project.seats().count(), 0.0);
	seatElasticitySwing = std::vector<double>(project.seats().count(), 0.0);
	seatLocalEffects = std::vector<double>(project.seats().count(), 0.0);
	seatPreviousSwingEffect = std::vector<double>(project.seats().count(), 0.0);
	seatFederalSwingEffect = std::vector<double>(project.seats().count(), 0.0);
}

void SimulationIteration::determineFedStateCorrelation()
{
	auto fedElectionDate = sim.settings.fedElectionDate;
	if (!fedElectionDate.IsValid()) {
		fedStateCorrelation = 0.0f;
		return;
	}
	auto projDate = project.projections().view(sim.settings.baseProjection).getSettings().endDate;
	auto dateDiff = abs((projDate - fedElectionDate).GetDays());
	float gammaMedian = 0.5555f * exp(-0.00294f * float(dateDiff));
	fedStateCorrelation = rng.gamma(3.0f, 0.374f) * gammaMedian;
}

void SimulationIteration::determineOverallTpp()
{
	// First, randomly determine the national swing for this particular simulation
	auto projectedSample = project.projections().view(sim.settings.baseProjection).generateSupportSample(project.models());
	daysToElection = projectedSample.daysToElection;
	iterationOverallTpp = projectedSample.voteShare.at(TppCode);
	if (sim.settings.forceTpp.has_value()) {
		float tppChange = sim.settings.forceTpp.value() - iterationOverallTpp;
		iterationOverallTpp = sim.settings.forceTpp.value();
		projectedSample.voteShare["ALP"] = predictorCorrectorTransformedSwing(projectedSample.voteShare["ALP"], tppChange);
		projectedSample.voteShare["LNP"] = predictorCorrectorTransformedSwing(projectedSample.voteShare["LNP"], -tppChange);
	}
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
			overallPreferenceFlow[OthersIndex] = preferenceFlow;
			overallPreferenceFlow[EmergingIndIndex] = preferenceFlow;
			overallPreferenceFlow[CoalitionPartnerIndex] = 15.0f;
			continue;
		}
		if (sampleKey == EmergingOthersCode) {
			overallPreferenceFlow[EmergingPartyIndex] = preferenceFlow;
			continue;
		}
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				overallPreferenceFlow[partyIndex] = preferenceFlow;
				break;
			}
		}
	}

	for (auto const& [sampleKey, exhaustRate] : projectedSample.exhaustRate) {
		if (sampleKey == UnnamedOthersCode) {
			overallExhaustRate[OthersIndex] = exhaustRate;
			overallExhaustRate[EmergingIndIndex] = exhaustRate;
			overallExhaustRate[CoalitionPartnerIndex] = 0.25f;
			continue;
		}
		if (sampleKey == EmergingOthersCode) {
			overallExhaustRate[EmergingPartyIndex] = exhaustRate;
			continue;
		}
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				overallExhaustRate[partyIndex] = exhaustRate;
				break;
			}
		}
	}

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		// Give any party without a sampled preference flow (e.g. Independents) a preference flow relative to generic others
		if (run.previousPreferenceFlow.contains(partyIndex)) {
			overallPreferenceFlow[partyIndex] = overallPreferenceFlow[OthersIndex] +
				run.previousPreferenceFlow[partyIndex] - run.previousPreferenceFlow[OthersIndex];
		}
		// if it isn't in the file, then just assume equal to overall others.
		else {
			overallPreferenceFlow[partyIndex] = overallPreferenceFlow[OthersIndex];
		}
	}

	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		// Give any party without a sampled exhaust rate (e.g. Independents) an exhaust rate relative to generic others
		if (run.previousExhaustRate.contains(partyIndex)) {
			overallExhaustRate[partyIndex] = overallExhaustRate[OthersIndex] +
				run.previousExhaustRate[partyIndex] - run.previousExhaustRate[OthersIndex];
		}
		// if it isn't in the file, then just assume equal to overall others.
		else {
			overallExhaustRate[partyIndex] = overallExhaustRate[OthersIndex];
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

	if (sim.isLive() && run.liveOverallTppPercentCounted) {
		float liveSwing = run.liveOverallTppSwing;
		float liveStdDev = stdDevOverall(run.liveOverallTppPercentCounted);
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.5f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev) * run.sampleRepresentativeness;
		iterationOverallSwing = (iterationOverallSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
		iterationOverallTpp = iterationOverallSwing + sim.settings.prevElection2pp;
	}
}

void SimulationIteration::incorporateLiveOverallFps()
{
	if (sim.isLiveAutomatic() && run.liveOverallFpPercentCounted) {
		for (auto [partyIndex, _] : overallFpTarget) {
			if (partyIndex == 0 || partyIndex == 1) continue;
			float liveTarget = run.liveOverallFpTarget[partyIndex];
			float liveStdDev = stdDevOverall(run.liveOverallFpPercentCounted) * 1.8f;
			liveTarget += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
			// just guesswork, minors on average fall slightly in postcount
			postCountFpShift[partyIndex] = std::normal_distribution<float>(-0.3f, partyIndex == -1 ? 3.0f : 1.5f)(gen);
			liveTarget += postCountFpShift[partyIndex];
			float priorWeight = 1.5f;
			float liveWeight = 1.0f / (liveStdDev * liveStdDev) * run.sampleRepresentativeness;
			float targetShift = (overallFpTarget[partyIndex] * priorWeight + liveTarget * liveWeight) / (priorWeight + liveWeight) - overallFpTarget[partyIndex];
			overallFpTarget[partyIndex] = predictorCorrectorTransformedSwing(overallFpTarget[partyIndex], targetShift);
			overallFpSwing[partyIndex] = overallFpTarget[partyIndex] - run.previousFpVoteShare[partyIndex];
		}
	}
}

void SimulationIteration::determineIndDistributionParameters()
{
	// All these are set up so that the distribution of quantiles
	// for independent candidates are correlated via a beta distribution
	// in any given single election while maintaining the original uniform
	// distribution (approximately) across many simulations
	if (rng.uniform() < 0.1f) {
		if (rng.uniform() < 0.5f) {
			indAlpha = 0.725f;
			indBeta = 2.0f / std::pow(rng.uniform(), 0.6f);
		}
		else {
			indAlpha = 2.0f / std::pow(rng.uniform(), 0.6f);
			indBeta = 0.725f;
		}
	}
	else {
		if (rng.uniform() < 0.5f) {
			indAlpha = 2.0f;
			indBeta = 2.0f / std::pow(rng.uniform(), 0.84f);
		}
		else {
			indAlpha = 2.0f / std::pow(rng.uniform(), 0.84f);
			indBeta = 2.0f;
		}
	}
}

void SimulationIteration::determinePpvcBias()
{
	constexpr float DefaultPpvcBiasStdDev = 3.0f;
	float defaultPpvcBias = rng.normal(0.0f, DefaultPpvcBiasStdDev);
	float observedPpvcStdDev = DefaultPpvcBiasStdDev * std::pow(400000.0f / std::max(run.ppvcBiasConfidence, 0.1f), 0.6f);
	float observedWeight = 1.0f / observedPpvcStdDev;
	float originalWeight = 1.0f;
	float mixFactor = observedWeight / (originalWeight + observedWeight);
	float observedPpvcBias = rng.normal(run.ppvcBiasObserved, std::min(DefaultPpvcBiasStdDev, observedPpvcStdDev));
	ppvcBias = mix(defaultPpvcBias, observedPpvcBias, mixFactor);
}

void SimulationIteration::determineDecVoteBias()
{
	constexpr float DefaultDecBiasStdDev = 3.0f;
	float defaultDecVoteBias = rng.normal(0.0f, DefaultDecBiasStdDev);
	decVoteBias = defaultDecVoteBias;
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
	partyIdeologies[CoalitionPartnerIndex] = 3;
	partyConsistencies[CoalitionPartnerIndex] = 2;
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
		// Halve this as rough fudge factor for the fact it isn't backtested.
		float pollIntercept = run.regionPollBehaviour[regionIndex].baseSwingDeviation * 0.5f;
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
		// Naive swing - the swing we get without any region polling history
		float pollRawDeviation = thisRegion.swingDeviation;
		float rmse = run.regionBaseBehaviour[regionIndex].rmse;
		float kurtosis = run.regionBaseBehaviour[regionIndex].kurtosis;
		float randomVariation = rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis);
		// Use polled variation assuming correlation is about the same as worst performing state
		float naiveSwing = medianNaiveSwing + pollRawDeviation * 0.3f + randomVariation;
		swingToTransform = naiveSwing;
	}

	float transformedTpp = transformVoteShare(thisRegion.lastElection2pp) + swingToTransform;
	float detransformedTpp = detransformVoteShare(transformedTpp);
	regionSwing[regionIndex] = detransformedTpp - thisRegion.lastElection2pp;
}

void SimulationIteration::modifyLiveRegionalSwing(int regionIndex)
{
	if (sim.isLive() && run.liveRegionTppPercentCounted[regionIndex]) {
		float liveSwing = run.liveRegionSwing[regionIndex];
		float liveStdDev = stdDevSingleSeat(run.liveRegionTppPercentCounted[regionIndex]) * 0.4f;
		liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
		float priorWeight = 0.03f;
		float liveWeight = 1.0f / (liveStdDev * liveStdDev);
		float priorSwing = regionSwing[regionIndex];
		regionSwing[regionIndex] = (priorSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	}
}

void SimulationIteration::correctRegionalSwings()
{
	// Adjust regional swings to keep the implied overall 2pp the same as that actually projected
	double weightedSwings = 0.0;
	double weightSums = 0.0;
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		int regionId = project.regions().indexToId(regionIndex);
		double regionTurnout = 0.0;
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			Seat const& seat = project.seats().viewByIndex(seatIndex);
			if (seat.region != regionId) continue;
			double turnout = double(run.pastSeatResults[seatIndex].turnoutCount);
			regionTurnout += turnout;
		}
		weightedSwings += double(regionSwing[regionIndex]) * regionTurnout;
		weightSums += double(regionTurnout);
	}

	float tempOverallSwing = float(weightedSwings / weightSums);
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
	const float tppPrev = seat.tppMargin + 50.0f;
	float transformedTpp = transformVoteShare(tppPrev);
	const float elasticity = run.seatParameters[seatIndex].elasticity;
	// float trend = run.seatParameters[seatIndex].trend;
	const float volatility = run.seatParameters[seatIndex].volatility;
	const bool useVolatility = run.seatParameters[seatIndex].loaded;
	// Save effects so we can record them for the display
	const float thisRegionSwing = regionSwing[project.regions().idToIndex(seat.region)];
	const float elasticitySwing = (elasticity - 1.0f) * thisRegionSwing;
	const float localEffects = run.seatPartyOneTppModifier[seatIndex];
	const float previousSwingEffect = run.seatPreviousTppSwing[seatIndex] * run.tppSwingFactors.previousSwingModifier;
	const float federalSwingEffect = fedStateCorrelation * seat.transposedTppSwing * logitDeriv(tppPrev);
	transformedTpp += thisRegionSwing + elasticitySwing;
	// Add modifiers for known local effects
	transformedTpp += localEffects;
	// Remove the average local modifier across the region
	transformedTpp -= run.regionLocalModifierAverage[seat.region];
	transformedTpp += previousSwingEffect;
	transformedTpp += federalSwingEffect;
	float swingDeviation = run.tppSwingFactors.meanSwingDeviation;
	if (run.regionCode == "fed") swingDeviation += run.tppSwingFactors.federalModifier;
	if (useVolatility) swingDeviation = 0.75f * volatility + 0.25f * swingDeviation;
	float kurtosis = run.tppSwingFactors.swingKurtosis;
	// Add random noise to the new margin of this seat
	transformedTpp += rng.flexibleDist(0.0f, swingDeviation, swingDeviation, kurtosis, kurtosis);
	if (sim.isLive() && run.liveSeatTcpBasis[seatIndex] > 0.0f && !std::isnan(run.liveSeatTppSwing[seatIndex])) {
		float tppLive = (tppPrev + run.liveSeatTppSwing[seatIndex] > 10.0f ?
			tppPrev + run.liveSeatTppSwing[seatIndex] :
			predictorCorrectorTransformedSwing(tppPrev, run.liveSeatTppSwing[seatIndex]));
		tppLive = basicTransformedSwing(tppLive, ppvcBias * run.liveSeatPpvcSensitivity[seatIndex]);
		tppLive = basicTransformedSwing(tppLive, decVoteBias * run.liveSeatDecVoteSensitivity[seatIndex]);
		float liveTransformedTpp = transformVoteShare(tppLive);
		float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatTcpBasis[seatIndex] * 0.2f)));
		if (run.liveSeatTcpBasis[seatIndex] > 0.6f) liveSwingDeviation = std::min(liveSwingDeviation, run.liveEstDecVoteRemaining[seatIndex] * 0.05f);
		liveTransformedTpp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		float liveFactor = 1.0f - pow(2.0f, -run.liveSeatTcpCounted[seatIndex] * 0.2f);
		float mixedTransformedTpp = mix(transformedTpp, liveTransformedTpp, liveFactor);
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(mixedTransformedTpp) - 50.0f;
	}
	else {
		// Margin for this simulation is finalised, record it for later averaging
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
	}

	const float totalFixedEffects = thisRegionSwing + elasticitySwing + localEffects + previousSwingEffect + federalSwingEffect;
	const float fixedSwingSize = detransformVoteShare(transformVoteShare(tppPrev) + totalFixedEffects) - tppPrev;
	const float transformFactor = fixedSwingSize / totalFixedEffects;
	seatRegionSwing[seatIndex] += double(thisRegionSwing * transformFactor);
	seatElasticitySwing[seatIndex] += double(elasticitySwing * transformFactor);
	seatLocalEffects[seatIndex] += double(localEffects * transformFactor);
	seatPreviousSwingEffect[seatIndex] += double(previousSwingEffect * transformFactor);
	seatFederalSwingEffect[seatIndex] += double(federalSwingEffect * transformFactor);
}

void SimulationIteration::correctSeatTppSwings()
{
	if (run.regionCode == "fed") {
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
				if (sim.isLive()) {
					// If a seat has much live data, don't adjust it any more.
					swingAdjust *= std::min(1.0f, 2.0f / run.liveSeatTcpCounted[seatIndex] - 0.2f);
				}
				partyOneNewTppMargin[seatIndex] += swingAdjust;
			}
		}
	}
	// Now fix seats to the nation tpp as the above calculation doesn't always do this
	double totalTpp = 0.0;
	double totalTurnout = 0.0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		double turnout = double(run.pastSeatResults[seatIndex].turnoutCount);
		double turnoutScaledTpp = double(partyOneNewTppMargin[seatIndex] + 50.0) * turnout;
		totalTpp += turnoutScaledTpp;
		totalTurnout += turnout;
	}
	float averageTpp = float(totalTpp / totalTurnout);
	float swingAdjust = iterationOverallTpp - averageTpp;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (sim.isLive()) {
			// If a seat has much live data, don't adjust it any more.
			swingAdjust *= std::min(1.0f, 2.0f / run.liveSeatTcpCounted[seatIndex] - 0.2f);
		}
		partyOneNewTppMargin[seatIndex] += swingAdjust;
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
				voteShare = predictorCorrectorTransformedSwing(voteShare, -seat.tcpChange.at(project.parties().view(seat.challenger).abbreviation));
			}
			else if (partyIndex == project.parties().idToIndex(seat.challenger) &&
				seat.tcpChange.contains(project.parties().view(seat.challenger).abbreviation)) {
				voteShare = predictorCorrectorTransformedSwing(voteShare, seat.tcpChange.at(project.parties().view(seat.challenger).abbreviation));
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
		else if (effectiveIndependent && project.parties().idToIndex(seat.incumbent) == partyIndex) {
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
		if (partyIndex == OthersIndex) {
			if (seat.runningParties.size() && !contains(seat.runningParties, OthersCode)) continue;
		}
		// Note: this means major party vote shares get passed on as-is
		seatFpVoteShare[seatIndex][partyIndex] = voteShare;
	}

	pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = tempPastResults[OthersIndex];
	determineSeatEmergingParties(seatIndex);
	if (seat.confirmedProminentIndependent) determineSeatConfirmedInds(seatIndex);
	determineSeatEmergingInds(seatIndex);
	determineSeatOthers(seatIndex);

	adjustForFpCorrelations(seatIndex);

	if (sim.isLiveAutomatic()) incorporateLiveSeatFps(seatIndex);

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
	if (seat.runningParties.size() && partyIndex == run.indPartyIndex && !seat.incumbentRecontestConfirmed) {
		if (!contains(seat.runningParties, project.parties().viewByIndex(partyIndex).abbreviation)) {
			voteShare = 0.0f;
			return;
		}
	}
	if (partyIndex == run.indPartyIndex && seat.confirmedProminentIndependent) {
		// this case will be handled by the "confirmed independent" logic instead
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
	if (rng.uniform() > recontestRateMixed || (seat.retirement && partyIndex == seat.incumbent && !overallFpTarget.contains(partyIndex))) {
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

	float quantile = partyIndex == run.indPartyIndex ? rng.beta(indAlpha, indBeta) * 0.5f + 0.5f : rng.uniform();
	float variableVote = rng.flexibleDist(0.0f, lowerRmseMixed, upperRmseMixed, lowerKurtosisMixed, upperKurtosisMixed, quantile);
	transformedFp += variableVote;
	// Model can't really deal with the libs not existing (=> large OTH vote) in Richmond 2018
	// so likely underestimates GRN fp support here. This is a temporary workaround to bring in line
	// with other seats expecting a small ~3% TCP swing to greens, hopefully will find a better fix for this later.
	if (partyIndex == 2 && seat.name == "Richmond" && project.getElectionName() == "2022 Victorian State Election") {
		transformedFp += 5.5f;
	}

	if (partyIndex == run.indPartyIndex && run.seatBettingOdds[seatIndex].contains(run.indPartyIndex)) {
		// Exact values of odds above $15 don't generally mean much, so cap them at this level
		constexpr float OddsCap = 15.0f;
		float cappedOdds = std::min(run.seatBettingOdds[seatIndex][run.indPartyIndex], OddsCap);
		// the last part of this line compensates for the typical bookmaker's margin
		float impliedChance = 1.0f / (cappedOdds * (2.0f / 1.88f));
		// significant adjustment downwards to adjust for longshot bias.
		// this number isn't really treated as a probability from here on so it's ok for
		// it to become negative.
		if (impliedChance < 0.4f) impliedChance -= 1.3f * (0.4f - impliedChance);
		float pivot = transformVoteShare(32.0f); // fp vote expected for 50% chance of winning
		constexpr float range = 42.0f;
		float voteShareCenter = pivot + range * (impliedChance - 0.5f);
		constexpr float variation = 20.0f;
		float transformedBettingFp = rng.normal(voteShareCenter, variation);
		// If the betting odds are very favourable to the independent, tend to stick with
		// the existing estimate as the betting estimate would probably be an underestimate
		// for very popular independents
		float mixFactor = std::min(5.0f - 5.0f * impliedChance, 0.5f);
		transformedFp = mix(transformedFp, transformedBettingFp, mixFactor);
	} else if (partyIndex == run.grnPartyIndex && run.seatBettingOdds[seatIndex].contains(run.grnPartyIndex)) {
		// Exact values of odds above $15 don't generally mean much, so cap them at this level
		constexpr float OddsCap = 15.0f;
		float cappedOdds = std::min(run.seatBettingOdds[seatIndex][run.grnPartyIndex], OddsCap);
		// the last part of this line compensates for the typical bookmaker's margin
		float impliedChance = 1.0f / ((cappedOdds - 1.0f) * (1.0f / 0.88f) + 1.0f);
		// No longshot bias adjustment for Greens
		float prevLibFp = run.pastSeatResults[seatIndex].fpVotePercent.contains(1) ?
			run.pastSeatResults[seatIndex].fpVotePercent[1] : 10.0f;
		float prevAlpFp = run.pastSeatResults[seatIndex].fpVotePercent.contains(0) ?
			run.pastSeatResults[seatIndex].fpVotePercent[0] : 10.0f;
		float prevGrnFp = run.pastSeatResults[seatIndex].fpVotePercent.contains(2) ?
			run.pastSeatResults[seatIndex].fpVotePercent[2] : 10.0f;
		float prevOthFp = run.pastSeatResults[seatIndex].fpVotePercent.contains(-1) ?
			run.pastSeatResults[seatIndex].fpVotePercent[-1] : 10.0f;
		// First step establishes the mean at a position that historically relates to this
		// % chance of winning
		if (seat.name == "Pascoe Vale") {
			// Account for previous independent taking most votes
			prevLibFp += 7.0f;
		}
		// First approach: GRN-ALP contest. Suitable for ALP margin >20%. LIBs preferences considered
		const float assumedLibPrefFlow = 0.75f;
		const float estimatedLibPrefs = prevLibFp * assumedLibPrefFlow;
		const float estimatedOthPrefs = prevOthFp * 0.5f;
		const float requiredGrnVotes1 = 50.0f - estimatedLibPrefs - estimatedOthPrefs;
		const float grnFpCenter1 = std::clamp(requiredGrnVotes1 + 80.0f * 
			(impliedChance >= 0.5f ? std::pow(impliedChance - 0.5f, 1.6f) : -0.6f * std::pow(0.5f - impliedChance, 1.6f)),
		10.0f, 80.0f);

		// Second approach: GRN-LIB contest. Suitable for ALP margin <12%. Just need to get ahead of ALP
		// Assumes chance of LIB win is small (adjust if we get a race where this isn't the case)
		const float prevLeftFp = prevAlpFp + prevGrnFp;
		const float requiredGrnVotes2 = prevLeftFp * 0.5f;
		const float grnFpCenter2 = std::clamp(requiredGrnVotes2 + 40.0f *
			(impliedChance >= 0.5f ? std::pow(impliedChance - 0.5f, 1.6f) : -0.75f * -std::pow(0.5f - impliedChance, 1.6f)),
			10.0f, 80.0f);

		const float grnFpCenter = mix(grnFpCenter2, grnFpCenter1, 
			std::clamp((seat.tppMargin - 12.0f) / 8.0f, 0.0f, 1.0f));

		float transformedCenter = transformVoteShare(grnFpCenter);
		const float variation = 10.0f * (1.0f - 0.75f * std::abs(impliedChance - 0.5f));
		float transformedBettingFp = rng.normal(transformedCenter, variation);
		transformedFp = mix(transformedFp, transformedBettingFp, 0.7f);
	}
	// treat other parties like independent I guess
	else if (run.seatBettingOdds[seatIndex].contains(partyIndex)) {
		// Exact values of odds above $15 don't generally mean much, so cap them at this level
		constexpr float OddsCap = 15.0f;
		float cappedOdds = std::min(run.seatBettingOdds[seatIndex][partyIndex], OddsCap);
		// the last part of this line compensates for the typical bookmaker's margin
		float impliedChance = 1.0f / (cappedOdds * (2.0f / 1.88f));
		// significant adjustment downwards to adjust for longshot bias.
		// this number isn't really treated as a probability from here on so it's ok for
		// it to become negative.
		if (impliedChance < 0.4f) impliedChance -= 1.3f * (0.4f - impliedChance);
		float pivot = transformVoteShare(32.0f); // fp vote expected for 50% chance of winning
		constexpr float range = 42.0f;
		float voteShareCenter = pivot + range * (impliedChance - 0.5f);
		constexpr float variation = 20.0f;
		float transformedBettingFp = rng.normal(voteShareCenter, variation);
		// If the betting odds are very favourable to the independent, tend to stick with
		// the existing estimate as the betting estimate would probably be an underestimate
		// for very popular independents
		float mixFactor = std::min(5.0f - 5.0f * impliedChance, 0.5f);
		transformedFp = mix(transformedFp, transformedBettingFp, mixFactor);
	}

	float regularVoteShare = detransformVoteShare(transformedFp);

	if (seat.prominentMinors.size() && partyIndex >= Mp::Others && contains(seat.prominentMinors, project.parties().viewByIndex(partyIndex).abbreviation)) {
		regularVoteShare += (1.0f - std::clamp(regularVoteShare / ProminentMinorFlatBonusThreshold, 0.0f, 1.0f)) * ProminentMinorFlatBonus;
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare, rng.uniform() * rng.uniform() * ProminentMinorBonusMax);
	}

	if (partyIndex == run.indPartyIndex && seat.incumbent != run.indPartyIndex) {
		// Add potential re-runs under "Emerging Independent" instead
		voteShare = 0.0f;
		seatFpVoteShare[seatIndex][EmergingIndIndex] = regularVoteShare;
	}
	else {
		voteShare = regularVoteShare;
	}
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
	float adjustedModifier = std::max(0.2f, seatModifier * fpModificationAdjustment[partyIndex]);
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
	bool isFederal = run.regionCode == "fed";
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
		// increased vote share prospect for inds with more viability
		if (run.seatMinorViability[seatIndex].contains(run.indPartyIndex)) {
			rmse *= 1.0f + (0.4f * run.seatMinorViability[seatIndex][run.indPartyIndex]);
		}
		if (seat.incumbent == 0) rmse *= 1.1f;
		float quantile = rng.beta(indAlpha, indBeta) * 0.5f + 0.5f;
		float variableVote = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis, quantile));
		float transformedVoteShare = variableVote + run.indEmergence.fpThreshold;
		if (run.seatBettingOdds[seatIndex].contains(run.indPartyIndex)) {
			// Exact values of odds above $15 don't generally mean much, so cap them at this level
			constexpr float OddsCap = 15.0f;
			float cappedOdds = std::min(run.seatBettingOdds[seatIndex][run.indPartyIndex], OddsCap);
			// the last part of this line compensates for the typical bookmaker's margin
			const float origImpliedChance = 1.0f / (cappedOdds * (2.0f / 1.88f));
			float impliedChance = origImpliedChance;
			// significant adjustment downwards to adjust for longshot bias.
			// this number isn't really treated as a probability from here on so it's ok for
			// it to become negative.
			if (impliedChance < 0.35f) impliedChance -= 0.5f * (0.35f - impliedChance);
			if (impliedChance < 0.25f) impliedChance -= 0.5f * (0.25f - impliedChance);
			float pivot = transformVoteShare(32.0f); // fp vote expected for 50% chance of winning
			// Adjust for ALP tpp margin, otherwise this formula primarily intended for LNP seats
			// doesn't work well for safe ALP seats
			pivot += std::clamp(seat.tppMargin * std::pow(origImpliedChance, 1.5f) * 8.0f, 0.0f, 40.0f);
			constexpr float range = 42.0f;
			float voteShareCenter = pivot + range * (impliedChance - 0.5f);
			//if (seat.tppMargin > 3.0f) voteShareCenter += 12.0f; // bandaid fix for the ALP candidates having a harder time with same fp
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
				float pollRaw = poll.first;
				pollRaw = pollRaw * 0.503f + 15.59f;
				weightedSum += pollRaw * weight;
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
	bool isFederal = run.regionCode == "fed";
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
	// but in other situations we want ind running chance to be affected by the presence of other confirmed independents
	else indEmergenceRate *= run.indEmergenceModifier;
	// Re-running challengers also count as a strong candidate, so reduce chance of further independents when they are running
	if (seatFpVoteShare[seatIndex].contains(EmergingPartyIndex)) indEmergenceRate *= 0.3f;
	// Beta distribution flipped because it's desired for the high rate of ind emergence
	// to match high rate of ind voting
	if (1.0f - rng.beta(indAlpha, indBeta) < std::max(0.01f, indEmergenceRate)) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - run.indEmergence.fpThreshold;
		if (isFederal) rmse *= (1.0f + run.indEmergence.fedVoteCoeff / interceptSize);
		if (isRural) rmse *= (1.0f + run.indEmergence.ruralVoteCoeff / interceptSize);
		if (isProvincial) rmse *= (1.0f + run.indEmergence.provincialVoteCoeff / interceptSize);
		if (isOuterMetro) rmse *= (1.0f + run.indEmergence.outerMetroVoteCoeff / interceptSize);
		float prevOthersCoeff = run.indEmergence.prevOthersVoteCoeff * prevOthers;
		rmse *= (1.0f + prevOthersCoeff / interceptSize);
		// The quantile should only fall within the upper half of the distribution
		// so that the correlation created using the beta distribution works
		// as intended
		float quantile = rng.beta(indAlpha, indBeta) * 0.5f + 0.5f;
		float variableVote = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis, quantile));
		float transformedVoteShare = variableVote + run.indEmergence.fpThreshold;
		seatFpVoteShare[seatIndex][EmergingIndIndex] += detransformVoteShare(transformedVoteShare);
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

	float swingAdjust = overallFpTarget[OthersIndex] - detransformVoteShare(-77.0f);
	voteShare = basicTransformedSwing(voteShare, swingAdjust);

	// Reduce others vote by the "others" parties already assigned vote share.
	float existingVoteShare = 0.0f;
	if (seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) existingVoteShare += seatFpVoteShare[seatIndex][run.indPartyIndex];
	if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) existingVoteShare += seatFpVoteShare[seatIndex][EmergingIndIndex];
	if (seatFpVoteShare[seatIndex].contains(EmergingPartyIndex)) existingVoteShare += seatFpVoteShare[seatIndex][EmergingPartyIndex];
	voteShare = basicTransformedSwing(voteShare, -existingVoteShare);

	seatFpVoteShare[seatIndex][OthersIndex] = voteShare;
}

void SimulationIteration::adjustForFpCorrelations(int seatIndex)
{
	//Seat const& seat = project.seats().viewByIndex(seatIndex);

	// GRN/IND correlation, move to separate function if any other correlation is analysed.
	if (!seatFpVoteShare[seatIndex].contains(run.grnPartyIndex)) return;
	float currentInd = 0.0f;
	if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) currentInd += seatFpVoteShare[seatIndex][EmergingIndIndex];
	if (seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) currentInd += seatFpVoteShare[seatIndex][run.indPartyIndex];
	float pastInd = 0.0f;
	if (pastSeatResults[seatIndex].fpVotePercent.contains(run.indPartyIndex)) pastInd += pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex];
	float indSwing = currentInd - pastInd;
	// Prevent minor inds (especially past ones) from affecting the overall trend too much
	float scaling = std::clamp(std::max(currentInd, pastInd) / 8.0f - 0.5f, 0.0f, 1.0f);
	float projectedGrnEffect = -0.3981311670993329f * indSwing * scaling;
	float transformedGrnFp = transformVoteShare(seatFpVoteShare[seatIndex][run.grnPartyIndex]);
	transformedGrnFp += projectedGrnEffect;
	seatFpVoteShare[seatIndex][run.grnPartyIndex] = detransformVoteShare(transformedGrnFp);
}

void SimulationIteration::incorporateLiveSeatFps(int seatIndex)
{
	[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
		!run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
		seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
		// we had a confirmed independent but they didn't meet the threshold, move votes to OTH
		seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][run.indPartyIndex];
		seatFpVoteShare[seatIndex][run.indPartyIndex] = 0.0f;
	}
	if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
		!run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
		seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) {
		// we had an emerging independent but they didn't meet the threshold, move votes to OTH
		seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
		seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	}
	if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
		run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
		seatFpVoteShare[seatIndex].contains(EmergingIndIndex) &&
		!seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
		// we had an emerging independent and no confirmed independent and an independent vote meets
		// the threshold, move them to independent
		seatFpVoteShare[seatIndex][run.indPartyIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
		seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	}
	if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
		!run.liveSeatFpTransformedSwing[seatIndex].contains(EmergingIndIndex) &&
		seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) {
		// we still have an emerging ind (so wasn't promoted), and there's no other ind meeting the
		// threshold, so switch to others
		seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
		seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	}
	seatFpVoteShare[seatIndex][OthersIndex] = std::clamp(seatFpVoteShare[seatIndex][OthersIndex], 0.0f, 99.9f);
	seatFpVoteShare[seatIndex][run.indPartyIndex] = std::clamp(seatFpVoteShare[seatIndex][run.indPartyIndex], 0.0f, 99.9f);
	for (auto [partyIndex, swing] : run.liveSeatFpTransformedSwing[seatIndex]) {
		// Ignore major party fps for now
		if (isMajor(partyIndex)) continue;
		[[maybe_unused]] auto prevFpVoteShare = seatFpVoteShare[seatIndex];
		float projectedFp = (pastSeatResults[seatIndex].fpVotePercent.contains(partyIndex) ? 
			pastSeatResults[seatIndex].fpVotePercent.at(partyIndex) : 0.0f);
		float liveTransformedFp = transformVoteShare(projectedFp);
		if (projectedFp == 0.0f || std::isnan(swing) || std::isnan(liveTransformedFp)) {
			liveTransformedFp = transformVoteShare(run.liveSeatFpPercent[seatIndex][partyIndex]);
		}
		else {
			liveTransformedFp += swing;
		}
		float swingDeviation = run.tppSwingFactors.meanSwingDeviation * 2.0f; // placeholder value
		float percentCounted = run.liveSeatFpCounted[seatIndex];
		float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(percentCounted * 0.2f)));
		liveTransformedFp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		if (postCountFpShift.contains(partyIndex)) liveTransformedFp += postCountFpShift[partyIndex];
		float liveFactor = 1.0f - pow(2.0f, -percentCounted * 0.5f);
		float priorFp = seatFpVoteShare[seatIndex][partyIndex];
		if (partyIndex == run.indPartyIndex && priorFp <= 0.0f) priorFp = seatFpVoteShare[seatIndex][EmergingIndIndex];
		float transformedPriorFp = transformVoteShare(priorFp);
		// Sometimes the live results will have an independent showing even if one
		// wasn't expected prior. In these cases, the seat Fp vote share will be a 0,
		// and its transformation will be NaN, so just ignore it and use the live value only.
		float mixedTransformedFp = priorFp > 0.0f ?
			mix(transformedPriorFp, liveTransformedFp, liveFactor) : liveTransformedFp;
		float detransformedFp = detransformVoteShare(mixedTransformedFp);
		seatFpVoteShare[seatIndex][partyIndex] = detransformedFp;
	}
	if (!run.liveSeatFpTransformedSwing[seatIndex].contains(EmergingIndIndex)) {
		seatFpVoteShare[seatIndex][EmergingIndIndex] *= std::min(1.0f, 2.0f / run.liveSeatFpCounted[seatIndex]);
	}
	if (run.liveSeatFpCounted[seatIndex] > 5.0f) {
		seatFpVoteShare[seatIndex][EmergingPartyIndex] *= std::min(1.0f, 2.0f / run.liveSeatFpCounted[seatIndex]);
	}
}

void SimulationIteration::prepareFpsForNormalisation(int seatIndex)
{
	// Subsequent to this procedure, fp vote shares for this seat will
	// be normalised such that their sum equals 100. For for seats with
	// (especially large) increases in a minor party's FP vote, this would result in
	// a decrease in the size of this increase which is not desired.
	// On the other hand, if multiple minor parties are increasing
	// they will crowd each other out to some extent (which is desired)
	// The objective here is to adjust the first preferences such that
	// an increase in only one minor party would not hinder their vote,
	// and this is achieved by lowering the combined fp vote share for the
	// major parties by the same amount as the minor party increased,
	// and maintaining the crowding effect that this normalisation achieves.
	float maxPrevious = 0.0f;
	float totalVotePercent = 0.0f;
	for (auto& [party, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (!isMajor(party) && voteShare > maxPrevious) maxPrevious = voteShare;
	}
	float maxCurrent = 0.0f;
	for (auto& [party, voteShare] : seatFpVoteShare[seatIndex]) {
		if (party == CoalitionPartnerIndex) continue;
		if (!isMajor(party) && voteShare > maxCurrent) maxCurrent = voteShare;
		totalVotePercent += voteShare;
	}
	// Adjustment to prevent high OTH vote from crowding out other minor parties
	// Crowding should only occur between defined parties
	if (seatFpVoteShare[seatIndex][OthersIndex] > pastSeatResults[seatIndex].fpVotePercent[OthersIndex]) {
		maxCurrent += seatFpVoteShare[seatIndex][OthersIndex] - pastSeatResults[seatIndex].fpVotePercent[OthersIndex];
	}
	// Some sanity checks here to make sure major party votes aren't reduced below zero or actually increased
	float diffCeiling = std::min(30.0f, 0.8f * (seatFpVoteShare[seatIndex][0] + seatFpVoteShare[seatIndex][1]));
	float diff = std::max(0.0f, std::min(diffCeiling, maxCurrent - maxPrevious));
	// In live sims, want to avoid reducing actual recorded vote tallies through normalisation
	// so make sure the we adjust the major party vote to make normalisation have minimal effect,
	// especially if more than a trivial amount of vote is counted.
	if (sim.isLiveAutomatic()) {
		float liveFactor = 1.0f - pow(2.0f, -0.5f * run.liveSeatFpCounted[seatIndex]);
		diff = mix(diff, totalVotePercent - 100.0f, liveFactor);
	}
	// The values for the majors (i.e. parties 0 and 1) are overwritten anyway,
	// so this only has the effect of softening effect of the normalisation.
	// This ensures that the normalisation is only punishing to minor parties
	// when more than one rises in votes (thus crowding each other out)
	float partyOneProportion = seatFpVoteShare[seatIndex][0] / (seatFpVoteShare[seatIndex][0] + seatFpVoteShare[seatIndex][1]);
	float partyOneAdjust = diff * partyOneProportion;
	float partyTwoAdjust = diff * (1.0f - partyOneProportion);
	seatFpVoteShare[seatIndex][0] -= partyOneAdjust;
	seatFpVoteShare[seatIndex][1] -= partyTwoAdjust;
}

void SimulationIteration::determineSeatEmergingParties(int seatIndex)
{
	float voteShare = 0.0f;
	determinePopulistFp(seatIndex, EmergingPartyIndex, voteShare);
	seatFpVoteShare[seatIndex][EmergingPartyIndex] = voteShare;
}

void SimulationIteration::allocateMajorPartyFp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);

	[[maybe_unused]] auto oldFpVotes = seatFpVoteShare[seatIndex]; // for debug purposes, since this gets changed later on

	// Step 0: Prepare preference flow calculation functions

	// In general the ALP TPP will correspond to the entered seat margin
	float partyOneCurrentTpp = 50.0f + partyOneNewTppMargin[seatIndex];

	auto calculateEffectivePreferenceFlow = [&](int partyIndex, float voteShare, bool isCurrent) {
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
		float basePreferenceFlow = isCurrent ? overallPreferenceFlow[partyIndex] : run.previousPreferenceFlow[partyIndex];
		float transitionPreferenceFlow = mix(basePreferenceFlow, upperPreferenceFlow, std::min(voteShare - 5.0f, 10.0f) * 0.05f);
		float summedPreferenceFlow = basePreferenceFlow * std::min(voteShare, 5.0f) +
			transitionPreferenceFlow * std::clamp(voteShare - 5.0f, 0.0f, 10.0f) +
			upperPreferenceFlow * std::max(voteShare - 15.0f, 0.0f);
		float effectivePreferenceFlow = std::clamp(summedPreferenceFlow / voteShare, 1.0f, 99.0f);
		return effectivePreferenceFlow;
	};

	auto calculateEffectiveExhaustRate = [&](int partyIndex, bool isCurrent) {
		float baseExhaustRate = isCurrent ? overallExhaustRate[partyIndex] : run.previousExhaustRate[partyIndex];
		return baseExhaustRate;
	};

	// Step 1: get an estimate of preference flows from the previous election

	float previousNonMajorFpShare = 0.0f;
	float previousPartyOnePrefEstimate = 0.0f;
	float previousExhaustRateEstimate = 0.0f;
	float previousExhuastDenominator = 0.0f;

	for (auto [partyIndex, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (isMajor(partyIndex)) continue;
		if (partyIndex == CoalitionPartnerIndex) {
			previousPartyOnePrefEstimate += 0.15f * voteShare;
			continue;
		}
		float exhaustRate = calculateEffectiveExhaustRate(partyIndex, false);
		// Use a special formula for IND-like preference flows that accounts for tactical voting
		if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
			float effectivePreferenceFlow = calculateEffectivePreferenceFlow(partyIndex, voteShare, false);
			if (!preferenceVariation.contains(partyIndex)) {
				preferenceVariation[partyIndex] = rng.normal(0.0f, 15.0f);
			}
			float randomisedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
			previousPartyOnePrefEstimate += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		else {
			float previousPreferences = run.previousPreferenceFlow[partyIndex];
			if (!preferenceVariation.contains(partyIndex)) {
				preferenceVariation[partyIndex] = rng.normal(0.0f, 15.0f);
			}
			float randomisedPreferenceFlow = basicTransformedSwing(previousPreferences, preferenceVariation[partyIndex]);
			previousPartyOnePrefEstimate += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		previousNonMajorFpShare += voteShare * (1.0f - exhaustRate);
		previousExhaustRateEstimate += exhaustRate * voteShare;
		previousExhuastDenominator += voteShare;
	}
	previousExhaustRateEstimate /= previousExhuastDenominator;

	// Step 2: Calculate the preference bias between the estimate from last election and actual results

	// If party two didn't run last election in this seat, we can't make any sensible inferences about
	// the preference bias, so just leave it at zero. (Maybe interpolate from similar seats eventually?)
	bool previousPartyTwoExists = pastSeatResults[seatIndex].fpVotePercent.contains(Mp::Two);
	float preferenceBiasRate = 0.0f;
	float exhaustBiasRate = 0.0f;
	bool majorTcp = pastSeatResults[seatIndex].tcpVotePercent.contains(Mp::One) && pastSeatResults[seatIndex].tcpVotePercent.contains(Mp::Two);
	// Need to calculate:
	// (a) from estimate: % of non-exhausting vote reaching party one
	// (b) from actual results: % of non-exhausting vote reaching party one
	// For (b), difficult to calculate under OPV if TCP includes a non-major (at this stage we don't have separate TPPs)
	// so in that case, also assume standard 
	if (previousPartyTwoExists && previousNonMajorFpShare && (majorTcp || previousExhaustRateEstimate < 0.01f)) {
		float previousPartyOneTppPercent = 0.0f;
		//float previousExhaustRate = 0.0f;
		auto const& fpCounts = pastSeatResults[seatIndex].fpVoteCount;
		auto const& tcpCounts = pastSeatResults[seatIndex].tcpVoteCount;
		auto prevTcpSum = std::accumulate(tcpCounts.begin(), tcpCounts.end(), 0, [](int acc, const auto& el) {return acc + el.second; });
		auto prevFpSum = std::accumulate(fpCounts.begin(), fpCounts.end(), 0, [](int acc, const auto& el) {return acc + el.second; });
		auto prevPartyOneTcpCount = 0;
		auto majorFpSum = fpCounts.at(Mp::One) + fpCounts.at(Mp::Two);
		if (majorTcp) {
			previousPartyOneTppPercent = pastSeatResults[seatIndex].tcpVotePercent[Mp::One];
			prevPartyOneTcpCount = tcpCounts.at(Mp::One);
			// Note, this can theoretically be below 0 in intra-coalition contests
			float previousExhaustRate = 1.0f - float(prevTcpSum - majorFpSum) / float(prevFpSum - majorFpSum);
			if (previousExhaustRate < 0.04f || IgnoreExhaust.contains({run.getTermCode(), seat.name})) previousExhaustRate = 0.0f;
			exhaustBiasRate = previousExhaustRate - previousExhaustRateEstimate;
		}
		// *** Of course, this shouldn't be hard-coded like this: Need to have a pre-redistribution
		// TPP for each seat, but don't want to spend time on that just right now for one special case
		else if (seat.name == "Prahran") {
			previousPartyOneTppPercent = 57.55f;
		}
		else {
			previousPartyOneTppPercent = 50.0f + seat.tppMargin;
			// given the checks above, exhaust rate will be close to zero anyway
		}
		
		// If no TPP count then get it from the known TPP margin
		if (!prevPartyOneTcpCount) prevPartyOneTcpCount = prevTcpSum * (float(previousPartyOneTppPercent) * 0.01f);

		float previousPrefRateEstimate = previousPartyOnePrefEstimate / previousNonMajorFpShare;
		float previousPrefRate = float(prevPartyOneTcpCount - fpCounts.at(Mp::One)) / float(prevTcpSum - majorFpSum);
		// Amount by which actual TPP is higher than estimated TPP, per 1% of the non-exhausted vote
		preferenceBiasRate = previousPrefRate - previousPrefRateEstimate;

		//if (seat.name == "Northcote") {
		//	PA_LOG_VAR(majorTcp);
		//	PA_LOG_VAR(fpCounts);
		//	PA_LOG_VAR(tcpCounts);
		//	PA_LOG_VAR(prevTcpSum);
		//	PA_LOG_VAR(prevPartyOneTcpCount);
		//	PA_LOG_VAR(majorFpSum);
		//	PA_LOG_VAR(previousPartyOneTppPercent);
		//	PA_LOG_VAR(prevPartyOneTcpCount);
		//	PA_LOG_VAR(previousNonMajorFpShare);
		//	PA_LOG_VAR(previousPartyOnePrefEstimate);
		//	PA_LOG_VAR(prevPartyOneTcpCount - fpCounts.at(Mp::One));
		//	PA_LOG_VAR(prevTcpSum - majorFpSum);
		//	PA_LOG_VAR(previousPrefRateEstimate);
		//	PA_LOG_VAR(previousPrefRate);
		//	PA_LOG_VAR(preferenceBiasRate);
		//}
	}

	// Step 3: Get an estimate for the *current* election

	float currentPartyOnePrefs = 0.0f;
	float currentNonMajorFpShare = 0.0f;
	float currentExhaustRateEstimate = 0.0f;
	float currentExhuastDenominator = 0.0f;

	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (isMajor(partyIndex)) continue;
		if (partyIndex == CoalitionPartnerIndex) {
			currentPartyOnePrefs += 0.15f * voteShare;
			continue;
		}
		float exhaustRate = calculateEffectiveExhaustRate(partyIndex, true);
		if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
			float effectivePreferenceFlow = calculateEffectivePreferenceFlow(partyIndex, voteShare, true);
			float randomisedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		else {
			float currentPreferences = overallPreferenceFlow[partyIndex];
			float randomisedPreferenceFlow = basicTransformedSwing(currentPreferences, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		currentNonMajorFpShare += voteShare * (1.0f - exhaustRate);
		currentExhaustRateEstimate += exhaustRate * voteShare;
		currentExhuastDenominator += voteShare;
	}
	currentExhaustRateEstimate /= currentExhuastDenominator;
	currentExhaustRateEstimate = std::clamp(currentExhaustRateEstimate + exhaustBiasRate, 0.0f, 1.0f);

	static bool ProducedExhaustRateWarning = false;
	if (currentExhaustRateEstimate && overallExhaustRate[OthersIndex] < 0.01f && !ProducedExhaustRateWarning) {
		PA_LOG_VAR(overallExhaustRate);
		logger << "Warning: A exhaust rate was produced for an election that appears to be for compulsory preferential voting. Seat concerned: " + seat.name + ", observed exhaust rate: " + std::to_string(currentExhaustRateEstimate) << "\n";
		ProducedExhaustRateWarning = true;
	}

	// Step 4: Adjust the current flow estimate according the bias the previous flow estimate had

	float biasAdjustedPartyOnePrefs = currentPartyOnePrefs + preferenceBiasRate * currentNonMajorFpShare;

	// If it's been determined that the overall preference flow needs a correction, do that here.
	float overallAdjustedPartyOnePrefs = biasAdjustedPartyOnePrefs + prefCorrection * currentNonMajorFpShare;
	float overallAdjustedPartyTwoPrefs = currentNonMajorFpShare - overallAdjustedPartyOnePrefs;

	// Step 5: Actually esimate the major party fps based on these adjusted flows

	// adjust everything that's still being used to be scaled so that the total non-exhausted votes is equal to 100%
	float majorFpShare = seatFpVoteShare[seatIndex][0] + seatFpVoteShare[seatIndex][1];
	float nonExhaustedProportion = mix(1.0f, majorFpShare * 0.01f, currentExhaustRateEstimate * 1.0f);
	float adjustmentFactor = 1.0f / nonExhaustedProportion;
	overallAdjustedPartyOnePrefs *= adjustmentFactor;
	overallAdjustedPartyTwoPrefs *= adjustmentFactor;

	float partyTwoCurrentTpp = 100.0f - partyOneCurrentTpp;

	// Estimate Fps by removing expected preferences from expected tpp, but keeping it above zero
	// (as high 3rd-party fps can combine with a low tpp to push this below zero)
	float newPartyOneFp = predictorCorrectorTransformedSwing(partyOneCurrentTpp, -overallAdjustedPartyOnePrefs);
	float newPartyTwoFp = predictorCorrectorTransformedSwing(partyTwoCurrentTpp, -overallAdjustedPartyTwoPrefs);

	float newPartyOneTpp = overallAdjustedPartyOnePrefs + newPartyOneFp;
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

	// Re-scale back to numbers including exhuasted votes
	newPartyOneFp /= adjustmentFactor;
	newPartyTwoFp /= adjustmentFactor;

	//if (seat.name == "Northern Tablelands") {
	//	PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//	PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
	//	PA_LOG_VAR(partyOneCurrentTpp);
	//	PA_LOG_VAR(partyTwoCurrentTpp);
	//	PA_LOG_VAR(oldFpVotes);
	//	PA_LOG_VAR(seatFpVoteShare[seatIndex]);
	//	PA_LOG_VAR(currentNonMajorFpShare);
	//	PA_LOG_VAR(previousNonMajorFpShare);
	//	PA_LOG_VAR(previousPartyOnePrefEstimate);
	//	PA_LOG_VAR(previousExhaustRateEstimate);
	//	PA_LOG_VAR(previousExhuastDenominator);
	//	PA_LOG_VAR(preferenceBiasRate);
	//	PA_LOG_VAR(majorTcp);
	//	PA_LOG_VAR(overallPreferenceFlow);
	//	PA_LOG_VAR(currentPartyOnePrefs);
	//	PA_LOG_VAR(currentNonMajorFpShare);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
	//	PA_LOG_VAR(newPartyOneTpp);
	//	PA_LOG_VAR(newPartyTwoTpp);
	//	PA_LOG_VAR(totalTpp);
	//	PA_LOG_VAR(addPartyOneFp);
	//	PA_LOG_VAR(newPartyOneFp);
	//	PA_LOG_VAR(newPartyTwoFp);
	//	PA_LOG_VAR(majorFpShare);
	//	PA_LOG_VAR(nonExhaustedProportion);
	//	PA_LOG_VAR(adjustmentFactor);
	//	PA_LOG_VAR(currentExhaustRateEstimate);
	//	PA_LOG_VAR(currentExhuastDenominator);
	//	PA_LOG_VAR(exhaustBiasRate);
	//}

	seatFpVoteShare[seatIndex][Mp::One] = newPartyOneFp;
	seatFpVoteShare[seatIndex][Mp::Two] = newPartyTwoFp;

	//if (checkForNans("after setting seatFpVoteShares")) {
	//	PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//	PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
	//	PA_LOG_VAR(partyOneCurrentTpp);
	//	PA_LOG_VAR(partyTwoCurrentTpp);
	//	PA_LOG_VAR(oldFpVotes);
	//	PA_LOG_VAR(seatFpVoteShare[seatIndex]);
	//	PA_LOG_VAR(currentNonMajorFpShare);
	//	PA_LOG_VAR(previousNonMajorFpShare);
	//	PA_LOG_VAR(previousPartyOnePrefEstimate);
	//	PA_LOG_VAR(previousExhaustRateEstimate);
	//	PA_LOG_VAR(previousExhuastDenominator);
	//	PA_LOG_VAR(preferenceBiasRate);
	//	PA_LOG_VAR(majorTcp);
	//	PA_LOG_VAR(overallPreferenceFlow);
	//	PA_LOG_VAR(currentPartyOnePrefs);
	//	PA_LOG_VAR(currentNonMajorFpShare);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
	//	PA_LOG_VAR(newPartyOneTpp);
	//	PA_LOG_VAR(newPartyTwoTpp);
	//	PA_LOG_VAR(totalTpp);
	//	PA_LOG_VAR(addPartyOneFp);
	//	PA_LOG_VAR(newPartyOneFp);
	//	PA_LOG_VAR(newPartyTwoFp);
	//	PA_LOG_VAR(majorFpShare);
	//	PA_LOG_VAR(nonExhaustedProportion);
	//	PA_LOG_VAR(adjustmentFactor);
	//	PA_LOG_VAR(currentExhaustRateEstimate);
	//	PA_LOG_VAR(currentExhuastDenominator);
	//	PA_LOG_VAR(exhaustBiasRate);
	//	throw 1;
	//}

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
	// By default - assume that the largest non-major non-OTH's primary vote is
	// real, and fix it. If it's really that high preference flows will probably
	// change a bit to accomodate the major party vote anyway
	if (!fixedVote) {
		for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
			if (partyIndex < Mp::Others) continue;
			if (partyIndex == run.indPartyIndex) continue;
			if (voteShare > fixedVote) {
				fixedVote = voteShare;
				fixedParty = partyIndex;
			}
		}
	}

	float totalVoteShare = 0.0f;
	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (partyIndex == fixedParty) continue;
		totalVoteShare += voteShare;
	}
	float totalTarget = 100.0f - fixedVote;
	float correctionFactor = totalTarget / totalVoteShare;

	for (auto& [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (partyIndex == fixedParty) continue;
		voteShare *= correctionFactor;
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
		checkForNans("d");
		applyCorrectionsToSeatFps();
		checkForNans("e");
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
			if (partyIndex == CoalitionPartnerIndex) continue;
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
		if (!isMajor(partyIndex)) totalPrefs += tempOverallFp[partyIndex] * (1.0f - overallExhaustRate[partyIndex]);
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
	checkForNans("d1");
	for (auto [partyIndex, vote] : tempOverallFp) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (partyIndex != OthersIndex) {
			checkForNans("d0a");
			if (isMajor(partyIndex)) continue;
			if (!tempOverallFp[partyIndex]) continue; // avoid division by zero when we have non-existent emerging others
			float correctionFactor = overallFpTarget[partyIndex] / tempOverallFp[partyIndex];
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				if (seatFpVoteShare[seatIndex].contains(partyIndex)) {
					// prevent outlier seats from getting monster swings
					float swingCap = std::max(0.0f, tempOverallFp[partyIndex] * (correctionFactor - 1.0f) * 3.0f);
					float correctionSwing = std::min(swingCap, seatFpVoteShare[seatIndex][partyIndex] * (correctionFactor - 1.0f));
					// don't re-adjust fps when we have a significant actual count
					if (sim.isLiveAutomatic()) correctionSwing *= std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]);
					float newValue = predictorCorrectorTransformedSwing(seatFpVoteShare[seatIndex][partyIndex], correctionSwing);
					seatFpVoteShare[seatIndex][partyIndex] = newValue;
				}
			}
			checkForNans("d0b");
		}
		else {
			checkForNans("d0c");
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				float allocation = seatFpVoteShare[seatIndex][OthersIndex] * (othersCorrectionFactor - 1.0f);
				FloatByPartyIndex categories;
				float totalOthers = 0.0f;
				for (auto [seatPartyIndex, seatPartyVote] : seatFpVoteShare[seatIndex]) {
					// protect independents and quasi-independents from having their votes squashed here
					if (seatPartyIndex == run.indPartyIndex) continue;
					if (!overallFpSwing.contains(seatPartyIndex) && seatPartyIndex >= 2) continue;
					if (seatPartyIndex == OthersIndex || !overallFpTarget.contains(seatPartyIndex)) {
						categories[seatPartyIndex] = seatPartyVote;
						totalOthers += seatPartyVote;
					}
				}
				if (!totalOthers) continue;
				for (auto& [seatPartyIndex, voteShare] : categories) {
					float additionalVotes = allocation * voteShare / totalOthers;
					// don't re-adjust fps when we have a significant actual count
					if (sim.isLiveAutomatic()) additionalVotes *= std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]);
					float newValue = predictorCorrectorTransformedSwing(seatFpVoteShare[seatIndex][seatPartyIndex], additionalVotes);
					seatFpVoteShare[seatIndex][seatPartyIndex] = newValue;
				}
			}
			checkForNans("d0e");
		}
	}
	checkForNans("d2");
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		allocateMajorPartyFp(seatIndex);
	}
	checkForNans("d3");
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
		// Don't readjust fps when there is a meaningful actual fp count
		float seatPartyOneAdjust = partyOneAdjust;
		float seatPartyTwoAdjust = partyTwoAdjust;
		if (sim.isLiveAutomatic()) {
			seatPartyOneAdjust = mix(1.0f, seatPartyOneAdjust, std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]));
			seatPartyTwoAdjust = mix(1.0f, seatPartyTwoAdjust, std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]));
		}
		seatFpVoteShare[seatIndex][Mp::One] = seatFpVoteShare[seatIndex][Mp::One] * seatPartyOneAdjust;
		seatFpVoteShare[seatIndex][Mp::Two] = seatFpVoteShare[seatIndex][Mp::Two] * seatPartyTwoAdjust;
		normaliseSeatFp(seatIndex);
	}
}

void SimulationIteration::determineSeatFinalResult(int seatIndex)
{

	Seat const& seat = project.seats().viewByIndex(seatIndex);
	typedef std::pair<int, float> PartyVotes;
	auto partyVoteLess = [](PartyVotes a, PartyVotes b) {return a.second < b.second; };

	// transfer fp vote shares to vector
	std::vector<PartyVotes> originalVoteShares; // those still in the count
	std::vector<PartyVotes> excludedVoteShares; // excluded from the count, original values
	std::vector<PartyVotes> accumulatedVoteShares;
	if (seatFpVoteShare[seatIndex].contains(CoalitionPartnerIndex)) {
		seatFpVoteShare[seatIndex][CoalitionPartnerIndex] = 0;
	}
	if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex) && seatFpVoteShare[seatIndex][EmergingIndIndex] < detransformVoteShare(run.indEmergence.fpThreshold)) {
		seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
		seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	}
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
			// This is a fallback estimate for parties without a specified within-party exahust rate
			float survivalRate = 1.0f - overallExhaustRate[sourceParty];
			// Fallback figure for major parties when OPV is in force
			if (isMajor(sourceParty) && overallExhaustRate[OthersIndex] > 0.01f) survivalRate = 0.4f;

			// if it's a final-two situation, check if we have known preference flows
			if (int(accumulatedVoteShares.size() == 2)) {
				if (run.ncPreferenceFlow.contains(sourceParty)) {
					auto const& item = run.ncPreferenceFlow[sourceParty];
					std::pair<int, int> targetParties = { accumulatedVoteShares[0].first, accumulatedVoteShares[1].first };
					if (item.contains(targetParties)) {
						float flow = item.at(targetParties);
						float transformedFlow = transformVoteShare(flow);
						transformedFlow += rng.normal(0.0f, 10.0f);
						flow = detransformVoteShare(transformedFlow);
						// later, include custom exhaust rate for known nc preference flows
						accumulatedVoteShares[0].second += sourceVoteShare * 0.01f * flow * survivalRate;
						accumulatedVoteShares[1].second += sourceVoteShare * 0.01f * (100.0f - flow) * survivalRate;
						continue;
					}
				}
			}
			std::vector<float> weights(accumulatedVoteShares.size());
			int alpIndex = -1;
			int lnpIndex = -1;
			int grnIndex = -1;
			int indIndex = -1;
			int othIndex = -1;
			// Ideally this calculation should also estimate exhaustion rates
			// replacing the above fallback values
			for (int targetIndex = 0; targetIndex < int(accumulatedVoteShares.size()); ++targetIndex) {
				auto [targetParty, targetVoteShare] = accumulatedVoteShares[targetIndex];
				if (targetParty == 0) alpIndex = targetIndex;
				if (targetParty == 1) lnpIndex = targetIndex;
				if (targetParty == -4) lnpIndex = targetIndex;
				if (targetParty == -1) othIndex = targetIndex;
				if (targetParty == run.grnPartyIndex) grnIndex = targetIndex;
				if (targetParty == run.indPartyIndex || partyIdeologies[targetParty] == 2) indIndex = targetIndex;
				int ideologyDistance = abs(partyIdeologies[sourceParty] - partyIdeologies[targetParty]);
				if (bothMajorParties(sourceParty, targetParty)) ++ideologyDistance;
				float consistencyBase = PreferenceConsistencyBase[partyConsistencies[sourceParty]];
				float thisWeight = std::pow(consistencyBase, -ideologyDistance);
				float randomFactor = rng.uniform(0.6f, 1.4f);
				thisWeight *= randomFactor;
				thisWeight *= std::sqrt(targetVoteShare);
				weights[targetIndex] = thisWeight;
			}

			// Rather hacky way to handle GRN -> ALP/IND flows in cases where another candidate (usually LNP)
			// is still in the running. Depends on ALP being party index 0,
			// which is the case by my convention but won't apply in an old election without Greens or
			// if someone else makes their own file. Replace with a proper system when convenient.
			if (alpIndex >= 0 && indIndex >= 0 && sourceParty == run.grnPartyIndex &&
				(seat.tppMargin < -5.0f || !isMajor(seat.incumbent))) {
				float combinedWeights = weights[alpIndex] + weights[indIndex];
				float indShare = rng.uniform(0.5f, 0.8f);
				weights[indIndex] = combinedWeights * indShare;
				weights[alpIndex] = combinedWeights * (1.0f - indShare);
			}

			// Same deal with OTH -> ALP/GRN
			if (alpIndex >= 0 && grnIndex >= 0 && sourceParty == -1) {
				float combinedWeights = weights[alpIndex] + weights[grnIndex];
				float grnShare = rng.uniform(0.55f, 0.75f);
				weights[grnIndex] = combinedWeights * grnShare;
				weights[alpIndex] = combinedWeights * (1.0f - grnShare);
			}

			// and OTH -> GRN/LIB
			if (lnpIndex >= 0 && grnIndex >= 0 && sourceParty == -1) {
				float combinedWeights = weights[lnpIndex] + weights[grnIndex];
				float grnShare = rng.uniform(0.35f, 0.65f);
				weights[grnIndex] = combinedWeights * grnShare;
				weights[lnpIndex] = combinedWeights * (1.0f - grnShare);
			}

			float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0000001f); // avoid divide by zero warning
			if ((sourceParty == CoalitionPartnerIndex || sourceParty == 1) && lnpIndex != -1) {
				float totalWeightWithoutLnp = totalWeight - weights[lnpIndex];
				weights[lnpIndex] = totalWeightWithoutLnp * 4.0f;
				totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0000001f); // avoid divide by zero warning
			}
			for (int targetIndex = 0; targetIndex < int(accumulatedVoteShares.size()); ++targetIndex) {
				accumulatedVoteShares[targetIndex].second += sourceVoteShare * weights[targetIndex] / totalWeight * survivalRate;
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
			if (accumulatedVoteShares[0].first == Mp::One && (accumulatedVoteShares[1].first == Mp::Two || accumulatedVoteShares[1].first == CoalitionPartnerIndex)) {
				accumulatedVoteShares[0].second = partyOneNewTppMargin[seatIndex] + 50.0f;
				accumulatedVoteShares[1].second = 50.0f - partyOneNewTppMargin[seatIndex];
				accumulatedVoteShares[1].first = Mp::Two;
				break;
			}
			else if ((accumulatedVoteShares[0].first == Mp::Two || accumulatedVoteShares[0].first == CoalitionPartnerIndex) && accumulatedVoteShares[1].first == Mp::One) {
				accumulatedVoteShares[0].second = 50.0f - partyOneNewTppMargin[seatIndex];
				accumulatedVoteShares[1].second = partyOneNewTppMargin[seatIndex] + 50.0f;
				accumulatedVoteShares[0].first = Mp::Two;
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
			float adjustmentFactor = 100.0f / (accumulatedVoteShares[0].second + accumulatedVoteShares[1].second);
			accumulatedVoteShares[0].second *= adjustmentFactor;
			accumulatedVoteShares[1].second *= adjustmentFactor;
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

	std::pair<PartyVotes, PartyVotes> topTwo = std::minmax(accumulatedVoteShares[0], accumulatedVoteShares[1], partyVoteLess);
	if (topTwo.first.first == CoalitionPartnerIndex) topTwo.first.first = 1;
	if (topTwo.second.first == CoalitionPartnerIndex) topTwo.second.first = 1;

	// For non-standard Tcp scenarios, if it's a match to the previous tcp pair then compare with that
	// and adjust the current results to match.
	if (!bothMajorParties(topTwo.first.first, topTwo.second.first)) {
		//auto const& prevResults = pastSeatResults[seatIndex];
		//if (prevResults.tcpVotePercent.count(topTwo.first.first) && prevResults.tcpVotePercent.count(topTwo.second.first)) {
		//	// Allocate the previous elections fp votes as if it were now
		//	std::vector<PartyVotes> pseudoAccumulated;
		//	std::vector<PartyVotes> pseudoExcluded;
		//	for (auto [party, voteShare] : prevResults.fpVotePercent) {
		//		if (prevResults.tcpVotePercent.count(party)) {
		//			pseudoAccumulated.push_back({ party, voteShare });
		//		}
		//		else {
		//			pseudoExcluded.push_back({ party, voteShare });
		//		}

		//	}
		//	if (pseudoAccumulated[0].first != topTwo.first.first) std::swap(pseudoAccumulated[0], pseudoAccumulated[1]);
		//	allocateVotes(pseudoAccumulated, pseudoExcluded);
		//	float bias = pseudoAccumulated[0].second - prevResults.tcpVotePercent.at(topTwo.first.first);
		//	float totalAllocatedPrev = std::accumulate(pseudoExcluded.begin(), pseudoExcluded.end(), 0.0f,
		//		[](float acc, PartyVotes const& votes) {return acc + votes.second; });
		//	float biasRate = bias / totalAllocatedPrev;
		//	float totalAllocatedNow = std::accumulate(excludedVoteShares.begin(), excludedVoteShares.end(), 0.0f,
		//		[](float acc, PartyVotes const& votes) {return acc + votes.second; });
		//	topTwo.first.second -= biasRate * totalAllocatedNow;
		//	topTwo.second.second += biasRate * totalAllocatedNow;

		//	if (topTwo.second.second < topTwo.first.second) std::swap(topTwo.first, topTwo.second);
		//}

		// if we're live, do further adjustments ...
		if (sim.isLiveAutomatic()) {
			bool matched = false;
			float firstTcp = 0.0f;
			if (sim.isLiveAutomatic() && topTwo.first.first == run.liveSeatTcpParties[seatIndex].first
				&& topTwo.second.first == run.liveSeatTcpParties[seatIndex].second)
			{
				matched = true;
				if (!std::isnan(run.liveSeatTcpSwing[seatIndex]) && pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.first.first) &&
					pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.second.first))
				{
					firstTcp = basicTransformedSwing(pastSeatResults[seatIndex].tcpVotePercent.at(topTwo.first.first), run.liveSeatTcpSwing[seatIndex]);
				}
				else {
					firstTcp = run.liveSeatTcpPercent[seatIndex];
				}
			}
			else if (sim.isLiveAutomatic() && topTwo.first.first == run.liveSeatTcpParties[seatIndex].second
				&& topTwo.second.first == run.liveSeatTcpParties[seatIndex].first)
			{
				matched = true;
				if (!std::isnan(run.liveSeatTcpSwing[seatIndex]) && pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.first.first) &&
					pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.second.first))
				{
					firstTcp = basicTransformedSwing(pastSeatResults[seatIndex].tcpVotePercent.at(topTwo.first.first), -run.liveSeatTcpSwing[seatIndex]);
				}
				else {
					firstTcp = 100.0f - run.liveSeatTcpPercent[seatIndex];
				}
			}
			if (matched) {
				float transformedTcpCalc = transformVoteShare(topTwo.first.second);
				float transformedTcpLive = transformVoteShare(firstTcp);
				float liveSwingDeviation = std::min(10.0f, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatTcpCounted[seatIndex] * 0.2f)));
				transformedTcpLive += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
				float liveFactor = 1.0f - pow(2.0f, -run.liveSeatTcpCounted[seatIndex] * 0.2f);
				float mixedTransformedTpp = mix(transformedTcpCalc, transformedTcpLive, liveFactor);
				topTwo.first.second = detransformVoteShare(mixedTransformedTpp);
				topTwo.second.second = 100.0f - topTwo.first.second;
			}
		}
	}

	// incorporate non-classic live 2pp results
	if (sim.isLiveAutomatic() && !(isMajor(topTwo.first.first) && isMajor(topTwo.second.first))) {
		float tcpLive = topTwo.first.second;
		if (topTwo.first.first == run.liveSeatTcpParties[seatIndex].first && topTwo.second.first == run.liveSeatTcpParties[seatIndex].second) {
			tcpLive = run.liveSeatTcpPercent[seatIndex];
		}
		else if (topTwo.first.first == run.liveSeatTcpParties[seatIndex].second && topTwo.second.first == run.liveSeatTcpParties[seatIndex].first) {
			tcpLive = 100.0f - run.liveSeatTcpPercent[seatIndex];
		}
		float liveTransformedTcp = transformVoteShare(tcpLive);
		float priorTransformedTcp = transformVoteShare(topTwo.first.second);
		if (!isMajor(topTwo.first.first) && isMajor(topTwo.second.first)) {
			liveTransformedTcp -= 0.8f; // lazy adjustment for poor performance of 3rd parties in declarations/postals
		}
		else if (isMajor(topTwo.first.first) && !isMajor(topTwo.second.first)) {
			liveTransformedTcp += 0.8f; // lazy adjustment for poor performance of 3rd parties in declarations/postals
		}
		float swingDeviation = run.tppSwingFactors.meanSwingDeviation * 1.5f;
		float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatTcpBasis[seatIndex] * 0.2f)));
		liveTransformedTcp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		float liveFactor = 1.0f - pow(2.0f, -run.liveSeatTcpCounted[seatIndex] * 0.2f);
		float mixedTransformedTcp = mix(priorTransformedTcp, liveTransformedTcp, liveFactor);
		topTwo.first.second = detransformVoteShare(mixedTransformedTcp);
		topTwo.second.second = 100.0f - topTwo.first.second;
		if (topTwo.first.second > topTwo.second.second) std::swap(topTwo.first, topTwo.second);
	}

	seatWinner[seatIndex] = topTwo.second.first;
	auto byParty = std::minmax(topTwo.first, topTwo.second); // default pair operator orders by first element

	seatTcpVoteShare[seatIndex] = { {byParty.first.first, byParty.second.first}, byParty.first.second };

	if (sim.isLive()) applyLiveManualOverrides(seatIndex);
}

void SimulationIteration::applyLiveManualOverrides(int seatIndex)
{
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
	partyWins[EmergingPartyIndex] = 0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		int partyIndex = seatWinner[seatIndex];
		if (partyIndex == EmergingIndIndex) partyIndex = run.indPartyIndex;
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
		for (auto a = sortedPartyWins.begin(); a != sortedPartyWins.end(); ++a) {
			if (a->first == run.indPartyIndex) {
				sortedPartyWins.erase(a);
				break;
			}
		}
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
	recordSwingFactors();
	recordMajorityResult();
	recordPartySeatWinCounts();
}

void SimulationIteration::recordVoteTotals()
{
	double totalTpp = 0.0;
	std::map<int, double> totalFp;
	double totalTurnout = 0.0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		double turnout = double(run.pastSeatResults[seatIndex].turnoutCount);
		double turnoutScaledTpp = double(partyOneNewTppMargin[seatIndex] + 50.0) * turnout;
		for (auto [party, vote] : seatFpVoteShare[seatIndex]) {
			if (tempOverallFp.contains(party)) {
				totalFp[party] += vote * turnout;
			}
			else {
				totalFp[-1] += vote * turnout;
			}
		}
		totalTpp += turnoutScaledTpp;
		totalTurnout += turnout;
	}
	totalTpp /= totalTurnout;
	for (auto& [party, vote] : totalFp) vote /= totalTurnout;
	short tppBucket = short(floor(totalTpp * 10.0f));
	++sim.latestReport.tppFrequency[tppBucket];

	float othersFp = 0.0f;
	for (auto const& [partyIndex, fp]: totalFp) {
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

void SimulationIteration::recordSwingFactors()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		run.seatRegionSwingSums[seatIndex] += seatRegionSwing[seatIndex];
		run.seatElasticitySwingSums[seatIndex] += seatElasticitySwing[seatIndex];
		run.seatLocalEffectsSums[seatIndex] += seatLocalEffects[seatIndex];
		run.seatPreviousSwingEffectSums[seatIndex] += seatPreviousSwingEffect[seatIndex];
		run.seatFederalSwingEffectSums[seatIndex] += seatFederalSwingEffect[seatIndex];
	}
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
