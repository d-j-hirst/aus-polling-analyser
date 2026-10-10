#pragma once

#include <algorithm>
#include <cmath>

// Reconciliation normally finds a multiplier that brings the combined major-
// party FP share back to its election-wide target after each seat is normalised.
// A protected local candidate can make that target unattainable. Distinguish
// this ordinary modelling constraint from a genuinely invalid numerical state.
namespace MajorPartyFpMath {

	enum class SearchStatus { Matched, Constrained, Invalid };

	struct SearchResult {
		SearchStatus status = SearchStatus::Invalid;
		float factor = 1.0f;
		float lowerShare = 0.0f;
		float upperShare = 0.0f;
	};

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
