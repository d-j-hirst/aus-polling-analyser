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
static std::mutex debugMutex;

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

bool isMajor(int partyIndex, int natPartyIndex = -100) {
	return partyIndex == Mp::One || partyIndex == Mp::Two || partyIndex == natPartyIndex;
}

SimulationIteration::SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void SimulationIteration::reset()
{
	pastSeatResults.clear();
	regionSeatCount.clear();
	partyWins.clear();
	regionSwing.clear();
	partyOneNewTppMargin.clear();
	seatWinner.clear();
	seatFpVoteShare.clear();
	seatTcpVoteShare.clear();
	iterationOverallTpp = 0.0f;
	iterationOverallSwing = 0.0f;
	daysToElection = 0;
	overallFpTarget.clear();
	overallFpSwing.clear();
	overallPreferenceFlow.clear();
	overallExhaustRate.clear();
	homeRegion.clear();
	seatContested.clear();
	centristPopulistFactor.clear();
	partyIdeologies.clear();
	partyConsistencies.clear();
	fpModificationAdjustment.clear();
	tempOverallFp.clear();
	nationalsShare.clear();

	postCountFpShift.clear();

	seatRegionSwing.clear();
	seatElasticitySwing.clear();
	seatLocalEffects.clear();
	seatPreviousSwingEffect.clear();
	seatFederalSwingEffect.clear();
	seatByElectionEffect.clear();
	seatThirdPartyExhaustEffect.clear();
	seatPollEffect.clear();
	seatMrpPollEffect.clear();

	prefCorrection = 0.0f;
	overallFpError = 0.0f;
	nonMajorFpError = 0.0f;
	othersCorrectionFactor = 0.0f;
	fedStateCorrelation = 0.0f;
	ppvcBias = 0.0f;
	liveSystemicBias = 0.0f;
	decVoteBias = 0.0f;
	indAlpha = 1.0f;
	indBeta = 1.0f;

	partySupport = std::array<int, 2>();
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
		determineIntraCoalitionSwing();
		determineIndDistributionParameters();
		determinePpvcBias();
		determineDecVoteBias();
		determineRegionalSwings();

		if (checkForNans("Before seat initial results")) {
			reset();
			continue;
		}

		determineSeatInitialResults();

		if (checkForNans("Before reconciling")) {
			reset();
			continue;
		}

		reconcileSeatAndOverallFp();

		if (checkForNans("Before reconciling")) {
			reset();
			continue;
		}

		if (checkForNans("After reconciling")) {
			reset();
			continue;
		}

		seatTcpVoteShare.resize(project.seats().count());
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			determineSeatFinalResult(seatIndex);
		}

		assignDirectWins();
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
	seatByElectionEffect = std::vector<double>(project.seats().count(), 0.0);
	seatThirdPartyExhaustEffect = std::vector<double>(project.seats().count(), 0.0);
	seatPollEffect = std::vector<double>(project.seats().count(), 0.0);
	seatMrpPollEffect = std::vector<double>(project.seats().count(), 0.0);
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
	auto& projection = project.projections().access(0);
	auto projectedSample = projection.generateNowcastSupportSample(project.models(), project.projections().view(sim.settings.baseProjection).getSettings().endDate);
	daysToElection = projectedSample.daysToElection;
	iterationOverallTpp = projectedSample.voteShare.at(TppCode);

	if (run.isLiveAutomatic()) {
		iterationOverallTpp = detransformVoteShare(
			transformVoteShare(iterationOverallTpp) + run.liveElection->getFinalSpecificTppDeviation()
		);
	}

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
	if (run.isLiveAutomatic()) {
		// When incorporating live election fp totals, we need to make sure the
		// parties' target totals add up to 100%, which can be done by re-adjusting the
		// major parties' targets to account for the minor parties' changes.
		float netMinorChange = 0.0f;
		for (auto [partyIndex, _] : overallFpTarget) {
			if (partyIndex == 0 || partyIndex == 1) continue;
			float minorChange = detransformVoteShare(
				transformVoteShare(overallFpTarget[partyIndex]) + run.liveElection->getFinalSpecificFpDeviations(partyIndex)
			) - overallFpTarget[partyIndex];
			netMinorChange += minorChange;
			overallFpTarget[partyIndex] += minorChange;
		}
		float majorTotals = overallFpTarget[0] + overallFpTarget[1];
		float correctionFactor = (majorTotals - netMinorChange) / majorTotals;
		overallFpTarget[0] *= correctionFactor;
		overallFpTarget[1] *= correctionFactor;
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
}

void SimulationIteration::determineIntraCoalitionSwing()
{
	float baseOverallRmse = run.nationalsParameters.rmse;
	float baseOverallKurtosis = run.nationalsParameters.kurtosis;
	// Need to have live results influence this
	intraCoalitionSwing = rng.flexibleDist(0.0f, baseOverallRmse, baseOverallRmse, baseOverallKurtosis, baseOverallKurtosis);
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
	// constexpr float DefaultPpvcBiasStdDev = 6.0f;
	// float defaultPpvcBias = rng.normal(0.0f, DefaultPpvcBiasStdDev);
	// float observedPpvcStdDev = DefaultPpvcBiasStdDev * std::pow(400000.0f / std::max(run.ppvcBiasConfidence, 0.1f), 0.6f);
	// float observedWeight = 1.0f / observedPpvcStdDev;
	// float originalWeight = 1.0f;
	// float mixFactor = observedWeight / (originalWeight + observedWeight);
	// float observedPpvcBias = rng.normal(run.ppvcBiasObserved, std::min(DefaultPpvcBiasStdDev, observedPpvcStdDev));
	// ppvcBias = mix(defaultPpvcBias, observedPpvcBias, mixFactor);
	//if (run.isLive()) {
	//	std::lock_guard<std::mutex> lock(debugMutex);
	//	PA_LOG_VAR(defaultPpvcBias);
	//	PA_LOG_VAR(run.ppvcBiasConfidence);
	//	PA_LOG_VAR(run.ppvcBiasObserved);
	//	PA_LOG_VAR(observedPpvcStdDev);
	//	PA_LOG_VAR(observedWeight);
	//	PA_LOG_VAR(originalWeight);
	//	PA_LOG_VAR(mixFactor);
	//	PA_LOG_VAR(std::min(DefaultPpvcBiasStdDev, observedPpvcStdDev));
	//	PA_LOG_VAR(observedPpvcBias);
	//	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
	//		PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//		PA_LOG_VAR(run.liveSeatPpvcSensitivity[seatIndex]);
	//		PA_LOG_VAR(run.liveSeatDecVoteSensitivity[seatIndex]);
	//		PA_LOG_VAR(run.liveEstDecVoteRemaining[seatIndex]);
	//	}
	//}
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
	// 0.75f is a subjective guesstimate, too little data to calculate the
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
				if (contains(run.runningParties[seatIndex], party.abbreviation)) {
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
				if ((party.abbreviation == "ON" || party.abbreviation == "ONP") && std::stoi(run.yearCode) >= 2024) {
					// One Nation has announced their intention to run candidates in ~all seats, so assume this will happen for now
					HalfSeatsPrimary = 0.5f;
				}
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
			float lowerRmse = std::max((0.5f - std::abs(estimatedSeats - totalSeats * 0.5f) / totalSeats) * 0.6f, 0.01f) * totalSeats;
			float upperRmse = std::min((totalSeats - estimatedSeats) * 1.0f, lowerRmse);
			int actualSeats = int(floor(std::clamp(rng.flexibleDist(estimatedSeats, lowerRmse, upperRmse), std::max(7.0f, estimatedSeats * 0.4f), totalSeats) + 0.5f));
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
		float pollRawDeviation = run.regionSwingDeviations[regionIndex];
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
		float pollRawDeviation = run.regionSwingDeviations[regionIndex];
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
	if (run.isLiveAutomatic()) {
		float transformedBaselineTpp = run.liveElection->getRegionTppBaseline(regionIndex);
		float transformedLiveSwing = run.liveElection->getRegionFinalSpecificTppDeviation(regionIndex);
		float transformedLiveTpp = transformedBaselineTpp + transformedLiveSwing;
		float untransformedLiveSwingDeviation = detransformVoteShare(transformedLiveTpp - transformedBaselineTpp);
		regionSwing[regionIndex] += untransformedLiveSwingDeviation;
	}

	// if (run.isLive() && run.liveRegionTppBasis[regionIndex]) {
	// 	float liveSwing = run.liveRegionSwing[regionIndex];
	// 	//float liveStdDev = stdDevSingleSeat(run.liveRegionTppBasis[regionIndex]) * 0.4f;
	// 	float liveStdDev = stdDevSingleSeat(run.liveRegionTppBasis[regionIndex]) * 1.0f;
	// 	liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	// 	liveSwing += liveSystemicBias;
	// 	float priorWeight = 1.0f;
	// 	float liveWeight = 1.0f / (liveStdDev * liveStdDev);
	// 	float priorSwing = regionSwing[regionIndex];
	// 	regionSwing[regionIndex] = mix(priorSwing, liveSwing, liveWeight / (priorWeight + liveWeight));
	// }
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
		determineSeatInitialFp(seatIndex);
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatTpp(seatIndex);
	}

	correctSeatTppSwings();

	nationalsShare.resize(project.seats().count());
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineNationalsShare(seatIndex);
		allocateMajorPartyFp(seatIndex);
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
	const float byElectionEffect = run.tppSwingFactors.byElectionSwingModifier * seat.byElectionSwing * logitDeriv(tppPrev);

	float thirdPartyExhaustEffect = 0.0f;
	// Adjust the TPP to take into account exhaustion under OPV from aligned 3rd-party candidates
	// This code should at some point be adjusted to generalise for other seats
	if (seat.name == "Kiama" && run.getTermCode() == "2023nsw") {
		const float indShare = seatFpVoteShare[seatIndex][run.indPartyIndex];
		// Assumes that this IND takes votes 80/20 from LNP/ALP and 50% then exhaust
		float bias = basicTransformedSwing(0.8f, rng.normal(0.0f, 0.15f));
		float exhaustRate = basicTransformedSwing(0.5f, rng.normal(0.0f, 0.15f));
		const float alpBase = tppPrev - indShare * ((1.0f - bias) * exhaustRate);
		const float lnpBase = (100.0f - tppPrev) - indShare * (bias * exhaustRate);
		const float alpNew = alpBase / (alpBase + lnpBase) * 100.0f;
		thirdPartyExhaustEffect = alpNew - tppPrev;
	}
	transformedTpp += thisRegionSwing + elasticitySwing;
	// Add modifiers for known local effects
	transformedTpp += localEffects;
	// Remove the average local modifier across the region
	// Only do this for federal elections since we don't have regional swing estimates otherwise
	if (run.regionCode == "fed") transformedTpp -= run.regionLocalModifierAverage[seat.region];
	transformedTpp += previousSwingEffect;
	transformedTpp += federalSwingEffect;
	transformedTpp += byElectionEffect;
	transformedTpp += thirdPartyExhaustEffect;
	// All non-polling factors taken into account, so now adjust towards available seat polls
	const float MrpPollWeight = 0.1f;
	float mrpPollEffect = MrpPollWeight * (run.seatTppMrpPolls[seatIndex] ? run.seatTppMrpPolls[seatIndex] - detransformVoteShare(transformedTpp) : 0.0f);
	transformedTpp += mrpPollEffect;
	const float PollWeight = (run.regionCode == "fed") ? 0.23f : 0.345f;
	float pollEffect = PollWeight * (run.seatTppPolls[seatIndex] ? run.seatTppPolls[seatIndex] - detransformVoteShare(transformedTpp) : 0.0f);
	transformedTpp += pollEffect;
	// All fixed factors taken to account, add random factors
	float swingDeviation = run.tppSwingFactors.meanSwingDeviation;
	if (run.regionCode == "fed") swingDeviation += run.tppSwingFactors.federalModifier;
	if (useVolatility) swingDeviation = 0.75f * volatility + 0.25f * swingDeviation;
	float kurtosis = run.tppSwingFactors.swingKurtosis;
	// Add random noise to the new margin of this seat
	transformedTpp += rng.flexibleDist(0.0f, swingDeviation, swingDeviation, kurtosis, kurtosis);
	if (run.isLive()) {
		transformedTpp += run.liveElection->getSeatTppDeviation(seat.name);
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
		
		// float tppLive = (tppPrev + run.liveSeatTppSwing[seatIndex] > 10.0f ?
		// 	tppPrev + run.liveSeatTppSwing[seatIndex] :
		// 	predictorCorrectorTransformedSwing(tppPrev, run.liveSeatTppSwing[seatIndex]));
		// //tppLive = basicTransformedSwing(tppLive, ppvcBias * run.liveSeatPpvcSensitivity[seatIndex]);
		// //tppLive = basicTransformedSwing(tppLive, decVoteBias * run.liveSeatDecVoteSensitivity[seatIndex]);
		// float liveTransformedTpp = transformVoteShare(tppLive);
		// liveTransformedTpp += liveSystemicBias;
		// float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatTppBasis[seatIndex] * 0.2f)));
		// //if (run.liveSeatTppBasis[seatIndex] > 0.6f) liveSwingDeviation = std::min(liveSwingDeviation, run.liveEstDecVoteRemaining[seatIndex] * 0.05f);
		// liveTransformedTpp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		// float liveFactor = 1.0f - pow(2.0f, -run.liveSeatTppBasis[seatIndex] * 0.2f);
		// float mixedTransformedTpp = mix(transformedTpp, liveTransformedTpp, liveFactor);
		// partyOneNewTppMargin[seatIndex] = detransformVoteShare(mixedTransformedTpp) - 50.0f;
	}
	else {
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
	}


	const float totalFixedEffects = thisRegionSwing + elasticitySwing + localEffects + previousSwingEffect +
		federalSwingEffect + byElectionEffect + thirdPartyExhaustEffect;
	const float fixedSwingSize = detransformVoteShare(transformVoteShare(tppPrev) + totalFixedEffects) - tppPrev;
	const float transformFactor = fixedSwingSize / totalFixedEffects;
	seatRegionSwing[seatIndex] += double(thisRegionSwing * transformFactor);
	seatElasticitySwing[seatIndex] += double(elasticitySwing * transformFactor);
	seatLocalEffects[seatIndex] += double(localEffects * transformFactor);
	seatPreviousSwingEffect[seatIndex] += double(previousSwingEffect * transformFactor);
	seatFederalSwingEffect[seatIndex] += double(federalSwingEffect * transformFactor);
	seatByElectionEffect[seatIndex] += double(byElectionEffect * transformFactor);
	seatThirdPartyExhaustEffect[seatIndex] += double(thirdPartyExhaustEffect * transformFactor);
	seatPollEffect[seatIndex] += double(pollEffect * transformFactor);
	seatMrpPollEffect[seatIndex] += double(mrpPollEffect * transformFactor);
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
				//if (run.isLive()) {
				//	// If a seat has much live data, don't adjust it any more.
				//	swingAdjust *= std::min(1.0f, 2.0f / run.liveSeatTcpCounted[seatIndex] - 0.2f);
				//}
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
		//if (run.isLive()) {
		//	// If a seat has much live data, don't adjust it any more.
		//	swingAdjust *= std::min(1.0f, 2.0f / run.liveSeatTcpCounted[seatIndex] - 0.2f);
		//}
		partyOneNewTppMargin[seatIndex] += swingAdjust;
	}
}

