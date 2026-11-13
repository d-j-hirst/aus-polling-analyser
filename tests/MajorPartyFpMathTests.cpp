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

	void checkHistoricalCoalitionPreferences()
	{
		auto const close = [](double a, double b) { return std::abs(a - b) < 1e-12; };
		// Euroa 2022 ended Labor/National. The Liberal exclusion sent 1,353
		// ballots to Labor, at most 1,172 of which had come from other piles.
		auto const euroa = MajorPartyFpMath::historicalMinorPreferenceRange({
			42242, 12840, 13496, 9227, 16926, 181, 1353});
		assert(euroa && close(euroa->lower, 2733.0 / 6679) && close(euroa->upper, 3905.0 / 6679));
		assert(euroa->supportedDifference(0.50) == 0.0);
		assert(close(euroa->supportedDifference(0.65), euroa->upper - 0.65));

		// Bass reverses the roles. The 5,506 National primaries are the
		// excluded partner's original votes in its Labor/Liberal count.
		auto const bass = MajorPartyFpMath::historicalMinorPreferenceRange({
			41404, 13478, 12482, 5506, 20803, 0, 2700});
		assert(bass && close(bass->lower, 4625.0 / 9938) && close(bass->upper, 7325.0 / 9938));

		// The separate Labor/National scrutiny in Mallee identifies exactly
		// 2,152 original Liberal ballots reaching Labor.
		auto const mallee = MajorPartyFpMath::historicalMinorPreferenceRange({
			85834, 15020, 33270, 23363, 22610, 2152, 2152});
		assert(mallee && close(mallee->lower, 5438.0 / 14181) && mallee->lower == mallee->upper);

		// An unknown partner split can still establish an upper bound. A
		// seat without an excluded partner yields a single measured rate.
		auto const shepparton = MajorPartyFpMath::historicalMinorPreferenceRange({
			41301, 3285, 13242, 8491, 10597, 0, 8491});
		assert(shepparton && shepparton->lower == 0.0 && close(shepparton->upper, 7312.0 / 16283));
		auto const nationalOnly = MajorPartyFpMath::historicalMinorPreferenceRange({
			1000, 250, 600, 0, 320, 0, 0});
		assert(nationalOnly && close(nationalOnly->lower, 70.0 / 150));
		assert(nationalOnly->lower == nationalOnly->upper);
		assert(!MajorPartyFpMath::historicalMinorPreferenceRange({1000, 250, 600, 50, 500, 0, 50}));
		assert(!MajorPartyFpMath::historicalMinorPreferenceRange({1000, 250, 600, 50, 320, 45, 40}));
	}

	void checkClassicPrimaryAllocation()
	{
		constexpr double tolerance = 1e-12;
		// Reconstruct the first incompatible now-cast examples in Euroa,
		// Morwell and Mildura using the old split transfer in reverse. With
		// the single National candidate, the new primary equation allocates
		// the entire preference requirement to the minor pool. Check that a
		// later adjustment is also limited by the fixed TPP.
		struct Example {
			double laborTpp;
			double combinedMajor;
			double minorPreferences;
			double oldLaborPrimary;
		};
		for (auto const& example : std::array{
			Example{29.523, 55.126, 1.493, 29.725},
			Example{28.062, 52.533, 0.47467, 29.139},
			Example{23.819, 70.971, 2.469, 24.598}}) {
			double const labor = MajorPartyFpMath::classicLaborPrimary(
				example.laborTpp, example.combinedMajor, example.minorPreferences, 0.0);
			assert(std::abs(labor + example.minorPreferences - example.laborTpp) < tolerance);
			assert(labor < example.oldLaborPrimary);
			auto const corrected = MajorPartyFpMath::compatibleClassicLaborPrimary(
				example.oldLaborPrimary, example.combinedMajor, example.laborTpp, 0.0, 0.1);
			assert(corrected && *corrected == example.laborTpp);
		}

		// With both running, this quantity is paid from the Coalition primary
		// once. Swapping Liberal and National leaves the smaller primary and
		// the hypothetical Labor/Coalition TPP unchanged.
		for (double nationalFraction : {0.0, 0.32, 0.5, 0.68, 1.0}) {
			double const leakageFraction = MajorPartyFpMath::coalitionLeakageFraction(nationalFraction);
			double const laborTpp = 29.0 + 10.0 + leakageFraction * 41.0;
			double const primary = MajorPartyFpMath::classicLaborPrimary(laborTpp, 70.0, 10.0, leakageFraction);
			assert(std::abs(primary - 29.0) < tolerance);
			auto const same = MajorPartyFpMath::compatibleClassicLaborPrimary(primary, 70.0, laborTpp, leakageFraction, 0.1);
			assert(same && *same == primary);
			auto const upper = MajorPartyFpMath::compatibleClassicLaborPrimary(55.0, 70.0, laborTpp, leakageFraction, 0.1);
			assert(upper && std::abs(*upper + (70.0 - *upper) * leakageFraction - laborTpp) < tolerance);
			auto const lower = MajorPartyFpMath::compatibleClassicLaborPrimary(1.0, 70.0, laborTpp, leakageFraction, 0.1);
			assert(lower && std::abs(*lower + (70.0 - *lower) * leakageFraction + 30.0 - laborTpp) < tolerance);
		}
		assert(!MajorPartyFpMath::compatibleClassicLaborPrimary(0.1, 90.0, 2.0, 0.077, 0.1));
	}
}

int main()
{
	checkReachableTargets();
	checkProtectedLocalSupport();
	checkInvalidCalculations();
	checkHistoricalCoalitionPreferences();
	checkClassicPrimaryAllocation();
}
