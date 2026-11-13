#pragma once

// Form local minor-party FP support from the wider party model and explicit
// candidate-profile assessments. SimulationIteration uses the centrist/populist
// baseline before seat polls and its prominent-candidate supplements afterwards.
// The viability scale is shared with the existing Greens/independent calculations.
// All shares still pass through normalization, statewide FP reconciliation and
// any subsequent integration of live votes.

#include "General.h"

namespace MinorPartyFpMath {

inline constexpr double ProminentMinorFlatBonus = 5.0;
inline constexpr double ProminentMinorFlatBonusThreshold = 10.0;
inline constexpr double ProminentMinorBonusMax = 35.0;

// Viability is an operator-supplied assessment of a candidate's local profile,
// not a probability or a poll percentage. Zero means the ordinary party model;
// +1 multiplies the relevant support quantity by 1.4, and -1 by 0.6. The 0.4
// coefficient is inherited from the Greens/independent treatment, not fitted
// separately to other parties. Returning unsuccessful INDs retain their existing
// exemption because their previous primary already describes their profile.
inline double viabilityMultiplier(double viability)
{
	return 1.0 + (0.4 * viability);
}

// Produce a centrist/populist candidate's starting FP percentage for one
// possible election. effectivePartyFp is the election-wide support draw (or
// the odds-calibration substitute); adjustedSeatModifier distributes support
// geographically and accounts for the seats contested. incumbentFp is the
// previous local primary for an incumbent, or zero for other candidates.
//
// Preserve the existing choice of the lower of a proportional local change and
// its bounded transformed-share calculation, then raise it to at least the
// incumbent's previous primary. That starting floor can exceed both projections;
// subsequent viability, regional and local effects can still lower the share.
// Apply the candidate's explicit viability
// assessment to that local baseline, not to the party's statewide target or its
// probability of contesting. Thus 15% and viability +1 becomes 21% here, before
// regional/local variation, polls and subsequent reconciliation.
inline double populistBaseline(
	double effectivePartyFp, double adjustedSeatModifier,
	double incumbentFp, double viability)
{
	double const transformedProjection = predictorCorrectorTransformedSwing<double>(
		effectivePartyFp, effectivePartyFp * (adjustedSeatModifier - 1.0));
	double const proportionalProjection = effectivePartyFp * adjustedSeatModifier;
	double const localBaseline = std::max<double>(incumbentFp,
		std::min<double>(transformedProjection, proportionalProjection));
	// Zero/negative support cannot be transformed to finite log-odds. Retain
	// the existing 0.01--99% numerical bounds after applying viability; these
	// do not establish candidacy or a guaranteed final minimum primary vote.
	return std::clamp<double>(
		localBaseline * viabilityMultiplier(viability), 0.01, 99.0);
}

// Prominent candidates receive the existing small-primary supplement and a
// variable additional boost. percentage is the generated local FP share after
// regional/local variation and seat polls; uniform1/2 are the caller's existing
// independent 0--1 draws. This function draws no new randomness.
//
// Scale the flat supplement by the viability multiplier and the variable boost
// by its square, matching the existing prominent-candidate treatment in
// determineSpecificPartyFp. Keep this path's existing additive flat supplement
// (the continuing-candidate path uses a bounded adjustment instead). These bonus
// constants are inherited assumptions. A missing/zero rating reproduces the
// existing bonuses, while extreme negative ratings cannot turn a bonus negative
// or make its squared multiplier grow again.
inline double prominentPopulistFp(
	double percentage, double viability, double uniform1, double uniform2)
{
	double const multiplier = std::max<double>(0.0, viabilityMultiplier(viability));
	percentage += (1.0 - std::clamp<double>(
		percentage / ProminentMinorFlatBonusThreshold, 0.0, 1.0)) *
		ProminentMinorFlatBonus * multiplier;
	return predictorCorrectorTransformedSwing<double>(percentage,
		uniform1 * uniform2 * ProminentMinorBonusMax * multiplier * multiplier);
}

} // namespace MinorPartyFpMath
