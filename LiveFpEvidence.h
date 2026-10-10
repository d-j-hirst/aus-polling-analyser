#pragma once

#include "LiveNewVotes.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

// Two descriptions of the same votes: historical swing, and absolute current
// composition. They compete for influence; their precisions must not be added
// as if they were independent samples. All shares below refer to future votes.
namespace LiveFpEvidence {
using Shares = LiveNewVotes::Shares;
// Common, directly maintained settings. Absolute evidence is deliberately
// weaker than fully comparable swing evidence; a fourth-power competition
// gives a smooth preference to whichever description is more informative.
inline constexpr double AbsoluteStrength = .5;
inline constexpr int CompetitionPower = 4;
inline constexpr double EvidenceVotes = 500;
inline constexpr double PriorReplacementRate = 50;
inline constexpr double EarlyUncertaintyCompletion = .1;
struct Unit {
    // Maps are keyed by ballot candidate identity, never modelling-slot order.
    // Counted/projected contain votes, not shares; group identifies a common
    // vote type and booth type within one seat. Relevance discounts transfers.
    Shares counted, projected;
    std::set<int> comparable;
    double relevance = 1;
    int group = 0;
    bool ordinary = false, aggregatedEarly = false;
};
struct Evidence {
    // Historical/absolute contain *remaining vote counts*. The confidence
    // denominator is the seat's expected final FP total. Completion, confidence
    // and final future proportions are all fractions, not percentages.
    double completion = 0, observed = 0, breadth = 0;
    Shares counted, historical, absolute, historicalConfidence;
};
struct Prepared {
    Evidence evidence;
    Shares weight, future;
    // One multiplicative adjustment per candidate preserves the relative
    // differences between locations. Row normalisation retains every unit's
    // turnout total; balancing also preserves the desired seat-wide totals.
    Shares factors;
};
inline double total(Shares const& values) {
    double sum = 0;
    for (auto const& [id,value] : values) sum += value;
    return sum;
}
inline double value(Shares const& values, int id) {
    auto it = values.find(id);
    return it == values.end() ? 0 : it->second;
}
inline void add(Shares& to, Shares const& from) {
    for (auto const& [id,v] : from) to[id] += v;
}
inline Shares smoothed(Shares const& counts, Shares const& candidates) {
    Shares shares;
    for (auto const& [id,unused] : candidates) shares[id] = value(counts,id) + .5;
    return LiveNewVotes::normalize(std::move(shares));
}
inline Shares remainder(Unit const& unit) {
    Shares result;
    for (auto const& [id,v] : unit.projected) result[id] = std::max(0.,v-value(unit.counted,id));
    return result;
}
inline Evidence collect(std::vector<Unit> const& units, double seatConfidence) {
    Evidence evidence;
    Shares ordinary;
    std::map<int,Unit> groups;
    double expected = 0, squaredSizes = 0;
    for (auto const& unit : units) {
        add(evidence.counted,unit.counted);
        add(evidence.historical,remainder(unit));
        double size = total(unit.counted);
        expected += total(unit.projected);
        squaredSizes += size*size;
        if (unit.ordinary) add(ordinary,unit.counted);
        for (int id : unit.comparable) evidence.historicalConfidence[id] += size*unit.relevance;
        auto& group = groups[unit.group];
        add(group.counted,unit.counted); add(group.projected,unit.projected);
        group.ordinary = unit.ordinary; group.aggregatedEarly = unit.aggregatedEarly;
    }
    evidence.observed = total(evidence.counted);
    evidence.completion = expected > 0 ? evidence.observed/expected : 0;
    evidence.breadth = squaredSizes > 0 ? evidence.observed*evidence.observed/squaredSizes : 0;
    for (auto& [id,v] : evidence.historicalConfidence) v = expected > 0 ? std::min(seatConfidence,v/expected) : 0;
    if (evidence.historical.empty() || total(evidence.historical) == 0) return evidence;
    auto background = smoothed(total(ordinary)>0 ? ordinary : evidence.counted,evidence.historical);
    for (auto const& [id,group] : groups) {
        double counted = total(group.counted), target = total(group.projected);
        double pool = std::max(0.,target-counted);
        double weight = group.ordinary ? (counted>0 ? 1 : 0)
            : LiveNewVotes::observedWeight(counted,target,group.aggregatedEarly
                ? LiveNewVotes::Category::AggregatedEarly : LiveNewVotes::Category::General);
        auto observed = smoothed(group.counted,evidence.historical);
        for (auto const& [party,share] : background)
            evidence.absolute[party] += pool*((1-weight)*share+weight*observed.at(party));
    }
    return evidence;
}
inline double breadthGate(Evidence const& evidence) {
    // Effective reporting locations is N^2 / sum(n_i^2): ten equally sized
    // booths give ten, while one dominant batch remains close to one. Half a
    // thousand votes and one location stabilise the first small observations.
    return evidence.observed/(evidence.observed+EvidenceVotes)*evidence.breadth/(evidence.breadth+1);
}
inline double absoluteWeight(Evidence const& evidence, int party) {
    double absolute = AbsoluteStrength*evidence.completion;
    double historical = value(evidence.historicalConfidence,party);
    double a = std::pow(absolute,CompetitionPower), h = std::pow(historical,CompetitionPower);
    return a+h > 0 ? a/(a+h)*breadthGate(evidence) : 0;
}
inline Shares combine(Evidence const& evidence, Shares& weights) {
    if (total(evidence.historical) == 0) return {};
    auto historical = LiveNewVotes::normalize(evidence.historical);
    if (evidence.observed == 0) return historical;
    auto absolute = LiveNewVotes::normalize(evidence.absolute);
    double pool = total(evidence.historical);
    double floor = .5/(pool+.5*historical.size());
    Shares result;
    for (auto const& [id,share] : historical) {
        double w = weights[id] = absoluteWeight(evidence,id);
        result[id] = std::exp((1-w)*std::log(std::max(floor,share))
            +w*std::log(std::max(floor,value(absolute,id))));
    }
    return LiveNewVotes::normalize(std::move(result));
}
inline double replacementWeight(Evidence const& evidence) {
    // This replaces residual pre-election variation, not historical swing
    // information. A redistributed seat can have reliable absolute FP counts
    // even when it has little comparable history. The same gradual phase-in
    // used for live variability is discounted for a small or concentrated count.
    return -std::expm1(-PriorReplacementRate*evidence.completion*breadthGate(evidence));
}
inline double disagreementVariance(Prepared const& prepared, int party) {
    auto const& e = prepared.evidence;
    double w = value(prepared.weight,party), pool = total(e.historical);
    double size = e.observed+pool;
    if (size == 0 || w == 0) return 0;
    double fixed = value(e.counted,party);
    auto odds = [fixed,size](double future) {
        double votes = fixed+future;
        return std::log((votes+.5)/(std::max(0.,size-votes)+.5));
    };
    double gap = odds(value(e.absolute,party))-odds(value(e.historical,party));
    // Disagreement is uncertainty, not another independent observation. Use
    // the between-method variance on natural log odds without dividing either
    // method's sampling variance by a spurious combined sample size.
    return w*(1-w)*gap*gap;
}
inline double additionalVariance(Prepared const& prepared, int party) {
    auto const& e = prepared.evidence;
    double pool = total(e.historical), size = pool+e.observed;
    if (pool == 0 || e.observed == 0) return 0;
    double votes = value(e.counted,party)+pool*value(prepared.future,party);
    double q = (pool*value(prepared.future,party)+.25)/(pool+.25*prepared.future.size());
    double overall = (votes+.25)/(size+.5);
    // A few locations can be unrepresentative even when their vote totals are
    // precise. This extra SD is in log proportions of the remaining pool:
    // one / sqrt(B+1), fading by exp(-completion/0.1). Convert it to whole-seat
    // log odds for the existing finite-pool sampler. It neither moves counted
    // votes nor leaves a fixed percentage-point error when counting finishes.
    double remainingSd = std::exp(-e.completion/EarlyUncertaintyCompletion)/std::sqrt(e.breadth+1);
    double response = pool/size*q/(overall*(1-overall));
    return disagreementVariance(prepared,party)+std::pow(response*remainingSd,2);
}
inline Shares adjusted(Unit const& unit, Shares const& factors) {
    auto future = remainder(unit);
    double pool = total(future);
    if (pool == 0 || factors.empty()) return unit.projected;
    // A half vote permits a currently zero *estimate* to move. Counted zeros
    // are untouched, and candidates absent from a unit's ballot stay absent.
    for (auto& [id,v] : future) v = std::max(v,.5)*value(factors,id);
    future = LiveNewVotes::normalize(std::move(future));
    for (auto& [id,v] : future) v = value(unit.counted,id)+pool*v;
    return future;
}
inline Shares balance(std::vector<Unit> const& units, Shares const& target, double pool) {
    // Alternate matching seat candidate totals and each unit's fixed future
    // total. This retains ordinary geographic/category differences while
    // making their sum agree with the competing-evidence seat estimate. The
    // tolerance is one thousandth of a vote, not a fraction of the electorate.
    Shares factors;
    for (auto const& [id,v] : target) factors[id] = 1;
    for (int iteration = 0; iteration < 200; ++iteration) {
        Shares actual;
        for (auto const& unit : units) {
            auto projection = adjusted(unit,factors);
            for (auto const& [id,v] : projection) actual[id] += std::max(0.,v-value(unit.counted,id));
        }
        double largest = 0;
        for (auto& [id,factor] : factors) {
            double desired = target.at(id)*pool, obtained = value(actual,id);
            largest = std::max(largest,std::abs(desired-obtained));
            if (obtained>0) factor *= desired/obtained;
        }
        if (largest < .001) return factors;
    }
    throw std::runtime_error("FP evidence composition could not be balanced across turnout units.");
}
inline Prepared prepare(std::vector<Unit> const& units, double seatConfidence) {
    Prepared result;
    result.evidence = collect(units,seatConfidence);
    result.future = combine(result.evidence,result.weight);
    double pool = total(result.evidence.historical);
    if (result.evidence.observed>0 && pool>0)
        result.factors = balance(units,result.future,pool);
    return result;
}
} // namespace LiveFpEvidence
