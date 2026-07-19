#include "SimulationIteration.h"

#include "CountProgress.h"
#include "SpecialPartyCodes.h"
#include "PollingProject.h"
#include "RandomGenerator.h"
#include "Simulation.h"
#include "SimulationRun.h"

using Mp = Simulation::MajorParty;

static std::mutex recordMutex;

static RandomGenerator rng;

// Threshold at which longshot-bias correction starts being applied for seats being approximated from betting odds
constexpr float LongshotOddsThreshold = 2.5f;

constexpr float ProminentMinorFlatBonus = 5.0f;
constexpr float ProminentMinorFlatBonusThreshold = 10.0f;
constexpr float ProminentMinorBonusMax = 35.0f;
constexpr int MaxIterationRetries = 10;

namespace {
	struct InvalidIteration {};
}

// How strongly preferences align with ideology based on the "consistency" property of a party
constexpr std::array<float, 3> PreferenceConsistencyBase = { 1.2f, 1.4f, 1.8f };

const std::set<std::pair<std::string, std::string>> IgnoreExhaust = {
	{"2022vic", "St Albans"},
	{"2022vic", "Sydenham"},
};

enum class VariabilityTag : std::uint32_t {
	LiveManualOverrides = 1,
	TcpLiveShare = 2,
	OthGrnLibSplit = 3,
	OthAlpGrnSplit = 4,
	GrnAlpIndSplit = 5,
	PrefFlowUnknown = 6,
	PrefFlowKnown = 7,
	ExhaustKnown = 8,
	SeatPreferenceVariation = 9,
	SeatExhaustVariation = 10,
	ConfirmedIndsOddsBasedShare = 11,
	ConfirmedIndsOddsWeightCheck = 12,
	ConfirmedIndsOddsCalibration = 13,
	ConfirmedIndsContestRate = 14,
	ProminentPopulistBonus1 = 15,
	ProminentPopulistBonus2 = 16,
	ProminentMinorBonus1 = 17,
	ProminentMinorBonus2 = 18,
	MinorQuantile = 19,
	ProminentMinorPreBonus = 20,
	RecontestRateCheck = 21,
	KiamaBias = 22,
	KiamaExhaust = 23,
	EmergingPartyHomeRegion = 24,
	PopulismLevel = 25,
	IndDist1 = 26,
	IndDist2 = 27,
	IndDist3 = 28,
	IndDist4 = 29,
	IndDist5 = 30,
	IndDist6 = 31,
	IndDist7 = 32,
	IntraCoalitionSwing = 33,
	SeatTpp = 34,
	RegionSwingNaive = 35,
	RegionSwing = 36,
	MinorPartySeats = 37,
	MinorPartySeatPriority = 38,
	IntraCoalitionSwingGlobal = 39,
	PopulistFp = 40,
	FedStateCorrelation = 41,
	EmergingPartyHomeRegion2 = 42,
	ConfirmedIndVariation = 43,
	IndEmergenceDecision = 44,
	IndEmergenceQuantile = 45,
	CoalitionFutureRetirement = 46,
	NationalsLiveVariability = 47,
};

bool isMajor(int partyIndex, int natPartyIndex = -100) {
	return partyIndex == Mp::One || partyIndex == Mp::Two || partyIndex == natPartyIndex;
}

SimulationIteration::SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run, int iterationIndex)
	: project(project), sim(sim), run(run), iterationIndex(iterationIndex)
{
}

void SimulationIteration::reset()
{
	liveElection.reset();
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
	othersCorrectionFactor = 0.0f;
	fedStateCorrelation = 0.0f;
	ppvcBias = 0.0f;
	intraCoalitionSwing = 0.0f;
	indAlpha = 1.0f;
	indBeta = 1.0f;

	effectiveWins = std::array<int, 2>();
	partySupport = std::array<int, 2>();
}

bool SimulationIteration::hasInvalidValues(std::string const& location, bool forceDebug, bool checkTppMargins) {
	static bool alreadyLogged = false;
	auto report = [&](int seatIndex, std::string const& type, std::string const& valueType) {
		std::lock_guard<std::mutex> lock(recordMutex);
		if (alreadyLogged && !forceDebug) return;
		alreadyLogged = true;
		if (seatIndex >= 0 && seatIndex < project.seats().count()) {
			logger << "Warning: A " << type << " value for seat " <<
				project.seats().viewByIndex(seatIndex).name << " was " << valueType << "!\n";
		}
		else {
			logger << "Warning: A simulation " << type << " value was " << valueType << "!\n";
		}
		logger << "At simulation location " << location << "\n";
		logger << "Simulation iteration aborted to prevent a freeze, trying to redo.\n";
		if (seatIndex >= 0 && seatIndex < int(seatFpVoteShare.size())) {
			PA_LOG_VAR(seatFpVoteShare[seatIndex]);
		}
		if (seatIndex >= 0 && seatIndex < int(partyOneNewTppMargin.size())) {
			PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
		}
		PA_LOG_VAR(iterationOverallTpp);
		PA_LOG_VAR(overallFpTarget);
		PA_LOG_VAR(overallFpSwing);
		PA_LOG_VAR(regionSwing);
		PA_LOG_VAR(partyOneNewTppMargin);
		PA_LOG_VAR(seatFpVoteShare);
		return;
	};

	if (!std::isfinite(iterationOverallTpp)) {
		report(-1, "overall TPP", "not finite");
		return true;
	}
	if (iterationOverallTpp <= 0.0f || iterationOverallTpp >= 100.0f) {
		report(-1, "overall TPP", "outside the open range 0-100");
		return true;
	}
	for (auto const& [party, voteShare] : overallFpTarget) {
		if (!std::isfinite(voteShare) ||
			voteShare < 0.0f || voteShare > 100.0f) {
			report(-1, "overall FP",
				"outside 0-100 for party index " + std::to_string(party));
			return true;
		}
	}
	for (auto const& [party, preferenceFlow] : overallPreferenceFlow) {
		if (!std::isfinite(preferenceFlow) ||
			preferenceFlow < 0.0f || preferenceFlow > 100.0f) {
			report(-1, "preference flow",
				"outside 0-100 for party index " + std::to_string(party));
			return true;
		}
	}
	for (auto const& [party, exhaustRate] : overallExhaustRate) {
		if (!std::isfinite(exhaustRate) ||
			exhaustRate < 0.0f || exhaustRate > 1.0f) {
			report(-1, "exhaust rate",
				"outside 0-1 for party index " + std::to_string(party));
			return true;
		}
	}
	if (!std::isfinite(indAlpha) || indAlpha <= 0.0f ||
		!std::isfinite(indBeta) || indBeta <= 0.0f) {
		report(-1, "independent distribution parameters", "not positive and finite");
		return true;
	}
	for (int regionIndex = 0; regionIndex < int(regionSwing.size()); ++regionIndex) {
		if (!std::isfinite(regionSwing[regionIndex])) {
			report(-1, "regional swing", "not finite for region index " + std::to_string(regionIndex));
			return true;
		}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		if (int(seatFpVoteShare.size()) > seatIndex) {
			float nonMajorTotal = 0.0f;
			for (auto [party, voteShare] : seatFpVoteShare[seatIndex]) {
				if (!std::isfinite(voteShare)) {
					report(seatIndex, "FP", "not finite");
					return true;
				}
				if (voteShare < 0.0f) {
					report(seatIndex, "FP", "below 0 (" + std::to_string(voteShare) + ")");
					return true;
				}
				if (voteShare > 100.0f) {
					report(seatIndex, "FP", "above 100 (" + std::to_string(voteShare) + ")");
					return true;
				}
				if (party != Mp::One && party != Mp::Two && party != run.natPartyIndex) {
					nonMajorTotal += voteShare;
				}
			}
			if (checkTppMargins && nonMajorTotal >= 100.0f) {
				report(seatIndex, "non-major FP share", "above 100 (" + std::to_string(nonMajorTotal) + ")");
				return true;
			}
		}
		if (int(partyOneNewTppMargin.size()) <= seatIndex) {
			report(seatIndex, "TCP", "invalid because partyOneNewTppMargin is too small");
			return true;
		}
		if (!std::isfinite(partyOneNewTppMargin[seatIndex])) {
			report(seatIndex, "TCP", "not finite");
			return true;
		}
		if (partyOneNewTppMargin[seatIndex] <= -50 && checkTppMargins) {
			report(seatIndex, "TCP", "below 0");
			return true;
		}
		if (partyOneNewTppMargin[seatIndex] >= 50 && checkTppMargins) {
			report(seatIndex, "TCP", "above 100");
			return true;
		}
	}
	return false;
}

int SimulationIteration::runIteration()
{
	retryCount = 0;
	auto retryOrThrow = [&]() {
		if (retryCount >= MaxIterationRetries) {
			throw std::runtime_error(
				"Simulation iteration " + std::to_string(iterationIndex) +
				" remained invalid after the initial attempt and " +
				std::to_string(MaxIterationRetries) + " retries.");
		}
		++retryCount;
		reset();
	};

	bool gotValidResult = false;
	while (!gotValidResult) {
		try {
			// Build the election-wide scenario first, then progressively distribute
			// it through regions and seats before applying any live observations.
			if (run.isLive() && !run.doingBettingOddsCalibrations && !run.doingLiveBaselineSimulation) {
				liveElection = std::make_unique<LiveV2::Election>(
					run.liveElection->generateScenario(randomSampleIndex()));
			}
			loadPastSeatResults();
			initialiseIterationSpecificCounts();
			determineFedStateCorrelation();
			determineOverallTpp();
			decideMinorPartyPopulism();
			determineHomeRegions();
			determineMinorPartyContests();
			determineIntraCoalitionSwing();
			determineIndDistributionParameters();
			determineRegionalSwings();

			if (hasInvalidValues("Before seat initial results", false, false)) {
				retryOrThrow();
				continue;
			}

			// Convert the regional scenario to seat-level FP and TPP estimates,
			// then reconcile them back to the sampled election-wide totals.
			determineSeatInitialResults();

			if (hasInvalidValues("Before reconciling")) {
				retryOrThrow();
				continue;
			}

			reconcileSeatAndOverallFp();

			if (hasInvalidValues("After reconciling")) {
				retryOrThrow();
				continue;
			}

			// Live observations override the corresponding portions of the
			// pre-election scenario while retaining uncertainty elsewhere.
			incorporateLiveResults();

			if (hasInvalidValues("After live results",
				!run.doingLiveBaselineSimulation && !run.doingBettingOddsCalibrations)) {
				retryOrThrow();
				continue;
			}

			// Resolve each seat's final TCP contest and winner, then aggregate
			// support relationships into the government-level result.
			seatTcpVoteShare.resize(project.seats().count());
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				determineSeatFinalResult(seatIndex);
			}

			assignDirectWins();
			assignSupportsPartyWins();
		}
		catch (InvalidIteration const&) {
			retryOrThrow();
			continue;
		}
		catch (std::bad_alloc const& err) {
			PA_LOG_VAR(err.what());
			retryOrThrow();
			continue;
		}
		gotValidResult = true;
	}

	std::lock_guard<std::mutex> lock(recordMutex);
	recordIterationResults();
	return retryCount;
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
	float gammaMedian = 0.7f * exp(-0.00294f * float(dateDiff)); // increased from 0.5555f
	fedStateCorrelation = gammaMedian * variabilityGamma(3.0f, 0.374f, 0, 0, uint32_t(VariabilityTag::FedStateCorrelation));
}

void SimulationIteration::determineOverallTpp()
{
	// Sample the simulation's configured projection, not necessarily the first
	// projection in the project.
	auto& projection = project.projections().access(sim.settings.baseProjection);
	auto projectedSample = projection.generateNowcastSupportSample(
		project.models(),
		randomSampleIndex(),
		project.projections().view(sim.settings.baseProjection).getSettings().endDate
	);
	daysToElection = projectedSample.daysToElection;
	iterationOverallTpp = projectedSample.voteShare.at(TppCode);

	if (sim.settings.forceTpp.has_value()) {
		float tppChange = sim.settings.forceTpp.value() - iterationOverallTpp;
		iterationOverallTpp = sim.settings.forceTpp.value();
		projectedSample.voteShare["ALP"] = predictorCorrectorTransformedSwing(projectedSample.voteShare["ALP"], tppChange);
		projectedSample.voteShare["LNP"] = predictorCorrectorTransformedSwing(projectedSample.voteShare["LNP"], -tppChange);
	}
	iterationOverallSwing = iterationOverallTpp - sim.settings.prevElection2pp;

	// Translate the model's official party codes into the project-specific
	// party indices used throughout the seat simulation.
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
		if (overallPreferenceFlow.contains(partyIndex)) continue;
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
		if (overallExhaustRate.contains(partyIndex)) continue;
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
	// Draw the election-wide correlated shift from aggregate historical
	// errors. Seat-level errors are added separately in determineNationalsShare.
	float const baseOverallRmse = run.nationalsParameters.overallRmse;
	float const baseOverallKurtosis =
		run.nationalsParameters.overallKurtosis;
	float const quantile = variabilityUniform(
		0.0f, 1.0f, 0, 0,
		uint32_t(VariabilityTag::IntraCoalitionSwingGlobal));
	intraCoalitionSwing = rng.flexibleDist(
		0.0f, baseOverallRmse, baseOverallRmse,
		baseOverallKurtosis, baseOverallKurtosis, quantile);
}

