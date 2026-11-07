#pragma once

// Decide whether Liberal and National both survive the preference count while
// preserving the existing combined Coalition allocation against other parties.
// This supplies candidate identities, not their final TCP shares. The ordinary
// and live simulations share this calculation; final TPP/TCP shares are supplied
// separately by SimulationIteration::determineSeatFinalResult.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace CoalitionPairing {

using PartyShare = std::pair<int, float>;
using PartyShares = std::vector<PartyShare>;

enum class Status { NotApplicable, OnePartnerRemains, BothFinalists, Invalid };

struct Decision {
	Status status = Status::NotApplicable;
	// Used only for OnePartnerRemains. Keep the combined Coalition identity in
	// preference calculations until the final result is labelled with this party.
	int survivingParty = -1;
};

// This is a deliberately broad modelling assumption, not a fitted coefficient.
// Incoming Coalition preferences divide around 50:50, with the central 90% of
// National fractions about 25--75%. Varying log(fraction / (1-fraction)) keeps
// draws between zero and one, unlike adding an unbounded error to a percentage.
// See docs/coalition-pairing.md for the scope of the historical checks.
inline constexpr float IncomingSplitLogOddsSd = 0.6679f;

inline float nationalIncomingFraction(float logOddsDraw)
{
	return float(1.0 / (1.0 + std::exp(-double(logOddsDraw))));
}

// original/excluded contain party identities and original FP percentage shares
// of the seat's formal votes. "original" includes the still-combined Coalition;
// "excluded" initially contains anonymous Others, which cannot be a finalist.
// coalitionParty is also the eventual Liberal identity. nationalPrimaryFraction
// is the already-simulated National fraction of that combined primary (0--1),
// including any live FP integration; this helper never changes those primaries.
//
// allocatePreferences supplies the existing preference/exhaustion calculation:
// it adds excluded original primaries to a private copy of the remaining groups.
// sampleIncomingFraction supplies one National fraction (0--1) of the combined
// incoming preferences, held fixed through every exclusion in this scenario.
// It is called only when both Coalition candidates have positive primaries.
//
// The returned decision either selects both partners or identifies the survivor
// for the usual combined-Coalition calculation. No live TCP pair is supplied:
// an indicative count is evidence about shares conditional on a pair, not proof
// that those candidates survive. The caller handles authoritative final results
// before this step. Invalid means a broken numerical account, not an unlikely
// but valid election outcome; the simulation should reject that iteration.
template<typename SampleIncomingFraction, typename AllocatePreferences>
Decision determine(
	PartyShares const& original, PartyShares const& excluded,
	int coalitionParty, int nationalParty, float nationalPrimaryFraction,
	SampleIncomingFraction const& sampleIncomingFraction,
	AllocatePreferences const& allocatePreferences)
{
	if (nationalParty < 0) return {};
	if (nationalParty == coalitionParty ||
		!std::isfinite(nationalPrimaryFraction) ||
		nationalPrimaryFraction < 0.0f || nationalPrimaryFraction > 1.0f) {
		return {Status::Invalid};
	}
	auto const coalition = std::find_if(original.begin(), original.end(),
		[&](PartyShare const& entry) { return entry.first == coalitionParty; });
	if (coalition == original.end()) return {};
	float const combinedPrimary = coalition->second;
	if (!std::isfinite(combinedPrimary) || combinedPrimary < 0.0f) {
		return {Status::Invalid};
	}
	float const nationalPrimary = combinedPrimary * nationalPrimaryFraction;
	float const liberalPrimary = combinedPrimary - nationalPrimary;
	// The common one-candidate/merged-party case keeps its existing path, without
	// an extra draw or working copies. A small but positive partner is still a
	// candidate: there is no arbitrary primary threshold for being considered.
	if (nationalPrimary <= 0.0f || liberalPrimary <= 0.0f) return {};

	float const nationalIncoming = sampleIncomingFraction();
	if (!std::isfinite(nationalIncoming) ||
		nationalIncoming < 0.0f || nationalIncoming > 1.0f) {
		return {Status::Invalid};
	}
	auto remaining = original;
	auto excludedPrimaries = excluded;
	while (remaining.size() >= 2) {
		auto allocated = remaining;
		allocatePreferences(allocated, excludedPrimaries);
		auto const group = std::find_if(allocated.begin(), allocated.end(),
			[&](PartyShare const& entry) { return entry.first == coalitionParty; });
		if (group == allocated.end()) return {Status::Invalid};
		float const incomingPreferences = group->second - combinedPrimary;
		if (!std::isfinite(incomingPreferences) || incomingPreferences < 0.0f) {
			return {Status::Invalid};
		}

		// Preserve own primaries and divide only the additional Coalition votes.
		// For example, LIB 35 + NAT 25 receiving 10 points becomes 40/30 under
		// an even split, not 35/35. The group's total receiving weight is unchanged.
		float const liberal = liberalPrimary + (1.0f - nationalIncoming) * incomingPreferences;
		float const national = nationalPrimary + nationalIncoming * incomingPreferences;
		auto weakestOutside = allocated.end();
		for (auto candidate = allocated.begin(); candidate != allocated.end(); ++candidate) {
			if (!std::isfinite(candidate->second) || candidate->second < 0.0f) {
				return {Status::Invalid};
			}
			if (candidate->first != coalitionParty &&
				(weakestOutside == allocated.end() || candidate->second < weakestOutside->second)) {
				weakestOutside = candidate;
			}
		}
		if (weakestOutside == allocated.end()) return {Status::Invalid};

		if (std::min(liberal, national) <= weakestOutside->second) {
			// Once one partner is excluded, hand back to the existing combined
			// treatment against outsiders. Adding new leakage/exhaustion from
			// that partner would change a second modelling assumption. Its
			// surviving identity can differ from the original primary leader.
			// Exact ties retain the existing preference for the Liberal label
			// between partners; a tie with an outsider does not promote LIB/NAT.
			return {Status::OnePartnerRemains, liberal >= national ? coalitionParty : nationalParty};
		}
		if (remaining.size() == 2) return {Status::BothFinalists};

		// Both partners beat the weakest outside candidate. Redistribute that
		// candidate's ORIGINAL primaries next time, together with those already
		// excluded. Reusing its accumulated pile would count preferences twice.
		int const excludedParty = weakestOutside->first;
		auto const originalCandidate = std::find_if(remaining.begin(), remaining.end(),
			[&](PartyShare const& entry) { return entry.first == excludedParty; });
		if (originalCandidate == remaining.end()) return {Status::Invalid};
		excludedPrimaries.push_back(*originalCandidate);
		remaining.erase(originalCandidate);
	}
	return {Status::Invalid};
}

} // namespace CoalitionPairing
