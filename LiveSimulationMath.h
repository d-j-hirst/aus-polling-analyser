#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace LiveSimulationMath {

constexpr float EvidenceEpsilon = 0.0000001f;

// Normalize the existing live-evidence sigmoid so its endpoints are exact.
inline float evidenceWeight(float effectiveConfidence, float slope = 12.0f)
{
	if (effectiveConfidence <= EvidenceEpsilon) return 0.0f;
	if (effectiveConfidence >= 1.0f - EvidenceEpsilon) return 1.0f;

	auto logistic = [slope](float confidence) {
		return 1.0f /
			(1.0f + std::exp(-(slope * confidence - 0.5f)));
	};
	float const zero = logistic(0.0f);
	float const one = logistic(1.0f);
	return std::clamp(
		(logistic(effectiveConfidence) - zero) / (one - zero),
		0.0f, 1.0f);
}

struct ActivationResult {
	float partyShare = 0.0f;
	float sourceShare = 0.0f;
	bool requiresNormalisation = false;
};

// Introduce a party continuously from zero, drawing from its natural source
// category before asking the caller to normalize any remaining increase.
inline ActivationResult activateFromSource(
	float targetShare,
	float weight,
	float sourceShare,
	float sourceReserve = 0.0f)
{
	if (weight <= EvidenceEpsilon || targetShare <= 0.0f) {
		return {0.0f, sourceShare, false};
	}

	float const partyShare =
		std::clamp(targetShare, 0.0f, 100.0f) *
		std::clamp(weight, 0.0f, 1.0f);
	float const availableSource = std::max(
		0.0f,
		sourceShare - std::clamp(sourceReserve, 0.0f, sourceShare));
	float const transferred = std::min(availableSource, partyShare);
	return {
		partyShare,
		sourceShare - transferred,
		transferred + EvidenceEpsilon < partyShare,
	};
}

// Treat an activated party as multiple generic candidates when its live target
// is stronger than their implied average. This preserves a larger share for the
// remaining candidates without assuming that all candidates are equally strong.
inline float othersReserve(
	float priorOthersShare,
	int remainingCandidateCount,
	std::vector<float> const& activationShares)
{
	priorOthersShare = std::max(0.0f, priorOthersShare);
	remainingCandidateCount = std::max(0, remainingCandidateCount);
	if (priorOthersShare <= EvidenceEpsilon ||
		remainingCandidateCount == 0 || activationShares.empty()) {
		return 0.0f;
	}

	float const genericBaseline = priorOthersShare /
		float(remainingCandidateCount + activationShares.size());
	float activationUnits = 0.0f;
	for (float activationShare : activationShares) {
		activationUnits += std::max(
			1.0f,
			std::max(0.0f, activationShare) / genericBaseline);
	}
	return priorOthersShare * float(remainingCandidateCount) /
		(float(remainingCandidateCount) + activationUnits);
}

} // namespace LiveSimulationMath