void SimulationIteration::determineIndDistributionParameters()
{
	// All these are set up so that the distribution of quantiles
	// for independent candidates are correlated via a beta distribution
	// in any given single election while maintaining the original uniform
	// distribution (approximately) across many simulations
	if (variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist1)) < 0.1f) {
		if (variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist2)) < 0.5f) {
			indAlpha = 0.725f;
			indBeta = 2.0f / std::pow(variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist3)), 0.6f);
		}
		else {
			indAlpha = 2.0f / std::pow(variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist4)), 0.6f);
			indBeta = 0.725f;
		}
	}
	else {
		if (variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist5)) < 0.5f) {
			indAlpha = 2.0f;
			indBeta = 2.0f / std::pow(variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist6)), 0.84f);
		}
		else {
			indAlpha = 2.0f / std::pow(variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::IndDist7)), 0.84f);
			indBeta = 2.0f;
		}
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
			float populism = std::clamp(variabilityUniform(-1.0f, 2.0f, 0, 0, uint32_t(VariabilityTag::PopulismLevel)), 0.0f, 1.0f);
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
	if (project.regions().count() > 0 &&
		variabilityUniform(0.0f, 1.0f, 0, 0, uint32_t(VariabilityTag::EmergingPartyHomeRegion)) < 0.75f) {
		homeRegion[EmergingPartyIndex] = variabilityUniformInt(0, project.regions().count(), 0, 0, uint32_t(VariabilityTag::EmergingPartyHomeRegion2));
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
	}
	correctRegionalSwings();
}

void SimulationIteration::determineMinorPartyContests()
{
	// Candidate lists take precedence when available. Before nominations close,
	// estimate how many seats each minor party will contest and rank seats using
	// their centrist/populist and home-region modifiers.
	for (auto const& [partyIndex, voteShare] : overallFpTarget) {
		if (!(partyIndex >= 2 || partyIndex == EmergingPartyIndex)) continue;
		using Priority = std::pair<int, float>;
		std::vector<Priority> seatPriorities;
		std::vector<float> seatMods(project.seats().count());
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			float seatMod = calculateEffectiveSeatModifier(seatIndex, partyIndex);
			seatMods[seatIndex] = seatMod;
			float quantile = variabilityUniform(
				0.0f, 1.0f, seatIndex, partyIndex,
				uint32_t(VariabilityTag::MinorPartySeatPriority));
			float priority = rng.flexibleDist(seatMod, seatMod * 0.2f, seatMod * 0.6f, 3.0f, 6.0f, quantile);
			seatPriorities.push_back({ seatIndex, priority });
			// Make sure this seat is able to be contested later on for this party
			pastSeatResults[seatIndex].fpVotePercent.insert({ partyIndex, 0.0f });
		}

		if (seatPriorities.empty()) {
			seatContested[partyIndex] = {};
			fpModificationAdjustment[partyIndex] = 1.0f;
			continue;
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
				float halfSeatsPrimary = 5.0f;
				if ((party.abbreviation == "ON" || party.abbreviation == "ONP") && std::stoi(run.yearCode) >= 2024) {
					// One Nation has announced their intention to run candidates in ~all seats, so assume this will happen for now
					halfSeatsPrimary = 0.5f;
				}
				float seatProp = 2.0f - 2.0f * std::pow(2.0f, -voteShare / halfSeatsPrimary);
				// The seat total should adjust somewhat by the expected primary vote, but not entirely.
				// The entered seat total corresponds to expected seat contests at 5% primary vote
				estimatedSeats = std::clamp(party.seatTarget * seatProp, 0.0f, totalSeats);
			}
			else if (partyIndex == EmergingPartyIndex) {
				constexpr float HalfSeatsPrimary = 4.0f;
				float seatProp = 1.0f - std::pow(2.0f, -voteShare / HalfSeatsPrimary);
				estimatedSeats = totalSeats * seatProp;
			}
			else {
				logger << "Error: tried to determine minor party contest rate for a party category that hasn't been accounted for";
			}
			float lowerRmse = std::max((0.5f - std::abs(estimatedSeats - totalSeats * 0.5f) / totalSeats) * 0.6f, 0.01f) * totalSeats;
			float upperRmse = std::min((totalSeats - estimatedSeats) * 1.0f, lowerRmse);

			float quantile = variabilityUniform(0.0f, 1.0f, partyIndex, 0, uint32_t(VariabilityTag::MinorPartySeats));
			float minimumSeats = std::min(totalSeats, std::max(7.0f, estimatedSeats * 0.4f));
			int actualSeats = int(floor(std::clamp(
				rng.flexibleDist(estimatedSeats, lowerRmse, upperRmse, 3.0f, 3.0f, quantile),
				minimumSeats, totalSeats) + 0.5f));
			if (actualSeats < int(seatPriorities.size())) {
				std::nth_element(seatPriorities.begin(), std::next(seatPriorities.begin(), actualSeats), seatPriorities.end(),
					[](Priority const& a, Priority const& b) {return a.second > b.second; });
			}

			fpModificationAdjustment[partyIndex] = 0.0f;
			for (int seatPlace = 0; seatPlace < actualSeats; ++seatPlace) {
				seatContested[partyIndex][seatPriorities[seatPlace].first] = true;
				fpModificationAdjustment[partyIndex] += seatMods[seatPriorities[seatPlace].first];
			}
			// Preserve the sampled election-wide FP target after distributing
			// the party across only its selected seats.
			if (fpModificationAdjustment[partyIndex] > 0.0f) {
				fpModificationAdjustment[partyIndex] = totalSeats / fpModificationAdjustment[partyIndex];
			}
			else {
				// Missing or zero seat modifiers should not create an infinite
				// adjustment. The downstream minimum modifier remains active.
				fpModificationAdjustment[partyIndex] = 1.0f;
			}
		}
		else {
			if (fpModificationAdjustment[partyIndex] > 0.0f) {
				fpModificationAdjustment[partyIndex] = seatsKnown / fpModificationAdjustment[partyIndex];
			}
			else {
				fpModificationAdjustment[partyIndex] = 1.0f;
			}
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
		float quantile = variabilityUniform(0.0f, 1.0f, regionIndex, 0, uint32_t(VariabilityTag::RegionSwing));
		float randomVariation = rng.flexibleDist(0.0f, specificRmse, specificRmse, kurtosis, kurtosis, quantile);
		float totalDeviation = mixedDeviation + randomVariation;
		swingToTransform = iterationOverallSwing + totalDeviation;
	}
	else {
		// Naive swing - the swing we get without any region polling history
		float pollRawDeviation = run.regionSwingDeviations[regionIndex];
		float rmse = run.regionBaseBehaviour[regionIndex].rmse;
		float kurtosis = run.regionBaseBehaviour[regionIndex].kurtosis;
		float quantile = variabilityUniform(0.0f, 1.0f, regionIndex, 0, uint32_t(VariabilityTag::RegionSwingNaive));
		float randomVariation = rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis, quantile);
		// Use polled variation assuming correlation is about the same as worst performing state
		float naiveSwing = medianNaiveSwing + pollRawDeviation * 0.3f + randomVariation;
		swingToTransform = naiveSwing;
	}

	float transformedTpp = transformVoteShare(thisRegion.lastElection2pp) + swingToTransform;
	float detransformedTpp = detransformVoteShare(transformedTpp);
	regionSwing[regionIndex] = detransformedTpp - thisRegion.lastElection2pp;
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

	float tempOverallSwing = iterationOverallSwing;
	if (weightSums > 0.0) {
		tempOverallSwing = float(weightedSwings / weightSums);
	}
	else if (!regionSwing.empty()) {
		// Previous turnout should normally provide the weights. Equal region
		// weighting is a safe fallback for incomplete external data.
		tempOverallSwing = std::accumulate(regionSwing.begin(), regionSwing.end(), 0.0f) /
			float(regionSwing.size());
	}
	float regionSwingAdjustment = iterationOverallSwing - tempOverallSwing;
	for (float& swing : regionSwing) {
		swing += regionSwingAdjustment;
	}
}

void SimulationIteration::determineSeatInitialResults()
{
	// Construct minor-party FPs first because special TPP adjustments can depend
	// on them. Major-party FPs are then inferred from the resulting TPP and
	// preference flows.
	seatFpVoteShare.assign(project.seats().count(), {});
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatInitialFp(seatIndex);

		if (hasInvalidValues("After initial FPs", false, false)) {
			throw InvalidIteration();
		}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatTpp(seatIndex);

		if (hasInvalidValues("After initial TPPs", false, false)) {
			throw InvalidIteration();
		}
	}

	correctSeatTppSwings();

	nationalsShare.assign(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineNationalsShare(seatIndex);

		allocateMajorPartyFp(seatIndex);

		if (hasInvalidValues("After corrections")) {
			throw InvalidIteration();
		}
	}
}

void SimulationIteration::determineSeatTpp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	const float tppPrev = seat.tppMargin + 50.0f;
	if (!std::isfinite(tppPrev) || tppPrev <= 0.0f || tppPrev >= 100.0f) {
		throw std::runtime_error(
			"Seat " + seat.name +
				" must have a previous TPP strictly between 0 and 100.");
	}
	float transformedTpp = transformVoteShare(tppPrev);
	const float elasticity = run.seatParameters[seatIndex].elasticity;
	// float trend = run.seatParameters[seatIndex].trend;
	const float volatility = run.seatParameters[seatIndex].volatility;
	const bool useVolatility = run.seatParameters[seatIndex].loaded;
	// Save effects so we can record them for the display
	const int regionIndex = project.regions().idToIndex(seat.region);
	if (regionIndex == RegionCollection::InvalidIndex) {
		throw std::runtime_error(
			"Seat " + seat.name + " refers to a region that does not exist.");
	}
	const float thisRegionSwing = regionSwing.at(regionIndex);
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
		float bias = basicTransformedSwing(0.8f, variabilityNormal(0.0f, 0.15f, 0, 0, uint32_t(VariabilityTag::KiamaBias)));
		float exhaustRate = basicTransformedSwing(0.5f, variabilityNormal(0.0f, 0.15f, 0, 0, uint32_t(VariabilityTag::KiamaExhaust)));
		const float alpBase = tppPrev - indShare * ((1.0f - bias) * exhaustRate);
		const float lnpBase = (100.0f - tppPrev) - indShare * (bias * exhaustRate);
		const float exhaustedTotal = alpBase + lnpBase;
		if (exhaustedTotal > 0.0f) {
			const float alpNew = alpBase / exhaustedTotal * 100.0f;
			thirdPartyExhaustEffect = alpNew - tppPrev;
		}
	}
	transformedTpp += thisRegionSwing + elasticitySwing;
	// Add modifiers for known local effects
	transformedTpp += localEffects;
	// Remove the average local modifier across the region
	// Only do this for federal elections since we don't have regional swing estimates otherwise
	if (run.regionCode == "fed") {
		transformedTpp -= run.regionLocalModifierAverage.at(regionIndex);
	}
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
	float quantile = variabilityUniform(0.0f, 1.0f, seatIndex, 0, uint32_t(VariabilityTag::SeatTpp));
	transformedTpp += rng.flexibleDist(0.0f, swingDeviation, swingDeviation, kurtosis, kurtosis, quantile);
	partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;


	const float totalFixedEffects = thisRegionSwing + elasticitySwing + localEffects + previousSwingEffect +
		federalSwingEffect + byElectionEffect + thirdPartyExhaustEffect;
	const float fixedSwingSize = detransformVoteShare(transformVoteShare(tppPrev) + totalFixedEffects) - tppPrev;
	// These display attributions use one common conversion back to raw TPP
	// points. They are intentionally approximate: nonlinear scaling and later
	// post-processing mean exact marginal effects would depend on application
	// order. A sequential attribution could replace this if greater precision
	// becomes useful.
	// Use the limiting inverse-logit derivative when fixed effects cancel
	// exactly; dividing 0 by 0 here previously contaminated the diagnostics.
	const float transformFactor = std::abs(totalFixedEffects) > 0.000001f ?
		fixedSwingSize / totalFixedEffects :
		1.0f / logitDeriv(tppPrev);
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
			double unweightedSwing = 0.0;
			int seatCount = 0;
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				if (seat.region != regionId) continue;
				double const swing = double(partyOneNewTppMargin[seatIndex] - seat.tppMargin);
				double const turnout = std::max(
					double(run.pastSeatResults[seatIndex].turnoutCount), 0.0);
				double turnoutScaledSwing = swing * turnout;
				totalSwing += turnoutScaledSwing;
				totalTurnout += turnout;
				unweightedSwing += swing;
				++seatCount;
			}
			if (!seatCount) continue;
			// Previous turnout normally provides the weights. Equal seat weighting
			// keeps incomplete historical input finite and internally consistent.
			double averageSwing = totalTurnout > 0.0 ?
				totalSwing / totalTurnout :
				unweightedSwing / double(seatCount);
			float swingAdjust = regionSwing[regionIndex] - float(averageSwing);
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				if (seat.region != regionId) continue;
				partyOneNewTppMargin[seatIndex] = (predictorCorrectorTransformedSwing(partyOneNewTppMargin[seatIndex] + 50.0f, swingAdjust) - 50.0f);
			}
		}
	}
	// Now fix seats to the nation tpp as the above calculation doesn't always do this
	double totalTpp = 0.0;
	double totalTurnout = 0.0;
	double unweightedTpp = 0.0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		double const seatTpp = double(partyOneNewTppMargin[seatIndex] + 50.0);
		double const turnout = std::max(
			double(run.pastSeatResults[seatIndex].turnoutCount), 0.0);
		double turnoutScaledTpp = seatTpp * turnout;
		totalTpp += turnoutScaledTpp;
		totalTurnout += turnout;
		unweightedTpp += seatTpp;
	}
	if (project.seats().count() == 0) return;
	float averageTpp = totalTurnout > 0.0 ?
		float(totalTpp / totalTurnout) :
		float(unweightedTpp / double(project.seats().count()));
	float swingAdjust = iterationOverallTpp - averageTpp;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		partyOneNewTppMargin[seatIndex] = (predictorCorrectorTransformedSwing(partyOneNewTppMargin[seatIndex] + 50.0f, swingAdjust) - 50.0f);
	}
}

