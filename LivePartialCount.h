#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <iterator>
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
using Transfers = std::map<int,Counts>; // Source candidate -> counted finalist votes.

// This is the FP batch actually used for a published TCP, not whichever FP
// happens to accompany it in a later feed. Optional transfer details preserve
// Queensland's measured candidate flows without retaining the source XML.
struct TcpBasis {
    Counts fp, tcp;
    Transfers transfers;
    // This status belongs to the matched pair, not a later FP-only recheck.
    bool recheckedReconciledPair = false;
};
inline constexpr double ComparableTotalFraction = .02;
// An observed flow supplies this many non-finalist votes' worth of evidence
// before it receives half the weight. This limits dependence on a tiny batch.
inline constexpr double PriorPreferenceVotes = 500;
// Historical booth comparisons normally keep their full extrapolation weight.
// Near-unanimous transfers are sensitive to tiny FP/TCP discrepancies, whose
// effect becomes large on log odds. Preserve flows through the low 90s, then
// gradually stop using an extreme historical comparison to train other votes.
// This changes evidence weight, never the recorded TCP or a current matched
// cohort's measured flow. The limits apply symmetrically to either finalist.
inline constexpr double HistoricalFlowFullWeight = .92;
inline constexpr double HistoricalFlowZeroWeight = .98;

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

inline double historicalFlowReliability(double share) {
    double magnitude = std::max(share,1-share);
    double x = std::clamp((magnitude-HistoricalFlowFullWeight)/
        (HistoricalFlowZeroWeight-HistoricalFlowFullWeight),0.0,1.0);
    // Smoothstep gives both joins a zero slope. A small count revision near
    // either boundary cannot abruptly turn a comparison on or off.
    return 1-x*x*(3-2*x);
}

// When one TCP finalist changes, reporting booths can compare the shared
// finalist's current share with its old share. Usually several booths supply
// that movement. A first small booth must contribute only a small fraction of
// the seat's evidence: unreported and historically unmatched locations still
// belong in the expected-vote denominator, though they supply no comparison.
struct SharedFinalistTcpEvidence {
    // This retains the existing discount for comparing different opponents.
    static constexpr float MaximumConfidence = 0.1f;

    void addBooth(float expectedVotes, float transformedSwing = 0.0f,
        float comparisonWeight = 0.0f) {
        expectedSeatVotes += expectedVotes;
        weightedSwing += transformedSwing * comparisonWeight;
        comparisonVotes += comparisonWeight;
    }

    bool available() const { return comparisonVotes > 0.0f; }
    float meanSwing() const {
        return available() ? weightedSwing / comparisonVotes : 0.0f;
    }
    float confidence() const {
        return expectedSeatVotes > 0.0f
            ? MaximumConfidence * std::sqrt(std::clamp(
                comparisonVotes / expectedSeatVotes, 0.0f, 1.0f))
            : 0.0f;
    }

private:
    // Movement keeps its existing prior-vote/relevance weights. Coverage uses
    // the projected FP total of every seat location, including declarations.
    float weightedSwing = 0.0f;
    float comparisonVotes = 0.0f;
    float expectedSeatVotes = 0.0f;
};

struct PreferenceTransfer {
    double votes = 0, received = 0; // Non-finalist FP pool and votes reaching first.
    double share() const { return received/votes; }
};

inline std::optional<PreferenceTransfer> measureTransfer(Counts const& fp, Counts const& tcp,
    int first, int second) {
    double ft = total(fp), tt = total(tcp);
    if (ft <= 0 || tt <= 0 || tcp.size() != 2 || !tcp.count(first) || !tcp.count(second)
        || std::abs(ft-tt)/ft > ComparableTotalFraction) return {};
    double preferences = ft-value(fp,first)-value(fp,second);
    if (preferences <= 0) return {};
    // Normal counts share an FP/TCP total. Retain the existing normalization
    // for small discrepancies; the historical reliability guard uses this
    // inferred share before half-vote smoothing conceals a near-boundary rate.
    double received = value(tcp,first)*ft/tt-value(fp,first);
    if (received < 0 || received > preferences) return {};
    return PreferenceTransfer{preferences,received};
}

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

inline bool validBasis(TcpBasis const& basis) {
    if (basis.tcp.size() != 2 || total(basis.fp) <= 0 || total(basis.fp) != total(basis.tcp)) return false;
    for (auto const& [candidate,votes] : basis.fp) if (votes < 0) return false;
    for (auto const& [candidate,votes] : basis.tcp)
        if (!basis.fp.contains(candidate) || votes < value(basis.fp,candidate)) return false;
    if (basis.transfers.empty()) return true;
    std::map<int,double> reconstructed;
    for (auto const& [candidate,votes] : basis.fp) {
        if (basis.tcp.contains(candidate)) { reconstructed[candidate] += votes; continue; }
        auto row = basis.transfers.find(candidate);
        if (row == basis.transfers.end() || total(row->second) != votes) return false;
        for (auto const& [finalist,count] : row->second) {
            if (count < 0 || !basis.tcp.contains(finalist)) return false;
            reconstructed[finalist] += count;
        }
    }
    for (auto const& [candidate,counts] : basis.transfers)
        if (!basis.fp.contains(candidate) || basis.tcp.contains(candidate)) return false;
    if (reconstructed.size() != basis.tcp.size()) return false;
    for (auto const& [candidate,votes] : basis.tcp)
        if (!reconstructed.contains(candidate) || reconstructed.at(candidate) != votes) return false;
    return true;
}

