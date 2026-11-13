#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

// Use classic counts to relate major-party first preferences to TPP in one
// district, and find adjustments to match the overall primary-vote forecast.
namespace MajorPartyFpMath {
	// One historical compulsory-preferential count, with the actual Coalition
	// finalist and all votes measured on the same formal ballots. The range
	// for the excluded partner's votes reaching Labor can be exact, bounded
	// by a published mixed transfer, or as broad as its entire primary vote.
	struct HistoricalClassicVotes {
		int formal;
		int laborPrimary;
		int coalitionFinalistPrimary;
		int excludedPartnerPrimary;
		int laborTpp;
		int minimumPartnerToLabor;
		int maximumPartnerToLabor;
	};

	struct MinorPreferenceRange {
		double lower;
		double upper;

		// The count supports a correction only where the expected fraction
		// lies outside the range that the unknown source can explain.
		double supportedDifference(double expected) const
		{
			return std::clamp(expected, lower, upper) - expected;
		}
	};

	// Work out the fraction of original minor-party and independent ballots
	// that can have reached Labor. Removing the other Coalition candidate's
	// contribution lets the next forecast carry over a local preference
	// difference even if that candidate no longer stands. Missing or
	// irreconcilable counts cannot supply a local correction.
	inline std::optional<MinorPreferenceRange> historicalMinorPreferenceRange(HistoricalClassicVotes const& votes)
	{
		if (votes.laborPrimary <= 0 || votes.coalitionFinalistPrimary <= 0 ||
			votes.excludedPartnerPrimary < 0 || votes.laborTpp < votes.laborPrimary ||
			votes.laborTpp > votes.formal || votes.minimumPartnerToLabor < 0 ||
			votes.maximumPartnerToLabor > votes.excludedPartnerPrimary) {
			return std::nullopt;
		}
		std::int64_t const minorPrimary = std::int64_t(votes.formal) - votes.laborPrimary -
			votes.coalitionFinalistPrimary - votes.excludedPartnerPrimary;
		if (minorPrimary <= 0) return std::nullopt;
		int const laborPreferences = votes.laborTpp - votes.laborPrimary;
		// At least this many ballots must have come from the excluded partner
		// if the total going to Labor exceeds the entire minor-party pool.
		int const lowerPartner = std::max(votes.minimumPartnerToLabor, laborPreferences - int(minorPrimary));
		int const upperPartner = std::min(votes.maximumPartnerToLabor, laborPreferences);
		if (lowerPartner > upperPartner) return std::nullopt;
		return MinorPreferenceRange{
			double(laborPreferences - upperPartner) / double(minorPrimary),
			double(laborPreferences - lowerPartner) / double(minorPrimary)};
	}

	// With both Coalition candidates standing, the comparison uses the same
	// quantity of ballots reaching Labor for either hypothetical classic pair.
	// It is 15.4% of the smaller partner's primary, borrowing the scale of the
	// earlier split-change adjustment. This is a modelling assumption for a
	// single simulated TPP, independent of measured historical flows.
	inline constexpr double ClassicPartnerLeakageRate = 0.154;

	// Convert the National fraction of Coalition FP into the fraction of
	// the combined Coalition primary that reaches Labor.
	inline double coalitionLeakageFraction(double nationalFraction)
	{
		return ClassicPartnerLeakageRate * std::min(nationalFraction, 1.0 - nationalFraction);
	}

	// All vote shares are percentage points of formal votes. The excluded
	// partner's contribution to Labor is a fraction of the combined Coalition
	// primary, which is what remains of the major vote after Labor's primary.
	// Solve TPP = Labor FP + minor preferences + that Coalition contribution.
	inline double classicLaborPrimary(double laborTpp, double combinedMajorPrimary,
		double minorPreferencesToLabor, double coalitionLeakageFraction)
	{
		return (laborTpp - minorPreferencesToLabor - coalitionLeakageFraction * combinedMajorPrimary) /
			(1.0 - coalitionLeakageFraction);
	}

