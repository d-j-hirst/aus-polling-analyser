#pragma once

// Allocate the existing Others primary vote across confirmed district ballots.
// Separately modelled parties and independents keep their own FP draws. The
// shares here are percentages of all formal district first preferences.
#include <algorithm>
#include <cmath>
#include <vector>

namespace GroupedOthersFp {
struct District {
	int seatIndex;
	double turnout;
	double growthOdds;
	int knownParties = 0;
	double knownShareSum = 0.0;
	bool useFieldSize = false;
};

// A longer field tends to attract more votes, though two similar candidates
// can also compete for the same voters. Use one quarter of the proportional
// change on the log-odds scale, the milder growth setting checked on Victoria
// 2022 and SA 2026. Adding one to each count permits a genuine empty old field.
inline double ballotGrowthOdds(int previous, int current)
{
	return std::pow((1.0 + current) / (1.0 + previous), 0.25);
}

struct Allocation {
	int seatIndex;
	double turnout;
	double original;
	double major;
	double proposed;
};

// Most grouped candidates have only a shared estimate. Where a known nominee
// has a prior local share, replace half of one ordinary candidate's allowance.
// The half weight recognises that a different election attracts different
// voters. A separately drawn emerging party occupies a party slot first. If
// its identity is ambiguous, average the known priors over the slots left.
inline double propose(District const& district, double grouped, double major,
	int remainingCandidates, int partySlots, bool separateEmergingParty)
{
	double share = grouped * district.growthOdds /
		(1.0 + grouped * 0.01 * (district.growthOdds - 1.0));
	if (district.knownParties && partySlots && remainingCandidates) {
		double const fractionStillGrouped = double(partySlots - int(separateEmergingParty)) / partySlots;
		double const ordinary = share / remainingCandidates;
		share += 0.5 * fractionStillGrouped * (district.knownShareSum - district.knownParties * ordinary);
	}
	return std::clamp(share, 0.0, grouped + major);
}

// Preserve the eligible districts' combined Others ballots in each simulation,
// weighting by the same turnout as statewide FP reconciliation. If the local
// estimates together ask for too many votes, scale them down proportionally.
// If they ask for too few, return the difference in proportion to available
// major-party ballots. This keeps every district within its vote budget.
inline void preserveTotal(std::vector<Allocation>& seats)
{
	double change = 0.0, proposedVotes = 0.0, availableVotes = 0.0;
	for (auto const& s : seats) {
		change += s.turnout * (s.proposed - s.original);
		proposedVotes += s.turnout * s.proposed;
		availableVotes += s.turnout * (s.major + s.original - s.proposed);
	}
	if (change == 0.0) return;
	double const fraction = change > 0.0 ? change / proposedVotes : -change / availableVotes;
	for (auto& s : seats) {
		double const available = s.major + s.original - s.proposed;
		s.proposed += fraction * (change > 0.0 ? -s.proposed : available);
		s.proposed = std::clamp(s.proposed, 0.0, s.original + s.major);
	}
}
}