void SimulationIteration::determineSeatInitialFp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	seatFpVoteShare.resize(project.seats().count());
	// add any "prominent minors" to seat results so that they can be processed.
	// also keep a record of the party indices.
	for (auto const& minorParty : run.seatProminentMinors[seatIndex]) {
		if (!pastSeatResults[seatIndex].fpVotePercent.contains(minorParty)) {
			// make sure a prominent candidate clears the threshold to not be counted as
			// a generic populist if the party didn't run here before.
			pastSeatResults[seatIndex].fpVotePercent[minorParty] = detransformVoteShare(run.indEmergence.fpThreshold + 0.01f);
		}
	}
	auto tempPastResults = pastSeatResults[seatIndex].fpVotePercent;

	for (auto [partyIndex, voteShare] : tempPastResults) {
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
		bool isNational = partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("NAT"));
		if (isNational) {
			// For now, count Nationals primaries as Liberal/Coalition
			// They will be reallocated at a later stage
			seatFpVoteShare[seatIndex][Mp::Two] += voteShare;
			continue;
		}
		bool effectiveGreen = partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("GRN"));
		if (!overallFpSwing.contains(partyIndex)) effectiveGreen = false;
		bool effectiveIndependent = partyIndex >= Mp::Others && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("IND"));
		if (!overallFpSwing.contains(partyIndex)) effectiveIndependent = true;
		if (transformVoteShare(voteShare) < run.indEmergence.fpThreshold) effectiveIndependent = false;
		bool effectivePopulist = partyIndex >= Mp::Others && !effectiveGreen && !effectiveIndependent &&
			(overallFpSwing.contains(partyIndex) || contains(run.seatProminentMinors[seatIndex], partyIndex));
		if (effectiveGreen) {
			determineSpecificPartyFp(seatIndex, partyIndex, voteShare, run.greensSeatStatistics);
		}
		// only continue to apply independent votes if they're an incumbent
		// assume re-runs don't happen unless explicitly confirmed
		else if (effectiveIndependent && (
			project.parties().idToIndex(seat.incumbent) == partyIndex || contains(run.seatProminentMinors[seatIndex], partyIndex)
		)) {
			determineSpecificPartyFp(seatIndex, partyIndex, voteShare, run.indSeatStatistics);
		}
		else if (effectivePopulist) {
			determinePopulistFp(seatIndex, partyIndex, voteShare);
		}
		else if (partyIndex >= Mp::Others) {
			// For non-major candidates that don't fit into the above categories,
			// convert their past votes into "Others" votes
			tempPastResults[OthersIndex] += tempPastResults[partyIndex];
			tempPastResults[partyIndex] = 0;
			continue;
		}
		if (partyIndex == OthersIndex) {
			if (run.runningParties[seatIndex].size() && !contains(run.runningParties[seatIndex], OthersCode)) continue;
		}
		// Note: this means major party vote shares get passed on as-is
		seatFpVoteShare[seatIndex][partyIndex] += voteShare;
	}

	pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = tempPastResults[OthersIndex];
	determineSeatEmergingParties(seatIndex);

	if (seat.confirmedProminentIndependent) determineSeatConfirmedInds(seatIndex);

	determineSeatEmergingInds(seatIndex);

	determineSeatOthers(seatIndex);

	adjustForFpCorrelations(seatIndex);

	if (run.isLiveAutomatic()) incorporateLiveSeatFps(seatIndex);

	// Helps to effect minor party crowding, i.e. if too many minor parties
	// rise in their fp vote, then they're all reduced a bit more than if only one rose.
	prepareFpsForNormalisation(seatIndex);
	normaliseSeatFp(seatIndex);
}

