#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Portable mathematics for rechecking preferences on already-counted ballots.
// Feed selection and party mapping belong to LiveV2. No future counts, turnout
// estimates, GUI objects or shared random engine enter this calculation.
namespace LivePreferenceCorrections {

struct Observation {
    std::size_t booth = 0;
    double formal = 0, fpA = 0, fpB = 0, otherParty = 0, gainA = 0;
    bool ppvc = false;
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
