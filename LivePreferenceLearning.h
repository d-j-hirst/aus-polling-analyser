#pragma once

// Prepare non-classic TCP preference adjustments once per live snapshot.
// Each measurement compares matched FP/TCP with the ranking model's transfer
// rates for those same voters. The forecast uses the result after exclusions.
#include "LivePartialCount.h"
#include "PreferenceModel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace LivePreferenceLearning {

using Pair = PreferenceModel::Pair;
inline constexpr int Ordinary = 0;

struct Evidence {
    double weight = 0, weightedOffset = 0, preferenceVotes = 0;
    int units = 0;

    // Each large reporting unit contributes at most one unit of information.
    // Four hundred transfers give half that weight. This first-pass scale
    // allows for variation across locations beyond ballot-counting noise.
    void add(double offset, double votes) {
        double influence = votes / (votes + 400.0);
        weight += influence;
        weightedOffset += influence * offset;
        preferenceVotes += votes;
        ++units;
    }
    double mean() const { return weight > 0 ? weightedOffset / weight : 0; }
};

// Fit one shift in the log odds of ranking the first finalist higher. Use
// each eliminated party's own prior rate and the measured size of its FP
// group. Aggregate TCP does not identify which party's rate was different.
inline std::optional<double> observedOffset(LivePartialCount::Counts const& fp,
    LivePartialCount::Counts const& tcp, LivePartialCount::Flows const& rates, Pair pair) {
    using namespace LivePartialCount;
    auto transfer = measureTransfer(fp,tcp,pair[0],pair[1]);
    if (!transfer) return {};
    bool adjustable = false;
    for (auto const& [party,votes] : fp) {
        auto rate = rates.find(party);
        if (rate == rates.end()) return {};
        if (votes > 0 && rate->second > 0 && rate->second < 1) adjustable = true;
    }
    if (!adjustable) return {};
    double target = value(fp,pair[0]) + transfer->votes * (transfer->received + .5) / (transfer->votes + 1);
    // Bound a single booth's influence on the pooled estimate. A shift of
    // two log-odds units already moves a 50% flow to about 12% or 88%.
    double lower = -2, upper = 2;
    for (int step = 0; step < 28; ++step) {
        double middle = (lower + upper) / 2;
        if (expectedFirst(fp,rates,middle) < target) lower = middle;
        else upper = middle;
    }
    return (lower + upper) / 2;
}

using Categories = std::map<int,Evidence>;
using Districts = std::map<int,Categories>;

struct Adjustment {
    double ordinary = 0, category = 0, local = 0;
    double retainedPriorSpread = 1;
    double otherDistrictWeight = 0, categoryDistrictWeight = 0, localWeight = 0;
    bool hasEvidence() const { return otherDistrictWeight > 0 || categoryDistrictWeight > 0 || localWeight > 0; }
    double mean() const { return ordinary + category + local; }
};

// Withhold the forecast district from both shared fits. Compare vote types
// within districts so the order in which places report does not turn a
// district difference into an apparent vote-type effect. A district supplies
// at most one effective observation to either fit.
inline Adjustment estimate(Districts const& districts, int target, int category) {
    Adjustment result;
    double ordinarySum = 0, differenceSum = 0;
    for (auto const& [district,types] : districts) {
        if (district == target) continue;
        auto ordinary = types.find(Ordinary);
        if (ordinary == types.end()) continue;
        double const ordinaryWeight = ordinary->second.weight / (ordinary->second.weight + 2);
        ordinarySum += ordinaryWeight * ordinary->second.mean();
        result.otherDistrictWeight += ordinaryWeight;
        auto group = types.find(category);
        if (category == Ordinary || group == types.end()) continue;
        double const pairedWeight = std::min(ordinary->second.weight,group->second.weight);
        double const contrastWeight = pairedWeight / (pairedWeight + 2);
        differenceSum += contrastWeight * (group->second.mean() - ordinary->second.mean());
        result.categoryDistrictWeight += contrastWeight;
    }
    // Two effective districts of prior information for each shared effect.
    // A sparse count leaves most of the ranking model's uncertainty intact.
    double const sharedPrior = 2 / (2 + result.otherDistrictWeight);
    result.ordinary = ordinarySum / (2 + result.otherDistrictWeight);
    result.category = differenceSum / (2 + result.categoryDistrictWeight);
    // Cross-district evidence cannot remove the target district's own
    // variation. Preserve nearly half the variance even with many peers.
    result.retainedPriorSpread = std::sqrt(.45 + .55 * sharedPrior);
    auto local = districts.find(target);
    if (local == districts.end()) return result;

    auto update = [&](int code, double reference) {
        auto found = local->second.find(code);
        if (found == local->second.end()) return 0.0;
        double const weight = found->second.weight / (found->second.weight + 2);
        result.localWeight += weight;
        result.retainedPriorSpread *= std::sqrt(1 - weight);
        return weight * (found->second.mean() - reference);
    };
    result.local = update(Ordinary,result.ordinary);
    if (category != Ordinary)
        result.local += update(category,result.mean());
    return result;
}

struct Projection {
    Adjustment adjustment;
    // Original first preferences in the two still-unallocated pools. The
    // first group is fixed by reported FP. The second is a prepared mix for
    // future voters, rescaled to the current scenario's total and party mix.
    std::map<int,double> pending, future;
};

// Prepare the primaries still awaiting a split. When the feed identifies
// the FP cohort used for its TCP, subtract those exact candidate totals.
// Otherwise the unfinished voters inherit the observed mix. A revision has
// already fixed all identifiable finalist primaries in the counted bounds,
// so its unresolved ballots belong to the preference pool alone.
inline std::map<int,double> pendingPrimaries(LivePartialCount::Counts const& fp,
    std::optional<LivePartialCount::Evidence> const& batch, Pair pair,
    double counted, bool revised) {
    double const pending = std::max(0.0,LivePartialCount::total(fp)-counted);
    if (pending == 0) return {};
    auto mix = fp;
    if (revised) {
        mix.erase(pair[0]);
        mix.erase(pair[1]);
    }
    else if (counted > 0 && batch && batch->matchingCurrentTcp &&
        ((batch->first == pair[0] && batch->second == pair[1]) ||
         (batch->first == pair[1] && batch->second == pair[0]))) {
        auto remaining = LivePartialCount::remainingCohort(fp,*batch);
        if (remaining && LivePartialCount::total(*remaining) > 0)
            mix = std::move(*remaining);
    }
    double const denominator = LivePartialCount::total(mix);
    std::map<int,double> result;
    if (pending > 0 && denominator > 0) for (auto const& [party,votes] : mix)
        if (votes > 0) result.emplace(party,pending*votes/denominator);
    return result;
}

inline double transferRate(double sampled, double reference, Adjustment const& adjustment,
    double categoryVariation) {
    if (sampled == 0 || sampled == 1) return sampled;
    if (!adjustment.hasEvidence() && categoryVariation == 0) return sampled;
    double const centre = LivePartialCount::logOdds(reference);
    double const draw = LivePartialCount::logOdds(sampled);
    return LivePartialCount::inverseLogOdds(centre + adjustment.mean() + categoryVariation +
        adjustment.retainedPriorSpread * (draw - centre));
}
}