	// The aggregate FP correction can move votes between the two major sides.
	// Find the closest split that still allows the district's TPP, holding the
	// combined major primary and every minor primary fixed. The endpoints
	// come from sending none or all of the minor ballots to Labor. An empty
	// interval means those fixed inputs do not permit both minimum primaries.
	inline std::optional<double> compatibleClassicLaborPrimary(double laborPrimary,
		double combinedMajorPrimary, double laborTpp, double coalitionLeakageFraction,
		double minimumMajorPrimary)
	{
		if (!std::isfinite(laborPrimary) || !std::isfinite(combinedMajorPrimary) ||
			!std::isfinite(laborTpp) || !std::isfinite(coalitionLeakageFraction)) return std::nullopt;
		double const minorPrimary = 100.0 - combinedMajorPrimary;
		double const lower = std::max(minimumMajorPrimary,
			classicLaborPrimary(laborTpp, combinedMajorPrimary, minorPrimary, coalitionLeakageFraction));
		double const upper = std::min(combinedMajorPrimary - minimumMajorPrimary,
			classicLaborPrimary(laborTpp, combinedMajorPrimary, 0.0, coalitionLeakageFraction));
		if (lower > upper) return std::nullopt;
		return std::clamp(laborPrimary, lower, upper);
	}

	enum class SearchStatus { Matched, Constrained, Invalid };

	struct SearchResult {
		SearchStatus status = SearchStatus::Invalid;
		float factor = 1.0f;
		float lowerShare = 0.0f;
		float upperShare = 0.0f;
	};

	// Find a multiplier that brings the combined major-party FP share back
	// to its election-wide target after each district is normalised. A
	// protected local candidate can make that target unattainable with valid
	// votes, which is different from a genuinely invalid numerical state.
	template<typename ProjectedShare>
	SearchResult findNormalisedCorrection(
		float target,
		float minimumFactor,
		float initialFactor,
		ProjectedShare projectedShare)
	{
		// The caller supplies the real seat-normalisation calculation. Invalid
		// shares, including its negative failure sentinel, must still reject the
		// scenario rather than being mistaken for an unreachable valid target.
		auto const validShare = [](float share) {
			return std::isfinite(share) && share >= 0.0f && share <= 100.0f;
		};
		if (!std::isfinite(target) || target <= 0.0f || target >= 100.0f ||
			!std::isfinite(minimumFactor) || minimumFactor <= 0.0f ||
			!std::isfinite(initialFactor) || initialFactor <= 0.0f) {
			return {};
		}

		float lowerFactor = minimumFactor;
		float upperFactor = std::max({1.0f, initialFactor, minimumFactor});
		SearchResult result;
		result.lowerShare = projectedShare(lowerFactor);
		result.upperShare = projectedShare(upperFactor);
		if (!validShare(result.lowerShare) || !validShare(result.upperShare)) {
			return result;
		}

		// Retain the existing bounded search: do not chase an asymptote with
		// ever larger multipliers when protected candidates limit the available
		// vote. The terminal reconciliation stage can then adjust eligible minor
		// categories and keep its best complete, TPP-compatible solution.
		constexpr float UpperExpansionLimit = 100.0f;
		while (result.upperShare < target && upperFactor < UpperExpansionLimit) {
			upperFactor *= 2.0f;
			result.upperShare = projectedShare(upperFactor);
			if (!validShare(result.upperShare)) return result;
		}
		if (result.lowerShare > target || result.upperShare < target) {
			result.status = SearchStatus::Constrained;
			return result;
		}

		constexpr int SearchIterations = 20;
		for (int i = 0; i < SearchIterations; ++i) {
			float const midpoint = (lowerFactor + upperFactor) * 0.5f;
			float const midpointShare = projectedShare(midpoint);
			if (!validShare(midpointShare)) return result;
			if (midpointShare < target) lowerFactor = midpoint;
			else upperFactor = midpoint;
		}
		result.status = SearchStatus::Matched;
		result.factor = (lowerFactor + upperFactor) * 0.5f;
		return result;
	}
}
