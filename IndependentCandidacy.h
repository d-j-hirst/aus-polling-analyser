#pragma once

#include <algorithm>
#include <map>
#include <numeric>
#include <span>
#include <string>
#include <utility>

#include "SpecialPartyCodes.h"

// Decide which independent campaigns can contribute to a simulated seat, and
// distinguish an actual candidate in the preference count from the report's
// threshold-based Emerging Independent category. SimulationIteration uses
// these rules in ordinary forecasts, live baselines and live forecasts alike.
namespace IndependentCandidacy {

// The seat file lists independent and other candidacies separately. In any
// given simulation some of each can have their own FP and preference account.
// Only the remaining candidates share the generic Others pile. An additional
// named independent in a live count cannot remove an unrelated minor party.
struct OtherCandidateRoster {
	int others = 0;
	int independents = 0;

	int remaining(int separateIndependents, bool separateEmergingParty) const {
		return std::max(0, others - int(separateEmergingParty)) +
			std::max(0, independents - separateIndependents);
	}
};

// Others represents candidates who would normally be excluded individually.
// Distributing their combined vote at once can keep a near-zero independent
// in the count long enough to win. Scale their usual preference weight by
// their ORIGINAL primary relative to the weaker of two tests: 20% of this
// draw's residual Others pile, or 80% of one average candidate in that pile.
// Both are modelling assumptions. With 30% shared by six candidates, 4%
// qualifies for full weight and 2% gets half weight. When the roster is
// unknown only the first test is available. The FP inputs can both be
// percentages or both fractions; the multiplier changes no primary votes.
inline double othersPreferenceWeight(double independentFp, double groupedFp, int groupedCandidates)
{
	if (groupedFp <= 0.0) return 1.0;
	double thresholdFraction = 0.20;
	if (groupedCandidates > 0) thresholdFraction = std::min(thresholdFraction, 0.80 / groupedCandidates);
	return std::clamp((independentFp / groupedFp) / thresholdFraction, 0.0, 1.0);
}

// The legacy count allocates continuing ballots using relative weights for
// the surviving candidates. After applying its usual flow settings, reduce
// the affected independent weights and return the new sum to divide by.
// This redistributes the whole continuing pile and retains any exhaustion.
inline double adjustLegacyOthersWeights(std::span<std::pair<int,double> const> candidates,
	std::span<double> weights, std::map<int,double> const& reductions, double originalTotal)
{
	bool reduced = false;
	for (std::size_t i = 0; i < candidates.size(); ++i) {
		auto factor = reductions.find(candidates[i].first);
		if (factor == reductions.end()) continue;
		weights[i] *= factor->second;
		reduced = true;
	}
	if (reduced) return std::accumulate(weights.begin(), weights.end(), 0.0);
	return originalTotal;
}

// Before the ballot is configured, the model can sample whether a previously
// strong independent returns. Once candidates are known, an IND entry only
// establishes that some independent is running: the previous unsuccessful
// campaign needs its own confirmation. Incumbents keep their existing
// recontest/retirement treatment, which is applied separately by the simulator.
inline bool usePreviousResult(
	bool ballotConfigured,
	bool independentIncumbent,
	bool previousCandidateConfirmed)
{
	return !ballotConfigured || independentIncumbent || previousCandidateConfirmed;
}

// A generated emerging independent can fall below the emergence threshold
// when its FP share is normalised against the other parties. If the ballot
// establishes that this candidate exists, retain it through preferences: it
// can receive transfers before being excluded, unlike the generic Others pool.
// This does not generate a candidate or give it any additional primary votes.
//
// independentCount is the number of independent candidates in sRunningParties.
// A separately modelled independent with positive FP already accounts for one
// of those candidates. Counted votes for the live principal independent can
// also establish candidacy when that person occupies the emerging slot and
// was not configured beforehand. An empty feed or projected votes alone cannot.
inline bool hasKnownEmergingCandidate(
	int independentCount,
	bool separateIndependentPresent,
	bool observedEmergingCandidate)
{
	int const separatelyRepresented = separateIndependentPresent ? 1 : 0;
	return independentCount > separatelyRepresented || observedEmergingCandidate;
}

// Classify FP shares for reporting, in percentage points of the seat's formal
// vote. The threshold is supplied from the historical emergence parameters,
// converted back from the model's transformed scale by the caller. Below it,
// the candidate's ORIGINAL primary share belongs in Others, even if transfers
// later allowed the candidate to reach the TCP or win. Do not change that TCP
// result or the separate, named/prominent-independent slot.
//
// Unknown small candidates use this grouping before preferences as before.
// Known candidates use it only after preferences and baseline candidate moments
// have been recorded. A zero emerging entry then means no qualifying emerging
// candidate in the FP report, not that a known person literally received zero
// votes. Keeping the zero entry lets the histogram record that distinction
// consistently; repeating the grouping cannot add the votes twice.
template<class Share>
inline void mergeSmallEmergingFpIntoOthers(
	std::map<int, Share>& primaryShares,
	double emergenceThresholdPercent)
{
	auto emerging = primaryShares.find(EmergingIndIndex);
	if (emerging == primaryShares.end()) return;
	Share const primaryShare = emerging->second;
	if (primaryShare <= 0.0f || primaryShare >= emergenceThresholdPercent) return;
	primaryShares[OthersIndex] += primaryShare;
	emerging->second = 0.0f;
}

} // namespace IndependentCandidacy
