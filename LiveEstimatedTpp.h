#pragma once

#include "LiveSimulationMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

// A shared conversion error belongs to received FP evidence whose TPP still
// needs estimating. Ordinary observed TPP replaces that source progressively;
// an empty feed or a declaration of candidates with zero votes supplies none.
// This portable component describes influence, not a counted-vote account.
namespace LiveEstimatedTpp {

struct Unit {
    // Expected votes weight a booth/category within its seat. Confidence and
    // relevance retain the live model's comparison quality; they are fractions.
    double expectedVotes = 0, countedFp = 0, countedTpp = 0;
    double fpConfidence = 0, observedTppConfidence = 0, relevance = 1;
    bool contributes = false, optionalPreferential = false;
};

template<class Counts>
inline double observedTppVotes(Counts const& tcp, int nationals) {
    // Identity alone is insufficient: some feeds publish the selected pair
    // before counting starts. A genuine zero for one finalist remains valid
    // when the other finalist has received votes.
    if (!tcp.contains(0) || (!tcp.contains(1) && !tcp.contains(nationals))) return 0;
    double total = 0;
    for (auto const& [party,votes] : tcp) total += votes;
    return total;
}

inline double inferredFraction(Unit const& unit) {
    if (unit.countedFp == 0) return 0;
    // In compulsory preferential counts the FP/TCP gap represents pending
    // preferences. Under OPV it may instead be genuine exhaustion; the retained
    // OPV projection uses its measured continuing-vote share once TPP exists.
    if (unit.optionalPreferential && unit.countedTpp > 0) return 0;
    return std::clamp(1-unit.countedTpp/unit.countedFp, 0.0, 1.0);
}

inline void validate(Unit const& unit) {
    for (double value : {unit.expectedVotes, unit.countedFp, unit.countedTpp})
        if (!std::isfinite(value) || value < 0)
            throw std::logic_error("Invalid estimated-TPP source count.");
    for (double value : {unit.fpConfidence, unit.observedTppConfidence, unit.relevance})
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::logic_error("Invalid estimated-TPP source confidence.");
}

struct Evidence {
    // These are quality-weighted evidence votes, NOT additional votes or fixed
    // lower bounds. The expected denominator includes unreported services too.
    double expectedVotes = 0, inferredEvidenceVotes = 0, observedEvidenceVotes = 0;

    void add(Unit const& unit) {
        validate(unit);
        expectedVotes += unit.expectedVotes;
        if (!unit.contributes) return;
        double weight = unit.expectedVotes*unit.relevance;
        // FP-derived TPP retains the existing half-strength convention. Partial
        // classic TCP displaces only its matched share of FP, rather than
        // switching off conversion uncertainty for the whole booth or seat.
        inferredEvidenceVotes += weight*.5*unit.fpConfidence*inferredFraction(unit);
        if (unit.countedTpp > 0)
            observedEvidenceVotes += weight*unit.observedTppConfidence;
    }

    double confidence() const {
        return expectedVotes > 0 ? std::clamp(
            (inferredEvidenceVotes+observedEvidenceVotes)/expectedVotes, 0.0, 1.0) : 0;
    }

    double inferredShare() const {
        double total = inferredEvidenceVotes+observedEvidenceVotes;
        return total > 0 ? inferredEvidenceVotes/total : 0;
    }

    double influence() const {
        // Use the same response as the accepted TPP movement, then retain only
        // the part supplied by estimates. This permits extrapolation beyond the
        // counted FP fraction while smoothly limiting a first small booth.
        return inferredShare()*LiveSimulationMath::observationWeight(float(confidence()));
    }
};

} // namespace LiveEstimatedTpp
