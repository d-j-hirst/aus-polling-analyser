#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "LiveData.h"

// Small, portable operations shared by the live count bridge and its tests.
// These operations preserve the counted allocation supplied by the caller and
// redistribute only the outstanding pool. Preference rechecking supplies a
// scenario-specific counted allocation; ordinary share variation cannot undo it.
namespace LiveTurnoutMath {

inline double remaining(double expected, double counted) {
    // Usually this is simply the expected final count minus votes already
    // reported. Check the account before returning that outstanding amount;
    // a substantive shortfall is an input/calculation error, not negative votes.
    if (!std::isfinite(expected) || !std::isfinite(counted) || expected < 0 || counted < 0)
        throw std::runtime_error("Invalid live turnout count account.");
    // Candidate projections are stored as floats, while counted allocations
    // use doubles. Fractional preference transfers can round a projection (or
    // the sum of a pair) just below its counted allocation. Allow that storage
    // error, scaled to the count, without accepting a substantive shortfall.
    double const tolerance = std::max(1e-7,
        double(std::numeric_limits<float>::epsilon()) * std::max(expected, counted));
    if (expected < counted - tolerance)
        throw std::runtime_error("Invalid live turnout count account: projected "
            + std::to_string(expected) + ", counted " + std::to_string(counted) + ".");
    return std::max(0., expected - counted);
}

inline double completion(double counted, double expected) {
    // Completion is counted votes as a proportion of the same expected final
    // count used in the projection. A finished account rounded slightly down
    // still means 100%; accepted storage rounding cannot produce a value above it.
    double const total = counted + remaining(expected, counted);
    return total > 0 ? counted / total : 0;
}

inline double pairTarget(double expectedFp, double countedFp, double countedPair) {
    // FP and final-pair counts normally cover the same formal voters. The pair
    // target must also cover future voters expected by the turnout model.
    double addition = remaining(expectedFp, countedFp);
    if (!std::isfinite(countedPair) || countedPair < 0)
        throw std::runtime_error("Invalid counted TCP/TPP total.");
    // In a compulsory-preferential count, counted formal FP votes still
    // need a final-pair count when only some preferences have been published.
    // That outstanding work is separate from future FP additions: a zero-
    // addition draw must not erase it. Preserve an observed TCP excess as well,
    // rather than reducing a published candidate count to force agreement.
    // OPV requires a continuing-vote account that explicitly allows exhaustion;
    // it cannot use an observed shortfall as evidence of completed exhaustion.
    return std::max(countedFp, countedPair) + addition;
}

inline std::map<int, float> varyRemaining(
    std::map<int, float> const& projected,
    std::map<int, double> const& counted,
    std::map<int, double> const& transformedChanges,
    bool normaliseFpChanges = false) {
    // Apply the simulator's share variation to the uncounted voters, retaining
    // every counted candidate vote. FP changes are normalised across parties;
    // a TCP/TPP change represents a single movement between the final pair.
    double total = 0, observed = 0;
    for (auto const& [party, votes] : projected) {
        if (!std::isfinite(votes) || votes < 0)
            throw std::runtime_error("Invalid live party projection.");
        total += votes;
    }
    for (auto const& [party, votes] : counted) {
        if (!projected.count(party) || !std::isfinite(votes) || votes < 0)
            throw std::runtime_error("Counted party is absent from live projection.");
        observed += votes;
        remaining(projected.at(party), votes);
    }
    double pool = remaining(total, observed);
    bool changed = false;
    for (auto const& [party, change] : transformedChanges) {
        if (!projected.count(party) || !std::isfinite(change))
            throw std::runtime_error("Invalid live remaining-share variation.");
        changed = changed || change != 0;
    }
    // An unchanged or fully counted account stays bit-for-bit unchanged.
    if (!changed || pool == 0 || projected.size() == 1) return projected;

    // Keep this hot-path scratch space contiguous: the main simulation calls
    // it for every seat, without needing another tree of party allocations.
    std::vector<std::pair<int, double>> logWeights;
    logWeights.reserve(projected.size());
    double maximum = -INFINITY;
    for (auto const& [party, votes] : projected) {
        double fixed = counted.count(party) ? counted.at(party) : 0;
        double addition = remaining(votes, fixed);
        // A zero allocation is not a candidate-availability statement. Smooth
        // it by a quarter vote when taking logs, as for other possible zeros.
        double share = (addition + .25) / (pool + .25 * projected.size());
        double overall = (votes + .25) / (total + .5);
        double response = pool / total * share * (1 - share)
            / (overall * (1 - overall));
        double change = transformedChanges.count(party) ? transformedChanges.at(party) : 0;
        // LiveV2's preparation measures changes in the full-seat log odds, on
        // a scale of 25. Convert their local response to the remaining pool.
        // Joint log weights then allocate that finite pool without ever moving
        // counted votes or clipping a perturbed party share at a count floor.
        // FP preparation changes each party's log odds separately and then
        // normalises all parties. Its log-weight response includes (1-share);
        // a single TCP/TPP pair change instead directly moves the pair log odds.
        // Omitting this distinction would double a pair of opposing FP changes
        // even with no counted votes, rather than preserve the prepared scale.
        double weightResponse = normaliseFpChanges ? 1 - share : 1;
        double value = std::log(share) + .04 * change / response * weightResponse;
        if (!std::isfinite(value)) throw std::runtime_error("Invalid remaining-share response.");
        logWeights.emplace_back(party, value);
        maximum = std::max(maximum, value);
    }
    double denominator = 0;
    for (auto const& [party, value] : logWeights) denominator += std::exp(value - maximum);
    std::map<int, float> result;
    for (auto const& [party, value] : logWeights) {
        double fixed = counted.count(party) ? counted.at(party) : 0;
        result[party] = float(fixed + pool * std::exp(value - maximum) / denominator);
    }
    return result;
}

struct PartyAccount {
    std::map<int, double> counted;
    std::map<int, float> projected;
};

struct UnitComposition {
    std::map<int, double> base;
    std::vector<std::pair<int, double>> additionShares;
};

inline UnitComposition prepareUnitComposition(std::map<int, float> const& projected,
    std::map<int, double> const& counted, double countedFp) {
    // Freeze the mix of future voters in a reporting unit. Its base also holds
    // estimated preferences on already-counted FP votes, including a partially
    // reported pair. Those estimates survive a draw with no new FP votes, but
    // remain outside the counted-party constraints so their composition can
    // still vary. For FP itself, all counted votes are already represented.
    UnitComposition result;
    if (projected.empty()) return result;
    double total = 0, observed = 0;
    for (auto const& [p,v] : projected) total += v;
    for (auto const& [p,v] : counted) {
        if (!projected.count(p)) throw std::runtime_error("Counted party is absent from unit projection.");
        remaining(projected.at(p),v); observed += v;
    }
    if (!(total > 0)) return result;
    double pool = remaining(total,observed);
    double missingPreferences = std::max(0., countedFp - observed);
    // Projected party maps are floats. Allow their reduction error, while
    // rejecting a projection that omits a substantive known FP preference pool.
    if (missingPreferences > pool + std::max(.02, total * 2e-6))
        throw std::runtime_error("Live pair projection omits preferences on counted FP votes.");
    for (auto const& [p,v] : projected) {
        double fixed = counted.count(p) ? counted.at(p) : 0;
        double share = observed > 0 && pool > 1e-6 ? remaining(v,fixed)/pool : v/total;
        result.additionShares.emplace_back(p,share);
        result.base[p] = fixed + missingPreferences * share;
    }
    return result;
}

inline std::map<int, float> varySampledRemaining(std::map<int, float> const& sampled,
    std::map<int, float> const& mean, std::map<int, double> const& counted,
    std::map<int, double> const& changes, bool normaliseFpChanges = false) {
    // Reduced preparation measured composition variation at the mean count.
    // Transfer that remaining-voter mix to the sampled count, so a late batch
    // carries composition uncertainty instead of spreading a fixed, tiny
    // full-seat standard deviation over many more voters.
    double total = 0, observed = 0;
    for (auto const& [p,v] : sampled) total += v;
    for (auto const& [p,v] : counted) {
        if (!sampled.count(p)) throw std::runtime_error("Counted party is absent from sampled projection.");
        remaining(sampled.at(p),v); observed += v;
    }
    double pool = remaining(total,observed);
    if (pool == 0 || changes.empty()) return sampled;
    auto variedMean = varyRemaining(mean,counted,changes,normaliseFpChanges);
    if (variedMean == mean) return sampled;
    std::vector<std::pair<int,double>> weights;
    double maximum = -INFINITY;
    for (auto const& [p,v] : sampled) {
        double fixed = counted.count(p) ? counted.at(p) : 0;
        double value = std::log(remaining(v,fixed)+.25)
            + std::log(remaining(variedMean.at(p),fixed)+.25)
            - std::log(remaining(mean.at(p),fixed)+.25);
        weights.emplace_back(p,value); maximum = std::max(maximum,value);
    }
    double denominator = 0;
    for (auto const& [p,v] : weights) denominator += std::exp(v-maximum);
    std::map<int,float> result;
    for (auto const& [p,v] : weights)
        result[p] = float((counted.count(p) ? counted.at(p) : 0)+pool*std::exp(v-maximum)/denominator);
    return result;
}

template<class Classifier>
inline PartyAccount partitionAccount(LiveData::VoteCountAccount const& source, Classifier classify) {
    // The simulator has fewer categories than the feed. Map each candidate to
    // exactly one represented party, independent proxy or Others group, using
    // the same partition for counted votes and the projected remaining pool.
    if (!source.counted || !source.projected) throw std::runtime_error("Missing live vote account.");
    PartyAccount result;
    for (auto const& [party, votes] : *source.projected) result.projected[classify(party)] += votes;
    for (auto const& [party, votes] : *source.counted) result.counted[classify(party)] += votes;
    return result;
}

inline PartyAccount resizeAccount(PartyAccount account, double target) {
    // Used when a larger account assigns a new total to a subset, such as the
    // Coalition. Preserve that subset's counted candidates while changing
    // only its remaining allowance. If the mean had no measurable remainder,
    // the existing overall mix supplies the provisional addition composition.
    double total = 0, counted = 0;
    for (auto const& [party, votes] : account.projected) total += votes;
    for (auto const& [party, votes] : account.counted) counted += votes;
    // Shares cross the simulator boundary as floats. Permit their rounding
    // error at a finished subset's count, but reject a substantive shortfall.
    if (target < counted && counted - target <= std::max(1e-7, counted * 2e-7)) target = counted;
    double oldPool = remaining(total, counted), newPool = remaining(target, counted);
    for (auto& [party, votes] : account.projected) {
        double fixed = account.counted.count(party) ? account.counted.at(party) : 0;
        double weight = oldPool > 0 ? remaining(votes, fixed) / oldPool
            : (total > 0 ? votes / total : 1./account.projected.size());
        votes = float(fixed + newPool * weight);
    }
    return account;
}

inline std::map<int, float> reconcileForecast(
    PartyAccount account, std::map<int, float> const& proposedShares, bool normaliseFpChanges = false) {
    // Called after the main simulator has blended its prior and live evidence.
    // Interpret that blend as a preference for the outstanding pool, relative
    // to the live scenario's composition. Reallocate that finite pool on the
    // transformed scale instead of rescaling already counted candidate votes.
    double total = 0, proposedTotal = 0;
    for (auto const& [party, votes] : account.projected) total += votes;
    for (auto const& [party, share] : proposedShares) {
        if (!std::isfinite(share) || share < 0) throw std::runtime_error("Invalid proposed live share.");
        proposedTotal += share;
        if (share > 0) account.projected.try_emplace(party, 0.f);
    }
    if (!(total > 0) || !(proposedTotal > 0)) throw std::runtime_error("Empty live forecast account.");
    double observed = 0;
    for (auto const& [party,votes] : account.counted) {
        if (!account.projected.count(party)) throw std::runtime_error("Counted party is absent from forecast account.");
        remaining(account.projected.at(party),votes); observed += votes;
    }
    if (remaining(total,observed) == 0) {
        // Most late iterations choose no additions. Preserve their exact
        // account directly, avoiding unnecessary transforms in this hot path.
        for (auto& [party,votes] : account.projected) votes = float(100*double(votes)/total);
        return account.projected;
    }
    std::map<int, double> changes;
    // FP variation perturbs each party independently before normalising.
    // A two-party log-odds change already describes movement toward one
    // candidate and away from the other. Supply it once in pair mode;
    // opposing changes to both weights would double the intended movement.
    bool const singlePairChange = !normaliseFpChanges && account.projected.size() == 2;
    for (auto const& [party, votes] : account.projected) {
        double target = proposedShares.count(party) ? total * proposedShares.at(party) / proposedTotal : 0;
        // A quarter-vote equivalent permits a possible zero without clipping
        // its log odds or treating an unobserved party as a counted candidate.
        changes[party] = 25 * (std::log((target + .25) / (total - target + .25))
            - std::log((votes + .25) / (total - votes + .25)));
        if (singlePairChange) break;
    }
    auto votes = varyRemaining(account.projected, account.counted, changes, normaliseFpChanges);
    for (auto& [party, count] : votes) count = float(100 * double(count) / total);
    return votes;
}
}
