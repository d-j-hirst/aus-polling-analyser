#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Allocate possible additional formal votes after accounting for a selected
// postal finishing amount. Counted votes and unfinished physical-booth estimates
// sit outside this account. Its unused portion represents non-voting/informality,
// so a change in one declaration category need not be offset vote-for-vote by
// another. The portable Python equivalent is turnout/declaration_allocation.py.
namespace TurnoutModel {
// parent is the available number of enrolled people in this draw: declaration
// additions plus unused capacity. logOdds are natural logs of each category's
// weight relative to the unused portion (whose weight is one). postalLimits are
// sampled finishing remainders, in votes; infinity means unconstrained. They are
// scenario quantities, not known final totals or administrative completion flags.
//
// Usually ordinary proportional allocation suffices. When it would exceed a
// postal finishing amount, reserve that amount first, then divide the remainder
// between other categories and unused turnout in their existing proportions.
// Thus a discarded large postal allowance cannot suppress another category.
// Non-postal local finishing limits are deliberately applied by the caller
// afterwards; redistributing their reductions would change a separate model.
inline std::vector<double> allocateDeclarationAdditions(double parent,
    std::vector<double> const& logOdds, std::vector<double> const& postalLimits) {
    std::vector<double> additions(logOdds.size());
    std::vector<bool> reserved(logOdds.size(),false);
    double remaining = parent;
    for (;;) {
        // Recompute the scale after fixing a postal amount. Even a discarded
        // extremely large weight must not cause the surviving weights to round
        // to zero. The unused term always remains available.
        double maximum = 0;
        for (std::size_t k = 0; k < logOdds.size(); ++k)
            if (!reserved[k]) maximum = std::max(maximum,logOdds[k]);
        double denominator = std::exp(-maximum);
        for (std::size_t k = 0; k < logOdds.size(); ++k)
            if (!reserved[k]) denominator += std::exp(logOdds[k]-maximum);
        bool constrained = false;
        double newlyReserved = 0;
        for (std::size_t k = 0; k < logOdds.size(); ++k) {
            if (reserved[k]) continue;
            additions[k] = remaining*std::exp(logOdds[k]-maximum)/denominator;
            if (additions[k] > postalLimits[k]) {
                additions[k] = postalLimits[k];
                newlyReserved += additions[k];
                reserved[k] = true;
                constrained = true;
            }
        }
        if (!constrained) return additions;
        remaining = std::max(0.,remaining-newlyReserved);
        // Every repeat fixes at least one service permanently. A single postal
        // account needs at most one repeat; this is not statistical fitting.
    }
}
}