void SimulationIteration::determineSeatInitialFp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	int const incumbentPartyIndex =
		project.parties().idToIndex(seat.incumbent);
	int const challengerPartyIndex =
		project.parties().idToIndex(seat.challenger);
	if (!seat.tcpChange.empty() &&
		challengerPartyIndex == PartyCollection::InvalidIndex) {
		throw std::runtime_error(
			"Seat " + seat.name +
				" has TCP-change adjustments but no valid challenger.");
	}
	std::string const challengerCode =
		challengerPartyIndex == PartyCollection::InvalidIndex ?
		std::string() :
		project.parties().viewByIndex(challengerPartyIndex).abbreviation;

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
			if (partyIndex == incumbentPartyIndex &&
				seat.tcpChange.contains(challengerCode)) {
				voteShare = predictorCorrectorTransformedSwing(
					voteShare, -seat.tcpChange.at(challengerCode));
			}
			else if (partyIndex == challengerPartyIndex &&
				seat.tcpChange.contains(challengerCode)) {
				voteShare = predictorCorrectorTransformedSwing(
					voteShare, seat.tcpChange.at(challengerCode));
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
		// A strong historical candidate whose party has no separate current
		// trend is treated as a quasi-independent. These candidates are often
		// personally established and only nominally aligned with a minor party.
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
			incumbentPartyIndex == partyIndex ||
			contains(run.seatProminentMinors[seatIndex], partyIndex)
		)) {
			determineSpecificPartyFp(seatIndex, partyIndex, voteShare, run.indSeatStatistics);
		}
		else if (effectivePopulist) {
			determinePopulistFp(seatIndex, partyIndex, voteShare);
		}
		else if (partyIndex >= Mp::Others) {
			continue;
		}
		if (partyIndex == OthersIndex) {
			if (run.runningParties[seatIndex].size() && !contains(run.runningParties[seatIndex], OthersCode)) continue;
		}
		// Note: this means major party vote shares get passed on as-is
		seatFpVoteShare[seatIndex][partyIndex] += voteShare;
	}

	determineSeatEmergingParties(seatIndex);

	if (seat.confirmedProminentIndependent) determineSeatConfirmedInds(seatIndex);

	determineSeatEmergingInds(seatIndex);

	determineSeatOthers(seatIndex);

	adjustForFpCorrelations(seatIndex);

	// Helps to effect minor party crowding, i.e. if too many minor parties
	// rise in their fp vote, then they're all reduced a bit more than if only one rose.
	prepareFpsForNormalisation(seatIndex);

	normaliseSeatFp(seatIndex);
}

void SimulationIteration::determineSpecificPartyFp(
	int seatIndex,
	int partyIndex,
	float& voteShare,
	SimulationRun::SeatStatistics const& seatStatistics)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	int const incumbentPartyIndex = project.parties().idToIndex(seat.incumbent);

	if (run.runningParties[seatIndex].size() && partyIndex >= Mp::Others &&
		!contains(run.runningParties[seatIndex], project.parties().viewByIndex(partyIndex).abbreviation)) {
		voteShare = 0.0f;
		return;
	}
	if (partyIndex == run.indPartyIndex && seat.confirmedProminentIndependent) {
		// this case will be handled by the "confirmed independent" logic instead
		voteShare = 0.0f;
		return;
	}
	float modifiedVoteShare = voteShare;
	float minorViability = getAt(
		run.seatMinorViability[seatIndex], partyIndex, 0.0f);
	float minorVoteMod = 1.0f + (0.4f * minorViability);
	modifiedVoteShare = std::clamp(
		modifiedVoteShare * minorVoteMod, 0.01f, 99.0f);

	float transformedFp = transformVoteShare(modifiedVoteShare);
	float seatStatisticsExact = (std::clamp(transformedFp, seatStatistics.scaleLow, seatStatistics.scaleHigh)
		- seatStatistics.scaleLow) / seatStatistics.scaleStep;
	int seatStatisticsLower = int(std::floor(seatStatisticsExact));
	float seatStatisticsMix = seatStatisticsExact - float(seatStatisticsLower);
	using StatType = SimulationRun::SeatStatistics::TrendType;
	// At the upper scale point the mix factor is zero, so no following
	// element is needed.
	auto getMixedStat = [&](StatType statType) {
		auto const& values = seatStatistics.trend[int(statType)];
		float const lower = values[seatStatisticsLower];
		if (seatStatisticsMix == 0.0f) return lower;
		return mix(
			lower,
			values[seatStatisticsLower + 1],
			seatStatisticsMix);
	};
	float recontestRateMixed = getMixedStat(StatType::RecontestRate);
	float recontestIncumbentRateMixed = getMixedStat(StatType::RecontestIncumbentRate);
	float timeToElectionFactor = std::clamp(
		1.78f - 0.26f * log(float(std::max(daysToElection, 1))),
		0.0f, 1.0f);

	if (incumbentPartyIndex == partyIndex) {
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
		(contains(run.runningParties[seatIndex], OthersCode) ||
			contains(run.runningParties[seatIndex], std::string("IND")))) {
		recontestRateMixed = 1.0f;
	}
	recontestRateMixed = std::clamp(recontestRateMixed, 0.0f, 1.0f);
	// also, should some day handle retirements for minor parties that would be expected to stay competitive
	float recontestRateCheck = variabilityUniform(
		0.0f, 1.0f, seatIndex, partyIndex,
		uint32_t(VariabilityTag::RecontestRateCheck));
	if (recontestRateCheck > recontestRateMixed ||
		(seat.retirement && partyIndex == incumbentPartyIndex &&
			!overallFpTarget.contains(partyIndex))) {
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
		transformedFp += variabilityUniform(0.0f, 15.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::ProminentMinorPreBonus));
	}

	constexpr float OddsWeight = 0.6f;
	if (run.oddsCalibrationMeans.contains({ seatIndex, partyIndex })) {
		transformedFp = run.oddsCalibrationMeans[{seatIndex, partyIndex}];
	}
	else if (run.oddsFinalMeans.contains({ seatIndex, partyIndex })) {
		transformedFp = mix(transformedFp, run.oddsFinalMeans[{seatIndex, partyIndex}], OddsWeight);
	}
	float quantile = partyIndex == run.indPartyIndex ?
		variabilityBeta(indAlpha, indBeta, seatIndex, partyIndex, uint32_t(VariabilityTag::MinorQuantile)) :
		variabilityUniform(0.0f, 1.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::MinorQuantile));
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
		float uniform1 = variabilityUniform(0.0f, 1.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::ProminentMinorBonus1));
		float uniform2 = variabilityUniform(0.0f, 1.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::ProminentMinorBonus2));
		regularVoteShare = predictorCorrectorTransformedSwing(
			regularVoteShare, uniform1 * uniform2 * ProminentMinorBonusMax * minorVoteMod * minorVoteMod
		);
	}

	constexpr float MaxSpecificPartyFpShare = 99.0f;
	regularVoteShare = std::min(regularVoteShare, MaxSpecificPartyFpShare);

	if (partyIndex == run.indPartyIndex &&
		incumbentPartyIndex != run.indPartyIndex) {
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
	// Use an incumbent's previous support as the baseline floor before regional
	// and random variation. Those later effects can still produce a final vote
	// below the previous result. std::clamp cannot be used because the baseline
	// floor can legitimately exceed modifiedFp2.
	float modifiedFp = std::max(
		incumbentFp, std::min(modifiedFp1, modifiedFp2));
	modifiedFp = std::clamp(modifiedFp, 0.01f, 99.0f);

	float transformedFp = transformVoteShare(modifiedFp);

	int const regionIndex = project.regions().idToIndex(seat.region);
	if (run.regionFpSwingDeviations.contains(partyIndex)) {
		transformedFp += getAt(
			run.regionFpSwingDeviations.at(partyIndex), regionIndex, 0.0f);
	}

	float populism = centristPopulistFactor[partyIndex];
	float lowerRmse = mix(run.centristStatistics.lowerRmse, run.populistStatistics.lowerRmse, populism);
	float upperRmse = mix(run.centristStatistics.upperRmse, run.populistStatistics.upperRmse, populism);
	float lowerKurtosis = mix(run.centristStatistics.lowerKurtosis, run.populistStatistics.lowerKurtosis, populism);
	float upperKurtosis = mix(run.centristStatistics.upperKurtosis, run.populistStatistics.upperKurtosis, populism);

	float quantile = variabilityUniform(
		0.0f, 1.0f, seatIndex, partyIndex,
		uint32_t(VariabilityTag::PopulistFp));
	transformedFp += rng.flexibleDist(0.0f, lowerRmse, upperRmse, lowerKurtosis, upperKurtosis, quantile);

	float regularVoteShare = detransformVoteShare(transformedFp);

	if (prominent) {
		regularVoteShare += (1.0f - std::clamp(regularVoteShare / ProminentMinorFlatBonusThreshold, 0.0f, 1.0f)) * ProminentMinorFlatBonus;
		float uniform1 = variabilityUniform(0.0f, 1.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::ProminentPopulistBonus1));
		float uniform2 = variabilityUniform(0.0f, 1.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::ProminentPopulistBonus2));
		regularVoteShare = predictorCorrectorTransformedSwing(regularVoteShare, uniform1 * uniform2 * ProminentMinorBonusMax);
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
	float prevOthers = run.pastSeatResults[seatIndex].prevOthers;
	indContestRate += run.indEmergence.prevOthersRateMod * prevOthers;
	indContestRate = 0.9f + 0.1f * indContestRate;
	if (ballotConfirmed) indContestRate = 1.0f;
	if (std::max(0.01f, indContestRate) > variabilityUniform(0.0f, 1.0f, seatIndex, 0, uint32_t(VariabilityTag::ConfirmedIndsContestRate))) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - run.indEmergence.fpThreshold;
		if (std::abs(interceptSize) < 0.000001f) {
			throw std::runtime_error(
				"Independent-emergence vote intercept must differ from its FP threshold.");
		}
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
		if (project.parties().idToIndex(seat.incumbent) == Mp::One) {
			rmse *= 1.1f;
		}
		rmse = std::max(rmse, 0.0f);
		float quantile = variabilityBeta(indAlpha, indBeta, seatIndex, run.indPartyIndex, uint32_t(VariabilityTag::ConfirmedIndVariation)) * 0.5f + 0.5f;
		float variableVote = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis, quantile));
		float transformedVoteShare = variableVote + run.indEmergence.fpThreshold;

		constexpr float OddsWeight = 0.6f;
		float adjustedWeight = seat.name == "Kiama" && run.getTermCode() == "2023nsw" ? 1.0f : OddsWeight;
		constexpr float oddsBasedVariation = 20.0f;
		if (run.oddsCalibrationMeans.contains({ seatIndex, run.indPartyIndex })) {
			const float voteShareCenter = run.oddsCalibrationMeans[{seatIndex, run.indPartyIndex}];
			transformedVoteShare = variabilityNormal(
				voteShareCenter, oddsBasedVariation, seatIndex, 0, uint32_t(VariabilityTag::ConfirmedIndsOddsCalibration)
			);
		}
		// else, because if we're calibrating betting results we don't want seat polls to interfere with that 
		else {
			if (run.oddsFinalMeans.contains({ seatIndex, run.indPartyIndex })) {
				if (adjustedWeight > variabilityUniform(0.0f, 1.0f, seatIndex, 0, uint32_t(VariabilityTag::ConfirmedIndsOddsWeightCheck))) {
					const float voteShareCenter = run.oddsFinalMeans[{seatIndex, run.indPartyIndex}];
					float oddsBasedVoteShare = variabilityNormal(
						voteShareCenter, oddsBasedVariation, seatIndex, 0, uint32_t(VariabilityTag::ConfirmedIndsOddsBasedShare)
					);
					transformedVoteShare = oddsBasedVoteShare;
				}
			}
			if (run.seatPolls[seatIndex].contains(run.indPartyIndex)) {
				float weightedSum = 0.0f;
				float sumOfWeights = 0.0f;
				for (auto const& poll : run.seatPolls[seatIndex][run.indPartyIndex]) {
					constexpr float QualityWeightBase = 0.6f;
					float weight = myPow(QualityWeightBase, poll.second);
					if (!std::isfinite(weight) || weight <= 0.0f) continue;
					float pollRaw = poll.first;
					pollRaw = pollRaw * 0.503f + 15.59f;
					weightedSum += pollRaw * weight;
					sumOfWeights += weight;
				}
				if (sumOfWeights > 0.0f) {
					float transformedPollFp = transformVoteShare(
						std::clamp(weightedSum / sumOfWeights, 0.1f, 99.9f));
					constexpr float MaxPollWeight = 0.8f;
					constexpr float PollWeightBase = 0.6f;
					float pollFactor = MaxPollWeight *
						(1.0f - std::powf(PollWeightBase, sumOfWeights));
					transformedVoteShare = mix(
						transformedVoteShare, transformedPollFp, pollFactor);
				}
			}
		}

		constexpr float MaxSpecificPartyFpShare = 99.0f;
		float const independentVoteShare = std::min(
			detransformVoteShare(transformedVoteShare),
			MaxSpecificPartyFpShare);
		seatFpVoteShare[seatIndex][run.indPartyIndex] = std::max(
			seatFpVoteShare[seatIndex][run.indPartyIndex],
			independentVoteShare);
	}
}

