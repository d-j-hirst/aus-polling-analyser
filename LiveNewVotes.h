#pragma once

#include "LivePartialCount.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

// Estimate the composition of voters whose first preferences have not arrived.
// Count sizes belong to turnout; this component changes only their composition.
// Keep these common parameters here so applying the method needs no private
// calibration files, election-specific branches or activation settings.
namespace LiveNewVotes {
using Shares = std::map<int,double>; // Fractions; eligible candidates only.
inline constexpr double HalfVote = .5;
inline constexpr double EarlyRemainingMultiplier = 4;
inline constexpr double EarlyEvidenceVotes = 300;
inline constexpr double PostalCoalitionLogOddsShift = -2.0/25;
// Independents share a modelling slot, but are separate local campaigns. A
// one-percent candidate's large proportional change says much less about a
// competitive independent elsewhere than a comparable established campaign.
// Five percent is the half-weight share, not an eligibility cutoff. Freeze the
// share at the pre-count baseline so a surprising first booth cannot promote
// itself into a strong source of movement for other districts.
inline constexpr double IndependentPoolingHalfWeightShare = .05;
enum class Category { General, AggregatedEarly };

inline double smoothedShare(double votes, double total, std::size_t candidates) {
    // A genuine observed zero in a tiny count is weak evidence. Half a vote
    // keeps its log odds finite and fades with count size. Integer records and
    // structurally absent candidates are never changed by this calculation.
    return (votes+HalfVote)/(total+HalfVote*double(candidates));
}

inline double observedWeight(double counted, double expected, Category category) {
    if (counted <= 0 || expected <= 0) return 0;
    if (category == Category::AggregatedEarly)
        // Early batches may come from different geographic areas. Their own
        // mix replaces the context more slowly while many voters remain.
        return counted/(counted+EarlyRemainingMultiplier*std::max(0.0,expected-counted)+EarlyEvidenceVotes);
    return std::clamp(counted/expected,0.0,1.0);
}

inline Shares normalize(Shares shares) {
    double sum = 0;
    for (auto const& [candidate,share] : shares) {
        if (!std::isfinite(share) || share < 0)
            throw std::runtime_error("Invalid future-voter primary composition.");
        sum += share;
    }
    if (!std::isfinite(sum) || sum <= 0)
        throw std::runtime_error("No positive future-voter primary composition.");
    for (auto& [candidate,share] : shares) share /= sum;
    return shares;
}

inline Shares composition(LivePartialCount::Counts const& counted, Shares const& context,
    double expected, Category category, Shares const& observedOffsets = {}, Shares const& variation = {}) {
    double n = LivePartialCount::total(counted);
    double weight = observedWeight(n,expected,category);
    Shares result;
    // The prepared context carries the current ballot identities. Do not add
    // historical candidates or invented zero-count candidates to that ballot.
    for (auto const& [candidate,prior] : context) {
        double transformed = LivePartialCount::logOdds(prior);
        if (weight > 0) {
            double observed = smoothedShare(LivePartialCount::value(counted,candidate),n,context.size());
            double shifted = LivePartialCount::logOdds(observed);
            if (observedOffsets.contains(candidate)) shifted += observedOffsets.at(candidate);
            transformed += weight*(shifted-transformed);
        }
        if (variation.contains(candidate)) transformed += variation.at(candidate);
        result[candidate] = LivePartialCount::inverseLogOdds(transformed);
    }
    return normalize(std::move(result));
}

inline Shares project(LivePartialCount::Counts const& counted, Shares const& future, double expected) {
    double additions = std::max(0.0,expected-LivePartialCount::total(counted));
    Shares result;
    for (auto const& [candidate,votes] : counted) result[candidate] = votes;
    for (auto const& [candidate,share] : future) result[candidate] += additions*share;
    return result;
}

inline double firstShare(Shares const& primaries, LivePartialCount::Flows const& flows, double offset) {
    double first = 0;
    for (auto const& [candidate,share] : primaries)
        first += share*LivePartialCount::preferenceRate(candidate,flows,offset);
    return first;
}

// Primary-movement evidence has a different denominator from count completion.
// A complete ordinary booth normally supplies its full observed size. A tiny
// partial record must never acquire its much larger historical booth's weight.
struct FpEvidence {
    double expected = 0, counted = 0, confidence = 0, relevance = 1;
    std::map<int,float> deviations, specific;
    // Effective observed votes for cross-district pooling only. An explicit
    // zero means no evidence for that candidate, even if other parties have
    // reported. Carry these votes upward without borrowing general confidence
    // or applying the source-share discount again at each level.
    std::map<int,double> poolingVotes;
};
inline double evidenceWeight(FpEvidence const& evidence) {
    return std::min(evidence.expected*evidence.confidence,evidence.counted)*evidence.relevance;
}
inline double independentPoolingWeight(double baselineShare) {
    double share = std::clamp(baselineShare,0.0,1.0);
    double scale = IndependentPoolingHalfWeightShare;
    return share*share/(share*share+scale*scale);
}
inline void prepareIndependentPooling(FpEvidence& evidence, int independent, double baselineShare) {
    evidence.poolingVotes[independent] = evidence.deviations.contains(independent)
        ? evidenceWeight(evidence)*independentPoolingWeight(baselineShare) : 0;
}
inline double candidateEvidenceWeight(FpEvidence const& evidence, int candidate) {
    return evidence.poolingVotes.contains(candidate) ? evidence.poolingVotes.at(candidate) : evidenceWeight(evidence);
}
inline double poolingConfidence(FpEvidence const& evidence, int candidate) {
    return evidence.expected > 0 ? std::clamp(candidateEvidenceWeight(evidence,candidate)/evidence.expected,0.0,1.0) : 0;
}
inline FpEvidence aggregate(std::vector<FpEvidence> const& children, double expected) {
    FpEvidence result; result.expected = expected;
    std::map<int,double> sums,weights;
    double confidenceVotes = 0, denominator = 0;
    for (auto const& child : children) {
        double weight = evidenceWeight(child);
        denominator += child.expected;
        result.counted += child.counted;
        if (!child.deviations.empty()) confidenceVotes += weight;
        for (auto const& [candidate,deviation] : child.deviations) {
            double candidateWeight = candidateEvidenceWeight(child,candidate);
            sums[candidate] += candidateWeight*deviation;
            weights[candidate] += candidateWeight;
        }
        for (auto const& [candidate,votes] : child.poolingVotes) result.poolingVotes[candidate] = 0;
    }
    if (denominator > 0) result.confidence = confidenceVotes/denominator;
    for (auto const& [candidate,sum] : sums)
        if (weights[candidate] > 0) result.deviations[candidate] = float(sum/weights[candidate]);
    for (auto& [candidate,votes] : result.poolingVotes) votes = weights[candidate];
    return result;
}
inline void condition(FpEvidence& evidence, std::vector<FpEvidence const*> const& parents, double weight,
    std::map<int,double> const& candidateWeights = {}) {
    // Match the existing hierarchy: remove the accepted broad movement, then
    // shrink the local residual according to the amount of observed evidence.
    for (auto const& [candidate,deviation] : evidence.deviations) {
        double residual = deviation;
        for (auto parent : parents)
            if (parent->specific.contains(candidate)) residual -= parent->specific.at(candidate);
        double candidateWeight = candidateWeights.contains(candidate) ? candidateWeights.at(candidate) : weight;
        evidence.specific[candidate] = float(residual*candidateWeight);
    }
}
}
