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
	determineIterationRegionalSwings();

	loadPastSeatResults();
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		determineSeatInitialResult(seatIndex);
	}

	// TODO: Ensure primary vote totals match original sample proportions
	calculateNewFpVoteTotals();

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
	// temporary for storing number of seat wins by each party in each region, 1st index = parties, 2nd index = regions
	regionSeatCount = std::vector<std::vector<int>>(project.parties().count(), std::vector<int>(project.regions().count()));

	partyWins = std::vector<int>(project.parties().count());
	incumbentNewMargin = std::vector<float>(project.seats().count(), 0.0f);
	seatWinner = std::vector<Party::Id>(project.seats().count(), Party::InvalidId);
}

void SimulationIteration::determineIterationOverallSwing()
{
	// First, randomly determine the national swing for this particular simulation
	auto projectedSample = project.projections().view(sim.settings.baseProjection).generateSupportSample(project.models());
	iterationOverallTpp = projectedSample.at(TppCode);
	iterationOverallSwing = iterationOverallTpp - sim.settings.prevElection2pp;

	for (auto const& [sampleKey, partySample] : projectedSample) {
		if (sampleKey == UnnamedOthersCode) {
			overallFp[OthersIndex] = partySample;
			continue;
		}
		for (auto const& [id, party] : project.parties()) {
			if (contains(party.officialCodes, sampleKey)) {
				int partyIndex = project.parties().idToIndex(id);
				overallFp[partyIndex] = partySample;
				break;
			}
		}
	}

	for (auto const& [partyIndex, partyFp] : overallFp) {
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

void SimulationIteration::determineSeatInitialResult(int seatIndex)
{
	determineSeatTpp(seatIndex);
	determineSeatInitialFp(seatIndex);
}

void SimulationIteration::determineSeatTpp(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	bool incIsOne = seat.incumbent != 1 || seat.challenger != 0; // stores whether the incumbent is Party One
										 // Add or subtract the simulation regional deviation depending on which party is incumbent
	float newMargin = seat.margin + regionSwing[project.regions().idToIndex(seat.region)] * (incIsOne ? 1.0f : -1.0f);
	// Add modifiers for known local effects (these are measured as positive if favouring the incumbent)
	newMargin += run.seatIncTppModifier[seatIndex];
	// Remove the average local modifier across the region
	newMargin -= run.regionLocalModifierAverage[seat.region] * (incIsOne ? 1.0f : -1.0f);
	// Add random noise to the new margin of this seat
	newMargin += std::normal_distribution<float>(0.0f, seatStdDev)(gen);

	// Margin for this simulation is finalised, record it for later averaging
	incumbentNewMargin[seatIndex] = newMargin;
}

void SimulationIteration::determineSeatInitialFp(int seatIndex)
{
	seatFpVoteShare.resize(project.seats().count());
	auto tempPastResults = pastSeatResults[seatIndex].fpVotePercent;
	for (auto [partyIndex, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		bool effectiveGreen = partyIndex >= 0 && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("GRN"));
		if (!overallFpSwing.contains(partyIndex)) effectiveGreen = false;
		bool effectiveIndependent = partyIndex >= 0 && contains(project.parties().viewByIndex(partyIndex).officialCodes, std::string("IND"));
		if (!overallFpSwing.contains(partyIndex)) effectiveIndependent = true;
		if (voteShare < run.indEmergence.fpThreshold) effectiveIndependent = false;
		bool effectivePopulist = partyIndex >= 2 && !effectiveGreen && !effectiveIndependent &&
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
		else if (partyIndex >= 2) {
			// For non-major candidates that don't fit into the above categories,
			// convert their past votes into "Others" votes
			pastSeatResults[seatIndex].fpVotePercent[OthersIndex] += pastSeatResults[seatIndex].fpVotePercent[partyIndex];
			pastSeatResults[seatIndex].fpVotePercent.erase(partyIndex);
			continue;
		}
		seatFpVoteShare[seatIndex][partyIndex] = voteShare;
	}
	determineSeatEmergingInds(seatIndex);
	allocateMajorPartyFp(seatIndex);
}

void SimulationIteration::determineSpecificPartyFp(int seatIndex, int partyIndex, float& voteShare, SimulationRun::SeatStatistics const seatStatistics) {
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	float transformedFp = transformVoteShare(voteShare);
	auto const& stats = run.greensSeatStatistics;
	float seatStatisticsExact = (std::clamp(transformedFp, stats.scaleLow, stats.scaleHigh) - stats.scaleLow) / stats.scaleStep;
	int seatStatisticsLower = int(std::floor(seatStatisticsExact));
	float seatStatisticsMix = seatStatisticsExact - float(seatStatisticsLower);
	using StatType = SimulationRun::SeatStatistics::TrendType;
	// ternary operator in second argument prevents accessing beyong the end of the array when at the upper end of the scale
	auto getMixedStat = [&](StatType statType) {
		return mix(stats.trend[int(statType)][seatStatisticsLower],
			(seatStatisticsMix ? stats.trend[int(statType)][seatStatisticsLower + 1] : 0.0f),
			seatStatisticsMix);
	};
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
	transformedFp += rng.flexibleDist(0.0f, lowerRmseMixed, upperRmseMixed, lowerKurtosisMixed, upperKurtosisMixed);
	voteShare = detransformVoteShare(transformedFp);
}

void SimulationIteration::determinePopulistFp(int seatIndex, int partyIndex, float& voteShare)
{
	float partyFp = overallFp[partyIndex];
	float seatModifier = run.seatPopulistModifiers[seatIndex];
	if (seatModifier == 0.0f) seatModifier = 1.0f;
	// Choosing the lower of these two values prevents the fp from being >= 100.0f in some scenarios
	float modifiedFp1 = predictorCorrectorTransformedSwing(partyFp, partyFp * (seatModifier - 1.0f));
	float modifiedFp2 = partyFp * seatModifier;
	float modifiedFp = std::min(modifiedFp1, modifiedFp2);
	float transformedFp = transformVoteShare(modifiedFp);
	transformedFp += rng.flexibleDist(0.0f, run.populistStatistics.lowerRmse, run.populistStatistics.upperRmse,
		run.populistStatistics.lowerKurtosis, run.populistStatistics.upperKurtosis);
	voteShare = detransformVoteShare(transformedFp);
}

void SimulationIteration::determineSeatEmergingInds(int seatIndex)
{
	// Emergence of new independents
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
	if (rng.uniform<float>() < std::max(0.01f, indEmergenceRate)) {
		float rmse = run.indEmergence.voteRmse;
		float kurtosis = run.indEmergence.voteKurtosis;
		float interceptSize = run.indEmergence.voteIntercept - transformVoteShare(run.indEmergence.fpThreshold);
		if (isFederal) rmse *= (1.0f + run.indEmergence.fedVoteCoeff / interceptSize);
		if (isRural) rmse *= (1.0f + run.indEmergence.ruralVoteCoeff / interceptSize);
		if (isProvincial) rmse *= (1.0f + run.indEmergence.provincialVoteCoeff / interceptSize);
		if (isOuterMetro) rmse *= (1.0f + run.indEmergence.outerMetroVoteCoeff / interceptSize);
		float prevOthersCoeff = run.indEmergence.prevOthersVoteCoeff * prevOthers;
		rmse *= (1.0f + prevOthersCoeff / interceptSize);
		float voteShare = abs(rng.flexibleDist(0.0f, rmse, rmse, kurtosis, kurtosis)) + run.indEmergence.fpThreshold;
		seatFpVoteShare[seatIndex][EmergingIndIndex] = voteShare;
	}
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
	if (seat.incumbent == 1 && seat.challenger == 0) partyOneCurrentTpp = 50.0f - incumbentNewMargin[seatIndex];
	else if (seat.incumbent == 0 && seat.challenger == 1) partyOneCurrentTpp = 50.0f + incumbentNewMargin[seatIndex];
	else partyOneCurrentTpp = incumbentNewMargin[seatIndex] + 50.0f;
	float partyTwoCurrentTpp = 100.0f - partyOneCurrentTpp;

	constexpr float DefaultPartyTwoPrimary = 15.0f;
	float preferenceBias = 0.0f;
	float previousNonMajorFpShare = 0.0f;
	float previousPartyOneTpp = 0.0f;
	float previousPartyOneFp = pastSeatResults[seatIndex].fpVotePercent[0];
	// ternary operator for situtations where party two didn't previously run, e.g. Richmond (vic) 2018
	float previousPartyTwoFp = (pastSeatResults[seatIndex].fpVotePercent.contains(1) ?
		pastSeatResults[seatIndex].fpVotePercent[1] : DefaultPartyTwoPrimary);
	if (pastSeatResults[seatIndex].tcpVote.contains(0) && pastSeatResults[seatIndex].tcpVote.contains(1)) {
		previousPartyOneTpp = pastSeatResults[seatIndex].tcpVote[0];
	}
	else if (seat.isClassic2pp()) {
		if (seat.incumbent == 0) {
			previousPartyOneTpp = 50.0f + seat.margin;
		}
		else {
			previousPartyOneTpp = 50.0f - seat.margin;
		}
	}
	else {
		previousPartyOneTpp = 50.0f + seat.margin;
	}

	float pastElectionPartyOnePrefEstimate = 0.0f;
	for (auto [partyIndex, voteShare] : pastSeatResults[seatIndex].fpVotePercent) {
		if (partyIndex >= 2) {
			pastElectionPartyOnePrefEstimate += voteShare * project.parties().viewByIndex(partyIndex).preferenceShare * 0.01f;
			previousNonMajorFpShare += voteShare;
		}
		else if (partyIndex <= OthersIndex) {
			pastElectionPartyOnePrefEstimate += voteShare * project.parties().getOthersPreferenceFlow() * 0.01f;
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
		if (partyIndex >= 2) {
			currentPartyOnePrefs += voteShare * project.parties().viewByIndex(partyIndex).preferenceShare * 0.01f;
			currentTotalPrefs += voteShare;
		}
		else if (partyIndex <= OthersIndex) {
			currentPartyOnePrefs += voteShare * project.parties().getOthersPreferenceFlow() * 0.01f;
			currentTotalPrefs += voteShare;
		}
	}
	float biasAdjustedPartyOnePrefs = currentPartyOnePrefs + preferenceBiasRate * currentTotalPrefs;
	float biasAdjustedPartyTwoPrefs = currentTotalPrefs - biasAdjustedPartyOnePrefs;

	// this number can be below zero ...
	float partyOneEstimate = partyOneCurrentTpp - biasAdjustedPartyOnePrefs;
	// so we calculate a swing ...
	float partyOneSwing = partyOneEstimate - previousPartyOneFp;
	// and then use logistic transformation to make sure it is above zero
	float newPartyOneFp = basicTransformedSwing(previousPartyOneFp, partyOneSwing);
	float newPartyOneTpp = biasAdjustedPartyOnePrefs + newPartyOneFp;
	// this number can be below zero ...
	float partyTwoEstimate = partyTwoCurrentTpp - biasAdjustedPartyTwoPrefs;
	// so we calculate a swing ...
	float partyTwoSwing = partyTwoEstimate - previousPartyTwoFp;
	// and then use logistic transformation to make sure it is above zero
	float newPartyTwoFp = basicTransformedSwing(previousPartyTwoFp, partyTwoSwing);
	float newPartyTwoTpp = biasAdjustedPartyTwoPrefs + newPartyTwoFp;
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

	//if (seat.name == "Melbourne") {
	//	PA_LOG_VAR(project.seats().viewByIndex(seatIndex).name);
	//	PA_LOG_VAR(partyOneCurrentTpp);
	//	PA_LOG_VAR(partyTwoCurrentTpp);
	//	PA_LOG_VAR(previousNonMajorFpShare);
	//	PA_LOG_VAR(previousPartyOneTpp);
	//	PA_LOG_VAR(pastElectionPartyOnePrefEstimate);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(preferenceBiasRate);
	//	PA_LOG_VAR(currentPartyOnePrefs);
	//	PA_LOG_VAR(currentTotalPrefs);
	//	PA_LOG_VAR(biasAdjustedPartyOnePrefs);
	//	PA_LOG_VAR(biasAdjustedPartyTwoPrefs);
	//	PA_LOG_VAR(partyOneEstimate);
	//	PA_LOG_VAR(partyTwoEstimate);
	//	PA_LOG_VAR(partyOneSwing);
	//	PA_LOG_VAR(partyTwoSwing);
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
	seatFpVoteShare[seatIndex][0] = newPartyOneFp;
	seatFpVoteShare[seatIndex][1] = newPartyTwoFp;
	normaliseSeatFp(seatIndex);
}

void SimulationIteration::normaliseSeatFp(int seatIndex)
{
	float totalVoteShare = 0.0f;
	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		totalVoteShare += voteShare;
	}
	float correctionFactor = 100.0f / totalVoteShare;
	for (auto& [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
		seatFpVoteShare[seatIndex][partyIndex] *= correctionFactor;
	}
}

void SimulationIteration::calculateNewFpVoteTotals()
{
	// Vote shares in each seat are converted to equivalent previous-election vote totals to ensure that
	// they reflect the turnout differences between seats (esp. Tasmanian seats)
	//std::map<int, int> partyVoteCount;
	//int totalVoteCount = 0;
	//for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
	//	int seatVoteCount = 0;
	//	for (auto [partyIndex, voteCount] : pastSeatResults[seatIndex].fpVoteCount) {
	//		seatVoteCount += voteCount;
	//	}
	//	for (auto [partyIndex, voteShare] : seatFpVoteShare[seatIndex]) {
	//		float voteCount = voteShare * float(seatVoteCount) * 0.01f;
	//		totalVoteCount += voteCount;
	//		partyVoteCount[partyIndex] += voteCount;
	//	}
	//}
	//for (auto [partyIndex, voteCount] : partyVoteCount) {
	//	overallFp[partyIndex] = float(voteCount) / float(totalVoteCount) * 100.0f;
	//}
}

void SimulationIteration::adjustClassicSeatResultFor3rdPlaceIndependent(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	// Sometimes a classic 2pp seat may also have a independent with a significant chance,
	// but not high enough to make the top two - if so this will give a certain chance to
	// override the swing-based result with a win from the challenger
	if ((!sim.isLiveAutomatic() || !seat.hasLiveResults()) && seat.challenger2Odds < 8.0f && !seat.overrideBettingOdds) {
		OddsInfo oddsInfo = calculateOddsInfo(seat);
		float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
		if (uniformRand >= oddsInfo.topTwoChance) seatWinner[seatIndex] = seat.challenger2;
	}
}

void SimulationIteration::adjustClassicSeatResultForBettingOdds(int seatIndex, SeatResult result)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	// If a seat is marked as classic by the AEC but betting odds say it isn't, possibly use the betting
	// odds to give a more accurate reflection
	// For e.g. 2016 Cowper had Coalition vs. Independent in betting but was classic in early count,
	// so the independent's recorded votes were considered insignificant and the seat was overly favourable
	// to the Coalition.
	if (result.significance < 1.0f) {
		if (!project.parties().oppositeMajors(seat.incumbent, seat.challenger)) {
			if (!sim.isLiveAutomatic() || seatWinner[seatIndex] == Party::InvalidId || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > result.significance) {
				if (sim.isLiveAutomatic() && seat.livePartyOne) {
					float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
					if (uniformRand < seat.partyTwoProb) {
						seatWinner[seatIndex] = seat.livePartyTwo;
					}
					else if (seat.livePartyThree && uniformRand < seat.partyTwoProb + seat.partyThreeProb) {
						seatWinner[seatIndex] = seat.livePartyThree;
					}
					else {
						seatWinner[seatIndex] = seat.livePartyOne;
					}
				}
				else {
					seatWinner[seatIndex] = simulateWinnerFromBettingOdds(seat);
				}
			}
		}
	}
}

void SimulationIteration::determineNonClassicSeatResult(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	determineSeatInitialFp(seatIndex);
	float liveSignificance = 0.0f;
	if (sim.isLiveAutomatic()) {
		SeatResult result = calculateLiveResultNonClassic2CP(seatIndex);
		seatWinner[seatIndex] = result.winner;
		liveSignificance = result.significance;
	}
	// If we haven't got very far into the live count, it might be unrepresentative,
	// so randomly choose between seat betting odds and the actual live count until
	// more results come in.
	if (liveSignificance < 1.0f) {
		if (!sim.isLiveAutomatic() || seatWinner[seatIndex] == Party::InvalidId || std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) > liveSignificance) {
			if (sim.isLive() && seat.livePartyOne != Party::InvalidId && seat.livePartyTwo != Party::InvalidId) {
				float uniformRand = std::uniform_real_distribution<float>(0.0f, 1.0f)(gen);
				if (uniformRand < seat.partyTwoProb) {
					seatWinner[seatIndex] = seat.livePartyTwo;
				}
				else if (seat.livePartyThree != Party::InvalidId && uniformRand < seat.partyTwoProb + seat.partyThreeProb) {
					seatWinner[seatIndex] = seat.livePartyThree;
				}
				else {
					seatWinner[seatIndex] = seat.livePartyOne;
				}
			}
			else {
				seatWinner[seatIndex] = simulateWinnerFromBettingOdds(seat);
			}
		}
	}
}

void SimulationIteration::determineSeatFinalResult(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	// Very simple placeholder for now, will of course be more complex later.
	// Just give the seat to the incumbent if it's already held by a major party and 2pp winner otherwise.
	if (seat.incumbent > 1) {
		seatWinner[seatIndex] = seat.incumbent;
	}
	else if (incumbentNewMargin[seatIndex] >= 0.0f) {
		seatWinner[seatIndex] = seat.incumbent;
	}
	else {
		seatWinner[seatIndex] = 1 - seat.incumbent;
	}
}

void SimulationIteration::recordSeatResult(int seatIndex)
{
	Seat& seat = project.seats().access(project.seats().indexToId(seatIndex));

	run.incumbentWinPercent[seatIndex] += seatWinner[seatIndex] == seat.incumbent ? 1.0 : 0.0;
	if (seatWinner[seatIndex] == 0) ++run.partyOneWinPercent[seatIndex];
	else if (seatWinner[seatIndex] == 1) ++run.partyTwoWinPercent[seatIndex];
	else ++run.othersWinPercent[seatIndex];

	run.seatIncumbentMarginAverage[seatIndex] += incumbentNewMargin[seatIndex];
}

void SimulationIteration::assignDirectWins()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		int winnerIndex = project.parties().idToIndex(seatWinner[seatIndex]);
		if (winnerIndex != PartyCollection::InvalidIndex) {
			partyWins[winnerIndex]++;
			int regionIndex = project.regions().idToIndex(seat.region);
			++regionSeatCount[winnerIndex][regionIndex];
		}
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

void SimulationIteration::recordMajorityResult()
{
	int minimumForMajority = project.seats().count() / 2 + 1;

	// Look at the overall result and classify it
	if (partyWins[0] >= minimumForMajority) ++run.partyMajority[Mp::One];
	else if (partySupport[0] >= minimumForMajority) ++run.partyMinority[Mp::One];
	else if (partyWins[1] >= minimumForMajority) ++run.partyMajority[Mp::Two];
	else if (partySupport[1] >= minimumForMajority) ++run.partyMinority[Mp::Two];
	else ++run.hungParliament;
}

void SimulationIteration::recordPartySeatWinCounts()
{
	int othersWins = 0;
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		++sim.latestReport.partySeatWinFrequency[partyIndex][partyWins[partyIndex]];
		if (partyIndex > 1) othersWins += partyWins[partyIndex];
		for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
			++run.regionPartyWins[regionIndex][partyIndex][regionSeatCount[partyIndex][regionIndex]];
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
	}
}