void SimulationIteration::determineSeatEmergingInds(int seatIndex)
{
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
	float prevOthers = run.pastSeatResults[seatIndex].prevOthers;
	indEmergenceRate += run.indEmergence.prevOthersRateMod * prevOthers;
	bool existingStrongCandidate = false;
	// increased change of emerging inds based on how competitive one was last time
	// (a bit ad-hoc for now, should do more scientifically later)
	if (run.pastSeatResults[seatIndex].fpVotePercent.contains(run.indPartyIndex)) {
		float multiplier = 1.0f;
		// Reduce emergence chance if an ind actually won last time
		// as re-run by the same ind will not count for this category in that case
		if (run.pastSeatResults[seatIndex].tcpVotePercent.contains(run.indPartyIndex) &&
			run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] > 50.0f) {
			multiplier *= 0.3f;
		}
		indEmergenceRate += 0.008f * multiplier * std::clamp(run.pastSeatResults[seatIndex].fpVotePercent.at(run.indPartyIndex) - 8.0f, 0.0f, 24.0f);
	}
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
	indEmergenceRate = std::clamp(indEmergenceRate, 0.01f, 1.0f);
	if (1.0f - variabilityBeta(indAlpha, indBeta, seatIndex, run.indPartyIndex, uint32_t(VariabilityTag::IndEmergenceDecision)) < indEmergenceRate) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - run.indEmergence.fpThreshold;
		if (std::abs(interceptSize) < 0.000001f) {
			throw std::runtime_error(
				"Independent-emergence vote intercept must differ from its FP threshold.");
		}
		if (isFederal) rmse *= (1.0f + run.indEmergence.fedVoteCoeff / interceptSize);
		if (isRural) rmse *= (1.0f + run.indEmergence.ruralVoteCoeff / interceptSize);
		if (isProvincial) rmse *= (1.0f + run.indEmergence.provincialVoteCoeff / interceptSize);
		if (isOuterMetro) rmse *= (1.0f + run.indEmergence.outerMetroVoteCoeff / interceptSize);
		float prevOthersCoeff = run.indEmergence.prevOthersVoteCoeff * prevOthers;
		rmse *= (1.0f + prevOthersCoeff / interceptSize);
		// increased vote for emerging inds based on how competitive one was last time
		// (a bit ad-hoc for now, should do more scientifically later)
		if (run.pastSeatResults[seatIndex].fpVotePercent.contains(run.indPartyIndex) && !existingStrongCandidate) {
			float multiplier = 1.0f;
			// Reduce vote if an ind actually won last time
			// as re-run by the same ind will not count for this category in that case
			if (run.pastSeatResults[seatIndex].tcpVotePercent.contains(run.indPartyIndex) &&
				run.pastSeatResults[seatIndex].tcpVotePercent[run.indPartyIndex] > 50.0f) {
				multiplier *= 0.5f;
			}
			rmse = predictorCorrectorTransformedSwing(
				rmse, 
				0.8f * multiplier * std::clamp(run.pastSeatResults[seatIndex].fpVotePercent.at(run.indPartyIndex) - 8.0f, 0.0f, 24.0f)
			);
		}
		rmse = std::max(rmse, 0.0f);
		// The quantile should only fall within the upper half of the distribution
		// so that the correlation created using the beta distribution works
		// as intended
		float quantile = variabilityBeta(indAlpha, indBeta, seatIndex, run.indPartyIndex, uint32_t(VariabilityTag::IndEmergenceQuantile)) * 0.5f + 0.5f;
		float variableVote = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis, quantile));
		float transformedVoteShare = variableVote + run.indEmergence.fpThreshold;
		seatFpVoteShare[seatIndex][EmergingIndIndex] += detransformVoteShare(transformedVoteShare);
	}
}