void SimulationIteration::determineSpecificPartyFp(int seatIndex, int partyIndex, float& voteShare, SimulationRun::SeatStatistics const seatStatistics) {
	Seat const& seat = project.seats().viewByIndex(seatIndex);

	if (run.runningParties[seatIndex].size() && partyIndex >= Mp::Others &&
		!contains(run.runningParties[seatIndex], project.parties().viewByIndex(partyIndex).abbreviation)) {
		voteShare = 0.0f;
		return;
	}
	if (run.runningParties[seatIndex].size() && partyIndex == run.indPartyIndex && !seat.incumbentRecontestConfirmed) {
		if (!contains(run.runningParties[seatIndex], project.parties().viewByIndex(partyIndex).abbreviation)) {
			voteShare = 0.0f;
			return;
		}
	}
	if (partyIndex == run.indPartyIndex && seat.confirmedProminentIndependent) {
		// this case will be handled by the "confirmed independent" logic instead
		voteShare = 0.0f;
		return;
	}
	float modifiedVoteShare = voteShare;
	float minorViability = run.seatMinorViability[seatIndex][partyIndex];
	float minorVoteMod = 1.0f + (0.4f * minorViability);
	modifiedVoteShare *= minorVoteMod;

	float transformedFp = transformVoteShare(modifiedVoteShare);
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
	if (run.runningParties[seatIndex].size() && partyIndex >= Mp::Others &&
		contains(run.runningParties[seatIndex], project.parties().viewByIndex(partyIndex).abbreviation)) {
		recontestRateMixed = 1.0f;
	}
	if (run.runningParties[seatIndex].size() && partyIndex == OthersIndex &&
		contains(run.runningParties[seatIndex], OthersCode) || contains(run.runningParties[seatIndex], std::string("IND"))) {
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
	if (partyIndex >= Mp::Others && contains(run.seatProminentMinors[seatIndex], partyIndex))
	{
		transformedFp += rng.uniform(0.0f, 15.0f);
	}

	constexpr float OddsWeight = 0.6f;
	if (run.oddsCalibrationMeans.contains({ seatIndex, partyIndex })) {
		transformedFp = run.oddsCalibrationMeans[{seatIndex, partyIndex}];
	}
	else if (run.oddsFinalMeans.contains({ seatIndex, partyIndex })) {
		transformedFp = mix(transformedFp, run.oddsFinalMeans[{seatIndex, partyIndex}], OddsWeight);
	}

	float quantile = partyIndex == run.indPartyIndex ? rng.beta(indAlpha, indBeta) : rng.uniform();
	float variableVote = rng.flexibleDist(0.0f, lowerRmseMixed, upperRmseMixed, lowerKurtosisMixed, upperKurtosisMixed, quantile);
	transformedFp += variableVote;

	// Model can't really deal with the libs not existing (=> large OTH vote) in Richmond 2018
	// so likely underestimates GRN fp support here. This is a temporary workaround to bring in line
	// with other seats expecting a small ~3% TCP swing to greens, hopefully will find a better fix for this later.
	if (partyIndex == 2 && seat.name == "Richmond" && run.getTermCode() == "2022vic") {
		transformedFp += 5.5f;
	}

	float regularVoteShare = detransformVoteShare(transformedFp);

	if (partyIndex >= Mp::Others && contains(run.seatProminentMinors[seatIndex], partyIndex)) {
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare,
			(1.0f - std::clamp(regularVoteShare / ProminentMinorFlatBonusThreshold, 0.0f, 1.0f))
			* ProminentMinorFlatBonus * minorVoteMod
		);
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare, rng.uniform() * rng.uniform() * ProminentMinorBonusMax * minorVoteMod * minorVoteMod
		);
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
	if (run.runningParties[seatIndex].size() && partyIndex >= Mp::Others &&
		!contains(run.runningParties[seatIndex], project.parties().viewByIndex(partyIndex).abbreviation)) {
		voteShare = 0.0f;
		return;
	}
	bool prominent = partyIndex >= Mp::Others && contains(seat.prominentMinors, project.parties().viewByIndex(partyIndex).abbreviation);
	float partyFp = overallFpTarget[partyIndex];
	if (partyFp == 0.0f && !prominent) {
		voteShare = 0.0f;
		return;
	}

	if (seatContested.contains(partyIndex)) {
		if (!seatContested[partyIndex][seatIndex] && !prominent && project.parties().idToIndex(seat.incumbent) != partyIndex) {
			voteShare = 0.0f;
			return;
		}
	}

	constexpr float OddsWeight = 0.9f;
	const float ProminentPopulistBaseFp = transformVoteShare(15.0f);
	float effectivePartyFp = partyFp;
	if (run.oddsCalibrationMeans.contains({ seatIndex, partyIndex })) {
		effectivePartyFp = detransformVoteShare(run.oddsCalibrationMeans[{seatIndex, partyIndex}]);
	}
	else if (run.oddsFinalMeans.contains({ seatIndex, partyIndex })) {
		effectivePartyFp = detransformVoteShare(mix(ProminentPopulistBaseFp, run.oddsFinalMeans[{seatIndex, partyIndex}], OddsWeight));
	}

	float seatModifier = partyFp ? calculateEffectiveSeatModifier(seatIndex, partyIndex) : 1.0f;
	float adjustedModifier = std::max(0.2f, seatModifier * (fpModificationAdjustment.contains(partyIndex) ? fpModificationAdjustment[partyIndex] : 1.0f));
	float modifiedFp1 = predictorCorrectorTransformedSwing(effectivePartyFp, effectivePartyFp * (adjustedModifier - 1.0f));
	float modifiedFp2 = effectivePartyFp * adjustedModifier;
	float incumbentFp = project.parties().idToIndex(seat.incumbent) == partyIndex ? voteShare : 0.0f;
	// Choosing the lower of modifiedFp1 and modifiedFp2 prevents the fp from being >= 100.0f in some scenarios
	float modifiedFp = std::clamp(modifiedFp1, incumbentFp, modifiedFp2);
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
	bool ballotConfirmed = contains(run.runningParties[seatIndex], project.parties().viewByIndex(run.indPartyIndex).abbreviation);
	if (run.runningParties[seatIndex].size() && !ballotConfirmed) {
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

		constexpr float OddsWeight = 0.6f;
		float adjustedWeight = seat.name == "Kiama" && run.getTermCode() == "2023nsw" ? 1.0f : OddsWeight;
		constexpr float oddsBasedVariation = 20.0f;
		if (run.oddsCalibrationMeans.contains({ seatIndex, run.indPartyIndex })) {
			const float voteShareCenter = run.oddsCalibrationMeans[{seatIndex, run.indPartyIndex}];
			transformedVoteShare = rng.normal(voteShareCenter, oddsBasedVariation);
		}
		// else, because if we're calibrating betting results we don't want seat polls to interfere with that 
		else {
			if (run.oddsFinalMeans.contains({ seatIndex, run.indPartyIndex })) {
				if (rng.uniform() < adjustedWeight) {
					const float voteShareCenter = run.oddsFinalMeans[{seatIndex, run.indPartyIndex}];
					float oddsBasedVoteShare = rng.normal(voteShareCenter, oddsBasedVariation);
					transformedVoteShare = oddsBasedVoteShare;
				}
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
		}

		seatFpVoteShare[seatIndex][run.indPartyIndex] = std::max(seatFpVoteShare[seatIndex][run.indPartyIndex], detransformVoteShare(transformedVoteShare));
	}
}

void SimulationIteration::determineSeatEmergingInds(int seatIndex)
{
	[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
	// For an emerging ind to be allowed must have one of the following:
	//  -final ballot is not available yet
	//  -at least two inds in the seat
	//  -one ind that isn't confirmed
	bool ballotConfirmed = run.indCount[seatIndex] >= 2 || (run.indCount[seatIndex] >= 1 && !seatFpVoteShare[seatIndex].contains(run.indPartyIndex));
	if (run.runningParties[seatIndex].size() && !ballotConfirmed) {
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
	bool existingStrongCandidate = false;
	for (auto [partyIndex, vote] : seatFpVoteShare[seatIndex]) {
		if (partyIndex < 0) continue;
		auto const& party = project.parties().viewByIndex(partyIndex);
		if ((
				party.ideology == 2 &&
				party.countAsParty == Party::CountAsParty::None &&
				party.supportsParty == Party::SupportsParty::None) ||
			partyIndex == run.indPartyIndex)
		{
			existingStrongCandidate = true;
			break;
		}
	}
	// Less chance of independents emerging when there's already a strong candidate
	if (existingStrongCandidate) indEmergenceRate *= std::min(run.indEmergenceModifier, 0.3f);
	// but in other situations we want ind running chance to be affected by the presence of other confirmed independents
	else indEmergenceRate *= run.indEmergenceModifier;
	// If a notable independent hasn't emerged by the time candidacy is confirmed, much less likely they will
	if (run.runningParties[seatIndex].size()) indEmergenceRate *= 0.1f;
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
	[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
	constexpr float MinPreviousOthFp = 2.0f;
	float voteShare = MinPreviousOthFp;
	if (pastSeatResults[seatIndex].fpVotePercent.contains(OthersIndex)) {
		voteShare = std::max(voteShare, pastSeatResults[seatIndex].fpVotePercent[OthersIndex]);
	}

	determineSpecificPartyFp(seatIndex, OthersIndex, voteShare, run.othSeatStatistics);

	if (run.runningParties[seatIndex].size()) {
		bool emergingPartyPresent = seatFpVoteShare[seatIndex].contains(EmergingPartyIndex) && seatFpVoteShare[seatIndex][EmergingPartyIndex] > 0;
		bool emergingIndPresent = seatFpVoteShare[seatIndex].contains(EmergingIndIndex) && seatFpVoteShare[seatIndex][EmergingIndIndex] > 0;
		bool confirmedIndPresent = seatFpVoteShare[seatIndex].contains(run.indPartyIndex) && seatFpVoteShare[seatIndex][run.indPartyIndex] > 0;
		int othersCount = run.othCount[seatIndex] - (emergingPartyPresent ? 1 : 0) +
			run.indCount[seatIndex] - (emergingIndPresent ? 1 : 0) - (confirmedIndPresent ? 1 : 0);

		if (!othersCount) {
			return;
		}

		// keep total others vote capped below the amount that would mean
		// that emerging inds/parties would need to be present
		voteShare *= 1.5f - 1.5f * pow(1.5f, -othersCount);
		const float voteCap = detransformVoteShare(run.indEmergence.fpThreshold) * othersCount;
		constexpr float CapLowThreshold = 0.75f;
		constexpr float CapThresholdSize = 1.0f - CapLowThreshold;
		const float lowThreshold = voteCap * CapLowThreshold;
		if (voteShare > lowThreshold) {
			// smoothly cap the vote share so that it asymptotically approaches the vote cap as original vote share increases
			voteShare = lowThreshold + (voteCap - voteCap / ((voteShare - lowThreshold) / (voteCap / CapThresholdSize) + 1)) * CapThresholdSize;
		}
	}
	// Reduce others vote by the "others" parties already assigned vote share.
	float existingVoteShare = 0.0f;
	if (seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) existingVoteShare += seatFpVoteShare[seatIndex][run.indPartyIndex];
	if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) existingVoteShare += seatFpVoteShare[seatIndex][EmergingIndIndex];
	if (seatFpVoteShare[seatIndex].contains(EmergingPartyIndex)) existingVoteShare += seatFpVoteShare[seatIndex][EmergingPartyIndex];
	if (run.runningParties[seatIndex].size()) existingVoteShare *= 0.5f;
	// This effect shouldn't overwhelm the vote share in seats with very high IND votes
	voteShare = basicTransformedSwing(voteShare, -std::min(voteShare * 0.6f, existingVoteShare));

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
	auto fpDeviations = run.liveElection->getSeatFpDeviations(seat.name);
	for (auto [partyIndex, swing] : fpDeviations) {
		if (isMajor(partyIndex)) continue;
		if (!seatFpVoteShare[seatIndex].contains(partyIndex)) continue;
		float priorFp = seatFpVoteShare[seatIndex][partyIndex];
		float transformedPriorFp = transformVoteShare(priorFp);
		float transformedUpdatedFp = transformedPriorFp + swing;
		float updatedFp = detransformVoteShare(transformedUpdatedFp);
		seatFpVoteShare[seatIndex][partyIndex] = updatedFp;
	}

	// if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
	// 	!run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
	// 	seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
	// 	// we had a confirmed independent but they didn't meet the threshold, move votes to OTH
	// 	seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][run.indPartyIndex];
	// 	seatFpVoteShare[seatIndex][run.indPartyIndex] = 0.0f;
	// }
	// if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
	// 	!run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
	// 	seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) {
	// 	// we had an emerging independent but they didn't meet the threshold, move votes to OTH
	// 	seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
	// 	seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	// }
	// if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
	// 	run.liveSeatFpTransformedSwing[seatIndex].contains(run.indPartyIndex) &&
	// 	seatFpVoteShare[seatIndex].contains(EmergingIndIndex) &&
	// 	!seatFpVoteShare[seatIndex].contains(run.indPartyIndex) &&
	// 	run.liveSeatFpPercent[seatIndex][run.indPartyIndex] > (20.0f / (run.liveSeatFpCounted[seatIndex] + 0.1f))) {
	// 	// we had an emerging independent and no confirmed independent and an independent vote meets
	// 	// the threshold, move them to independent
	// 	// in order for this to happen we need the fp to be above a certain level depending on the %
	// 	// counted - don't want big changes because an IND scraped above 8% in a tiny booth
	// 	seatFpVoteShare[seatIndex][run.indPartyIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
	// 	seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	// }
	// if (run.liveSeatFpTransformedSwing[seatIndex].size() &&
	// 	!run.liveSeatFpTransformedSwing[seatIndex].contains(EmergingIndIndex) &&
	// 	seatFpVoteShare[seatIndex].contains(EmergingIndIndex) &&
	// 	run.liveSeatFpCounted[seatIndex] > 0.5f) {
	// 	// we still have an emerging ind (so wasn't promoted), and there's no other ind meeting the
	// 	// threshold, so switch to others
	// 	// Also make sure we don't have a completely miniscule vote as this can cause a step change
	// 	seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
	// 	seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
	// }
	// if (seatFpVoteShare[seatIndex].contains(OthersIndex)) {
	// 	seatFpVoteShare[seatIndex][OthersIndex] = std::clamp(seatFpVoteShare[seatIndex][OthersIndex], 0.0f, 99.9f);
	// }
	// if (seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
	// 	seatFpVoteShare[seatIndex][run.indPartyIndex] = std::clamp(seatFpVoteShare[seatIndex][run.indPartyIndex], 0.0f, 99.9f);
	// }
	// for (auto [partyIndex, swing] : run.liveSeatFpTransformedSwing[seatIndex]) {
	// 	// Ignore major party fps for now
	// 	if (isMajor(partyIndex)) continue;
	// 	[[maybe_unused]] auto prevFpVoteShare = seatFpVoteShare[seatIndex];
	// 	float pastFp = (pastSeatResults[seatIndex].fpVotePercent.contains(partyIndex) ? 
	// 		pastSeatResults[seatIndex].fpVotePercent.at(partyIndex) : 0.0f);
	// 	// Prevent independents from being generated from tiny proportions of the vote.
	// 	if ((partyIndex == run.indPartyIndex || partyIndex == EmergingIndIndex) &&
	// 		run.liveSeatFpPercent[seatIndex][partyIndex] <= (20.0f / (run.liveSeatFpCounted[seatIndex] + 0.1f)))
	// 	{
	// 		continue;
	// 	}
	// 	float pastTransformedFp = transformVoteShare(pastFp);
	// 	float liveTransformedFp = transformVoteShare(std::max(0.1f, run.liveSeatFpPercent[seatIndex][partyIndex]));
	// 	if (pastFp != 0.0f && !std::isnan(swing) && !std::isnan(liveTransformedFp)) {
	// 		// Mix the "swung" and "straight" estimations of the party's fp vote,
	// 		// giving a higher weight to "swung" since it's usually more accurate
	// 		// but favouring "straight" in situations where the past vote is much smaller
	// 		// (e.g. 2% -> 20% swing) since the transformation distorts the result heavily in those situations.
	// 		float swungFp = pastTransformedFp + swing;
	// 		float swungDiff = abs(swungFp - pastTransformedFp);
	// 		// the std::min is here so that seats with more than about 30% have to earn it rather than just being extrapolated
	// 		float straightDiff = std::min(10.0f, abs(liveTransformedFp - pastTransformedFp));
	// 		float intermediate = std::pow(swungDiff / (straightDiff * 3.0f), 3.0f);
	// 		float swungWeight = 1.0f - intermediate / (intermediate + 1.0f);
	// 		// the "swung" weight is only for extrapolating off incomplete of booths, when getting close to a complete count should just be using straight results
	// 		swungWeight = std::clamp(swungWeight, 0.0f, (80.0f - run.liveSeatFpCounted[seatIndex]) / 40.0f);
	// 		liveTransformedFp = mix(liveTransformedFp, swungFp, swungWeight);
	// 	}
	// 	float swingDeviation = run.tppSwingFactors.meanSwingDeviation * 2.0f; // placeholder value
	// 	float percentCounted = run.liveSeatFpCounted[seatIndex];
	// 	float liveSwingDeviation = std::min(swingDeviation, 10.0f * pow(2.0f, -std::sqrt(percentCounted * 0.2f)));
	// 	liveTransformedFp += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
	// 	if (postCountFpShift.contains(partyIndex)) liveTransformedFp += postCountFpShift[partyIndex];
	// 	float liveFactor = 1.0f - pow(2.0f, -percentCounted * 0.2f);
	// 	float priorFp = seatFpVoteShare[seatIndex][partyIndex];
	// 	if (partyIndex == run.indPartyIndex && priorFp <= 0.0f) priorFp = seatFpVoteShare[seatIndex][EmergingIndIndex];
	// 	float transformedPriorFp = transformVoteShare(priorFp);
	// 	// Sometimes the live results will have an independent showing even if one
	// 	// wasn't expected prior. In these cases, the seat Fp vote share will be a 0,
	// 	// and its transformation will be NaN, so just ignore it and use the live value only.
	// 	float mixedTransformedFp = priorFp > 0.0f ?
	// 		mix(transformedPriorFp, liveTransformedFp, liveFactor) : liveTransformedFp;
	// 	float detransformedFp = detransformVoteShare(mixedTransformedFp);
	// 	seatFpVoteShare[seatIndex][partyIndex] = detransformedFp;
	// }
	// if (!run.liveSeatFpTransformedSwing[seatIndex].contains(EmergingIndIndex)) {
	// 	seatFpVoteShare[seatIndex][EmergingIndIndex] *= std::min(1.0f, 2.0f / run.liveSeatFpCounted[seatIndex]);
	// }
	// if (run.liveSeatFpCounted[seatIndex] > 5.0f) {
	// 	seatFpVoteShare[seatIndex][EmergingPartyIndex] *= std::min(1.0f, 2.0f / run.liveSeatFpCounted[seatIndex]);
	// }
}

void SimulationIteration::prepareFpsForNormalisation(int seatIndex)
{
	[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
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
	if (run.isLiveAutomatic()) {
		float seatCountProgress = run.liveElection->getSeatFpConfidence(seat.name) * 100.0f;
		float liveFactor = 1.0f - pow(2.0f, -0.5f * seatCountProgress);
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
	if (!run.othCount[seatIndex] && run.runningParties[seatIndex].size() > 0) return;
	float voteShare = 0.0f;
	determinePopulistFp(seatIndex, EmergingPartyIndex, voteShare);
	seatFpVoteShare[seatIndex][EmergingPartyIndex] = voteShare;
}

void SimulationIteration::determineNationalsShare(int seatIndex)
{
	auto const& seat = project.seats().viewByIndex(seatIndex);
	if (run.natPartyIndex < 0) return; // Nationals may not be relevant in some elections
	nationalsShare[seatIndex] = run.seatNationalsExpectation[seatIndex];

	// If Nationals are not running in this seat, then their share is zero
	if (seat.runningParties.size() > 0 && std::find(seat.runningParties.begin(), seat.runningParties.end(), "NAT") == seat.runningParties.end()) {
		nationalsShare[seatIndex] = 0.0f;
		return;
	}

	// If Nationals are the only party running in this seat, then their share is 100%
	if (seat.runningParties.size() > 0 && std::find(seat.runningParties.begin(), seat.runningParties.end(), project.parties().viewByIndex(1).abbreviation) == seat.runningParties.end()) {
		nationalsShare[seatIndex] = 1.0f;
		return;
	}

	// If the seat has a NAT candidate, raise the expectation to a minumum of 5%
	if (std::any_of(seat.candidateNames.begin(), seat.candidateNames.end(), 
		[](const auto& pair) { return pair.second == "NAT"; })) {
		nationalsShare[seatIndex] = std::max(nationalsShare[seatIndex], 0.05f);
	}

	if (nationalsShare[seatIndex] > 0 && nationalsShare[seatIndex] < 1) {
		float rmse = run.nationalsParameters.rmse;
		float kurtosis = run.nationalsParameters.kurtosis;
		float transformedShare = transformVoteShare(nationalsShare[seatIndex] * 100.0f);
		float transformedSwing = rng.flexibleDist(intraCoalitionSwing, rmse, rmse, kurtosis, kurtosis);
		transformedShare += transformedSwing;
		nationalsShare[seatIndex] = detransformVoteShare(transformedShare) * 0.01f;
	}
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
			previousPartyOnePrefEstimate += voteShare * effectivePreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		else {
			float previousPreferences = run.previousPreferenceFlow[partyIndex];
			previousPartyOnePrefEstimate += voteShare * previousPreferences * 0.01f * (1.0f - exhaustRate);
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
	}

	// Step 3: Get an estimate for the *current* election

	float currentPartyOnePrefs = 0.0f;
	float currentNonMajorFpShare = 0.0f;
	float currentExhaustRateEstimate = 0.0f;
	float currentExhuastDenominator = 0.0f;
	std::map<int, float> preferenceVariation; // in minor -> major preferences, after transformation
	std::map<int, float> exhaustVariation;

	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (isMajor(partyIndex)) continue;
		if (partyIndex == CoalitionPartnerIndex) {
			currentPartyOnePrefs += 0.15f * voteShare;
			continue;
		}
		float exhaustRate = calculateEffectiveExhaustRate(partyIndex, true);
		if (!preferenceVariation.contains(partyIndex)) preferenceVariation[partyIndex] = rng.normal(0.0f, 15.0f);
		if (!exhaustVariation.contains(partyIndex)) exhaustVariation[partyIndex] = rng.normal(0.0f, 0.15f);
		float randomisedExhaustRate = exhaustRate ? basicTransformedSwing(exhaustRate, exhaustVariation[partyIndex]) : 0.0f;
		if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
			float effectivePreferenceFlow = calculateEffectivePreferenceFlow(partyIndex, voteShare, true);
			float randomisedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - randomisedExhaustRate);
		}
		else {
			float currentPreferences = overallPreferenceFlow[partyIndex];
			float randomisedPreferenceFlow = basicTransformedSwing(currentPreferences, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - randomisedExhaustRate);
		}
		currentNonMajorFpShare += voteShare;
		currentExhaustRateEstimate += randomisedExhaustRate * voteShare;
		currentExhuastDenominator += voteShare;
	}
	currentExhaustRateEstimate /= currentExhuastDenominator;
	currentExhaustRateEstimate = std::clamp(currentExhaustRateEstimate + exhaustBiasRate, 0.0f, 1.0f);


	float currentNonMajorTppShare = currentNonMajorFpShare * (1.0f - currentExhaustRateEstimate);

	static bool ProducedExhaustRateWarning = false;
	if (currentExhaustRateEstimate && !std::isnan(currentExhaustRateEstimate) && overallExhaustRate[OthersIndex] < 0.01f && !ProducedExhaustRateWarning) {
		PA_LOG_VAR(overallExhaustRate);
		logger << "Warning: An exhaust rate was produced for an election that appears to be for compulsory preferential voting. Seat concerned: " + seat.name + ", observed exhaust rate: " + std::to_string(currentExhaustRateEstimate) << "\n";
		ProducedExhaustRateWarning = true;
	}

	// Step 4: Adjust the current flow estimate according the bias the previous flow estimate had

	currentPartyOnePrefs = std::clamp(
		currentPartyOnePrefs,
		0.01f * currentNonMajorTppShare,
		0.99f * currentNonMajorTppShare
	);
	float biasAdjustedPartyOnePrefs = basicTransformedSwing(currentPartyOnePrefs, preferenceBiasRate * currentNonMajorTppShare);

	// If it's been determined that the overall preference flow needs a correction, do that here.
	// Make sure that this falls well within the total non-major preferences available.
	float overallAdjustedPartyOnePrefs = std::clamp(
		biasAdjustedPartyOnePrefs + prefCorrection * currentNonMajorTppShare,
		0.01f * currentNonMajorTppShare,
		0.99f * currentNonMajorTppShare
	);
	float overallAdjustedPartyTwoPrefs = currentNonMajorTppShare - overallAdjustedPartyOnePrefs;

	// Step 4a: Integrate live forecast preference flow estimates (if applicable)

	// float beforeLiveAdjustment = overallAdjustedPartyOnePrefs; // REMOVE
	if (run.isLiveAutomatic()) {
		auto livePreferenceFlowInfo = run.liveElection->getSeatPreferenceFlowInformation(seat.name);
	// 	float liveExhaustRate = run.liveSeatExhaustRate[seatIndex];
		float liveWeight = 1.0f - std::pow(2.0f, -1.0f * livePreferenceFlowInfo.confidence * 50.0f);
		float currentPrefFlow = overallAdjustedPartyOnePrefs / currentNonMajorTppShare * 100.0f;
	// 	float currentExhaustRate = currentNonMajorTppShare / currentNonMajorFpShare;
	// 	float mixedExhaustRate = mix(currentExhaustRate, liveExhaustRate, liveWeight);
		float mixedExhaustRate = 0.0f; // replace with above line when implementing exhaust rates
		float modifiedPrefFlow = basicTransformedSwing(currentPrefFlow, livePreferenceFlowInfo.deviation);
		[[maybe_unused]] float mixedPrefFlow = mix(currentPrefFlow, modifiedPrefFlow, liveWeight);
	 	currentNonMajorTppShare = currentNonMajorFpShare * (1.0f - mixedExhaustRate);
	 	overallAdjustedPartyOnePrefs = currentNonMajorTppShare * mixedPrefFlow * 0.01f;
		overallAdjustedPartyTwoPrefs = currentNonMajorTppShare - overallAdjustedPartyOnePrefs;
	// 	currentExhaustRateEstimate = mixedExhaustRate;
	 }
	// float afterLiveAdjustment = overallAdjustedPartyOnePrefs; // REMOVE

	// Step 5: Actually estimate the major party fps based on these adjusted flows

	// adjust everything that's still being used to be scaled so that the total non-exhausted votes is equal to 100%
	float majorFpShare = 100.0f - currentNonMajorFpShare;
	float nonExhaustedProportion = mix(1.0f, majorFpShare * 0.01f, currentExhaustRateEstimate * 1.0f);

	float partyTwoCurrentTpp = 100.0f - partyOneCurrentTpp;

	// Estimate Fps by removing expected preferences from expected tpp, but keeping it above zero
	// (as high 3rd-party fps can combine with a low tpp to push this below zero)
	float partyOneScaledTpp = partyOneCurrentTpp * nonExhaustedProportion;
	float partyTwoScaledTpp = partyTwoCurrentTpp * nonExhaustedProportion;
	float newPartyOneFp = predictorCorrectorTransformedSwing(partyOneScaledTpp, -overallAdjustedPartyOnePrefs);
	float newPartyTwoFp = predictorCorrectorTransformedSwing(partyTwoScaledTpp, -overallAdjustedPartyTwoPrefs);

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
	float addPartyTwoFp = (partyTwoCurrentTpp * totalTpp * 0.01f - newPartyTwoTpp) / (1.0f - partyTwoCurrentTpp * 0.01f);
	float finalPartyOneFp = newPartyOneFp;
	float finalPartyTwoFp = newPartyTwoFp;

	float addedPartyOneFp = finalPartyOneFp;
	float addedPartyTwoFp = finalPartyTwoFp;
	if (addPartyOneFp >= 0.0f) {
		addedPartyOneFp = addPartyOneFp + finalPartyOneFp;
	}
	else {
		addedPartyTwoFp = addPartyTwoFp + finalPartyTwoFp;
	}

	if (run.isLiveAutomatic()) {
		auto majorPartyBalance = run.liveElection->getSeatMajorPartyBalance(seat.name);
		float liveWeight = 1.0f - std::pow(2.0f, -1.0f * majorPartyBalance.confidence * 50.0f);
		liveWeight *= std::min(1.0f, std::max(std::abs(addPartyOneFp), std::abs(addPartyTwoFp)) * 0.5f);
		float majorPartyTotal = finalPartyOneFp + finalPartyTwoFp;
		finalPartyOneFp = mix(addedPartyOneFp, majorPartyTotal * majorPartyBalance.alpShare, liveWeight);
		finalPartyTwoFp = mix(addedPartyTwoFp, majorPartyTotal * (1.0f - majorPartyBalance.alpShare), liveWeight);
	}
	else {
		finalPartyOneFp = addedPartyOneFp;
		finalPartyTwoFp = addedPartyTwoFp;
	}

	//if (seat.name == "Terrigal") {
	//	static int timesWritten = 0;
	//	if (timesWritten < 10000) {
	//		PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//		PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
	//		PA_LOG_VAR(partyOneCurrentTpp);
	//		PA_LOG_VAR(partyTwoCurrentTpp);
	//		PA_LOG_VAR(oldFpVotes);
	//		PA_LOG_VAR(seatFpVoteShare[seatIndex]);
	//		PA_LOG_VAR(previousNonMajorFpShare);
	//		PA_LOG_VAR(previousPartyOnePrefEstimate);
	//		PA_LOG_VAR(previousExhaustRateEstimate);
	//		PA_LOG_VAR(previousExhuastDenominator);
	//		PA_LOG_VAR(preferenceBiasRate);
	//		PA_LOG_VAR(exhaustBiasRate);
	//		PA_LOG_VAR(majorTcp);
	//		PA_LOG_VAR(overallPreferenceFlow);
	//		PA_LOG_VAR(overallExhaustRate);
	//		PA_LOG_VAR(currentPartyOnePrefs);
	//		PA_LOG_VAR(currentNonMajorFpShare);
	//		PA_LOG_VAR(currentNonMajorTppShare);
	//		PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//		PA_LOG_VAR(preferenceVariation);
	//		PA_LOG_VAR(prefCorrection);
	//		PA_LOG_VAR(overallAdjustedPartyOnePrefs);
	//		PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
	//		PA_LOG_VAR(partyOneScaledTpp);
	//		PA_LOG_VAR(partyTwoScaledTpp);
	//		PA_LOG_VAR(newPartyOneTpp);
	//		PA_LOG_VAR(newPartyTwoTpp);
	//		PA_LOG_VAR(totalTpp);
	//		PA_LOG_VAR(addPartyOneFp);
	//		PA_LOG_VAR(addPartyTwoFp);
	//		PA_LOG_VAR(newPartyOneFp);
	//		PA_LOG_VAR(newPartyTwoFp);
	//		PA_LOG_VAR(majorFpShare);
	//		PA_LOG_VAR(nonExhaustedProportion);
	//		PA_LOG_VAR(currentExhaustRateEstimate);
	//		PA_LOG_VAR(currentExhuastDenominator);
	//		PA_LOG_VAR(exhaustBiasRate);
	//		PA_LOG_VAR(finalPartyOneFp);
	//		PA_LOG_VAR(finalPartyTwoFp);
	//		++timesWritten;
	//	}
	//}

	// if (run.isLive() && !std::isnan(run.liveSeatMajorFpDiff[seatIndex])) {
	// 	float finalMajorFpShare = finalPartyOneFp + finalPartyTwoFp;
	// 	float liveWeight = 1.0f - 100.0f / (100.0f + run.liveSeatFpCounted[seatIndex] * run.liveSeatFpCounted[seatIndex]);
	// 	float majorFpDiff = std::clamp(run.liveSeatMajorFpDiff[seatIndex], finalMajorFpShare * -0.9f, finalMajorFpShare * 0.9f);
	// 	finalPartyOneFp = mix(finalPartyOneFp, (finalMajorFpShare + majorFpDiff) * 0.5f, liveWeight);
	// 	finalPartyTwoFp = mix(finalPartyTwoFp, (finalMajorFpShare - majorFpDiff) * 0.5f, liveWeight);
	// }

	if (run.natPartyIndex > 0) {
		// a final adjustment for the *change* in relative leakage among coalition parties
		// which will require a correction increasing the total coalition FP at the expense of Labor FP
		// in order to achieve the same TPP
		// (The absolute level of split is already covered in the preference calculations)
		float prevNatVote = pastSeatResults[seatIndex].fpVotePercent.contains(run.natPartyIndex) ? pastSeatResults[seatIndex].fpVotePercent.at(run.natPartyIndex) : 0.0f;
		float prevLibVote = pastSeatResults[seatIndex].fpVotePercent.contains(Mp::Two) ? pastSeatResults[seatIndex].fpVotePercent.at(Mp::Two) : 0.0f;
		float prevSplit = std::min(prevNatVote, prevLibVote) / (prevNatVote + prevLibVote);
		float currentSplit = std::min(nationalsShare[seatIndex], 1.0f - nationalsShare[seatIndex]);
		float splitChange = currentSplit - prevSplit;
		// skip unnecessary calculations if there's no change (common as most seats only one coalition party will contest)
		if (splitChange) {
			float extraCoalitionVoteNeeded = splitChange * finalPartyTwoFp * 0.154f;
			// make sure the adjustment doesn't overflow in either direction
			float partyOneAdjustment = predictorCorrectorTransformedSwing(finalPartyOneFp, -extraCoalitionVoteNeeded) - finalPartyOneFp;
			float partyTwoAdjustment = predictorCorrectorTransformedSwing(finalPartyTwoFp, extraCoalitionVoteNeeded) - finalPartyTwoFp;
			float finalPartyOneAdjustment = std::min(partyOneAdjustment, -partyTwoAdjustment);
			finalPartyOneFp += finalPartyOneAdjustment;
			finalPartyTwoFp -= finalPartyOneAdjustment;

		}
	}


	seatFpVoteShare[seatIndex][Mp::One] = finalPartyOneFp;
	seatFpVoteShare[seatIndex][Mp::Two] = finalPartyTwoFp;

	//if (checkForNans("after setting seatFpVoteShares")) {
	//	PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//	PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
	//	PA_LOG_VAR(partyOneCurrentTpp);
	//	PA_LOG_VAR(partyTwoCurrentTpp);
	//	PA_LOG_VAR(oldFpVotes);
	//	PA_LOG_VAR(seatFpVoteShare[seatIndex]);
	//	PA_LOG_VAR(previousNonMajorFpShare);
	//	PA_LOG_VAR(previousPartyOnePrefEstimate);
	//	PA_LOG_VAR(previousExhaustRateEstimate);
	//	PA_LOG_VAR(previousExhuastDenominator);
	//	PA_LOG_VAR(preferenceBiasRate);
	//	PA_LOG_VAR(exhaustBiasRate);
	//	PA_LOG_VAR(majorTcp);
	//	PA_LOG_VAR(overallPreferenceFlow);
	//	PA_LOG_VAR(overallExhaustRate);
	//	PA_LOG_VAR(currentPartyOnePrefs);
	//	PA_LOG_VAR(currentNonMajorFpShare);
	//	PA_LOG_VAR(currentNonMajorTppShare);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(preferenceVariation);
	//	PA_LOG_VAR(prefCorrection);
	//	PA_LOG_VAR(overallAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
	//	PA_LOG_VAR(partyOneScaledTpp);
	//	PA_LOG_VAR(partyTwoScaledTpp);
	//	PA_LOG_VAR(newPartyOneTpp);
	//	PA_LOG_VAR(newPartyTwoTpp);
	//	PA_LOG_VAR(totalTpp);
	//	PA_LOG_VAR(addPartyOneFp);
	//	PA_LOG_VAR(addPartyTwoFp);
	//	PA_LOG_VAR(newPartyOneFp);
	//	PA_LOG_VAR(newPartyTwoFp);
	//	PA_LOG_VAR(majorFpShare);
	//	PA_LOG_VAR(nonExhaustedProportion);
	//	PA_LOG_VAR(currentExhaustRateEstimate);
	//	PA_LOG_VAR(currentExhuastDenominator);
	//	PA_LOG_VAR(exhaustBiasRate);
	//	PA_LOG_VAR(finalPartyOneFp);
	//	PA_LOG_VAR(finalPartyTwoFp);
	//	throw 1;
	//}

	if (seat.incumbent >= Mp::Others && seatFpVoteShare[seatIndex][seat.incumbent]) {
		// Maintain constant fp vote for non-major incumbents
		normaliseSeatFp(seatIndex, seat.incumbent, seatFpVoteShare[seatIndex][seat.incumbent]);
	}
	else {
		normaliseSeatFp(seatIndex);
	}

	// if (seat.name == "Wentworth" && run.isLive() && !run.doingBettingOddsCalibrations && !run.doingLiveBaselineSimulation) {
	// 	logger << "Checkpoint Z\n";
	// 	PA_LOG_VAR(seatFpVoteShare[seatIndex][6]);
	// }
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
		else if (partyIndex == run.natPartyIndex) {
			// for now do nothing, but don't add to others
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
		if (!isMajor(partyIndex) && partyIndex != run.natPartyIndex) nonMajorFpError += error;
	}
	float tempMicroOthers = float(partyVoteCount[OthersIndex]) / float(totalVoteCount) * 100.0f;
	float indOthers = tempOverallFp[OthersIndex] - tempMicroOthers;
	othersCorrectionFactor = (overallFpTarget[OthersIndex] - indOthers) / tempMicroOthers;
}

void SimulationIteration::calculatePreferenceCorrections()
{
	float estTppSeats = 0.0f;
	float totalPrefs = 0.0f;
	float totalNonExhaust = 0.0f;
	for (auto [partyIndex, prefFlow] : tempOverallFp) {
		float voteSize = tempOverallFp[partyIndex] * (1.0f - overallExhaustRate[partyIndex]);
		estTppSeats += overallPreferenceFlow[partyIndex] * voteSize * 0.01f;
		totalNonExhaust += voteSize;
		if (!isMajor(partyIndex)) totalPrefs += voteSize;
	}
	estTppSeats /= (totalNonExhaust * 0.01f);
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
			if (!tempOverallFp[partyIndex] || !overallFpTarget[partyIndex]) continue; // avoid division by zero when we have non-existent emerging others
			float correctionFactor = overallFpTarget[partyIndex] / tempOverallFp[partyIndex];
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				if (partyIndex == seat.incumbent) continue;
				if (seatFpVoteShare[seatIndex].contains(partyIndex)) {
					// prevent outlier seats from getting monster swings
					float swingCap = std::max(0.0f, tempOverallFp[partyIndex] * (correctionFactor - 1.0f) * 3.0f);
					float correctionSwing = std::min(swingCap, seatFpVoteShare[seatIndex][partyIndex] * (correctionFactor - 1.0f));
					// don't re-adjust fps when we have a significant actual count
					//if (run.isLiveAutomatic()) correctionSwing *= std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]);
					float newValue = predictorCorrectorTransformedSwing(seatFpVoteShare[seatIndex][partyIndex], correctionSwing);
					seatFpVoteShare[seatIndex][partyIndex] = newValue;
				}
			}
			checkForNans("d0b");
		}
		else {
			checkForNans("d0c");
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
				float allocation = seatFpVoteShare[seatIndex][OthersIndex] * (othersCorrectionFactor - 1.0f);
				FloatByPartyIndex categories;
				float totalOthers = 0.0f;
				for (auto [seatPartyIndex, seatPartyVote] : seatFpVoteShare[seatIndex]) {
					// protect independents and quasi-independents from having their votes squashed here
					if (seatPartyIndex == run.indPartyIndex) continue;
					if (!overallFpSwing.contains(seatPartyIndex) && seatPartyIndex >= 2) continue;
					if (seatPartyIndex == seat.incumbent) continue;
					if (seatPartyIndex == OthersIndex || !overallFpTarget.contains(seatPartyIndex)) {
						categories[seatPartyIndex] = seatPartyVote;
						totalOthers += seatPartyVote;
					}
				}
				if (!totalOthers) continue;
				for (auto& [seatPartyIndex, voteShare] : categories) {
					float additionalVotes = allocation * voteShare / totalOthers;
					// don't re-adjust fps when we have a significant actual count
					//if (run.isLiveAutomatic()) additionalVotes *= std::pow(2.0f, -1.0f * run.liveSeatFpCounted[seatIndex]);
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
	if (run.natPartyIndex >= 0) majorFpTarget += overallFpTarget[run.natPartyIndex];
	// This formula calculates the adjustment needed for the current fp to reach the target fp *after normalisation*
	float adjustmentFactor = (majorFpTarget * (majorFpCurrent - 100.0f)) / (majorFpCurrent * (majorFpTarget - 100.0f));
	float totalMinors = 0.0f;
	float partyOnePrefs = 0.0f;
	for (auto [partyIndex, vote] : tempOverallFp) {
		if (isMajor(partyIndex) || partyIndex == run.natPartyIndex) continue;
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
		if (run.isLiveAutomatic()) {
			Seat const& seat = project.seats().viewByIndex(seatIndex);
			float seatCountProgress = run.liveElection->getSeatFpConfidence(seat.name) * 100.0f;
			seatPartyOneAdjust = mix(1.0f, seatPartyOneAdjust, std::pow(2.0f, -1.0f * seatCountProgress));
			seatPartyTwoAdjust = mix(1.0f, seatPartyTwoAdjust, std::pow(2.0f, -1.0f * seatCountProgress));
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
			// Typically non-classic exhaust rates are a bit higher than classic ones
			float survivalRate = (1.0f - overallExhaustRate[sourceParty]) * 0.85f;
			// Fallback figure for major parties when OPV is in force. Past observations suggest a somewhat lower
			// exhaust rate for ALP votes than other parties
			if (sourceParty == 0 && overallExhaustRate[OthersIndex] > 0.01f) survivalRate = 0.5f;
			else if (sourceParty == 1 && overallExhaustRate[OthersIndex] > 0.01f) survivalRate = 0.4f;

			// if it's a final-two situation, check if we have known preference flows
			if (int(accumulatedVoteShares.size() == 2)) {
				if (run.ncPreferenceFlow.contains(sourceParty)) {
					auto const& item = run.ncPreferenceFlow[sourceParty];
					std::pair<int, int> targetParties = { accumulatedVoteShares[0].first, accumulatedVoteShares[1].first };
					if (item.contains(targetParties)) {
						float flow = item.at(targetParties);
						// Based on previous elections Mirani ONP member got better flows than expected for an ONP candidate, expect this to continue as KAP to some extent
						if (seat.name == "Mirani" && run.getTermCode() == "2024qld" && isMajor(sourceParty) && targetParties.first == 3) flow += 5.0f;
						if (seat.name == "Mirani" && run.getTermCode() == "2024qld" && isMajor(sourceParty) && targetParties.second == 3) flow -= 5.0f;
						float transformedFlow = transformVoteShare(flow);
						// Higher variation in preference flow under OPV
						transformedFlow += rng.normal(0.0f, 10.0f + 10.0f * (1.0f - survivalRate));
						flow = detransformVoteShare(transformedFlow);
						float transformedSurvival = transformVoteShare(survivalRate);
						transformedSurvival += rng.normal(0.0f, 15.0f);
						survivalRate = detransformVoteShare(transformedSurvival);
						if (seat.name == "Kiama" && run.getTermCode() == "2023nsw" && sourceParty == 0) {
							flow = 50.0f;
							survivalRate = 0.2f;
						}
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
				if (isMajor(targetParty)) ++ideologyDistance;
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
		// if (run.isLiveAutomatic()) {
		// 	bool matched = false;
		// 	float firstTcp = 0.0f;
		// 	if (run.isLiveAutomatic() && topTwo.first.first == run.liveSeatTcpParties[seatIndex].first
		// 		&& topTwo.second.first == run.liveSeatTcpParties[seatIndex].second)
		// 	{
		// 		matched = true;
		// 		if (!std::isnan(run.liveSeatTcpSwing[seatIndex]) && pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.first.first) &&
		// 			pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.second.first))
		// 		{
		// 			firstTcp = basicTransformedSwing(pastSeatResults[seatIndex].tcpVotePercent.at(topTwo.first.first), run.liveSeatTcpSwing[seatIndex]);
		// 		}
		// 		else {
		// 			firstTcp = run.liveSeatTcpPercent[seatIndex];
		// 		}
		// 	}
		// 	else if (run.isLiveAutomatic() && topTwo.first.first == run.liveSeatTcpParties[seatIndex].second
		// 		&& topTwo.second.first == run.liveSeatTcpParties[seatIndex].first)
		// 	{
		// 		matched = true;
		// 		if (!std::isnan(run.liveSeatTcpSwing[seatIndex]) && pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.first.first) &&
		// 			pastSeatResults[seatIndex].tcpVotePercent.contains(topTwo.second.first))
		// 		{
		// 			firstTcp = basicTransformedSwing(pastSeatResults[seatIndex].tcpVotePercent.at(topTwo.first.first), -run.liveSeatTcpSwing[seatIndex]);
		// 		}
		// 		else {
		// 			firstTcp = 100.0f - run.liveSeatTcpPercent[seatIndex];
		// 		}
		// 	}
		// 	if (matched) {
		// 		float transformedTcpCalc = transformVoteShare(topTwo.first.second);
		// 		float transformedTcpLive = transformVoteShare(firstTcp);
		// 		float liveSwingDeviation = std::min(10.0f, 10.0f * pow(2.0f, -std::sqrt(run.liveSeatTcpCounted[seatIndex] * 0.2f)));
		// 		transformedTcpLive += rng.flexibleDist(0.0f, liveSwingDeviation, liveSwingDeviation, 5.0f, 5.0f);
		// 		float liveFactor = 1.0f - pow(2.0f, -run.liveSeatTcpCounted[seatIndex] * 0.2f);
		// 		float mixedTransformedTpp = mix(transformedTcpCalc, transformedTcpLive, liveFactor);
		// 		topTwo.first.second = detransformVoteShare(mixedTransformedTpp);
		// 		topTwo.second.second = 100.0f - topTwo.first.second;
		// 	}
		// }
	}

	if (run.natPartyIndex >= 0 && nationalsShare[seatIndex] > 0) {
		assignNationalsVotes(seatIndex);
		float natShare = seatFpVoteShare[seatIndex][run.natPartyIndex];
		float libShare = seatFpVoteShare[seatIndex][Mp::Two];
		float lowerShare = std::min(natShare, libShare) / (natShare + libShare);
		if (
			topTwo.first.first == Mp::Two && topTwo.first.second * lowerShare > topTwo.second.second
			|| topTwo.second.first == Mp::Two && topTwo.second.second * lowerShare > topTwo.first.second
		) {
			// This will be a LIB/NAT contest, so rearrange it accordingly
			topTwo.second.first = natShare > libShare ? run.natPartyIndex : Mp::Two;
			topTwo.second.second = natShare > libShare ? natShare : libShare;
			topTwo.first.first = natShare > libShare ? Mp::Two : run.natPartyIndex;
			topTwo.first.second = natShare > libShare ? libShare : natShare;
			// For now, simply assume preferences flow 50/50
			float prefShare = (100.0f - topTwo.first.second - topTwo.second.second) * 0.5f;
			topTwo.first.second += prefShare;
			topTwo.second.second += prefShare;
		}
		else if (seatFpVoteShare[seatIndex][run.natPartyIndex] > seatFpVoteShare[seatIndex][Mp::Two]) {
			if (topTwo.first.first == Mp::Two) topTwo.first.first = run.natPartyIndex;
			if (topTwo.second.first == Mp::Two) topTwo.second.first = run.natPartyIndex;
		}
	}

	// incorporate non-classic live 2cp results
	if (run.isLiveAutomatic() && !(isMajor(topTwo.first.first, run.natPartyIndex) && isMajor(topTwo.second.first, run.natPartyIndex))) {
		auto tcpInfo = run.liveElection->getSeatTcpInformation(project.seats().viewByIndex(seatIndex).name);
		if (tcpInfo.shares.contains(topTwo.first.first) && tcpInfo.shares.contains(topTwo.second.first)) {
			float priorShare = transformVoteShare(topTwo.first.second);
			float liveShare = tcpInfo.shares.at(topTwo.first.first);
			// placeholder insertion of variance, remove once the election model creates
			// its own better internal estimate of variance
			liveShare += rng.normal(0.0f, 12.0f * std::pow(2.0f, -4.0f * tcpInfo.confidence) - 0.25f);
			// Strongly favour use of live TCP results once there's a decent amount in
			float liveFactor = std::pow(tcpInfo.confidence, 0.1f);
			float mixedShare = mix(priorShare, liveShare, liveFactor);
			topTwo.first.second = detransformVoteShare(mixedShare);
			topTwo.second.second = 100.0f - topTwo.first.second;
			if (topTwo.first.second > topTwo.second.second) std::swap(topTwo.first, topTwo.second);
		}
	}

	seatWinner[seatIndex] = topTwo.second.first;
	auto byParty = std::minmax(topTwo.first, topTwo.second); // default pair operator orders by first element

	seatTcpVoteShare[seatIndex] = { {byParty.first.first, byParty.second.first}, byParty.first.second };

	if (run.isLive()) applyLiveManualOverrides(seatIndex);
}

void SimulationIteration::assignNationalsVotes(int seatIndex) {
	float nationalsVote = seatFpVoteShare[seatIndex][1] * nationalsShare[seatIndex];
	seatFpVoteShare[seatIndex][run.natPartyIndex] = nationalsVote;
	seatFpVoteShare[seatIndex][1] -= nationalsVote;
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
	else if (seatWinner[seatIndex] == Mp::Two || seatWinner[seatIndex] == run.natPartyIndex) ++run.partyTwoWinPercent[seatIndex];
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

void SimulationIteration::assignSupportsPartyWins()
{
	effectiveWins = { partyWins[Mp::One], partyWins[Mp::Two] };
	partySupport = { partyWins[Mp::One], partyWins[Mp::Two] };
	for (int partyNum = Mp::Others; partyNum < project.parties().count(); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.relationTarget >= Mp::Others) continue;
		if (thisParty.relationType == Party::RelationType::IsPartOf) {
			effectiveWins[thisParty.relationTarget] += partyWins[partyNum];
			partySupport[thisParty.relationTarget] += partyWins[partyNum];
		}
		else if (thisParty.relationType == Party::RelationType::Coalition) {
			effectiveWins[thisParty.relationTarget] += partyWins[partyNum];
			partySupport[thisParty.relationTarget] += partyWins[partyNum];
		}
	}
	for (int partyNum = Mp::Others; partyNum < project.parties().count(); ++partyNum) {
		Party const& thisParty = project.parties().viewByIndex(partyNum);
		if (thisParty.relationTarget >= Mp::Others) continue;
		if (thisParty.relationType == Party::RelationType::Supports) {
			partySupport[thisParty.relationTarget] += partyWins[partyNum];
		}
	}
}

void SimulationIteration::recordMajorityResult()
{
	int minimumForMajority = project.seats().count() / 2 + 1;

	std::array<int, 2> mpWins = { effectiveWins[Mp::One], effectiveWins[Mp::Two] };

	// Look at the overall result and classify it
	// Note for "supports" wins there is some logic to make sure a supporting party doesn't actually outnumber the larger party
	if (mpWins[Mp::One] >= minimumForMajority) ++run.partyMajority[Mp::One];
	else if (mpWins[Mp::Two] >= minimumForMajority) ++run.partyMajority[Mp::Two];
	else if (partySupport[Mp::One] >= minimumForMajority && mpWins[Mp::One] > partySupport[Mp::One] / 2) ++run.partyMinority[Mp::One];
	else if (partySupport[Mp::Two] >= minimumForMajority && mpWins[Mp::Two] > partySupport[Mp::Two] / 2) ++run.partyMinority[Mp::Two];
	else {
		std::vector<std::pair<int, int>> sortedPartyWins(partyWins.begin(), partyWins.end());
		sortedPartyWins[Mp::One] = { Mp::One, mpWins[Mp::One] };
		sortedPartyWins[Mp::Two] = { Mp::Two, mpWins[Mp::Two] };
		// Nats and Inds should never be considered for "most seats"
		// These are separate loops because otherwise the "erase" will interfere with the look operation
		for (auto a = sortedPartyWins.begin(); a != sortedPartyWins.end(); ++a) {
			if (a->first == run.indPartyIndex) {
				sortedPartyWins.erase(a);
				break;
			}
		}
		for (auto a = sortedPartyWins.begin(); a != sortedPartyWins.end(); ++a) {
			if (a->first == run.natPartyIndex) {
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
	int coalitionWins = 0;
	for (auto [partyIndex, wins] : partyWins) {
		if (!sim.latestReport.partySeatWinFrequency.contains(partyIndex)) {
			sim.latestReport.partySeatWinFrequency[partyIndex] = std::vector<int>(project.seats().count() + 1);
		}
		++sim.latestReport.partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
		if (partyIndex > 1) othersWins += partyWins[partyIndex];
		if (partyIndex == Mp::Two || partyIndex == run.natPartyIndex) coalitionWins += partyWins[partyIndex];
		for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
			if (!run.regionPartyWins[regionIndex].contains(partyIndex)) {
				run.regionPartyWins[regionIndex][partyIndex] = std::vector<int>(run.regionPartyWins[regionIndex][Mp::One].size());
			}
			int thisRegionSeatCount = regionSeatCount[partyIndex].size() ? regionSeatCount[partyIndex][regionIndex] : 0;
			++run.regionPartyWins[regionIndex][partyIndex][thisRegionSeatCount];
		}
	}
	++sim.latestReport.othersSeatWinFrequency[othersWins];
	++sim.latestReport.coalitionSeatWinFrequency[coalitionWins];
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
	if ((winner == Mp::Two || winner == run.natPartyIndex) && run.natPartyIndex >= 0) {
		++run.seatCoalitionWins[seatIndex];
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

void SimulationIteration::recordSeatTppVotes(int seatIndex)
{
	float tppPercent = partyOneNewTppMargin[seatIndex] + 50.0f;
	int bucket = std::clamp(int(std::floor(tppPercent * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
	++run.seatTppDistribution[seatIndex][bucket];
}

void SimulationIteration::recordRegionFpVotes(int regionIndex) {
	std::map<int, float> voteShareSum;
	float seatsSum = 0.0f;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (project.seats().viewByIndex(seatIndex).region == regionIndex) {
			for (auto [partyIndex, fpPercent] : seatFpVoteShare[seatIndex]) {
				voteShareSum[partyIndex] += fpPercent;
			}
			seatsSum += 1.0f;
		}
	}
	for (auto [partyIndex, voteShare] : voteShareSum) {
		float meanVoteShare = voteShare / seatsSum;
		int bucket = std::clamp(int(std::floor(meanVoteShare * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
		++run.regionPartyFpDistribution[regionIndex][partyIndex][bucket];
	}
}

void SimulationIteration::recordRegionTppVotes(int regionIndex) {
	float tppSum = 0.0f;
	float seatsSum = 0.0f;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (project.seats().viewByIndex(seatIndex).region == regionIndex) {
			tppSum += partyOneNewTppMargin[seatIndex] + 50.0f;
			seatsSum += 1.0f;
		}
	}
	float meanTpp = tppSum / seatsSum;
	int bucket = std::clamp(int(std::floor(meanTpp * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
	++run.regionTppDistribution[regionIndex][bucket];
}

void SimulationIteration::recordElectionFpVotes() {
	std::map<int, float> voteShareSum;
	float seatsSum = 0.0f;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto [partyIndex, fpPercent] : seatFpVoteShare[seatIndex]) {
			voteShareSum[partyIndex] += fpPercent;
		}
		seatsSum += 1.0f;
	}
	for (auto [partyIndex, voteShare] : voteShareSum) {
		float meanVoteShare = voteShare / seatsSum;
		int bucket = std::clamp(int(std::floor(meanVoteShare * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
		++run.electionPartyFpDistribution[partyIndex][bucket];
	}
}

void SimulationIteration::recordElectionTppVotes() {
	float tppSum = 0.0f;
	float seatsSum = 0.0f;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		tppSum += partyOneNewTppMargin[seatIndex] + 50.0f;
		seatsSum += 1.0f;
	}
	float meanTpp = tppSum / seatsSum;
	int bucket = std::clamp(int(std::floor(meanTpp * 0.01f * float(SimulationRun::FpBucketCount))), 0, SimulationRun::FpBucketCount - 1);
	++run.electionTppDistribution[bucket];
}

void SimulationIteration::recordIterationResults()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		recordSeatResult(seatIndex);
		recordSeatPartyWinner(seatIndex);
		recordSeatFpVotes(seatIndex);
		recordSeatTcpVotes(seatIndex);
		recordSeatTppVotes(seatIndex);
	}
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		recordRegionFpVotes(regionIndex);
		recordRegionTppVotes(regionIndex);
	}
	recordElectionFpVotes();
	recordElectionTppVotes();
	recordVoteTotals();
	recordSwings();
	recordSwingFactors();
	recordMajorityResult();
	recordPartySeatWinCounts();
}

void SimulationIteration::recordVoteTotals()
{
	double totalCoalitionFp = 0.0;
	double totalTpp = 0.0;
	std::map<int, double> totalFp;
	double totalTurnout = 0.0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		double turnout = double(run.pastSeatResults[seatIndex].turnoutCount);
		double turnoutScaledTpp = double(partyOneNewTppMargin[seatIndex] + 50.0) * turnout;
		for (auto [party, vote] : seatFpVoteShare[seatIndex]) {
			if (tempOverallFp.contains(party) || party == run.natPartyIndex) {
				totalFp[party] += vote * turnout;
			}
			else {
				totalFp[-1] += vote * turnout;
			}
			if (run.natPartyIndex >= 0 && (party == Mp::Two || party == run.natPartyIndex)) {
				totalCoalitionFp += vote * turnout;
			}
		}
		totalTpp += turnoutScaledTpp;
		totalTurnout += turnout;
	}
	totalTpp /= totalTurnout;
	for (auto& [party, vote] : totalFp) vote /= totalTurnout;
	short tppBucket = short(floor(totalTpp * 10.0f));
	++sim.latestReport.tppFrequency[tppBucket];

	if (run.natPartyIndex >= 0) {
		totalCoalitionFp /= totalTurnout;
		short coalitionFpBucket = short(floor(totalCoalitionFp * 10.0f));
		++sim.latestReport.coalitionFpFrequency[coalitionFpBucket];
	}

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
		run.seatByElectionEffectSums[seatIndex] += seatByElectionEffect[seatIndex];
		run.seatThirdPartyExhaustEffectSums[seatIndex] += seatThirdPartyExhaustEffect[seatIndex];
		run.seatPollEffectSums[seatIndex] += seatPollEffect[seatIndex];
		run.seatMrpPollEffectSums[seatIndex] += seatMrpPollEffect[seatIndex];
	}
}

float SimulationIteration::calculateEffectiveSeatModifier(int seatIndex, int partyIndex) const
{
	[[maybe_unused]] Seat const& seat = project.seats().viewByIndex(seatIndex);
	float populism = centristPopulistFactor.at(partyIndex);
	float seatModifier = mix(run.seatCentristModifiers[seatIndex], run.seatPopulistModifiers[seatIndex], populism);
	int regionIndex = project.regions().idToIndex(seat.region);
	float homeStateCoefficient = mix(run.centristStatistics.homeStateCoefficient, run.populistStatistics.homeStateCoefficient, populism);
	if (homeRegion.contains(partyIndex) && homeRegion.at(partyIndex) == regionIndex) seatModifier += homeStateCoefficient;
	float highVoteModifier = std::clamp(std::pow(2.0f, (3.0f - overallFpTarget.at(partyIndex)) * 0.1f), 0.2f, 1.0f);
	seatModifier = (seatModifier - 1.0f) * highVoteModifier + 1.0f;
	return seatModifier;
}
