#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>

// Preferences can lag behind first preferences within a counting service. A
// TCP share from an earlier batch describes both its primary mix and its flows;
// only the flows should be carried over to a later, differently composed batch.
// This component allocates votes still awaiting preferences independently of
// voters whose first preferences have not yet arrived. Turnout owns their sizes.
namespace LivePartialCount {
using Counts = std::map<int,int>;
using Flows = std::map<int,double>; // Fractions reaching the first finalist.
inline constexpr double ComparableTotalFraction = .02;
// An observed flow supplies this many non-finalist votes' worth of evidence
// before it receives half the weight. This limits dependence on a tiny batch.
inline constexpr double PriorPreferenceVotes = 500;

inline double total(Counts const& counts) {
    double result = 0;
    for (auto const& [candidate,votes] : counts) result += votes;
    return result;
}
inline double value(Counts const& counts, int candidate) {
    auto found = counts.find(candidate);
    return found == counts.end() ? 0 : found->second;
}
inline double logOdds(double share) {
    share = std::clamp(share,1e-6,1-1e-6);
    return std::log(share/(1-share));
}
inline double inverseLogOdds(double value) { return 1/(1+std::exp(-value)); }

struct Evidence {
    Counts fp;
    int first = -1, second = -1;
    double offset = 0, preferenceVotes = 0;
    bool matchingCurrentTcp = false;
    std::string fpSourceTime, tcpSourceTime;
};

inline double evidenceOffset(double prior, std::optional<Evidence> const& evidence, int first, int second) {
    if (!evidence || evidence->first != first || evidence->second != second) return prior;
    double weight = evidence->preferenceVotes/(evidence->preferenceVotes+PriorPreferenceVotes);
    return prior+weight*(evidence->offset-prior);
}

inline double preferenceRate(int candidate, Flows const& flows, double offset) {
    double rate = flows.at(candidate);
    // Finalists' own primaries remain with them. Only transferred votes have
    // an uncertain flow, whose displacement is measured on bounded log odds.
    return rate == 0 || rate == 1 ? rate : inverseLogOdds(logOdds(rate)+offset);
}
inline double expectedFirst(Counts const& fp, Flows const& flows, double offset) {
    double result = 0;
    for (auto const& [candidate,votes] : fp)
        result += votes*preferenceRate(candidate,flows,offset);
    return result;
}

inline std::optional<Evidence> measure(Counts const& fp, Counts const& tcp,
    Flows const& flows, int first, int second, bool matchingCurrentTcp = false) {
    double ft = total(fp), tt = total(tcp);
    if (ft <= 0 || tt <= 0 || tcp.size() != 2 || !tcp.count(first) || !tcp.count(second)
        || std::abs(ft-tt)/ft > ComparableTotalFraction) return {};
    double preferences = ft-value(fp,first)-value(fp,second);
    if (preferences <= 0) return {};
    double received = value(tcp,first)*ft/tt-value(fp,first);
    if (received < 0 || received > preferences) return {};
    double expected = (expectedFirst(fp,flows,0)-value(fp,first))/preferences;
    if (expected <= 0 || expected >= 1) return {};
    // Half a vote keeps genuine all-one-way tiny batches finite. It becomes
    // negligible for the large batches which can meaningfully train a flow.
    double actual = (received+.5)/(preferences+1);
    return Evidence{fp,first,second,logOdds(actual)-logOdds(expected),preferences,matchingCurrentTcp,{},{}};
}

inline std::optional<Counts> remainingCohort(Counts const& fp, Evidence const& evidence) {
    // A matched earlier FP/TCP pair remains a known primary cohort after more
    // TCP arrives. Revisions which put any earlier primary above its current
    // value still prevent subtraction; a later TCP total alone does not.
    Counts remaining = fp;
    for (auto const& [candidate,votes] : evidence.fp) {
        if (!remaining.count(candidate) || remaining.at(candidate) < votes) return {};
        remaining[candidate] -= votes;
    }
    return remaining;
}

inline double pendingFirst(Counts const& fp, Counts const& tcp, Flows const& flows,
    double priorOffset, std::optional<Evidence> const& evidence, int first, int second,
    double variation = 0) {
    double pending = std::max(0.0,total(fp)-total(tcp));
    if (pending == 0) return 0;
    double offset = evidenceOffset(priorOffset,evidence,first,second);
    bool comparable = evidence && evidence->first == first && evidence->second == second;
    if (comparable) {
        auto remaining = remainingCohort(fp,*evidence);
        if (remaining && total(*remaining) >= pending && total(*remaining) > 0)
            // With an exact current match this allocates the entire residual
            // FP vector. After another preference batch, its primary mix is
            // unknown: keep the last identified residual mix and apply it to
            // the smaller unfinished total. The new counted TCP stays fixed.
            return pending*expectedFirst(*remaining,flows,offset+variation)/total(*remaining);
    }
    // Without an identified batch, its primary mix is unknown. Estimate the
    // outstanding voters directly from the current FP mix and calibrated flows.
    // Subtracting counted TCP from a whole-FP estimate would falsely force all
    // remaining preferences to one side whenever that whole estimate is lower.
    return pending*expectedFirst(fp,flows,offset+variation)/total(fp);
}

}