struct RevisedTcp {
    double votes = 0, first = 0;
    // Feasible bounds on transfers affected by the revision. Finalists' own
    // revised primaries are exact. Removed ballots may be highly unrepresentative:
    // uncertainty scales with batch size, not its square root.
    double lower = 0, upper = 0;
    double uncertainVotes() const { return upper-lower; }
};

inline std::optional<RevisedTcp> reviseTcp(Counts const& fp, Counts const& tcp,
    TcpBasis const& basis, Flows const& flows, double offset, int first, int second,
    double variation = 0) {
    if (!validBasis(basis) || tcp != basis.tcp || !tcp.contains(first) || !tcp.contains(second)
        || fp.size() != basis.fp.size() || fp == basis.fp) return {};
    for (auto const& [candidate,votes] : fp)
        if (votes < 0 || !basis.fp.contains(candidate) || !flows.contains(candidate)) return {};
    // Swapping finalist order describes the same sampled allocation.
    if (first > second) variation = -variation;
    double pool = total(fp)-value(fp,first)-value(fp,second);
    double gain = value(tcp,first)-value(basis.fp,first);
    double lower = gain, upper = gain;
    for (auto const& [candidate,votes] : fp) {
        if (candidate == first || candidate == second) continue;
        double delta = votes-value(basis.fp,candidate);
        if (delta == 0) continue;
        double rate = preferenceRate(candidate,flows,offset);
        auto detail = basis.transfers.find(candidate);
        if (detail != basis.transfers.end() && value(basis.fp,candidate) > 0)
            rate = value(detail->second,first)/value(basis.fp,candidate);
        double lo = 0, hi = std::abs(delta);
        if (delta < 0 && detail != basis.transfers.end()) {
            // A removal cannot take more votes than this candidate originally
            // transferred to either finalist, even in an extreme scenario.
            lo = std::max(0.0,-delta-value(detail->second,second));
            hi = std::min(-delta,value(detail->second,first));
        }
        double change = std::clamp(std::abs(delta)*rate,lo,hi);
        if (variation != 0 && hi > lo) {
            // A unit-normal draw moves over the feasible batch range on log
            // odds. There is no switch to a whole-booth FP-only distribution.
            double fraction = (change-lo)/(hi-lo);
            change = lo+(hi-lo)*inverseLogOdds(logOdds(fraction)+2*variation);
        }
        if (delta > 0) { gain += change; lower += lo; upper += hi; }
        else { gain -= change; lower -= hi; upper -= lo; }
    }
    double primary = value(fp,first);
    lower = primary+std::clamp(lower,0.0,pool);
    upper = primary+std::clamp(upper,0.0,pool);
    return RevisedTcp{total(fp),std::clamp(primary+gain,lower,upper),lower,upper};
}

inline std::map<int,double> revisedLowerBounds(Counts const& fp, Counts const& tcp, TcpBasis const& basis) {
    if (tcp.size() != 2) return {};
    int first = tcp.begin()->first, second = std::next(tcp.begin())->first;
    Flows neutral;
    for (auto const& [candidate,votes] : fp)
        neutral[candidate] = candidate == first ? 1 : candidate == second ? 0 : .5;
    // The feasible endpoints do not depend on a flow prior. Turnout/count
    // constraints use these bounds, never the obsolete published TCP totals
    // and never the estimated centre as though it had been counted.
    auto revised = reviseTcp(fp,tcp,basis,neutral,0,first,second);
    return revised ? std::map<int,double>{{first,revised->lower},{second,revised->votes-revised->upper}}
        : std::map<int,double>{};
}

inline double projectFpOnly(Counts const& fp, Flows const& flows, double offset,
    double expectedVotes, double futureFirstShare) {
    // Received primaries already identify the major parties' own voters.
    // Preference uncertainty moves only transfers from other candidates; a
    // low seat/category projection cannot take away these counted primaries.
    // expectedVotes is the turnout target, and futureFirstShare (0..1) is the
    // surrounding estimate for voters whose FP has not yet been reported.
    // Normally a reported physical booth has no such future pool. Keeping the
    // two pools separate also handles an incomplete booth without extrapolating
    // its first batch to everyone still to arrive.
    double futureVotes = std::max(0.0, expectedVotes-total(fp));
    return expectedFirst(fp,flows,offset) + futureVotes*futureFirstShare;
}

inline std::optional<Evidence> measure(Counts const& fp, Counts const& tcp,
    Flows const& flows, int first, int second, bool matchingCurrentTcp = false) {
    auto transfer = measureTransfer(fp,tcp,first,second);
    if (!transfer) return {};
    double expected = (expectedFirst(fp,flows,0)-value(fp,first))/transfer->votes;
    if (expected <= 0 || expected >= 1) return {};
    // Half a vote keeps genuine all-one-way tiny batches finite. It becomes
    // negligible for the large batches which can meaningfully train a flow.
    double actual = (transfer->received+.5)/(transfer->votes+1);
    return Evidence{fp,first,second,logOdds(actual)-logOdds(expected),transfer->votes,matchingCurrentTcp,{},{}};
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
