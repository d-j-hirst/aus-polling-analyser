#pragma once

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

} // namespace IndependentCandidacy