void SimulationIteration::determineSeatOthers(int seatIndex)
{
	constexpr float MinPreviousOthFp = 2.0f;
	float voteShare = MinPreviousOthFp;
	voteShare = std::max(voteShare, pastSeatResults[seatIndex].prevOthers);

	determineSpecificPartyFp(seatIndex, OthersIndex, voteShare, run.othSeatStatistics);

	if (run.runningParties[seatIndex].size()) {
		bool emergingPartyPresent = seatFpVoteShare[seatIndex].contains(EmergingPartyIndex) && seatFpVoteShare[seatIndex][EmergingPartyIndex] > 0;
		bool emergingIndPresent = seatFpVoteShare[seatIndex].contains(EmergingIndIndex) && seatFpVoteShare[seatIndex][EmergingIndIndex] > 0;
		bool confirmedIndPresent = seatFpVoteShare[seatIndex].contains(run.indPartyIndex) && seatFpVoteShare[seatIndex][run.indPartyIndex] > 0;
		int othersCount = run.othCount[seatIndex] - (emergingPartyPresent ? 1 : 0) +
			run.indCount[seatIndex] - (emergingIndPresent ? 1 : 0) - (confirmedIndPresent ? 1 : 0);

		if (othersCount <= 0) {
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
	for (auto& [party, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (!isMajor(party, run.natPartyIndex) && voteShare > maxPrevious) {
			maxPrevious = voteShare;
		}
	}
	float maxCurrent = 0.0f;
	for (auto& [party, voteShare] : seatFpVoteShare[seatIndex]) {
		if (party == CoalitionPartnerIndex) continue;
		if (!isMajor(party) && voteShare > maxCurrent) maxCurrent = voteShare;
	}
	// Adjustment to prevent high OTH vote from crowding out other minor parties
	// Crowding should only occur between defined parties
	if (pastSeatResults[seatIndex].prevOthers > 0.0f && seatFpVoteShare[seatIndex][OthersIndex] > pastSeatResults[seatIndex].prevOthers) {
		maxCurrent += seatFpVoteShare[seatIndex][OthersIndex] - pastSeatResults[seatIndex].prevOthers;
	}
	// Some sanity checks here to make sure major party votes aren't reduced below zero or actually increased
	float diffCeiling = std::min(30.0f, 0.8f * (seatFpVoteShare[seatIndex][0] + seatFpVoteShare[seatIndex][1]));
	float diff = std::max(0.0f, std::min(diffCeiling, maxCurrent - maxPrevious));
	// The values for the majors (i.e. parties 0 and 1) are overwritten anyway,
	// so this only has the effect of softening effect of the normalisation.
	// This ensures that the normalisation is only punishing to minor parties
	// when more than one rises in votes (thus crowding each other out)
	float const majorPartyTotal =
		seatFpVoteShare[seatIndex][Mp::One] +
		seatFpVoteShare[seatIndex][Mp::Two];
	float partyOneProportion = majorPartyTotal > 0.0f ?
		seatFpVoteShare[seatIndex][Mp::One] / majorPartyTotal :
		0.5f;
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

	nationalsShare[seatIndex] = std::clamp(
		run.seatNationalsExpectation[seatIndex], 0.0f, 1.0f);
	auto const& runningParties = run.runningParties[seatIndex];
	int const incumbentPartyIndex = project.parties().idToIndex(seat.incumbent);

	// If Nationals are not running in this seat, then their share is zero
	if (!runningParties.empty() &&
		!contains(runningParties, std::string("NAT"))) {
		nationalsShare[seatIndex] = 0.0f;
		return;
	}

	// If Nationals are the only party running in this seat, then their share is 100%
	if (!runningParties.empty() &&
		!contains(
			runningParties,
			project.parties().viewByIndex(Mp::Two).abbreviation)) {
		nationalsShare[seatIndex] = 1.0f;
		return;
	}

	// If the seat has a NAT candidate, raise the expectation to a minimum of 5%
	if (std::any_of(seat.candidateNames.begin(), seat.candidateNames.end(), 
		[](const auto& pair) { return pair.second == "NAT"; })) {
		nationalsShare[seatIndex] = std::max(nationalsShare[seatIndex], 0.05f);
	}

	// If candidates aren't confirmed yet, consider the Coalition policy of not challenging each others' incumbents
	// which generally means the opposite coalition party will not run, but we must allow for the possibility of a future retirement 
	constexpr float ContinuationChance = 0.85f; // TODO: make this more sophisticated based on time until the election + calibrate historically
	float futureRetirementQuantile = variabilityUniform(
		0.0f, 1.0f, seatIndex, 0,
		uint32_t(VariabilityTag::CoalitionFutureRetirement));
	bool waException =
		project.regions().view(seat.region).name == "WA" ||
		run.regionCode == "wa";
	if (runningParties.empty() &&
		futureRetirementQuantile < ContinuationChance && !waException) {
		if (incumbentPartyIndex == Mp::Two && !seat.retirement) {
			nationalsShare[seatIndex] = 0.0f;
			return;
		}
		else if (incumbentPartyIndex == run.natPartyIndex && !seat.retirement) {
			nationalsShare[seatIndex] = 1.0f;
			return;
		}
	} // A possible retirement leaves both Coalition parties available.

	if (nationalsShare[seatIndex] > 0 && nationalsShare[seatIndex] < 1) {
		float rmse = run.nationalsParameters.rmse;
		float kurtosis = run.nationalsParameters.kurtosis;
		float transformedShare = transformVoteShare(nationalsShare[seatIndex] * 100.0f);
		float quantile = variabilityUniform(
			0.0f, 1.0f, seatIndex, 0,
			uint32_t(VariabilityTag::IntraCoalitionSwing));
		float transformedSwing = rng.flexibleDist(intraCoalitionSwing, rmse, rmse, kurtosis, kurtosis, quantile);
		transformedShare += transformedSwing;
		nationalsShare[seatIndex] = detransformVoteShare(transformedShare) * 0.01f;
	}
}

void SimulationIteration::allocateMajorPartyFp(int seatIndex, float preferenceFlowDeviation)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	int const incumbentPartyIndex = project.parties().idToIndex(seat.incumbent);

	auto oldFpVotes = seatFpVoteShare[seatIndex]; // retained for invalid-value diagnostics

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
	float previousExhaustDenominator = 0.0f;

	for (auto [partyIndex, voteShare] : run.pastSeatResults[seatIndex].fpVotePercent) {
		if (isMajor(partyIndex)) continue;
		if (partyIndex == CoalitionPartnerIndex) {
			previousPartyOnePrefEstimate += 0.15f * voteShare;
			continue;
		}
		float exhaustRate = calculateEffectiveExhaustRate(partyIndex, false);
		// Use a special formula for IND-like preference flows that accounts for tactical voting
		if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
			float effectivePreferenceFlow = calculateEffectivePreferenceFlow(partyIndex, voteShare, false);
			float adjustedPreferenceFlow = std::clamp(effectivePreferenceFlow + preferenceFlowDeviation, 1.0f, 99.0f);
			previousPartyOnePrefEstimate += voteShare * adjustedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		else {
			float previousPreferences = run.previousPreferenceFlow[partyIndex];
			float adjustedPreferenceFlow = std::clamp(previousPreferences + preferenceFlowDeviation, 1.0f, 99.0f);
			previousPartyOnePrefEstimate += voteShare * adjustedPreferenceFlow * 0.01f * (1.0f - exhaustRate);
		}
		previousNonMajorFpShare += voteShare * (1.0f - exhaustRate);
		previousExhaustRateEstimate += exhaustRate * voteShare;
		previousExhaustDenominator += voteShare;
	}
	if (previousExhaustDenominator > 0.0f) {
		previousExhaustRateEstimate /= previousExhaustDenominator;
	}
	else {
		previousExhaustRateEstimate = 0.0f;
	}

	// Step 2: Calculate the preference bias between the estimate from last election and actual results

	// If party two didn't run last election in this seat, we can't make any sensible inferences about
	// the preference bias, so just leave it at zero. (Maybe interpolate from similar seats eventually?)
	bool previousPartyOneExists = run.pastSeatResults[seatIndex].fpVotePercent.contains(Mp::One);
	bool previousPartyTwoExists = run.pastSeatResults[seatIndex].fpVotePercent.contains(Mp::Two);
	float preferenceBiasRate = 0.0f;
	float exhaustBiasRate = 0.0f;
	bool majorTcp = run.pastSeatResults[seatIndex].tcpVotePercent.contains(Mp::One) && run.pastSeatResults[seatIndex].tcpVotePercent.contains(Mp::Two);
	// Need to calculate:
	// (a) from estimate: % of non-exhausting vote reaching party one
	// (b) from actual results: % of non-exhausting vote reaching party one
	// For (b), difficult to calculate under OPV if TCP includes a non-major (at this stage we don't have separate TPPs)
	// so in that case, also assume standard 
	if (previousPartyOneExists && previousPartyTwoExists && previousNonMajorFpShare && (majorTcp || previousExhaustRateEstimate < 0.01f)) {
		float previousPartyOneTppPercent = 0.0f;
		//float previousExhaustRate = 0.0f;
		auto const& fpCounts = run.pastSeatResults[seatIndex].fpVoteCount;
		auto const& tcpCounts = run.pastSeatResults[seatIndex].tcpVoteCount;
		auto prevTcpSum = std::accumulate(tcpCounts.begin(), tcpCounts.end(), 0, [](int acc, const auto& el) {return acc + el.second; });
		auto prevFpSum = std::accumulate(fpCounts.begin(), fpCounts.end(), 0, [](int acc, const auto& el) {return acc + el.second; });
		float prevPartyOneTcpCount = 0.0f;
		auto majorFpSum =
			getAt(fpCounts, Mp::One, 0) +
			getAt(fpCounts, Mp::Two, 0);
		int const previousPreferenceFpCount = prevFpSum - majorFpSum;
		int const distributedPreferenceCount = prevTcpSum - majorFpSum;
		if (majorTcp && previousPreferenceFpCount > 0) {
			previousPartyOneTppPercent = run.pastSeatResults[seatIndex].tcpVotePercent[Mp::One];
			prevPartyOneTcpCount = float(getAt(tcpCounts, Mp::One, 0.0f));
			// Note, this can theoretically be below 0 in intra-coalition contests
			float previousExhaustRate =
				1.0f - float(distributedPreferenceCount) /
				float(previousPreferenceFpCount);
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
		if (!prevPartyOneTcpCount) {
			prevPartyOneTcpCount =
				float(prevTcpSum) * previousPartyOneTppPercent * 0.01f;
		}

		if (distributedPreferenceCount > 0) {
			float previousPrefRateEstimate =
				previousPartyOnePrefEstimate / previousNonMajorFpShare;
			float previousPrefRate =
				(prevPartyOneTcpCount - float(getAt(fpCounts, Mp::One, 0))) /
				float(distributedPreferenceCount);
			// Amount by which actual TPP is higher than estimated TPP,
			// per 1% of the non-exhausted vote.
			preferenceBiasRate =
				previousPrefRate - previousPrefRateEstimate;
		}
	}

	// Step 3: Get an estimate for the *current* election

	float currentPartyOnePrefs = 0.0f;
	float currentNonMajorFpShare = 0.0f;
	float currentExhaustRateEstimate = 0.0f;
	float currentExhaustDenominator = 0.0f;
	std::map<int, float> preferenceVariation; // in minor -> major preferences, after transformation
	std::map<int, float> exhaustVariation;

	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		if (isMajor(partyIndex)) continue;
		if (partyIndex == CoalitionPartnerIndex) {
			currentPartyOnePrefs += 0.15f * voteShare;
			continue;
		}
		float exhaustRate = calculateEffectiveExhaustRate(partyIndex, true);
		if (!preferenceVariation.contains(partyIndex)) preferenceVariation[partyIndex] =
			variabilityNormal(0.0f, 15.0f, seatIndex, partyIndex, uint32_t(VariabilityTag::SeatPreferenceVariation));
		if (!exhaustVariation.contains(partyIndex)) {
			// Exhaust rates are stored and consumed as proportions. This small
			// swing preserves the historically calibrated percentage-point-scale
			// variation despite basicTransformedSwing's 0-100 convention.
			exhaustVariation[partyIndex] = variabilityNormal(
				0.0f, 0.15f, seatIndex, partyIndex,
				uint32_t(VariabilityTag::SeatExhaustVariation));
		}
		float randomisedExhaustRate = exhaustRate ? basicTransformedSwing(exhaustRate, exhaustVariation[partyIndex]) : 0.0f;
		if (voteShare > 5.0f && (partyIndex <= OthersIndex || partyIdeologies[partyIndex] == 2)) {
			float effectivePreferenceFlow = calculateEffectivePreferenceFlow(partyIndex, voteShare, true);
			float adjustedPreferenceFlow = basicTransformedSwing(effectivePreferenceFlow, preferenceFlowDeviation);
			float randomisedPreferenceFlow = basicTransformedSwing(adjustedPreferenceFlow, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - randomisedExhaustRate);
		}
		else {
			float currentPreferences = overallPreferenceFlow[partyIndex];
			float adjustedPreferenceFlow = basicTransformedSwing(currentPreferences, preferenceFlowDeviation);
			float randomisedPreferenceFlow = basicTransformedSwing(adjustedPreferenceFlow, preferenceVariation[partyIndex]);
			currentPartyOnePrefs += voteShare * randomisedPreferenceFlow * 0.01f * (1.0f - randomisedExhaustRate);
		}
		currentNonMajorFpShare += voteShare;
		currentExhaustRateEstimate += randomisedExhaustRate * voteShare;
		currentExhaustDenominator += voteShare;
	}
	if (currentExhaustDenominator > 0.0f) {
		currentExhaustRateEstimate /= currentExhaustDenominator;
	}
	else {
		currentExhaustRateEstimate = 0.0f;
	}
	currentExhaustRateEstimate = std::clamp(currentExhaustRateEstimate + exhaustBiasRate, 0.0f, 1.0f);

	float currentNonMajorTppShare = currentNonMajorFpShare * (1.0f - currentExhaustRateEstimate);

	static std::once_flag producedExhaustRateWarning;
	if (currentExhaustRateEstimate &&
		std::isfinite(currentExhaustRateEstimate) &&
		overallExhaustRate[OthersIndex] < 0.01f) {
		std::call_once(producedExhaustRateWarning, [&]() {
			PA_LOG_VAR(overallExhaustRate);
			logger << "Warning: An exhaust rate was produced for an election "
				"that appears to use compulsory preferential voting. Seat: " +
				seat.name + ", observed exhaust rate: " +
				std::to_string(currentExhaustRateEstimate) + "\n";
		});
	}

	// Step 4: Adjust the current flow estimate according the bias the previous flow estimate had

	float biasAdjustedPartyOnePrefs = 0.0f;
	float overallAdjustedPartyOnePrefs = 0.0f;
	if (currentNonMajorTppShare > 0.0f) {
		currentPartyOnePrefs = std::clamp(
			currentPartyOnePrefs,
			0.01f * currentNonMajorTppShare,
			0.99f * currentNonMajorTppShare);
		biasAdjustedPartyOnePrefs = basicTransformedSwing(
			currentPartyOnePrefs,
			preferenceBiasRate * currentNonMajorTppShare);

		// If the overall preference flow needs a correction, keep it well
		// within the total non-major preferences available.
		overallAdjustedPartyOnePrefs = std::clamp(
			biasAdjustedPartyOnePrefs +
				prefCorrection * currentNonMajorTppShare,
			0.01f * currentNonMajorTppShare,
			0.99f * currentNonMajorTppShare);
	}
	float overallAdjustedPartyTwoPrefs = currentNonMajorTppShare - overallAdjustedPartyOnePrefs;

	// Step 5: Actually estimate the major party fps based on these adjusted flows

	// adjust everything that's still being used to be scaled so that the total non-exhausted votes is equal to 100%
	float majorFpShare = 100.0f - currentNonMajorFpShare;
	float nonExhaustedProportion = mix(1.0f, majorFpShare * 0.01f, currentExhaustRateEstimate * 1.0f);

	float partyTwoCurrentTpp = 100.0f - partyOneCurrentTpp;
	if (!std::isfinite(partyOneCurrentTpp) ||
		partyOneCurrentTpp <= 0.0f || partyOneCurrentTpp >= 100.0f) {
		throw InvalidIteration();
	}

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
		addedPartyOneFp = basicTransformedSwing(finalPartyOneFp, addPartyOneFp);
	}
	else {
		addedPartyTwoFp = basicTransformedSwing(finalPartyTwoFp, addPartyTwoFp);
	}

	finalPartyOneFp = std::clamp(addedPartyOneFp, 0.1f, 99.9f);
	finalPartyTwoFp = std::clamp(addedPartyTwoFp, 0.1f, 99.9f);

	if (run.natPartyIndex > 0) {
		// a final adjustment for the *change* in relative leakage among coalition parties
		// which will require a correction increasing the total coalition FP at the expense of Labor FP
		// in order to achieve the same TPP
		// (The absolute level of split is already covered in the preference calculations)
		float prevNatVote = run.pastSeatResults[seatIndex].fpVotePercent.contains(run.natPartyIndex) ? run.pastSeatResults[seatIndex].fpVotePercent.at(run.natPartyIndex) : 0.0f;
		float prevLibVote = run.pastSeatResults[seatIndex].fpVotePercent.contains(Mp::Two) ? run.pastSeatResults[seatIndex].fpVotePercent.at(Mp::Two) : 0.0f;
		float const previousCoalitionVote = prevNatVote + prevLibVote;
		float currentSplit = std::min(
			nationalsShare[seatIndex], 1.0f - nationalsShare[seatIndex]);
		// Without a previous Coalition FP split there is no baseline leakage
		// from which to calculate a change.
		if (previousCoalitionVote > 0.0f) {
			float prevSplit =
				std::min(prevNatVote, prevLibVote) / previousCoalitionVote;
			float splitChange = currentSplit - prevSplit;
			// Skip unnecessary calculations if there is no change, which is
			// common where only one Coalition party contests.
			if (splitChange) {
				float extraCoalitionVoteNeeded =
					splitChange * finalPartyTwoFp * 0.154f;
				// Make sure the adjustment does not overflow in either direction.
				float partyOneAdjustment = predictorCorrectorTransformedSwing(
					finalPartyOneFp, -extraCoalitionVoteNeeded) -
					finalPartyOneFp;
				float partyTwoAdjustment = predictorCorrectorTransformedSwing(
					finalPartyTwoFp, extraCoalitionVoteNeeded) -
					finalPartyTwoFp;
				float finalPartyOneAdjustment = partyOneAdjustment > 0 ?
					std::min(partyOneAdjustment, -partyTwoAdjustment) :
					std::max(partyOneAdjustment, -partyTwoAdjustment);
				finalPartyOneFp += finalPartyOneAdjustment;
				finalPartyTwoFp -= finalPartyOneAdjustment;
			}
		}
	}

	seatFpVoteShare[seatIndex][Mp::One] = finalPartyOneFp;
	seatFpVoteShare[seatIndex][Mp::Two] = finalPartyTwoFp;

	if (hasInvalidValues("amp1", true)) {
		PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
		PA_LOG_VAR(partyOneNewTppMargin[seatIndex]);
		PA_LOG_VAR(partyOneCurrentTpp);
		PA_LOG_VAR(partyTwoCurrentTpp);
		PA_LOG_VAR(oldFpVotes);
		PA_LOG_VAR(seatFpVoteShare[seatIndex]);
		PA_LOG_VAR(previousNonMajorFpShare);
		PA_LOG_VAR(previousPartyOnePrefEstimate);
		PA_LOG_VAR(previousExhaustRateEstimate);
		PA_LOG_VAR(previousExhaustDenominator);
		PA_LOG_VAR(preferenceBiasRate);
		PA_LOG_VAR(exhaustBiasRate);
		PA_LOG_VAR(majorTcp);
		PA_LOG_VAR(overallPreferenceFlow);
		PA_LOG_VAR(overallExhaustRate);
		PA_LOG_VAR(currentPartyOnePrefs);
		PA_LOG_VAR(currentNonMajorFpShare);
		PA_LOG_VAR(currentNonMajorTppShare);
		PA_LOG_VAR(biasAdjustedPartyOnePrefs);
		PA_LOG_VAR(preferenceVariation);
		PA_LOG_VAR(prefCorrection);
		PA_LOG_VAR(overallAdjustedPartyOnePrefs);
		PA_LOG_VAR(overallAdjustedPartyTwoPrefs);
		PA_LOG_VAR(partyOneScaledTpp);
		PA_LOG_VAR(partyTwoScaledTpp);
		PA_LOG_VAR(newPartyOneTpp);
		PA_LOG_VAR(newPartyTwoTpp);
		PA_LOG_VAR(totalTpp);
		PA_LOG_VAR(addPartyOneFp);
		PA_LOG_VAR(addPartyTwoFp);
		PA_LOG_VAR(addedPartyOneFp);
		PA_LOG_VAR(addedPartyTwoFp);
		PA_LOG_VAR(newPartyOneFp);
		PA_LOG_VAR(newPartyTwoFp);
		PA_LOG_VAR(majorFpShare);
		PA_LOG_VAR(nonExhaustedProportion);
		PA_LOG_VAR(currentExhaustRateEstimate);
		PA_LOG_VAR(currentExhaustDenominator);
		PA_LOG_VAR(exhaustBiasRate);
		PA_LOG_VAR(finalPartyOneFp);
		PA_LOG_VAR(finalPartyTwoFp);
		throw InvalidIteration();
	}

	if (incumbentPartyIndex >= Mp::Others &&
		seatFpVoteShare[seatIndex][incumbentPartyIndex]) {
		// Maintain constant fp vote for non-major incumbents
		normaliseSeatFp(
			seatIndex,
			incumbentPartyIndex,
			seatFpVoteShare[seatIndex][incumbentPartyIndex]);
	}
	else {
		normaliseSeatFp(seatIndex);
	}
	if (hasInvalidValues("After normalising major-party FPs")) {
		throw InvalidIteration();
	}
}

void SimulationIteration::normaliseSeatFp(int seatIndex, int fixedParty, float fixedVote)
{
	// By default, preserve the largest named non-major party. Its strong local
	// support is treated as real; major-party preference estimates must
	// accommodate it rather than normalisation automatically suppressing it.
	if (fixedVote == 0.0f) {
		for (auto const& [partyIndex, voteShare] :
			seatFpVoteShare[seatIndex]) {
			if (partyIndex < Mp::Others) continue;
			if (partyIndex == run.indPartyIndex) continue;
			if (voteShare > fixedVote) {
				fixedVote = voteShare;
				fixedParty = partyIndex;
			}
		}
	}

	float totalVoteShare = 0.0f;
	for (auto const& [partyIndex, voteShare] :
		seatFpVoteShare[seatIndex]) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (partyIndex == fixedParty) continue;
		totalVoteShare += voteShare;
	}
	float totalTarget = 100.0f - fixedVote;
	if (!std::isfinite(totalVoteShare) || totalVoteShare <= 0.0f ||
		!std::isfinite(totalTarget) || totalTarget < 0.0f) {
		throw InvalidIteration();
	}
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
	constexpr float AcceptableOverallFpError = 0.3f;

	for (int i = 0; i < MaxReconciliationCycles; ++i) {
		calculateNewFpVoteTotals();
		if (hasInvalidValues(
			"FP reconciliation totals, cycle " + std::to_string(i))) {
			throw InvalidIteration();
		}

		if (overallFpError < AcceptableOverallFpError) return;

		// The transformed minor-party corrections converge quickly but leave
		// some residual major-party bias after each seat is renormalised.
		if (i > 2) correctMajorPartyFpBias();
		if (hasInvalidValues(
			"major-party FP reconciliation, cycle " + std::to_string(i))) {
			throw InvalidIteration();
		}

		if (i < MaxReconciliationCycles - 1) {
			if (i > 1) calculatePreferenceCorrections();
			applyCorrectionsToSeatFps();
		}
	}

	// The final cycle can directly adjust the major parties, so refresh the
	// aggregate rather than leaving tempOverallFp and overallFpError stale.
	// A larger residual is permitted here: protected incumbents and locally
	// established candidates can make the national target impossible without
	// destroying the local support the seat model is intended to preserve.
	calculateNewFpVoteTotals();
}

