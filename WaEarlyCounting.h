#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

// WA's mixed early polling places can report election-day votes and pre-poll
// votes separately. This model describes routine FP additions at ONE place;
// it does not infer completion of preferences on those voters, or exclude a
// later correction. Other jurisdictions' incremental accounts do not use it.
namespace WaEarlyCounting {
inline constexpr char Version[] = "wa-early-batches-1";
struct State {
    int batches = 0;
    double smallOnly = 0, largeOnly = 0, both = 1;
};

// Counts are accepted formal FP totals, oldest first, ending at this snapshot.
// Repeated totals and removal/restoration below an earlier high do not establish
// another batch. The 100-vote / 1% allowance distinguishes substantial additions
// from small rechecks; it is an operational starting assumption, not a fit.
inline int substantialBatches(std::vector<double> const& counts) {
    double high = 0;
    int batches = 0;
    for (double count : counts) {
        if (count <= high) continue;
        if (high == 0 || count-high >= std::max(100., .01*high)) ++batches;
        high = count;
    }
    return std::min(2,batches);
}

// The clock is local hours since the epoch, with polling close supplied by
// election configuration. Time supports a combined report increasingly over
// the next calendar day, rather than twenty-four hours after each place reports.
// A missing date leaves the early mixture unchanged. An unreported place is
// handled by the existing turnout prior, never by this completion rule.
inline State prepare(double counted, std::vector<double> const& history,
    std::optional<double> now, std::optional<double> pollClose, bool complete) {
    State state;
    state.batches = substantialBatches(history);
    if (counted <= 0 || complete || state.batches >= 2) return state;
    state.batches = std::max(1,state.batches);
    double fade = 0;
    if (now && pollClose) {
        double nextMidnight = (std::floor(*pollClose/24)+1)*24;
        double position = std::clamp((*now-nextMidnight)/24,0.,1.);
        fade = position*position*(3-2*position);
    }
    // Below about 1,000 votes a small first component is the main explanation.
    // Size alone cannot settle it: retain 10-15% combined reports even on the
    // night, and smoothly move to combined reports during the following day.
    double smallBatchSupport = 1/(1+std::pow(counted/1000.,4));
    state.smallOnly = (1-fade)*.9*smallBatchSupport;
    state.largeOnly = (1-fade)*.85*(1-smallBatchSupport);
    state.both = 1-state.smallOnly-state.largeOnly;
    return state;
}

// A small first batch retains the existing broad turnout allowance. After a
// large first batch, the other component is usually much smaller: median 12%
// of votes already reported, with log-odds SD .6. These deliberately broad
// initial settings permit e.g. 2,009 votes after Geraldton's 11,923. They only
// REDUCE the existing allowance; they cannot take votes from another category.
// The fraction of that allowance uses a bounded scale, keeping additions valid.
inline double smallerRemainder(double counted, double broadAddition, double normal) {
    if (broadAddition <= 0) return 0;
    double median = .12*counted;
    double odds = median/std::max(.25,broadAddition-median);
    double logOdds = std::log(odds)+.6*normal;
    return broadAddition/(1+std::exp(-logOdds));
}
}