void SimulationIteration::recordIterationResults()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		recordSeatResult(seatIndex);
		recordSeatPartyWinner(seatIndex);
		recordSeatFpVotes(seatIndex);
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

SimulationIteration::SeatResult SimulationIteration::calculateResultMatched2cp(int seatIndex, float priorMargin)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	//if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
	//	return calculateLiveAutomaticResultMatched2cp(seatIndex, priorMargin);
	//}
	//else if (sim.isLive() && seat.outcome && seat.outcome->getPercentCountedEstimate()) {

	if (sim.isLive() && run.seatToOutcome[seatIndex] && run.seatToOutcome[seatIndex]->getPercentCountedEstimate()) { // replace with above line when live results are restored
		return calculateLiveManualResultMatched2cp(seatIndex, priorMargin);
	}
	else {
		Party::Id winner = (priorMargin >= 0.0f ? seat.incumbent : seat.challenger);
		Party::Id runnerUp = (priorMargin >= 0.0f ? seat.challenger : seat.incumbent);
		return { winner, runnerUp, abs(priorMargin), 0.0f };
	}
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveAutomaticResultMatched2cp(int seatIndex, float priorMargin)
{
	seatIndex;
	priorMargin;
	return SeatResult();
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//// All swings are in terms of a swing to candidate 0 as per latest results
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);

	//auto boothResults = sumMatched2cpBoothVotes(seatIndex, priorMargin);

	//// Now we have also tallied the estimated votes from booths that are uncounted but matched, if any

	//// Need to calculate the remaining pool of uncounted and unmatched booths
	//float enrolmentChange = 1.0f;
	//int estimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	//if (seat.previousResults) {
	//	enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
	//	estimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
	//}

	//estimateMysteryBoothVotes(seat, boothResults, estimatedTotalOrdinaryVotes);

	//int estimatedTotalVotes = estimateDeclarationVotes(seat, boothResults, estimatedTotalOrdinaryVotes, enrolmentChange);

	//float totalTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
	//float firstMargin = (float(boothResults.tcpTally[0]) - totalTally * 0.5f) / totalTally * 100.0f;
	//Party::Id winner = (firstMargin >= 0.0f ? firstParty : secondParty);
	//Party::Id runnerUp = (firstMargin >= 0.0f ? secondParty : firstParty);

	//// Third parties can potentially be a "spoiler" for a seat expected to be classic
	//// This check replaces the winner by a third party if it is simulated but doesn't
	//// affect the balance between the 2cp candidates.
	//SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(seat);
	//if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) {
	//	if (std::uniform_real_distribution<float>(0.0f, 1.0f)(gen) < spoilerResult.significance) {
	//		return spoilerResult;
	//	}
	//}

	//float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

	//return { winner, runnerUp, abs(firstMargin), float(significance) };
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveManualResultMatched2cp(int seatIndex, float priorMargin)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	float liveMargin = run.seatToOutcome[seatIndex]->incumbentSwing + seat.margin;
	float liveStdDev = stdDevSingleSeat(run.seatToOutcome[seatIndex]->getPercentCountedEstimate());
	liveMargin += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	float priorWeight = 0.5f;
	float liveWeight = 6.0f / (liveStdDev * liveStdDev);
	float newMargin = (priorMargin * priorWeight + liveMargin * liveWeight) / (priorWeight + liveWeight);
	Party::Id winner = (newMargin >= 0.0f ? seat.incumbent : seat.challenger);
	Party::Id runnerUp = (newMargin >= 0.0f ? seat.challenger : seat.incumbent);
	float significance = std::clamp(float(run.seatToOutcome[seatIndex]->percentCounted) * 0.2f, 0.0f, 1.0f);
	return { winner, runnerUp, abs(newMargin), significance };
}