void SimulationIteration::calculateNewFpVoteTotals()
{
	// Weight each seat by its previous-election turnout. This is particularly
	// important where electorate sizes differ substantially, such as Tasmania.
	std::map<int, double> partyVoteCount;
	double totalVoteCount = 0.0;
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		double const seatVoteCount =
			double(run.pastSeatResults[seatIndex].turnoutCount);
		for (auto const& [partyIndex, voteShare] :
			seatFpVoteShare[seatIndex]) {
			if (partyIndex == CoalitionPartnerIndex) continue;
			double const voteCount =
				double(voteShare) * seatVoteCount * 0.01;
			totalVoteCount += voteCount;
			partyVoteCount[partyIndex] += voteCount;
		}
	}
	if (!std::isfinite(totalVoteCount) || totalVoteCount <= 0.0) {
		throw InvalidIteration();
	}

	// Nationals votes are held within party two until assignNationalsVotes().
	// Reconcile against the combined Coalition target when a projection happens
	// to provide separate Liberal and National primary-vote series.
	FloatByPartyIndex reconciliationTargets = overallFpTarget;
	if (run.natPartyIndex >= 0 &&
		reconciliationTargets.contains(run.natPartyIndex)) {
		reconciliationTargets[Mp::Two] +=
			reconciliationTargets.at(run.natPartyIndex);
		reconciliationTargets.erase(run.natPartyIndex);
	}
	reconciliationTargets.try_emplace(OthersIndex, 0.0f);

	tempOverallFp.clear();
	for (auto const& [partyIndex, voteCount] : partyVoteCount) {
		float const fp =
			float(voteCount / totalVoteCount * 100.0);
		if (run.natPartyIndex >= 0 &&
			partyIndex == run.natPartyIndex) {
			tempOverallFp[Mp::Two] += fp;
		}
		else if (partyIndex != OthersIndex &&
			reconciliationTargets.contains(partyIndex)) {
			tempOverallFp[partyIndex] += fp;
		}
		else {
			tempOverallFp[OthersIndex] += fp;
		}
	}

	overallFpError = 0.0f;
	for (auto const& [partyIndex, target] : reconciliationTargets) {
		tempOverallFp.try_emplace(partyIndex, 0.0f);
		overallFpError +=
			std::abs(target - tempOverallFp.at(partyIndex));
	}

	float const tempMicroOthers =
		float(getAt(partyVoteCount, OthersIndex, 0.0) /
			totalVoteCount * 100.0);
	float const protectedOthers =
		tempOverallFp.at(OthersIndex) - tempMicroOthers;
	float const targetOthers =
		reconciliationTargets.at(OthersIndex);
	constexpr float MinimumAdjustableOthersShare = 0.0001f;
	if (tempMicroOthers > MinimumAdjustableOthersShare) {
		// Protected independents can already exceed the aggregate Others
		// target. In that case remove as much generic Others support as
		// possible, but never request a nonsensical negative scale.
		othersCorrectionFactor = std::max(
			0.0f,
			(targetOthers - protectedOthers) / tempMicroOthers);
	}
	else {
		// There is no generic Others reservoir through which to apply a
		// correction. Preserve the local candidates and accept the residual.
		othersCorrectionFactor = 1.0f;
	}
	if (!std::isfinite(overallFpError) ||
		!std::isfinite(othersCorrectionFactor)) {
		throw InvalidIteration();
	}
}

void SimulationIteration::calculatePreferenceCorrections()
{
	float estTppSeats = 0.0f;
	float totalPrefs = 0.0f;
	float totalNonExhaust = 0.0f;
	for (auto const& [partyIndex, fpShare] : tempOverallFp) {
		if (!overallExhaustRate.contains(partyIndex) ||
			!overallPreferenceFlow.contains(partyIndex)) {
			throw InvalidIteration();
		}
		float const voteSize =
			fpShare * (1.0f - overallExhaustRate.at(partyIndex));
		estTppSeats +=
			overallPreferenceFlow.at(partyIndex) * voteSize * 0.01f;
		totalNonExhaust += voteSize;
		if (!isMajor(partyIndex)) totalPrefs += voteSize;
	}
	if (totalNonExhaust <= 0.0f) throw InvalidIteration();
	if (totalPrefs <= 0.0f) return;

	estTppSeats /= (totalNonExhaust * 0.01f);
	float const prefError = estTppSeats - iterationOverallTpp;
	// Each cycle's seat FPs already include the previous preference correction,
	// so accumulate the remaining bias rather than replacing it.
	prefCorrection += prefError / totalPrefs;
	if (!std::isfinite(prefCorrection)) throw InvalidIteration();
}

void SimulationIteration::applyCorrectionsToSeatFps()
{
	for (auto const& [partyIndex, vote] : tempOverallFp) {
		if (partyIndex == CoalitionPartnerIndex) continue;
		if (partyIndex != OthersIndex) {
			if (isMajor(partyIndex)) continue;
			if (vote <= 0.0f ||
				!overallFpTarget.contains(partyIndex) ||
				overallFpTarget.at(partyIndex) <= 0.0f) {
				// There is nothing to scale when an emerging party is absent
				// from either the target or every simulated seat.
				continue;
			}
			float const correctionFactor =
				overallFpTarget.at(partyIndex) / vote;
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				int const incumbentPartyIndex =
					project.parties().idToIndex(seat.incumbent);
				if (partyIndex == incumbentPartyIndex) continue;
				if (seatFpVoteShare[seatIndex].contains(partyIndex)) {
					// Prevent an upward national correction from creating
					// implausibly large swings in an already-strong seat.
					float const swingCap = std::max(
						0.0f,
						vote * (correctionFactor - 1.0f) * 3.0f);
					float const correctionSwing = std::min(
						swingCap,
						seatFpVoteShare[seatIndex].at(partyIndex) *
							(correctionFactor - 1.0f));
					seatFpVoteShare[seatIndex][partyIndex] =
						predictorCorrectorTransformedSwing(
							seatFpVoteShare[seatIndex].at(partyIndex),
							correctionSwing);
				}
			}
		}
		else {
			for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
				Seat const& seat = project.seats().viewByIndex(seatIndex);
				int const incumbentPartyIndex =
					project.parties().idToIndex(seat.incumbent);
				float const allocation =
					getAt(seatFpVoteShare[seatIndex], OthersIndex, 0.0f) *
					(othersCorrectionFactor - 1.0f);

				FloatByPartyIndex categories;
				float totalOthers = 0.0f;
				for (auto const& [seatPartyIndex, seatPartyVote] :
					seatFpVoteShare[seatIndex]) {
					// Preserve independents, quasi-independents and incumbents:
					// their local support need not follow a national Others trend.
					if (seatPartyIndex == run.indPartyIndex ||
						seatPartyIndex == EmergingIndIndex) {
						continue;
					}
					if (!overallFpSwing.contains(seatPartyIndex) && seatPartyIndex >= 2) continue;
					if (seatPartyIndex == incumbentPartyIndex) continue;
					if (seatPartyIndex == OthersIndex || !overallFpTarget.contains(seatPartyIndex)) {
						categories[seatPartyIndex] = seatPartyVote;
						totalOthers += seatPartyVote;
					}
				}
				if (!totalOthers) continue;
				for (auto const& [seatPartyIndex, voteShare] : categories) {
					float const additionalVotes =
						allocation * voteShare / totalOthers;
					seatFpVoteShare[seatIndex][seatPartyIndex] =
						predictorCorrectorTransformedSwing(
							seatFpVoteShare[seatIndex].at(seatPartyIndex),
							additionalVotes);
				}
			}
		}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		// Leave at least 5% for the two major-party sides. This is a last-resort
		// bound for extreme correlated minor-party draws.
		float previousNonMajorFpShare = 0.0f;

		for (auto const& [partyIndex, voteShare] :
			seatFpVoteShare[seatIndex]) {
			if (isMajor(partyIndex, run.natPartyIndex) ||
				partyIndex == CoalitionPartnerIndex) {
				continue;
			}
			previousNonMajorFpShare += voteShare;
		}

		constexpr float MaxNonMajorShare = 95.0f;
		if (previousNonMajorFpShare > MaxNonMajorShare) {
			float const correctionFactor =
				MaxNonMajorShare / previousNonMajorFpShare;
			for (auto& [partyIndex, voteShare] :
				seatFpVoteShare[seatIndex]) {
				if (isMajor(partyIndex, run.natPartyIndex) ||
					partyIndex == CoalitionPartnerIndex) {
					continue;
				}
				voteShare *= correctionFactor;
			}
		}
	}

	if (hasInvalidValues(
		"after applying minor-party FP corrections", false, false)) {
		throw InvalidIteration();
	}
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		allocateMajorPartyFp(seatIndex);
	}
	if (hasInvalidValues("after reallocating major-party FPs")) {
		throw InvalidIteration();
	}
}

void SimulationIteration::correctMajorPartyFpBias()
{
	float const majorFpCurrent =
		getAt(tempOverallFp, Mp::One, 0.0f) +
		getAt(tempOverallFp, Mp::Two, 0.0f);
	float majorFpTarget =
		getAt(overallFpTarget, Mp::One, 0.0f) +
		getAt(overallFpTarget, Mp::Two, 0.0f);
	if (run.natPartyIndex >= 0) {
		majorFpTarget +=
			getAt(overallFpTarget, run.natPartyIndex, 0.0f);
	}
	if (majorFpCurrent <= 0.0f || majorFpCurrent >= 100.0f ||
		majorFpTarget <= 0.0f || majorFpTarget >= 100.0f) {
		throw InvalidIteration();
	}
	// Calculate the multiplier that reaches the target after the seat is
	// renormalised, while leaving the protected minor-party share in place.
	float const adjustmentFactor =
		(majorFpTarget * (majorFpCurrent - 100.0f)) /
		(majorFpCurrent * (majorFpTarget - 100.0f));
	float totalMinors = 0.0f;
	float partyOnePrefs = 0.0f;
	for (auto const& [partyIndex, vote] : tempOverallFp) {
		if (isMajor(partyIndex) || partyIndex == run.natPartyIndex) continue;
		if (!overallPreferenceFlow.contains(partyIndex)) {
			throw InvalidIteration();
		}
		partyOnePrefs +=
			overallPreferenceFlow.at(partyIndex) * vote * 0.01f;
		totalMinors += vote;
	}
	float const partyOneCurrent =
		getAt(tempOverallFp, Mp::One, 0.0f);
	float const partyTwoCurrent =
		getAt(tempOverallFp, Mp::Two, 0.0f);
	if (partyOneCurrent <= 0.0f || partyTwoCurrent <= 0.0f) {
		throw InvalidIteration();
	}
	float const partyOnePrefAdvantage =
		(partyOnePrefs * 2.0f - totalMinors) *
		totalMinors * 0.01f;
	float const partyOneTarget =
		partyOneCurrent * adjustmentFactor -
		partyOnePrefAdvantage * 0.005f;
	float const partyTwoTarget =
		partyTwoCurrent * adjustmentFactor +
		partyOnePrefAdvantage * 0.005f;
	float const partyOneAdjust = partyOneTarget / partyOneCurrent;
	float const partyTwoAdjust = partyTwoTarget / partyTwoCurrent;
	if (!std::isfinite(partyOneAdjust) ||
		!std::isfinite(partyTwoAdjust) ||
		partyOneAdjust <= 0.0f || partyTwoAdjust <= 0.0f) {
		throw InvalidIteration();
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		seatFpVoteShare[seatIndex][Mp::One] *= partyOneAdjust;
		seatFpVoteShare[seatIndex][Mp::Two] *= partyTwoAdjust;

		normaliseSeatFp(seatIndex);
	}
}

