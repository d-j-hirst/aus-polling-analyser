#include "../MajorPartyFpMath.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace {

	// Fictional seat shares. Normalisation preserves the locally established
	// candidate and redistributes the rest between major and other minor votes.
	struct Seat {
		float major;
		float protectedMinor;
		float otherMinor;
		float turnout;
	};

	template<std::size_t N>
	float normalisedMajorShare(std::array<Seat, N> const& seats, float factor)
	{
		double majorVotes = 0.0;
		double turnout = 0.0;
		for (auto const& seat : seats) {
			float const adjusted = seat.major * factor;
			float const share = adjusted * (100.0f - seat.protectedMinor) /
				(adjusted + seat.otherMinor);
			majorVotes += double(share) * seat.turnout;
			turnout += seat.turnout;
		}
		return float(majorVotes / turnout);
	}

	void checkReachableTargets()
	{
		std::array<Seat, 3> const seats{{
			{60.0f, 30.0f, 10.0f, 1000.0f},
			{70.0f, 20.0f, 10.0f, 3000.0f},
			{40.0f, 45.0f, 15.0f, 2000.0f}}};
		auto const projected = [&](float factor) {
			return normalisedMajorShare(seats, factor);
		};
		float const current = projected(1.0f);
		// Exercise the common case across a broad range of targets, using the
		// same closed-form starting factor as the simulator. The final share
		// must reflect separate seat normalisation and turnout weighting.
		for (float target : {5.0f, 20.0f, 40.0f, 55.0f, 65.0f, 69.0f}) {
			float const initial = target * (current - 100.0f) /
				(current * (target - 100.0f));
			auto const result = MajorPartyFpMath::findNormalisedCorrection(
				target, 0.0001f, initial, projected);
			assert(result.status == MajorPartyFpMath::SearchStatus::Matched);
			assert(std::isfinite(result.factor) && result.factor > 0.0f);
			assert(std::abs(projected(result.factor) - target) < 0.001f);
		}
	}

	void checkProtectedLocalSupport()
	{
		std::array<Seat, 1> const seats{{{60.0f, 30.0f, 10.0f, 1000.0f}}};
		auto const projected = [&](float factor) {
			return normalisedMajorShare(seats, factor);
		};
		// No finite multiplier can put 75% with the major parties while keeping
		// 30% with the protected candidate. The previous bracket check threw for
		// this valid account; the caller must now hand it to its terminal solver
		// without modifying the incoming seat shares or retrying the scenario.
		auto const constrained = MajorPartyFpMath::findNormalisedCorrection(
			75.0f, 0.0001f, 2.0f, projected);
		assert(constrained.status == MajorPartyFpMath::SearchStatus::Constrained);
		assert(constrained.upperShare < 70.0f && constrained.upperShare < 75.0f);
		assert(projected(1.0f) == 60.0f);

		// A lower target is reachable in the same seat. Keeping that candidate
		// fixed must not prevent the ordinary correction from being applied.
		auto const reachable = MajorPartyFpMath::findNormalisedCorrection(
			65.0f, 0.0001f, 1.25f, projected);
		assert(reachable.status == MajorPartyFpMath::SearchStatus::Matched);
		assert(std::abs(reachable.factor - 13.0f / 6.0f) < 0.00001f);

		// The positivity bound from preference compensation can also put the
		// minimum attainable share above a low target. This is a constraint,
		// provided the evaluated accounts themselves remain numerically valid.
		auto const lowerConstraint = MajorPartyFpMath::findNormalisedCorrection(
			20.0f, 0.5f, 1.0f, projected);
		assert(lowerConstraint.status == MajorPartyFpMath::SearchStatus::Constrained);
		assert(lowerConstraint.lowerShare > 20.0f);
	}

	void checkInvalidCalculations()
	{
		float const nan = std::numeric_limits<float>::quiet_NaN();
		float const infinity = std::numeric_limits<float>::infinity();
		auto const valid = [](float factor) { return 100.0f * factor / (1.0f + factor); };
		for (float target : {-1.0f, 0.0f, 100.0f, nan, infinity}) {
			assert(MajorPartyFpMath::findNormalisedCorrection(
				target, 0.0001f, 1.0f, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		for (float badFactor : {-1.0f, 0.0f, nan, infinity}) {
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, badFactor, 1.0f, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, 0.0001f, badFactor, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		for (float badShare : {-1.0f, 101.0f, nan, infinity}) {
			auto const bad = [badShare](float) { return badShare; };
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, 0.0001f, 1.0f, bad).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		// Failures part way through bracket expansion or binary search remain
		// failures, even when the endpoints examined so far were valid.
		assert(MajorPartyFpMath::findNormalisedCorrection(
			90.0f, 0.0001f, 1.0f, [&](float factor) {
				return factor > 1.0f ? nan : valid(factor);
			}).status == MajorPartyFpMath::SearchStatus::Invalid);
		assert(MajorPartyFpMath::findNormalisedCorrection(
			50.0f, 0.0001f, 2.0f, [&](float factor) {
				return factor > 0.25f && factor < 1.75f ? -1.0f : valid(factor);
			}).status == MajorPartyFpMath::SearchStatus::Invalid);
	}
}

int main()
{
	checkReachableTargets();
	checkProtectedLocalSupport();
	checkInvalidCalculations();
}