float SimulationIteration::calculateSeatRemainingSwing(Seat const& seat, float priorMargin)
{
	seat;
	priorMargin;
	return 0.0f;
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool incumbentFirst = firstParty == seat.incumbent;

	//float liveSwing = (incumbentFirst ? 1.0f : -1.0f) * seat.outcome->incumbentSwing;
	//float liveStdDev = stdDevSingleSeat(seat.outcome->getPercentCountedEstimate());
	//liveSwing += std::normal_distribution<float>(0.0f, liveStdDev)(gen);
	//float priorWeight = 0.5f;
	//float liveWeight = 6.0f / (liveStdDev * liveStdDev);
	//float priorSwing = (incumbentFirst ? 1.0f : -1.0f) * (priorMargin - seat.margin);
	//float remainingVoteSwing = (priorSwing * priorWeight + liveSwing * liveWeight) / (priorWeight + liveWeight);
	//return remainingVoteSwing;
}

SimulationIteration::BoothAccumulation SimulationIteration::sumMatched2cpBoothVotes(int seatIndex, float priorMargin)
{
	seatIndex;
	priorMargin;
	return BoothAccumulation();
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//BoothAccumulation boothAccumulation;

	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);

	//// At this point we have tallied all the counted votes from booths (matched or otherwise)
	//float remainingVoteSwing = calculateSeatRemainingSwing(seat, priorMargin);

	//boothAccumulation.tcpTally = run.seatTcpTally[seatIndex];

	//// To estimate the vote count for individual booths we need to adjust the previous election's total votes
	//// according to how the already-counted individual booth growth as occurred
	//// There may not be any old comparison votes in which case we assume no growth
	//for (auto boothId : seat.latestResults->booths) {
	//	Results::Booth const& booth = project.results().getBooth(boothId);
	//	if (booth.hasOldResults() && !booth.hasNewResults()) {
	//		int estimatedTotalVotes = int(std::round(float(booth.totalOldTcpVotes()) * run.seatIndividualBoothGrowth[seatIndex]));
	//		Party::Id boothFirstParty = project.results().getPartyByCandidate(booth.tcpCandidateId[0]);
	//		// Party const* boothSecondParty = project.getPartyByCandidate(booth.tcpCandidateId[1]);
	//		bool isInSeatOrder = boothFirstParty == firstParty;
	//		float oldVotes0 = float(isInSeatOrder ? booth.tcpVote[0] : booth.tcpVote[1]);
	//		float oldVotes1 = float(isInSeatOrder ? booth.tcpVote[1] : booth.tcpVote[0]);
	//		float oldPercent0 = oldVotes0 / (oldVotes0 + oldVotes1) * 100.0f;
	//		float boothSwingStdDev = 2.5f + 200.0f / booth.totalOldTcpVotes(); // small booths a lot swingier
	//		float boothSwing = remainingVoteSwing + std::normal_distribution<float>(0.0f, boothSwingStdDev)(gen);
	//		if (booth.isPPVC()) {
	//			// votes are already in order for the seat, not the booth
	//			if (firstParty == 0 && secondParty == 1) boothSwing += ppvcBias;
	//			if (secondParty == 0 && firstParty == 1) boothSwing -= ppvcBias;
	//		}
	//		float newPercent0 = std::clamp(oldPercent0 + boothSwing, 0.0f, 100.0f);
	//		int newVotes0 = int(std::round(newPercent0 * float(estimatedTotalVotes) * 0.01f));
	//		int newVotes1 = estimatedTotalVotes - newVotes0;
	//		boothAccumulation.tcpTally[0] += newVotes0;
	//		boothAccumulation.tcpTally[1] += newVotes1;
	//	}
	//	if (!booth.hasOldResults() && !booth.hasNewResults()) {
	//		using Mystery = BoothAccumulation::MysteryBoothType;
	//		if (booth.isPPVC()) ++boothAccumulation.mysteryBoothCounts[Mystery::Ppvc];
	//		else if (booth.name.find(" Team") != std::string::npos) ++boothAccumulation.mysteryBoothCounts[Mystery::Team];
	//		else ++boothAccumulation.mysteryBoothCounts[Mystery::Standard];
	//	}
	//}

	//return boothAccumulation;
}