void SimulationIteration::incorporateLiveResults()
{
	if (!sim.isLive() || run.doingBettingOddsCalibrations || run.doingLiveBaselineSimulation) return;

	// Incorporate live TPPs first

	// First, adjust seat margins towards the election baseline as confidence increases
	// This is because we want to replace the uncertainty measured in the prior
	// with the uncertainty estimated from the live results
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto seatTppInformation = liveElection->getSeatTppInformation(project.seats().viewByIndex(seatIndex).name);
		float priorMargin = partyOneNewTppMargin[seatIndex];
		float baselineMargin = detransformVoteShare(seatTppInformation.baseline) - 50.0f;
		float effectiveConfidence = std::max(seatTppInformation.confidence, seatTppInformation.completion * seatTppInformation.completion);
		// sigmoid function, very ad hoc but smooths out the transition from prior to baseline+results
		float baselineWeight = 1.606f / (1.0f + std::exp(-(12.0f * effectiveConfidence - 0.5f))) - 0.6063f;
		// shouldn't overflow as both priorMargin and baselineMargin will be within acceptable bounds
		// if baselineWeight is outside (0, 1) there is a logic error somewhere
		float mixedMargin = mix(priorMargin, baselineMargin, baselineWeight);
		partyOneNewTppMargin[seatIndex] = mixedMargin;
	}

	// Get the baseline and deviation for the election
	auto electionTppInformation = liveElection->getFinalSpecificTppInformation();
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		// now incorporate deviation
		float oldTpp = partyOneNewTppMargin[seatIndex] + 50.0f;
		float transformedTpp = transformVoteShare(oldTpp);
		transformedTpp += electionTppInformation.deviation;
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
	}

	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		auto regionTppDeviation = liveElection->getRegionFinalSpecificTppDeviation(regionIndex);
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			int regionId = project.seats().viewByIndex(seatIndex).region;
			if (project.regions().idToIndex(regionId) != regionIndex) continue;
			float oldTpp = partyOneNewTppMargin[seatIndex] + 50.0f;
			float transformedTpp = transformVoteShare(oldTpp);
			transformedTpp += regionTppDeviation;
			partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
		}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		float oldTpp = partyOneNewTppMargin[seatIndex] + 50.0f;
		float seatTppDeviation = liveElection->getSeatTppInformation(seat.name).deviation;
		float transformedTpp = transformVoteShare(oldTpp);
		transformedTpp += seatTppDeviation;
		partyOneNewTppMargin[seatIndex] = detransformVoteShare(transformedTpp) - 50.0f;
	}

	// Now, incorporate live FPs

	// First, need to split the coalition votes so that swings can be assigned to them separately
	// For now this is simplified and we just take the proportions from the seat's live results or the prior if there are none.
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		assignNationalsVotes(seatIndex);
	}

	// As with TPPs, adjust seat margins towards the election baseline as confidence increases
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto seatFpInformation = liveElection->getSeatFpInformation(project.seats().viewByIndex(seatIndex).name);
		for (auto [partyIndex, information] : seatFpInformation) {
			//if (partyIndex < Mp::Others) continue;
			int effectivePartyIndex = partyIndex;
			if (partyIndex == run.indPartyIndex && !seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
				effectivePartyIndex = EmergingIndIndex;
			}

			float priorFpShare = transformVoteShare(std::clamp(seatFpVoteShare[seatIndex][effectivePartyIndex], 1.0f, 99.0f));
			float baselineFpShare = information.baseline;
			float effectiveConfidence = std::max(information.confidence, information.completion * information.completion);
			// sigmoid function, very ad hoc but smooths out the transition from prior to baseline+results
			float baselineWeight = 1.606f / (1.0f + std::exp(-(12.0f * effectiveConfidence - 0.5f))) - 0.6063f;
			float mixedFpShare = mix(priorFpShare, baselineFpShare, baselineWeight);
			seatFpVoteShare[seatIndex][effectivePartyIndex] = detransformVoteShare(mixedFpShare);
		}
	}

	auto electionFpDeviations = liveElection->getFinalSpecificFpDeviations();
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		for (auto [partyIndex, deviation] : electionFpDeviations) {
		// if (partyIndex < Mp::Others) continue;
			int effectivePartyIndex = partyIndex;
			if (partyIndex == run.indPartyIndex && !seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
				effectivePartyIndex = EmergingIndIndex;
			}
			if (!seatFpVoteShare[seatIndex].contains(effectivePartyIndex)) continue;
			float transformedFp = transformVoteShare(seatFpVoteShare[seatIndex][effectivePartyIndex]);
			transformedFp += deviation;
			seatFpVoteShare[seatIndex][effectivePartyIndex] = detransformVoteShare(transformedFp);
		}
	}

	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
	 	auto regionFpDeviations = liveElection->getRegionFinalSpecificFpDeviations(regionIndex);
	 	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			int regionId = project.seats().viewByIndex(seatIndex).region;
			if (project.regions().idToIndex(regionId) != regionIndex) continue;
	 		for (auto [partyIndex, deviation] : regionFpDeviations) {
	 		//if (partyIndex < Mp::Others) continue;
				int effectivePartyIndex = partyIndex;	
				if (partyIndex == run.indPartyIndex && !seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
					effectivePartyIndex = EmergingIndIndex;
				}
	 			if (!seatFpVoteShare[seatIndex].contains(effectivePartyIndex)) continue;
	 			float transformedFp = transformVoteShare(seatFpVoteShare[seatIndex][effectivePartyIndex]);
	 			transformedFp += deviation;
	 			seatFpVoteShare[seatIndex][effectivePartyIndex] = detransformVoteShare(transformedFp);
	 		}
	 	}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const seat = project.seats().viewByIndex(seatIndex);
		auto seatFpInformation = liveElection->getSeatFpInformation(seat.name);
		for (auto [partyIndex, information] : seatFpInformation) {
			// if (partyIndex < Mp::Others) continue;
			int effectivePartyIndex = partyIndex;
			if (partyIndex == run.indPartyIndex && !seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
				effectivePartyIndex = EmergingIndIndex;
			}
			if (!seatFpVoteShare[seatIndex].contains(effectivePartyIndex)) continue;
			float deviation = information.deviation;
			float originalFp = seatFpVoteShare[seatIndex][effectivePartyIndex];
			float transformedFp = transformVoteShare(originalFp);
			transformedFp += deviation;
			seatFpVoteShare[seatIndex][effectivePartyIndex] = detransformVoteShare(transformedFp);
		}
		// Rapidly remove "emerging IND" possibilities as votes come in
		// if there's also an IND in the seat
		// Either there is an IND, and that'll be counted under run.indPartyIndex
		// or there isn't, and it shouldn't appear in results
		// Leaving in the EmergingIndIndex results alongside run.indPartyIndex sometimes
		// causes it to have a spurious change of winning as the live results only override
		// the run.indPartyIndex
		// (This will ideally need more refinement to handle a second significant IND as in Calare 2025)
		if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex) && seatFpVoteShare[seatIndex].contains(run.indPartyIndex)) {
			if (seatFpInformation.contains(run.indPartyIndex)) {
				seatFpVoteShare[seatIndex][EmergingIndIndex] = std::min(
					seatFpVoteShare[seatIndex][EmergingIndIndex],
					100.0f / (10000.0f * seatFpInformation[run.indPartyIndex].completion)
				);
			}
			else {
				seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
			}
		}
	}

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const seat = project.seats().viewByIndex(seatIndex);
		// Need to recalculate major party votes so that the major parties have the proper fp vote shares
		// auto preferenceDeviationInfo = liveElection->getSeatLivePreferenceFlowDeviation(seat.name);
		// float preferenceDeviationToUse = preferenceDeviationInfo.value * std::sqrt(preferenceDeviationInfo.confidence);
		// allocateMajorPartyFp(seatIndex, preferenceDeviationToUse);

		// Return Nationals to the race as for now it works better for the preference flow calculations
		// for them to be included as a single unit in the coalition
		// (maybe reverse this later)
		if (seatFpVoteShare[seatIndex].contains(run.natPartyIndex)) {
			seatFpVoteShare[seatIndex][Mp::Two] += seatFpVoteShare[seatIndex][run.natPartyIndex];
			seatFpVoteShare[seatIndex].erase(run.natPartyIndex);
		}
		if (seatFpVoteShare[seatIndex].contains(EmergingIndIndex)) {
			if (seatFpVoteShare[seatIndex][EmergingIndIndex] < detransformVoteShare(run.indEmergence.fpThreshold)) {
				seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingIndIndex];
				seatFpVoteShare[seatIndex][EmergingIndIndex] = 0.0f;
			}
		}
		if (seatFpVoteShare[seatIndex].contains(EmergingPartyIndex)) {
			auto seatFpInformation = liveElection->getSeatFpInformation(seat.name);
			float fpConfidence = seatFpInformation.size() > 0 ?
				std::max(seatFpInformation.begin()->second.confidence, seatFpInformation.begin()->second.completion * seatFpInformation.begin()->second.completion) :
				0.0f;
			float fpWeight = 1.6063f - 1.606f / (1.0f + std::exp(-(12.0f * fpConfidence - 0.5f)));
			seatFpVoteShare[seatIndex][EmergingPartyIndex] *= fpWeight;
			if (seatFpVoteShare[seatIndex][EmergingPartyIndex] < detransformVoteShare(run.indEmergence.fpThreshold)) {
				seatFpVoteShare[seatIndex][OthersIndex] += seatFpVoteShare[seatIndex][EmergingPartyIndex];
				seatFpVoteShare[seatIndex][EmergingPartyIndex] = 0.0f;
			}
		}
		// Unlike with specific party votes, the system of interlocking swings is not
		// helpful for others votes, so we just mix the prior with the observed value
		// (as total others votes are highly seat specific)
		auto seatOthersInformation = liveElection->getSeatOthersInformation(seat.name, seatFpVoteShare[seatIndex]);
		if (seatOthersInformation.value > 0.0f) {
			float priorOthersShare = seatFpVoteShare[seatIndex][OthersIndex];
			float observedOthersShare = seatOthersInformation.value;
			float effectiveConfidence = std::max(seatOthersInformation.confidence, seatOthersInformation.completion * seatOthersInformation.completion);
			float observedWeight = 1.606f / (1.0f + std::exp(-(12.0f * effectiveConfidence - 0.5f))) - 0.6063f;
			float mixedOthersShare = mix(priorOthersShare, observedOthersShare, observedWeight);
			seatFpVoteShare[seatIndex][OthersIndex] = mixedOthersShare;
		}
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

	auto softenExtremePreferenceFlow = [](float flow) {
		flow = std::clamp(flow, 1.0f, 99.0f);
		if (flow > 85.0f) {
			return basicTransformedSwing(85.0f, flow - 85.0f);
		}
		if (flow < 15.0f) {
			return 100.0f - basicTransformedSwing(85.0f, 15.0f - flow);
		}
		return flow;
	};

	// Function for allocating votes from excluded parties. Used in several places in this loop only,
	// so create once and use wherever needed
	auto allocateVotes = [&](std::vector<PartyVotes>& accumulatedVoteShares, std::vector<PartyVotes> const& excludedVoteShares) {
		for (auto [sourceParty, sourceVoteShare] : excludedVoteShares) {
			// This is a fallback estimate for parties without a specified within-party exhaust rate
			// Typically non-classic exhaust rates are a bit higher than classic ones
			float survivalRate = overallExhaustRate[sourceParty] ? (1.0f - overallExhaustRate[sourceParty]) * 0.85f : 1.0f;
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

						// higher flow for ON in regional areas, as suggested by results from 2017/2020 in Qld
						if (targetParties.first >= 0 && targetParties.second >= 0) {
							float regionalBoost = 5.0f - (10.0f * run.regionalProportion);
							if (run.seatTypes.at(seatIndex) == SimulationRun::SeatType::Provincial || run.seatTypes.at(seatIndex) == SimulationRun::SeatType::Rural) {
								if (sourceParty == 1 && project.parties().viewByIndex(targetParties.first).abbreviation == "ON" && targetParties.second == 0) flow += 5.0f + regionalBoost;
								if (sourceParty == 1 && project.parties().viewByIndex(targetParties.second).abbreviation == "ON" && targetParties.first == 0) flow -= 5.0f + regionalBoost;
							}
							else {
								if (sourceParty == 1 && project.parties().viewByIndex(targetParties.first).abbreviation == "ON" && targetParties.second == 0) flow -= 5.0f + regionalBoost;
								if (sourceParty == 1 && project.parties().viewByIndex(targetParties.second).abbreviation == "ON" && targetParties.first == 0) flow += 5.0f + regionalBoost;
							}
						}

						float transformedFlow = transformVoteShare(flow);
						// Higher variation in preference flow under OPV
						float randomFactorFlow = variabilityNormal(
							0.0f, 12.0f + 10.0f * (1.0f - survivalRate), seatIndex,
							RandomGenerator::combinePartyIds(sourceParty, RandomGenerator::combinePartyIds(targetParties.first, targetParties.second)),
							uint32_t(VariabilityTag::PrefFlowKnown)
						);
						transformedFlow += randomFactorFlow;
						flow = detransformVoteShare(transformedFlow);
						flow = softenExtremePreferenceFlow(flow);
						if (survivalRate && survivalRate < 1.0f) {
							float transformedSurvival = transformVoteShare(survivalRate * 100.0f);
							float randomFactorSurvival = variabilityNormal(
								0.0f, 15.0f, seatIndex,
								RandomGenerator::combinePartyIds(sourceParty, RandomGenerator::combinePartyIds(targetParties.first, targetParties.second)),
								uint32_t(VariabilityTag::ExhaustKnown)
							);
							transformedSurvival += randomFactorSurvival;
							survivalRate = detransformVoteShare(transformedSurvival) * 0.01f;
						}
						if (seat.name == "Kiama" && run.getTermCode() == "2023nsw" && sourceParty == 0) {
							flow = 50.0f;
							survivalRate = 0.2f;
						}
						if ((seat.name == "Narungga"|| seat.name == "MacKillop") && run.getTermCode() == "2026sa" && sourceParty == 0) {
							flow = 50.0f;
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
				float randomFactor = variabilityUniform(
					0.5f, 1.5f, seatIndex,
					RandomGenerator::combinePartyIds(sourceParty, targetParty),
					uint32_t(VariabilityTag::PrefFlowUnknown)
				);
				thisWeight *= randomFactor;
				thisWeight *= std::sqrt(targetVoteShare);
				weights[targetIndex] = thisWeight;
			}

			//if (alpIndex >= 0 && lnpIndex >= 0 && bothMajorParties(sourceParty, accumulatedVoteShares[alpIndex].first) == false &&
			//	bothMajorParties(sourceParty, accumulatedVoteShares[lnpIndex].first) == false) {
			//	// boost weight to major party when both are present and source is not major
			//	float combinedWeights = weights[alpIndex] + weights[lnpIndex];
			//	weights[alpIndex] = combinedWeights * 0.7f;
			//	weights[lnpIndex] = combinedWeights * 0.3f;
   //   }

			// Rather hacky way to handle GRN -> ALP/IND flows in cases where another candidate (usually LNP)
			// is still in the running. Depends on ALP being party index 0,
			// which is the case by my convention but won't apply in an old election without Greens or
			// if someone else makes their own file. Replace with a proper system when convenient.
			if (alpIndex >= 0 && indIndex >= 0 && sourceParty == run.grnPartyIndex &&
				(seat.tppMargin < -5.0f || !isMajor(seat.incumbent))) {
				float combinedWeights = weights[alpIndex] + weights[indIndex];
				float indShare = variabilityUniform(0.5f, 0.9f, seatIndex, 0, uint32_t(VariabilityTag::GrnAlpIndSplit));
				weights[indIndex] = combinedWeights * indShare;
				weights[alpIndex] = combinedWeights * (1.0f - indShare);
			}

			// Same deal with OTH -> ALP/GRN
			if (alpIndex >= 0 && grnIndex >= 0 && sourceParty == -1) {
				float combinedWeights = weights[alpIndex] + weights[grnIndex];
				float grnShare = variabilityUniform(0.55f, 0.75f, seatIndex, 0, uint32_t(VariabilityTag::OthAlpGrnSplit));
				weights[grnIndex] = combinedWeights * grnShare;
				weights[alpIndex] = combinedWeights * (1.0f - grnShare);
			}

			// and OTH -> GRN/LIB
			if (lnpIndex >= 0 && grnIndex >= 0 && sourceParty == -1) {
				float combinedWeights = weights[lnpIndex] + weights[grnIndex];
        float grnShare = variabilityUniform(0.35f, 0.65f, seatIndex, 0, uint32_t(VariabilityTag::OthGrnLibSplit));
				weights[grnIndex] = combinedWeights * grnShare;
				weights[lnpIndex] = combinedWeights * (1.0f - grnShare);
			}

			if ((seat.name == "Narungga" || seat.name == "MacKillop") && run.getTermCode() == "2026sa" && sourceParty == 0 && indIndex >= 0) {
				// Prevent ALP votes from heavily favouring these specific INDs and pushing them into TCP
				weights[indIndex] *= 0.2f;
			}

			float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0000001f); // avoid divide by zero warning
			if ((sourceParty == CoalitionPartnerIndex || sourceParty == 1) && lnpIndex != -1) {
				float totalWeightWithoutLnp = totalWeight - weights[lnpIndex];
				weights[lnpIndex] = totalWeightWithoutLnp * 4.0f;
				totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0000001f); // avoid divide by zero warning
			}
			if (accumulatedVoteShares.size() == 2) {
				float flow = 100.0f * weights[0] / totalWeight;
				flow = softenExtremePreferenceFlow(flow);
				weights[0] = flow;
				weights[1] = 100.0f - flow;
				totalWeight = 100.0f;
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
		auto tcpInfo = liveElection->getSeatTcpInformation(project.seats().viewByIndex(seatIndex).name);
		if (tcpInfo.shares.contains(topTwo.first.first) && tcpInfo.shares.contains(topTwo.second.first)) {
      auto oldTopTwo = topTwo;
			float priorShare = transformVoteShare(topTwo.first.second);
			float liveShare = tcpInfo.shares.at(topTwo.first.first);
			// TODO: Tune this
			// "tcpInfo.confidence" = how much confidence there is in the vote projection in uncounted areas
			// (~zero if there is no previous result to compare to)
			// "tcpInfo.completion" = how much is counted (from 0-1)
			// Use results cautiously until a lot of the vote is in by squaring the completion
			// But override this if there can be more confidence in the results as a result of swing matching
			float effectiveConfidence = std::max(tcpInfo.confidence, tcpInfo.completion * tcpInfo.completion);
			// Strongly favour use of live TCP results once there's a decent amount in
			// sigmoid function, very ad hoc but smooths out the transition from prior to baseline+results
			float baselineWeight = std::clamp(1.6065f / (1.0f + std::exp(-(14.0f * effectiveConfidence - 0.5f))) - 0.60651f, 0.0f, 1.0f);
			float mixedShare = mix(priorShare, liveShare, baselineWeight);
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
	auto const& seat = project.seats().viewByIndex(seatIndex);
	if (run.isLiveAutomatic()) {
		auto nationalsProportion = liveElection->getSeatNationalsProportion(seat.name);
		if (nationalsProportion && nationalsShare[seatIndex] > 0.0f && nationalsShare[seatIndex] < 1.0f) {
			const float liveEstimate = nationalsProportion.value().value;
			if (liveEstimate > 0.0f && liveEstimate < 1.0f) {
				const float effectiveConfidence = std::max(nationalsProportion.value().confidence, nationalsProportion.value().completion * nationalsProportion.value().completion);
				const float stdDev = std::min(10.0f, 1.0f / (effectiveConfidence + 0.02f) - 0.97f);
				const float transformedLive = transformVoteShare(liveEstimate * 100.0f);
				const float transformedPrior = transformVoteShare(nationalsShare[seatIndex] * 100.0f);
				const float liveWithVariability = transformedLive + variabilityNormal(
					0.0f, stdDev, seatIndex, 0, uint32_t(VariabilityTag::NationalsLiveVariability)
				);
				const float liveWeight = std::clamp(1.6065f / (1.0f + std::exp(-(14.0f * effectiveConfidence - 0.5f))) - 0.60651f, 0.0f, 1.0f);
				const float mixedShare = mix(transformedPrior, liveWithVariability, liveWeight);
				nationalsShare[seatIndex] = detransformVoteShare(mixedShare) * 0.01f;
			}
		}
	}
	float nationalsVote = seatFpVoteShare[seatIndex][1] * nationalsShare[seatIndex];
	seatFpVoteShare[seatIndex][run.natPartyIndex] = nationalsVote;
	seatFpVoteShare[seatIndex][1] -= nationalsVote;
}

void SimulationIteration::applyLiveManualOverrides(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	if (seat.livePartyOne != Party::InvalidId) {
		float prob = variabilityUniform(0.0f, 1.0f, seatIndex, 0, uint32_t(VariabilityTag::LiveManualOverrides));
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
		for (auto& el : sortedPartyWins) {
			if (el.first == Mp::One) el.second = mpWins[Mp::One];
			if (el.first == Mp::Two) el.second = mpWins[Mp::Two];
		}
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
		if (partyIndex > 1 && partyIndex != run.natPartyIndex) othersWins += partyWins[partyIndex];
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
	if (run.natPartyIndex >= 0) ++sim.latestReport.coalitionSeatWinFrequency[coalitionWins];
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
		int bucket = std::clamp(int(std::floor(fpPercent * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
		++run.seatPartyFpDistribution[seatIndex][partyIndex][bucket];
		if (!fpPercent) ++run.seatPartyFpZeros[seatIndex][partyIndex];
	}
}

void SimulationIteration::recordSeatTcpVotes(int seatIndex)
{

	auto parties = seatTcpVoteShare[seatIndex].first;
	float tcpPercent = seatTcpVoteShare[seatIndex].second;
	int bucket = std::clamp(int(std::floor(tcpPercent * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
	++run.seatTcpDistribution[seatIndex][parties][bucket];
}

void SimulationIteration::recordSeatTppVotes(int seatIndex)
{
	float tppPercent = partyOneNewTppMargin[seatIndex] + 50.0f;
	int bucket = std::clamp(int(std::floor(tppPercent * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
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
		int bucket = std::clamp(int(std::floor(meanVoteShare * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
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
	int bucket = std::clamp(int(std::floor(meanTpp * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
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
		int bucket = std::clamp(int(std::floor(meanVoteShare * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
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
	int bucket = std::clamp(int(std::floor(meanTpp * 0.01f * float(SimulationRun::BucketCount))), 0, SimulationRun::BucketCount - 1);
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
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	float populism = centristPopulistFactor.at(partyIndex);
	float seatModifier = mix(run.seatCentristModifiers[seatIndex], run.seatPopulistModifiers[seatIndex], populism);
	int regionIndex = project.regions().idToIndex(seat.region);
	float homeStateCoefficient = mix(run.centristStatistics.homeStateCoefficient, run.populistStatistics.homeStateCoefficient, populism);
	if (
		homeRegion.contains(partyIndex)
		&& homeRegion.at(partyIndex) == regionIndex
		&& !run.regionFpSwingDeviations.contains(partyIndex) /* don't do a home state modified if there are actual polled deviations */
	) seatModifier += homeStateCoefficient;
	float highVoteModifier = std::clamp(std::pow(2.0f, (15.0f - overallFpTarget.at(partyIndex)) * 0.05f), 0.2f, 1.0f);
	seatModifier = (seatModifier - 1.0f) * highVoteModifier + 1.0f;
	return seatModifier;
}

float SimulationIteration::variabilityNormal(float mean, float sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const {
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	if (retryCount) key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(retryCount));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	return RandomGenerator::normal_from_key(key, mean, sd);
}

float SimulationIteration::variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const {
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	if (retryCount) key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(retryCount));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	return RandomGenerator::uniform01_from_key(key) * (high - low) + low;
}

int SimulationIteration::variabilityUniformInt(int low, int high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const
{
	int span = high - low;
	if (span <= 0) return low;
	return low + std::min(
		int(variabilityUniform(0.0f, 1.0f, itemIndex, partyId, tag) * span),
		span - 1);
}

float SimulationIteration::variabilityGamma(float alpha, float beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const
{
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	if (retryCount) key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(retryCount));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	std::mt19937_64 engine(RandomGenerator::splitmix64(key));
	std::gamma_distribution<float> dist(alpha, beta);
	return dist(engine);
}

float SimulationIteration::variabilityBeta(float alpha, float beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const
{
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	if (retryCount) key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(retryCount));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	std::mt19937_64 engine(RandomGenerator::splitmix64(key));
	boost::random::beta_distribution<float> dist(alpha, beta);
	return dist(engine);
}

int SimulationIteration::randomSampleIndex() const
{
	if (!retryCount) return iterationIndex;

	std::uint64_t key = RandomGenerator::mixKey(
		static_cast<std::uint64_t>(iterationIndex),
		static_cast<std::uint64_t>(retryCount));
	return static_cast<int>(key & 0x7fffffff);
}
