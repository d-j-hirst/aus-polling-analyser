#pragma once

// Fit a district's primary movements jointly from ordinary election-day booths.
// Each row describes one location on the current ballot. It keeps its opening
// political geography, even when another location is the first to report.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>
#include "LiveFpSupportGradient.h"

namespace LiveOrdinaryUpdate {
using Mix = std::vector<double>;
struct Venue {
    Mix opening, votes;
    double turnout;
    bool ordinary;
    Mix historicalSupportTilts{};
};

inline double sum(Mix const& mix) { return std::accumulate(mix.begin(),mix.end(),0.); }

inline Mix tilted(Mix const& opening, Mix const& factors) {
    Mix shares(opening);
    for (size_t j=0; j<shares.size(); ++j) shares[j] *= factors[j];
    double const total = sum(shares);
    if (total>0) for (double& share : shares) share /= total;
    return shares;
}

// Express the election and regional movements as a change in each party's
// odds, then put all parties back on a common denominator. The simulator
// also needs this reference to centre its correlated pre-election draws.
inline Mix conditionedShares(Mix mix, Mix const& sharedOdds) {
    for (size_t j=0; j<mix.size(); ++j) {
        double p = mix[j];
        mix[j] = p*sharedOdds[j]/(1-p+p*sharedOdds[j]);
    }
    double mass = sum(mix);
    for (double& p : mix) p /= mass;
    return mix;
}

// Find one multiplier per candidate so that, after balancing shares within
// each booth, the turnout-weighted totals match the specified ballot counts.
// Failure means that the supplied geography cannot support the targets.
inline std::optional<Mix> fit(std::vector<Mix> const& rows, Mix const& weights, Mix const& targets) {
    Mix factors(targets.size(),1.);
    double const tolerance = 1e-9*sum(weights);
    for (int attempt=0; attempt<200; ++attempt) {
        Mix predicted(targets.size());
        for (size_t b=0; b<rows.size(); ++b) {
            auto shares = tilted(rows[b],factors);
            if (!(sum(shares)>0)) return {};
            for (size_t j=0; j<shares.size(); ++j) predicted[j] += weights[b]*shares[j];
        }
        bool done = true;
        for (size_t j=0; j<targets.size(); ++j) {
            if (std::abs(predicted[j]-targets[j])>tolerance) done = false;
            if (predicted[j]<=0 && targets[j]>tolerance) return {};
        }
        if (done) return factors;
        for (size_t j=0; j<targets.size(); ++j)
            if (predicted[j]>0) factors[j] *= targets[j]/predicted[j];
    }
    return {};
}

// Return the forecast mix of *outstanding* ballots. First reconcile the
// uncounted booth geography with the pre-election district mix, then apply
// the same regional and statewide movements used by the simulation draw.
// A booth's FP return supplies local evidence once. Unchanged records, a
// skipped snapshot and a different delivery order all give the same fit.
inline std::optional<Mix> remaining(std::vector<Venue> const& booths, Mix prior, Mix const& sharedOdds) {
    constexpr double BoothCap = 500.;
    std::vector<Mix> rows;
    Mix sizes;
    for (auto const& booth : booths) {
        rows.push_back(booth.opening);
        sizes.push_back(booth.turnout);
    }
    double total = sum(sizes);
    if (total<=0 || prior.empty()) return {};
    Mix target(prior);
    for (double& share : target) share *= total;
    auto openingFit = fit(rows,sizes,target);
    if (!openingFit) return {};
    prior = conditionedShares(prior,sharedOdds);
    std::vector<Mix> evidence{prior};
    Mix weights{0.}; // The first row represents the opening estimate in each fit below.
    Mix effectiveVotes(prior.size(),0.);
    Mix outstanding(booths.size());
    std::vector<size_t> ordinaryPending, otherPending;
    for (size_t b=0; b<booths.size(); ++b) {
        auto const& booth = booths[b];
        auto balanced = tilted(rows[b],*openingFit);
        if (!(sum(balanced)>0)) return {};
        rows[b] = conditionedShares(std::move(balanced),sharedOdds);
        double n = sum(booth.votes);
        outstanding[b] = std::max(0.,booth.turnout-n);
        if (outstanding[b]>0)
            (booth.ordinary ? ordinaryPending : otherPending).push_back(b);
        if (!booth.ordinary || n<=0) continue;
        // 500 people at one site contribute 250 effective ballots about
        // other sites. All 500 remain in the actual counted account.
        double reliability = 1./(1.+n/BoothCap);
        for (size_t j=0; j<effectiveVotes.size(); ++j) {
            if (rows[b][j]==0 && booth.votes[j]>0) return {};
            effectiveVotes[j] += reliability*booth.votes[j];
        }
        weights.push_back(n*reliability);
        evidence.push_back(rows[b]);
    }
    double const future = sum(outstanding);
    if (weights.size()==1 || future<=0) return {};

    // The same ordinary returns tell us more about other election-day booths
    // than about early centres, postal voters or other categories. The replay
    // trial in analysis/reports/booth-fp-update.md used opening estimates
    // worth 250 and 1,000 ballots respectively. These are calibration choices.
    // Keep each location's geography and combine its projected *outstanding*
    // ballots. A partially counted venue contributes only its remaining size.
    Mix result(prior.size());
    auto addGroup = [&](std::vector<size_t> const& pending, double priorVotes) {
        if (pending.empty()) return true;
        weights.front() = priorVotes;
        Mix targets = effectiveVotes;
        for (size_t j=0; j<prior.size(); ++j) targets[j] += priorVotes*prior[j];
        auto shift = fit(evidence,weights,targets);
        if (!shift) return false;
        for (size_t b : pending) {
            auto shares = tilted(rows[b],*shift);
            LiveFpSupportGradient::adjust(shares,booths[b].historicalSupportTilts);
            for (size_t j=0; j<shares.size(); ++j) result[j] += outstanding[b]*shares[j];
        }
        return true;
    };
    if (!addGroup(ordinaryPending,250.) || !addGroup(otherPending,1000.)) return {};
    for (double& p : result) p /= future;
    return result;
}
}