void SimulationIteration::estimateMysteryBoothVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes)
{
	seat;
	boothResults;
	estimatedTotalOrdinaryVotes;
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//Party::Id secondParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[1].candidateId);
	//float firstTallyPercent = float(boothResults.tcpTally[0]) / float(boothResults.tcpTally[0] + boothResults.tcpTally[1]) * 100.0f;

	//using Mystery = BoothAccumulation::MysteryBoothType;
	//if (boothResults.hasMysteryBooths()) {
	//	int estimatedRemainingOrdinaryVotes = std::max(0, estimatedTotalOrdinaryVotes - boothResults.tcpTally[0] - boothResults.tcpTally[1]);
	//	// sanity check to make sure we aren't assigning 5000 votes to a special hospital team or something
	//	int plausibleMaximumRemainingOrdinaryVotes = boothResults.mysteryBoothCounts[int(Mystery::Ppvc)] * 10000 +
	//		boothResults.mysteryBoothCounts[int(Mystery::Standard)] * 2000 + boothResults.mysteryBoothCounts[int(Mystery::Team)] * 200;
	//	float proportionPPVC = float(boothResults.mysteryBoothCounts[int(Mystery::Ppvc)] * 10000) / float(plausibleMaximumRemainingOrdinaryVotes);
	//	estimatedRemainingOrdinaryVotes = std::min(estimatedRemainingOrdinaryVotes, plausibleMaximumRemainingOrdinaryVotes);
	//	estimatedTotalOrdinaryVotes = estimatedRemainingOrdinaryVotes + boothResults.tcpTally[0] + boothResults.tcpTally[1];

	//	const float MysteryVoteStdDev = 6.0f;
	//	float incumbentMysteryPercent = std::normal_distribution<float>(firstTallyPercent, MysteryVoteStdDev)(gen);
	//	if (firstParty == 0 && secondParty == 1) incumbentMysteryPercent += ppvcBias * proportionPPVC;
	//	if (secondParty == 1 && firstParty == 0) incumbentMysteryPercent -= ppvcBias * proportionPPVC;
	//	int incumbentMysteryVotes = int(std::round(incumbentMysteryPercent * 0.01f * float(estimatedRemainingOrdinaryVotes)));
	//	int challengerMysteryVotes = estimatedRemainingOrdinaryVotes - incumbentMysteryVotes;
	//	boothResults.tcpTally[0] += incumbentMysteryVotes;
	//	boothResults.tcpTally[1] += challengerMysteryVotes;
	//}
}

