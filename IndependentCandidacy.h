#pragma once

#include <map>
#include <string>

#include "SpecialPartyCodes.h"

// Decide which independent campaigns can contribute to a simulated seat, and
// distinguish an actual candidate in the preference count from the report's
// threshold-based Emerging Independent category. SimulationIteration uses
// these rules in ordinary forecasts, live baselines and live forecasts alike.
namespace IndependentCandidacy {

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
