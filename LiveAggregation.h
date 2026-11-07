#pragma once

#include "LiveData.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

// Calculate how much each seat contributes to reported election/region vote
// shares. SimulationIteration calls this after drawing a possible election:
// its seat shares and winners are already determined. Export diagnostics also
// use these helpers, with mean projected turnout instead of a simulated draw.
//
// Pre-election forecasts weight seats by their previous-election vote totals.
// In a live forecast, those totals can misrepresent seats that have since grown
// or shrunk. We gradually replace their relative weights with the existing live
// turnout model's estimated final totals. This affects reported aggregate vote
// shares and their uncertainty, without changing any seat result.
//
// The "turnout fraction" below is the proportion of that replacement completed:
// 0 uses only historical seat weights, 1 uses only estimated current weights,
// and 0.5 gives the two sets of weights equal influence. It is NOT the proportion
// of votes counted. Count progress determines it through turnoutFraction().
namespace LiveAggregation {
inline constexpr char Method[] = "live_turnout_phase_in_v1";
inline constexpr double FullTurnoutWeightFpCompletion = .5;

inline double fpCompletion(std::optional<LiveData::CountProgress> const& progress) {
    // Supply the counted fraction used to choose the aggregation weights.
    // Coverage is accepted published FP / prepared mean final formal votes for
    // the whole election. It is frozen for the snapshot, including across count
    // draws and possible revisions; TCP arrival/pairing and model confidence do
    // not change it. No progress account means the old aggregation still applies.
    if (!progress) return 0;
    if (!std::isfinite(progress->fpVotes) || progress->fpVotes < 0
        || !std::isfinite(progress->expectedFormalVotes) || progress->expectedFormalVotes <= 0)
        throw std::runtime_error("Invalid FP progress for live aggregation.");
    return std::clamp(progress->fpVotes / progress->expectedFormalVotes, 0., 1.);
}

inline double turnoutFraction(std::optional<LiveData::CountProgress> const& progress) {
    // Convert count progress to the proportion of weight given to current seat
    // sizes, as defined above. SimulationPreparation calculates this once per
    // snapshot so every simulated election uses the same stage of replacement.
    // This is a pragmatic compatibility transition, not a fitted measure of
    // turnout accuracy. Sharing a turnout prior with ordinary forecasts would
    // also change their regional/national reconciliation and seat predictions,
    // and require forecast-date-appropriate inputs. Keep that separate work out
    // of this correction to late live totals. Historical weighting is exact at
    // zero counts; estimated current sizes have full weight by 50% FP coverage.
    //
    // The smoothstep curve x*x*(3-2*x) starts and ends with zero slope: one tiny
    // first booth has negligible effect, and crossing 50% makes no abrupt step.
    // It gives 10.4%, 50%, 89.6% new weighting at 10%, 25%, 40% counted. The 50%
    // endpoint is an explicit operating assumption, not historical calibration.
    // Recompute from this snapshot alone, without remembering earlier values.
    double x = std::clamp(fpCompletion(progress) / FullTurnoutWeightFpCompletion, 0., 1.);
    return x*x*(3-2*x);
}

struct Weights {
    // Relative seat weights in project order. Each vector sums to the historical
    // total for convenient units; these blended values are not measured counts.
    // Only ratios matter. FP uses formal ballots; TPP uses continuing ballots,
    // which can be fewer under optional preferential voting.
    std::vector<double> fp, tpp;
    double turnoutFraction = 0;
};

inline std::vector<double> blend(std::vector<double> const& historical,
    std::vector<double> const& current, double fraction) {
    // Both inputs contain one seat's vote total per entry, in project order.
    // fraction is the influence of current relative seat sizes (0 to 1), not
    // count completion. Return relative weights on the historical total's
    // scale; only their ratios are used when averaging seat vote shares.
    if (historical.size() != current.size())
        throw std::runtime_error("Live aggregation seat populations differ.");
    auto validTotal = [](auto const& counts) {
        double sum = 0;
        for (double count : counts) {
            if (!std::isfinite(count) || count < 0)
                throw std::runtime_error("Invalid seat size for live aggregation.");
            sum += count;
        }
        if (!std::isfinite(sum) || sum <= 0)
            throw std::runtime_error("No positive seat sizes for live aggregation.");
        return sum;
    };
    double historicalTotal = validTotal(historical), currentTotal = validTotal(current);
    std::vector<double> weights(historical.size());
    for (std::size_t i = 0; i < weights.size(); ++i) {
        // First express each seat as a proportion of its election's total.
        // Otherwise an election with more voters would receive extra influence,
        // beyond the historical/current balance specified by fraction.
        double historicalShare = historical[i] / historicalTotal;
        double currentShare = current[i] / currentTotal;
        weights[i] = ((1-fraction)*historicalShare + fraction*currentShare)*historicalTotal;
    }
    return weights;
}

template<class TotalsForSeat>
Weights prepare(std::vector<double> const& historical,
    double preparedTurnoutFraction, TotalsForSeat totalsForSeat) {
    // Supply the seat weights for one simulated election. historical contains
    // previous-election seat totals; preparedTurnoutFraction is the influence
    // of current sizes defined at the top of this file. totalsForSeat retrieves
    // estimated final FP and continuing TPP totals for a given seat index from
    // this simulated election's live vote account.
    //
    // Main automatic live forecasts supply the fraction chosen during snapshot
    // preparation. Supporting baselines/calibrations and ordinary forecasts use
    // zero because their results must retain the pre-election weighting.
    // Returning the original counts before consulting the live account preserves
    // their arithmetic exactly and avoids introducing a new input requirement.
    // Only historical counts and the snapshot's historical/current balance may be
    // reused. Call totalsForSeat anew for every scenario; its final sizes can
    // differ because outstanding votes and signed count revisions are drawn.
    Weights result{historical, historical, preparedTurnoutFraction};
    if (result.turnoutFraction == 0) return result;
    std::vector<double> fp, tpp;
    fp.reserve(historical.size()); tpp.reserve(historical.size());
    for (std::size_t s = 0; s < historical.size(); ++s) {
        // Main iterations supply their existing scenario's final count totals;
        // diagnostics supply prepared means. No extra turnout draw is made.
        // An explicitly inactive contest supplies zeros, rather than silently
        // borrowing its historical size as though voting were still pending.
        auto totals = totalsForSeat(s);
        fp.push_back(totals.fp); tpp.push_back(totals.tpp);
    }
    result.fp = blend(historical,fp,result.turnoutFraction);
    result.tpp = blend(historical,tpp,result.turnoutFraction);
    return result;
}

template<class TotalsForSeat>
Weights prepare(std::vector<double> const& historical,
    std::optional<LiveData::CountProgress> const& progress, TotalsForSeat totalsForSeat) {
    // One-off callers (including export diagnostics) can still supply progress
    // directly. The iteration path prepares this fraction once per run instead.
    return prepare(historical, turnoutFraction(progress), totalsForSeat);
}
} // namespace LiveAggregation