int SimulationIteration::estimateDeclarationVotes(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange)
{
	seat;
	boothResults;
	estimatedTotalOrdinaryVotes;
	enrolmentChange;
	return 0;
	//if (seat.previousResults) {
	//	return estimateDeclarationVotesUsingPreviousResults(seat, boothResults, estimatedTotalOrdinaryVotes, enrolmentChange);
	//}
	//else {
	//	return estimateDeclarationVotesWithoutPreviousResults(seat, boothResults, estimatedTotalOrdinaryVotes);
	//}
}

int SimulationIteration::estimateDeclarationVotesUsingPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes, float enrolmentChange)
{
	seat;
	boothResults;
	estimatedTotalOrdinaryVotes;
	enrolmentChange;
	return 0;
	//int estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
	//float declarationVoteChange = estimateDeclarationVoteProportionalChange(seat, estimatedTotalVotes, estimatedTotalOrdinaryVotes);
	//float ordinaryVoteSwing = calculateOrdinaryVoteSwing(seat, boothResults);

	//auto[firstAbsentVotes, secondAbsentVotes] = estimateAbsentVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	//auto[firstProvisionalVotes, secondProvisionalVotes] = estimateProvisionalVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	//auto[firstPrepollVotes, secondPrepollVotes] = estimatePrepollVotes(seat, ordinaryVoteSwing, declarationVoteChange);
	//auto[firstPostalVotes, secondPostalVotes] = estimatePostalVotes(seat, ordinaryVoteSwing, declarationVoteChange);

	//boothResults.tcpTally[0] += firstAbsentVotes + firstProvisionalVotes + firstPrepollVotes + firstPostalVotes;
	//boothResults.tcpTally[1] += secondAbsentVotes + secondProvisionalVotes + secondPrepollVotes + secondPostalVotes;
	//return estimatedTotalVotes;
}

int SimulationIteration::estimateDeclarationVotesWithoutPreviousResults(Seat const& seat, BoothAccumulation& boothResults, int estimatedTotalOrdinaryVotes)
{
	seat;
	boothResults;
	estimatedTotalOrdinaryVotes;
	return 0;
	//int estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	//if (seat.latestResults->ordinaryVotes() > estimatedTotalOrdinaryVotes) {
	//	float totalOrdinaryTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
	//	float estimatedNonOrdinaryVotePotential = float(seat.latestResults->enrolment - estimatedTotalOrdinaryVotes);
	//	float estimatedProportionRemainingFormal = float(estimatedTotalVotes) / estimatedNonOrdinaryVotePotential;
	//	constexpr float remainingFormalStdDev = 0.05f;
	//	estimatedProportionRemainingFormal = std::clamp(estimatedProportionRemainingFormal + std::normal_distribution<float>(0.0f, remainingFormalStdDev)(gen), 0.0f, 1.0f);
	//	float actualNonOrdinaryVotePotential = float(seat.latestResults->enrolment - seat.latestResults->ordinaryVotes());
	//	int estimatedTotalDeclarationVotes = int(actualNonOrdinaryVotePotential * estimatedProportionRemainingFormal);

	//	float firstPercent = float(boothResults.tcpTally[0]) / totalOrdinaryTally;
	//	constexpr float declarationStdDev = 0.05f;
	//	float declarationVoteFirstProportion = std::clamp(firstPercent + std::normal_distribution<float>(0.0f, declarationStdDev)(gen), 0.0f, 1.0f);
	//	int firstTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * declarationVoteFirstProportion);
	//	int secondTcpDeclarationVotes = int(estimatedTotalDeclarationVotes * (1.0f - declarationVoteFirstProportion));
	//	boothResults.tcpTally[0] += firstTcpDeclarationVotes;
	//	boothResults.tcpTally[1] += secondTcpDeclarationVotes;
	//}
	//return estimatedTotalVotes;
}

float SimulationIteration::estimateDeclarationVoteProportionalChange(Seat const& seat, int estimatedTotalVotes, int estimatedTotalOrdinaryVotes)
{
	seat;
	estimatedTotalVotes;
	estimatedTotalOrdinaryVotes;
	return 0.0f;
	//int estimatedDeclarationVotes = estimatedTotalVotes - estimatedTotalOrdinaryVotes;
	//int oldDeclarationVotes = seat.previousResults->total2cpVotes() - seat.previousResults->ordinaryVotes();
	//return float(estimatedDeclarationVotes) / float(oldDeclarationVotes);
}

