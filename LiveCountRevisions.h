#pragma once

#include "RandomGenerator.h"
#include "LiveTurnoutMath.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// Occasional additions/removals of already-reported voters, at one physical
// booth or declaration account. Accepted feed counts remain unchanged. A draw
// changes the scenario's counted and projected accounts together, leaving its
// estimate of genuinely uncounted voters separate. This is not a preference
// transfer between finalists (the existing preference-rechecking model does that).
namespace LiveCountRevisions {
using Counts = std::map<int,double>;
using Weights = std::vector<std::pair<int,double>>;
inline constexpr double Probability = .10;
inline constexpr double RelativeSd = .01;
inline constexpr double MixedEarlyRelativeSd = .05;
// Initial general allowance: 90% exactly unchanged, otherwise a centred signed
// normal with SD 1% of reported formal FP. The wider 5% allowance belongs to
// WA mixed early centres: before bounds, its central 99% is about +/-8.2% and
// covers the inspected late corrections. Their unusual size is not evidence
// for using the same width everywhere. Neither starting scale establishes
// multi-election calibration; the signed accounting is shared across policies.

inline double total(Counts const& counts) {
    double sum = 0; for (auto const& [p,v] : counts) sum += v; return sum;
}
inline double get(Counts const& counts, int p) {
    auto found = counts.find(p); return found == counts.end() ? 0 : found->second;
}
struct Direction {
    int candidate = 0;
    double cumulative = 0, limit = 0;
    Weights tpp, tcp;
};
struct Prepared {
    double counted = 0, sd = 0, tppCoverage = 0, tcpCoverage = 0;
    std::vector<Direction> directions;
};

// A corrected voter belongs to an FP candidate, rather than having the whole
// booth scaled proportionally. Finalists retain their own primary votes. For
// other candidates, use this booth's existing estimated preference-pool mix;
// no new preference bias is inferred. Aliases join NAT and LIB for classic TPP.
inline Weights pairDirection(int candidate, Counts const& fp, Counts const& pair,
    std::map<int,int> const& aliases) {
    if (pair.size() != 2 || total(pair) <= 0) return {};
    auto mapped = [&](int p) { auto it = aliases.find(p); return it == aliases.end() ? p : it->second; };
    if (pair.contains(mapped(candidate))) return {{mapped(candidate),1}};
    Counts primaries;
    for (auto const& [p,v] : fp) primaries[mapped(p)] += v;
    Weights weights;
    double pool = 0;
    for (auto const& [p,v] : pair) { double gain = std::max(0.,v-get(primaries,p)); weights.emplace_back(p,gain); pool += gain; }
    for (auto& [p,w] : weights) w = pool > 0 ? w/pool : pair.at(p)/total(pair);
    return weights;
}

// Base pair votes include estimated preferences still owed on counted FP.
// Only the reported fraction becomes a counted constraint; the other fraction
// remains an estimate. Symmetric bounds keep every candidate nonnegative on
// BOTH signs, so the revision adds spread without deliberately shifting means.
inline Prepared prepare(Counts const& fp, Counts const& tpp, Counts const& tcp,
    Counts const& baseTpp, Counts const& baseTcp, std::map<int,int> const& tppAliases = {},
    double relativeSd = RelativeSd) {
    Prepared result;
    result.counted = total(fp);
    result.sd = relativeSd*result.counted;
    if (result.counted <= 0) return result;
    result.tppCoverage = total(baseTpp) > 0 ? std::min(1.,total(tpp)/total(baseTpp)) : 0;
    result.tcpCoverage = total(baseTcp) > 0 ? std::min(1.,total(tcp)/total(baseTcp)) : 0;
    double cumulative = 0;
    for (auto const& [p,v] : fp) if (v > 0) {
        Direction d;
        d.candidate = p; d.cumulative = cumulative += v/result.counted; d.limit = v;
        d.tpp = pairDirection(p,fp,baseTpp,tppAliases);
        d.tcp = pairDirection(p,fp,baseTcp,{});
        auto bound = [&](Weights const& weights, Counts const& observed, Counts const& base, double coverage) {
            for (auto const& [q,w] : weights) if (w > 0) {
                if (coverage > 0) d.limit = std::min(d.limit,get(observed,q)/(w*coverage));
                if (coverage < 1) d.limit = std::min(d.limit,std::max(0.,get(base,q)-get(observed,q))/(w*(1-coverage)));
            }
        };
        bound(d.tpp,tpp,baseTpp,result.tppCoverage);
        bound(d.tcp,tcp,baseTcp,result.tcpCoverage);
        result.directions.push_back(std::move(d));
    }
    result.directions.back().cumulative = 1;
    return result;
}

struct Draw { Direction const* direction = nullptr; double amount = 0; };
inline Draw draw(Prepared const& prepared, std::uint64_t seed) {
    if (prepared.directions.empty() || RandomGenerator::uniform01_from_key(seed) >= Probability) return {};
    double choice = RandomGenerator::uniform01_from_key(RandomGenerator::mixKey(seed,1));
    auto found = std::find_if(prepared.directions.begin(),prepared.directions.end(),
        [&](auto const& d) { return choice < d.cumulative; });
    if (found == prepared.directions.end()) found = std::prev(prepared.directions.end()); // Float RNG may round to 1.
    double normal = RandomGenerator::normal_from_key(RandomGenerator::mixKey(seed,2),0,1);
    return {&*found, std::clamp(prepared.sd*normal,-found->limit,found->limit)};
}

// Earlier preference-only corrections and remaining-vote draws may change the
// available room in a district account. Further restrict BOTH signs by that
// room, not just the sampled sign. This preserves a centred revision and avoids
// restoring an obsolete raw TCP floor. All maps refer to one scenario only.
template<class Projected>
inline void bound(Counts const& counted, Projected const& projected, Weights const& weights,
    double coverage, double& limit) {
    for (auto const& [p,w] : weights) if (w > 0) {
        auto it = projected.find(p);
        if (it == projected.end()) { limit = 0; continue; }
        double observed = get(counted,p);
        if (coverage > 0) limit = std::min(limit,observed/(w*coverage));
        if (coverage < 1) limit = std::min(limit,std::max(0.,double(it->second)-observed)/(w*(1-coverage)));
    }
}
template<class Projected>
inline void apply(Counts& counted, Projected& projected, Weights const& weights,
    double coverage, double amount) {
    for (auto const& [p,w] : weights) {
        double const previousCounted = get(counted,p);
        double const totalChange = amount*w;
        double const countedChange = totalChange*coverage;
        // Preserve the remaining-voter allocation while updating its counted
        // base. Several booth revisions can affect the same district candidate;
        // direct repeated additions to its float projection lose that base.
        double const revised = LiveTurnoutMath::revisedProjection(
            projected.at(p),previousCounted,countedChange,totalChange);
        if (coverage > 0) counted[p] = previousCounted+countedChange;
        projected.at(p) = static_cast<typename Projected::mapped_type>(revised);
    }
}
}
