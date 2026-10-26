#pragma once

#include "LivePartialCount.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Portable mathematics for rechecking preferences on already-counted ballots.
// Feed selection and party mapping belong to LiveV2. No future counts, turnout
// estimates, GUI objects or shared random engine enter this calculation.
namespace LivePreferenceCorrections {

struct RevisionLimits {
    double lower = 0, upper = 0; // Allowed opposing transfer to finalist A, in votes.
};

// Normally the current FP/TCP pair is the measured preference account. While
// its FP is being revised, the retained original pair is still the measurement
// behind the forecast. Compare that pair with peers, never old TCP/new FP.
// References are used during preparation only; no count maps enter scenario draws.
struct MatchedCounts {
    LivePartialCount::Counts const& fp;
    LivePartialCount::Counts const& tcp;
    std::map<int,double> guaranteed; // Revised counted lower bounds, if applicable.
    bool retainedBasis = false, recheckedReconciledPair = false;

    std::optional<RevisionLimits> limits(int first, int second,
        LivePartialCount::Counts const& currentFp) const {
        if (!retainedBasis) return {};
        // Only surviving, allocated preferences can be rechecked here. Changed
        // transfers already vary in the revision account. Keeping those outside
        // this allowance also prevents a correction removing a finalist's FP.
        return RevisionLimits{-std::max(0.0,guaranteed.at(first)-LivePartialCount::value(currentFp,first)),
            std::max(0.0,guaranteed.at(second)-LivePartialCount::value(currentFp,second))};
    }
};

// currentFp/currentTcp are the effective reported counts. basis, when supplied,
// identifies the original FP batch belonging to that same TCP vector. The
// current recheck flag applies only to a matching current pair; a revised pair
// instead retains its own original status. Invalid/incomplete pairs stay excluded.
inline std::optional<MatchedCounts> matchedCounts(LivePartialCount::Counts const& currentFp,
    LivePartialCount::Counts const& currentTcp, LivePartialCount::TcpBasis const* basis,
    bool currentRecheckedPair) {
    if (basis && basis->fp != currentFp) {
        if (!LivePartialCount::validBasis(*basis) || basis->tcp != currentTcp) return {};
        auto bounds = LivePartialCount::revisedLowerBounds(currentFp,currentTcp,*basis);
        if (bounds.empty() || LivePartialCount::total(currentFp) <= 0) return {};
        return MatchedCounts{basis->fp,basis->tcp,std::move(bounds),true,basis->recheckedReconciledPair};
    }
    if (!LivePartialCount::validBasis({currentFp,currentTcp,{}})) return {};
    return MatchedCounts{currentFp,currentTcp,{},false,currentRecheckedPair};
}

struct Observation {
    std::size_t booth = 0;
    double formal = 0, fpA = 0, fpB = 0, otherParty = 0, gainA = 0;
    bool ppvc = false;
    bool recheckedReconciledPair = false;
    std::optional<RevisionLimits> revisionLimits;
};

struct Mixture {
    std::size_t booth = 0;
    double pool = 0, gain = 0;
    // Routine and larger background rechecks both centre on the reported
    // preference flow. Only the broad, regression-directed component has a
    // different centre. Scales are on log odds; probabilities are absolute
    // mixture weights, with the remaining probability retaining the count.
    double original = 0, routineScale = 0, backgroundScale = 0;
    double broadCentre = 0, broadScale = 0;
    double broadProbability = 0, routineProbability = 0, backgroundProbability = 0;
    double prediction = 0, scatter = 0, discrepancy = 0, fraction = 0;
    double expectedRevision = 0, revisionSecondMoment = 0;
    std::size_t peers = 0;
    bool directedCorrectionSuppressed = false;
    // Normally the original preference pool's full range. FP removals can
    // reduce the surviving, allocated pool without erasing its outlier evidence.
    double revisionLower = 0, revisionUpper = 0;
};

// Large contributors retain their actual mixture during main simulation.
// Others share an independently prepared bank of aggregate revisions. This
// representation retains asymmetric corrections rather than fitting a normal
// standard deviation to them, and is shared by all scenario copies.
struct SeatDistribution {
    std::vector<Mixture> explicitBooths;
    std::vector<double> aggregateRevisions;
    double expectedRevision = 0;
};

constexpr std::size_t AggregateSamples = 1024;
constexpr std::size_t ExplicitBooths = 4;
constexpr double SizeResponseScale = 1000;

std::vector<Mixture> prepare(std::vector<Observation> const& observations);
SeatDistribution compress(std::vector<Mixture> const& mixtures, std::uint64_t seed);
double draw(SeatDistribution const& distribution, std::uint64_t seed);

// Apply the same opposing transfer to a scenario's counted and projected pair.
// The estimated additions are thereby unchanged. Raw reported counts remain
// elsewhere in the provider and are never edited by this operation.
template<class Counted, class Projected>
void transfer(Counted& counted, Projected& projected, int partyA, int partyB, double revision) {
    counted.at(partyA) += revision;
    counted.at(partyB) -= revision;
    projected.at(partyA) = static_cast<typename Projected::mapped_type>(double(projected.at(partyA)) + revision);
    projected.at(partyB) = static_cast<typename Projected::mapped_type>(double(projected.at(partyB)) - revision);
}

} // namespace LivePreferenceCorrections