float SimulationIteration::calculateOrdinaryVoteSwing(Seat const& seat, BoothAccumulation const & boothResults)
{
	seat;
	boothResults;
	return 0.0f;
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//float totalOrdinaryTally = float(boothResults.tcpTally[0] + boothResults.tcpTally[1]);
	//float firstNewOrdinaryPercent = float(boothResults.tcpTally[0]) / totalOrdinaryTally * 100.0f;
	//float firstOldOrdinaryPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].ordinaryVotes) / float(seat.previousResults->ordinaryVotes()) * 100.0f;
	//return firstNewOrdinaryPercent - firstOldOrdinaryPercent;
}

SimulationIteration::TcpTally SimulationIteration::estimateAbsentVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	seat;
	ordinaryVoteSwing;
	declarationVoteChange;
	return SimulationIteration::TcpTally();
	//float absentStdDev = 5.0f; // needs more research
	//float absentSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, absentStdDev)(gen);
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//float firstOldAbsentPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].absentVotes) / float(seat.previousResults->absentVotes()) * 100.0f;
	//float firstNewAbsentPercent = firstOldAbsentPercent + absentSwing;
	//int estimatedAbsentVotes = int(std::round(float(seat.previousResults->absentVotes()) * declarationVoteChange));
	//int firstCountedAbsentVotes = seat.latestResults->finalCandidates[0].absentVotes;
	//int secondCountedAbsentVotes = seat.latestResults->finalCandidates[1].absentVotes;
	//int estimatedRemainingAbsentVotes = std::max(0, estimatedAbsentVotes - firstCountedAbsentVotes - secondCountedAbsentVotes);
	//int firstRemainingAbsentVotes = int(std::round(firstNewAbsentPercent * float(estimatedRemainingAbsentVotes) * 0.01f));
	//int secondRemainingAbsentVotes = estimatedRemainingAbsentVotes - firstRemainingAbsentVotes;
	//int firstAbsentVotes = firstCountedAbsentVotes + firstRemainingAbsentVotes;
	//int secondAbsentVotes = secondCountedAbsentVotes + secondRemainingAbsentVotes;
	//return { firstAbsentVotes, secondAbsentVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimateProvisionalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	seat;
	ordinaryVoteSwing;
	declarationVoteChange;
	return SimulationIteration::TcpTally();
	//float provisionalStdDev = 5.0f; // needs more research
	//float provisionalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, provisionalStdDev)(gen);
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//float firstOldProvisionalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].provisionalVotes) / float(seat.previousResults->provisionalVotes()) * 100.0f;
	//float firstNewProvisionalPercent = firstOldProvisionalPercent + provisionalSwing;
	//int estimatedProvisionalVotes = int(std::round(float(seat.previousResults->provisionalVotes()) * declarationVoteChange));
	//int firstCountedProvisionalVotes = seat.latestResults->finalCandidates[0].provisionalVotes;
	//int secondCountedProvisionalVotes = seat.latestResults->finalCandidates[1].provisionalVotes;
	//int estimatedRemainingProvisionalVotes = std::max(0, estimatedProvisionalVotes - firstCountedProvisionalVotes - secondCountedProvisionalVotes);
	//int firstRemainingProvisionalVotes = int(std::round(firstNewProvisionalPercent * float(estimatedRemainingProvisionalVotes) * 0.01f));
	//int secondRemainingProvisionalVotes = estimatedRemainingProvisionalVotes - firstRemainingProvisionalVotes;
	//int firstProvisionalVotes = firstCountedProvisionalVotes + firstRemainingProvisionalVotes;
	//int secondProvisionalVotes = secondCountedProvisionalVotes + secondRemainingProvisionalVotes;
	//return { firstProvisionalVotes, secondProvisionalVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimatePrepollVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	seat;
	ordinaryVoteSwing;
	declarationVoteChange;
	return SimulationIteration::TcpTally();
	//float prepollStdDev = 5.0f; // needs more research
	//float prepollSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, prepollStdDev)(gen);
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//float firstOldPrepollPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].prepollVotes) / float(seat.previousResults->prepollVotes()) * 100.0f;
	//float firstNewPrepollPercent = firstOldPrepollPercent + prepollSwing;
	//int estimatedPrepollVotes = int(std::round(float(seat.previousResults->prepollVotes()) * declarationVoteChange));
	//int firstCountedPrepollVotes = seat.latestResults->finalCandidates[0].prepollVotes;
	//int secondCountedPrepollVotes = seat.latestResults->finalCandidates[1].prepollVotes;
	//int estimatedRemainingPrepollVotes = std::max(0, estimatedPrepollVotes - firstCountedPrepollVotes - secondCountedPrepollVotes);
	//int firstRemainingPrepollVotes = int(std::round(firstNewPrepollPercent * float(estimatedRemainingPrepollVotes) * 0.01f));
	//int secondRemainingPrepollVotes = estimatedRemainingPrepollVotes - firstRemainingPrepollVotes;
	//int firstPrepollVotes = firstCountedPrepollVotes + firstRemainingPrepollVotes;
	//int secondPrepollVotes = secondCountedPrepollVotes + secondRemainingPrepollVotes;
	//return { firstPrepollVotes, secondPrepollVotes };
}

