#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace LiveSimulationMath {

constexpr double EvidenceEpsilon = 0.0000001;

// The live hierarchy uses this confidence response when accepting observed
// movement. Keep the same response available to source-specific uncertainty:
// many votes with comparable history should have more influence than one
// small booth. This is distinct from the simulator's baseline blend below.
inline double observationWeight(double confidence, double strength = 250.0)
{
	if (!std::isfinite(confidence) || !std::isfinite(strength) || strength < 0.0)
		throw std::runtime_error("Cannot weight invalid live-election evidence.");
	double const boundedConfidence = std::clamp(confidence, 0.0, 1.0);
	return std::min(1.0, 1.01 - 1.01 /
		(1.0 + std::pow(boundedConfidence, 1.6) * strength));
}

// Normalize the existing live-evidence sigmoid so its endpoints are exact.
inline double evidenceWeight(double effectiveConfidence, double slope = 12.0)
{
	if (effectiveConfidence <= EvidenceEpsilon) return 0.0;
	if (effectiveConfidence >= 1.0 - EvidenceEpsilon) return 1.0;

	auto logistic = [slope](double confidence) {
		return 1.0 /
			(1.0 + std::exp(-(slope * confidence - 0.5)));
	};
	double const zero = logistic(0.0);
	double const one = logistic(1.0);
	return std::clamp(
		(logistic(effectiveConfidence) - zero) / (one - zero),
		0.0, 1.0);
}

struct ActivationResult {
	double partyShare = 0.0;
	double sourceShare = 0.0;
	bool requiresNormalisation = false;
};

// Introduce a party continuously from zero, drawing from its natural source
// category before asking the caller to normalize any remaining increase.
inline ActivationResult activateFromSource(
	double targetShare,
	double weight,
	double sourceShare,
	double sourceReserve = 0.0)
{
	if (weight <= EvidenceEpsilon || targetShare <= 0.0) {
		return {0.0, sourceShare, false};
	}

	double const partyShare =
		std::clamp(targetShare, 0.0, 100.0) *
		std::clamp(weight, 0.0, 1.0);
	double const availableSource = std::max(
		0.0,
		sourceShare - std::clamp(sourceReserve, 0.0, sourceShare));
	double const transferred = std::min(availableSource, partyShare);
	return {
		partyShare,
		sourceShare - transferred,
		transferred + EvidenceEpsilon < partyShare,
	};
}

// Treat an activated party as multiple generic candidates when its live target
// is stronger than their implied average. This preserves a larger share for the
// remaining candidates without assuming that all candidates are equally strong.
inline double othersReserve(
	double priorOthersShare,
	int remainingCandidateCount,
	std::vector<double> const& activationShares)
{
	priorOthersShare = std::max(0.0, priorOthersShare);
	remainingCandidateCount = std::max(0, remainingCandidateCount);
	if (priorOthersShare <= EvidenceEpsilon ||
		remainingCandidateCount == 0 || activationShares.empty()) {
		return 0.0;
	}

	double const genericBaseline = priorOthersShare /
		double(remainingCandidateCount + activationShares.size());
	double activationUnits = 0.0;
	for (double activationShare : activationShares) {
		activationUnits += std::max(
			1.0,
			std::max(0.0, activationShare) / genericBaseline);
	}
	return priorOthersShare * double(remainingCandidateCount) /
		(double(remainingCandidateCount) + activationUnits);
}

} // namespace LiveSimulationMath