SimulationIteration::TcpTally SimulationIteration::estimatePostalVotes(Seat const& seat, float ordinaryVoteSwing, float declarationVoteChange)
{
	seat;
	ordinaryVoteSwing;
	declarationVoteChange;
	return SimulationIteration::TcpTally();
	//float postalStdDev = 5.0f; // needs more research
	//float postalSwing = ordinaryVoteSwing + std::normal_distribution<float>(0.0f, postalStdDev)(gen);
	//Party::Id firstParty = project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);
	//bool sameOrder = firstParty == project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//float firstOldPostalPercent = float(seat.previousResults->finalCandidates[sameOrder ? 0 : 1].postalVotes) / float(seat.previousResults->postalVotes()) * 100.0f;
	//float firstNewPostalPercent = firstOldPostalPercent + postalSwing;
	//int estimatedPostalVotes = int(std::round(float(seat.previousResults->postalVotes()) * declarationVoteChange));
	//int firstCountedPostalVotes = seat.latestResults->finalCandidates[0].postalVotes;
	//int secondCountedPostalVotes = seat.latestResults->finalCandidates[1].postalVotes;
	//int estimatedRemainingPostalVotes = std::max(0, estimatedPostalVotes - firstCountedPostalVotes - secondCountedPostalVotes);
	//int firstRemainingPostalVotes = int(std::round(firstNewPostalPercent * float(estimatedRemainingPostalVotes) * 0.01f));
	//int secondRemainingPostalVotes = estimatedRemainingPostalVotes - firstRemainingPostalVotes;
	//int firstPostalVotes = firstCountedPostalVotes + firstRemainingPostalVotes;
	//int secondPostalVotes = secondCountedPostalVotes + secondRemainingPostalVotes;
	//return { firstPostalVotes, secondPostalVotes };
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultNonClassic2CP(int seatIndex)
{
	Seat const& seat = project.seats().viewByIndex(seatIndex);
	return { seat.incumbent, seat.challenger, seat.margin, 0.0f }; // remove this line (NOT the above one) when restoring live results
	//if (sim.isLiveAutomatic() && seatPartiesMatchBetweenElections(seat)) {
	//	return calculateResultMatched2cp(seatIndex, seat.margin);
	//}
	//else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->total2cpVotes()) {
	//	return calculateResultUnmatched2cp(seatIndex);
	//}
	//else if (sim.isLiveAutomatic() && seat.latestResults && seat.latestResults->fpCandidates.size() && seat.latestResults->totalFpVotes()) {
	//	return calculateLiveResultFromFirstPreferences(seat);
	//}
	//else {
	//	return { seat.incumbent, seat.challenger, seat.margin, 0.0f };
	//}
}

SimulationIteration::SeatResult SimulationIteration::calculateResultUnmatched2cp(int seatIndex)
{
	seatIndex;
	return SeatResult();
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//TcpTally tcpTally = sumUnmatched2cpBoothVotes(seatIndex);

	//int estimatedTotalVotes = estimateTotalVotes(seat);

	//modifyUnmatchedTallyForRemainingVotes(seat, tcpTally, estimatedTotalVotes);

	//int totalVotes = tcpTally[0] + tcpTally[1];

	//float margin = (float(tcpTally[0]) - float(totalVotes) * 0.5f) / float(totalVotes) * 100.0f;

	//int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	//int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	//Party::Id firstParty = project.results().getPartyByCandidate(firstCandidateId);
	//Party::Id secondParty = project.results().getPartyByCandidate(secondCandidateId);

	//Party::Id winner = (margin >= 0.0f ? firstParty : secondParty);
	//Party::Id runnerUp = (margin >= 0.0f ? secondParty : firstParty);

	//// Third parties can potentially be a "spoiler" for a seat expected to be classic
	//// This check replaces the winner by a third party if it is simulated but doesn't
	//// affect the balance between the 2cp candidates.
	//SeatResult spoilerResult = calculateLiveResultFromFirstPreferences(seat);
	//if (spoilerResult.winner != winner && spoilerResult.winner != runnerUp) return spoilerResult;

	//float significance = std::clamp(float(seat.latestResults->total2cpVotes()) / float(estimatedTotalVotes) * 20.0f, 0.0f, 1.0f);

	//return { winner, runnerUp, margin, significance };
}

SimulationIteration::TcpTally SimulationIteration::sumUnmatched2cpBoothVotes(int seatIndex)
{
	seatIndex;
	return TcpTally();
	//Seat const& seat = project.seats().viewByIndex(seatIndex);
	//int firstCandidateId = seat.latestResults->finalCandidates[0].candidateId;
	//int secondCandidateId = seat.latestResults->finalCandidates[1].candidateId;
	//TcpTally tcpTally = { 0, 0 };
	//float preferenceFlowGuess = std::normal_distribution<float>(run.seatFirstPartyPreferenceFlow[seatIndex],
	//	run.seatPreferenceFlowVariation[seatIndex])(gen);
	//for (auto boothId : seat.latestResults->booths) {
	//	Results::Booth const& booth = project.results().getBooth(boothId);
	//	bool matchingOrder = project.results().getPartyByCandidate(booth.tcpCandidateId[0]) == 
	//		project.results().getPartyByCandidate(seat.latestResults->finalCandidates[0].candidateId);

	//	if (booth.hasNewResults()) {
	//		for (int i = 0; i <= 1; ++i) tcpTally[i] += (matchingOrder ? booth.newTcpVote[i] : booth.newTcpVote[1 - i]);
	//	}
	//	else if (booth.totalNewFpVotes()) {
	//		TcpTally tcpEstimate = { 0, 0 };
	//		for (auto const& candidate : booth.fpCandidates) {
	//			if (candidate.candidateId == firstCandidateId) {
	//				tcpEstimate[0] += candidate.fpVotes;
	//			}
	//			else if (candidate.candidateId != secondCandidateId) {
	//				tcpEstimate[0] += int(float(candidate.fpVotes) * preferenceFlowGuess);
	//			}
	//		}
	//		tcpEstimate[1] = booth.totalNewFpVotes() - tcpEstimate[0];
	//		for (int i = 0; i <= 1; ++i) tcpTally[i] += tcpEstimate[i];
	//	}
	//}
	//return tcpTally;
}

int SimulationIteration::estimateTotalVotes(Seat const& seat)
{
	seat;
	return 0;
	//// now estimate the remaining votes with considerable variance
	//float enrolmentChange = determineEnrolmentChange(seat, nullptr);
	//int estimatedTotalVotes = 0;
	//if (seat.previousResults) {
	//	estimatedTotalVotes = int(float(seat.previousResults->total2cpVotes()) * enrolmentChange);
	//}
	//else {
	//	estimatedTotalVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	//}
	//return estimatedTotalVotes;
}

void SimulationIteration::modifyUnmatchedTallyForRemainingVotes(Seat const& seat, TcpTally& tcpTally, int estimatedTotalVotes)
{
	seat;
	tcpTally;
	estimatedTotalVotes;
	//int totalBoothVotes = tcpTally[0] + tcpTally[1];
	//int minTotalVotes = totalBoothVotes;
	//int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	//constexpr float totalVoteNumberDeviation = 0.05f;
	//float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
	//int clampedTotalVotes = std::clamp(int(randomizedTotalVotes), minTotalVotes, maxTotalVotes);
	//int estimatedRemainingVotes = std::max(0, clampedTotalVotes - totalBoothVotes);

	//float firstProportionCounted = float(tcpTally[0]) / float(totalBoothVotes);
	//constexpr float firstProportionChangeDeviation = 0.08f;
	//float firstProportionChange = std::normal_distribution<float>(0.0f, firstProportionChangeDeviation)(gen);
	//float firstProportionRemaining = std::clamp(firstProportionChange + firstProportionCounted, 0.0f, 1.0f);
	//int firstRemainingVotes = int(firstProportionRemaining * float(estimatedRemainingVotes));
	//int secondRemainingVotes = estimatedRemainingVotes - firstRemainingVotes;
	//tcpTally[0] += firstRemainingVotes;
	//tcpTally[1] += secondRemainingVotes;
}

SimulationIteration::SeatResult SimulationIteration::calculateLiveResultFromFirstPreferences(Seat const& seat)
{
	seat;
	return SeatResult();
	//SeatCandidates candidates = collectSeatCandidates(seat);
	//int estimatedTotalVotes = estimateTotalVotes(seat);
	//projectSeatCandidatePrimaries(seat, candidates, estimatedTotalVotes);
	//distributePreferences(candidates);
	//return determineResultFromDistribution(seat, candidates, estimatedTotalVotes);
}

SimulationIteration::SeatCandidates SimulationIteration::collectSeatCandidates(Seat const& seat)
{
	seat;
	return SeatCandidates();
	//SeatCandidates candidates;
	//for (auto const& fpCandidate : seat.latestResults->fpCandidates) {
	//	candidates.push_back({ fpCandidate.totalVotes(), project.results().getPartyByCandidate(fpCandidate.candidateId) });
	//}
	//return candidates;
}

void SimulationIteration::projectSeatCandidatePrimaries(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes)
{
	seat;
	candidates;
	estimatedTotalVotes;
	//// now estimate the remaining votes with considerable variance
	//int countedVotes = std::accumulate(candidates.begin(), candidates.end(), 0,
	//	[](int i, SeatCandidate const& c) { return i + c.vote; });

	//int maxTotalVotes = (estimatedTotalVotes + seat.latestResults->enrolment) / 2;
	//int minTotalVotes = std::min(countedVotes, maxTotalVotes);
	//constexpr float totalVoteNumberDeviation = 0.05f;
	//float randomizedTotalVotes = float(estimatedTotalVotes) * std::normal_distribution<float>(1.0f, totalVoteNumberDeviation)(gen);
	//int clampedTotalVotes = std::clamp(int(randomizedTotalVotes), minTotalVotes, maxTotalVotes);
	//int estimatedRemainingVotes = std::max(0, clampedTotalVotes - countedVotes);

	//float totalProjectionWeight = 0.0f;
	//for (auto& candidate : candidates) {
	//	float countedProportion = float(candidate.vote) / float(countedVotes);
	//	float candidateStdDev = std::min(countedProportion * 0.2f, 0.04f);
	//	float remainingProportion = std::normal_distribution<float>(float(countedProportion), candidateStdDev)(gen);
	//	candidate.weight = std::max(0.0f, remainingProportion);
	//	totalProjectionWeight += candidate.weight;
	//}

	//for (auto& candidate : candidates) {
	//	candidate.vote += int(float(estimatedRemainingVotes) * candidate.weight / totalProjectionWeight);
	//}
}

void SimulationIteration::distributePreferences(SeatCandidates& candidates)
{
	candidates;
	//for (int sourceIndex = candidates.size() - 1; sourceIndex > 1; --sourceIndex) {
	//	SeatCandidate sourceCandidate = candidates[sourceIndex];
	//	if (sourceCandidate.partyId == Party::InvalidId) continue;
	//	Party const& sourceParty = project.parties().view(sourceCandidate.partyId);
	//	candidates[sourceIndex].vote = 0;
	//	std::vector<float> weights;
	//	weights.resize(candidates.size(), 0);
	//	for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
	//		SeatCandidate const& targetCandidate = candidates[targetIndex];
	//		if (targetCandidate.partyId == Party::InvalidId) continue;
	//		Party const& targetParty = project.parties().view(targetCandidate.partyId);
	//		int ideologyDistance = float(std::abs(sourceParty.ideology - targetParty.ideology));
	//		float consistencyBase = PreferenceConsistencyBase[sourceParty.consistency];
	//		float thisWeight = std::pow(consistencyBase, -ideologyDistance);
	//		if (!sourceParty.countsAsMajor() && !targetParty.countsAsMajor()) thisWeight *= 1.6f;
	//		if (project.parties().oppositeMajors(sourceCandidate.partyId, targetCandidate.partyId)) thisWeight /= 2.0f;
	//		// note, these frequent calls to the RNG are a significant bottleneck
	//		thisWeight *= std::uniform_real_distribution<float>(0.5f, 1.5f)(gen);
	//		thisWeight *= std::sqrt(float(targetCandidate.vote)); // preferences tend to flow to more popular candidates
	//		weights[targetIndex] = thisWeight;
	//	}
	//	float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0f) + 0.0000001f; // avoid divide by zero warning
	//	for (int targetIndex = sourceIndex - 1; targetIndex >= 0; --targetIndex) {
	//		// this will cause a few votes to be lost to rounding errors but since we're only running
	//		// these calculations when uncertainty is high that doesn't really matter.
	//		candidates[targetIndex].vote += int(float(sourceCandidate.vote) * weights[targetIndex] / totalWeight);
	//	}
	//	std::sort(candidates.begin(), candidates.end(),
	//		[](SeatCandidate lhs, SeatCandidate rhs) {return lhs.vote > rhs.vote; });
	//}
}

SimulationIteration::SeatResult SimulationIteration::determineResultFromDistribution(Seat const& seat, SeatCandidates& candidates, int estimatedTotalVotes)
{
	seat;
	candidates;
	estimatedTotalVotes;
	return SeatResult();
	//float totalTally = float(candidates[0].vote + candidates[1].vote);
	//float margin = (float(candidates[0].vote) - totalTally * 0.5f) / totalTally * 100.0f;
	//Party::Id winner = candidates[0].partyId;
	//Party::Id runnerUp = candidates[1].partyId;
	//float significance = std::clamp(float(seat.latestResults->totalFpVotes()) / float(estimatedTotalVotes) * 5.0f, 0.0f, 1.0f);

	//return { winner, runnerUp, margin, significance };
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
	seat;
	return false;
	//if (!seat.latestResults) return false;
	//if (!seat.previousResults) return false;
	//Party::Id oldPartyOne = project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[0].affiliationId);
	//Party::Id oldPartyTwo = project.results().getPartyByAffiliation(seat.previousResults->finalCandidates[1].affiliationId);
	//Party::Id newPartyOne = project.results().getPartyByAffiliation(seat.latestResults->finalCandidates[0].affiliationId);
	//Party::Id newPartyTwo = project.results().getPartyByAffiliation(seat.latestResults->finalCandidates[1].affiliationId);
	//if (oldPartyOne == newPartyOne && oldPartyTwo == newPartyTwo) return true;
	//if (oldPartyOne == newPartyTwo && oldPartyTwo == newPartyOne) return true;
	//return false;
}

float SimulationIteration::determineEnrolmentChange(Seat const& seat, int* estimatedTotalOrdinaryVotes)
{
	return 0.0f;
	seat;
	estimatedTotalOrdinaryVotes;
	//// Need to calculate the remaining pool of uncounted and unmatched booths
	//float enrolmentChange = 1.0f;
	//int tempEstimatedTotalOrdinaryVotes = int(float(seat.latestResults->enrolment) * run.previousOrdinaryVoteEnrolmentRatio);
	//if (seat.previousResults) {
	//	enrolmentChange = float(seat.latestResults->enrolment) / float(seat.previousResults->enrolment);
	//	tempEstimatedTotalOrdinaryVotes = int(float(seat.previousResults->ordinaryVotes()) * enrolmentChange);
	//}
	//if (estimatedTotalOrdinaryVotes) *estimatedTotalOrdinaryVotes = tempEstimatedTotalOrdinaryVotes;
	//return enrolmentChange;
}