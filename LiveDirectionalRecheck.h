#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

// When a cumulative count grows, new ballots and corrections to earlier
// ballots can be published together. An established FP tally can then show
// a sizeable movement between the finalists which the indicative TCP has not
// picked up. Keep the measured counts intact and bound the possible revision
// to the affected ballots. Every observation below is from the same location.
namespace LiveDirectionalRecheck {
using Pair = std::pair<int,int>;
constexpr int MinimumBallots = 10000;
constexpr int MinimumMarginDifference = 150;

struct Account {
    // Both leads use the first finalist minus the second in votes. Subtracting
    // the TCP lead from the FP lead lets us track how far their movements
    // differ, even when one candidate normally receives stronger preferences.
    Pair finalists;
    int fpTotal, fpFirst, fpSecond, tcpFirst, tcpSecond;
    int tcpTotal() const { return tcpFirst + tcpSecond; }
    int leadDifference() const { return (fpFirst-fpSecond) - (tcpFirst-tcpSecond); }
    bool comparable() const {
        return std::min(fpTotal,tcpTotal()) >= MinimumBallots &&
            std::abs(fpTotal-tcpTotal()) <= .01 * fpTotal;
    }
};

struct Revision {
    Pair finalists;
    double votesToFirst; // Moving one ballot between finalists changes the margin by two.
};

class Tracker {
public:
    // Observe successive accepted accounts in source order. A withdrawal,
    // change of pair or explicitly reconciled count ends the comparison.
    // A missing/reset account is passed as an empty optional.
    void observe(std::optional<Account> current) {
        if (!current || !previous || current->finalists != previous->finalists) {
            *this = {};
            previous = current;
            return;
        }
        auto const old = *previous;
        auto const& now = *current;
        previous = now;
        // A small recheck can remove ballots. A withdrawal beyond the same
        // 1% coverage allowance breaks comparability with the original batch.
        if (initialMargin && std::min(now.fpTotal,now.tcpTotal()) < .99 * originalSize) clearRevision();
        if (!now.comparable()) {
            // Keep the original reference through asynchronous additions.
            // Wait for comparable coverage before using it in a forecast.
            revision.reset();
            return;
        }
        if (!initialMargin) {
            // These screening thresholds were trialled on the 2022 Victorian
            // cumulative Early counts. Small, asynchronous returns can have
            // different preference flows without any underlying recheck.
            // Require growing TCP and an actual loss of primaries by the side
            // disadvantaged by the discrepancy. Unchanged TCP already has
            // its own FP-revision treatment.
            int const difference = now.leadDifference() - old.leadDifference();
            bool const donorLostPrimaries = difference > 0 ? now.fpSecond < old.fpSecond : now.fpFirst < old.fpFirst;
            if (!old.comparable() || now.fpTotal < old.fpTotal || now.tcpTotal() <= old.tcpTotal() ||
                std::abs(difference) < MinimumMarginDifference || !donorLostPrimaries) return;
            initialMargin = difference;
            originalDifference = old.leadDifference();
            originalSize = std::min(old.fpTotal,old.tcpTotal());
        }
        // Later comparable counts can explain some or all of the discrepancy.
        // Ordinary additions cannot inflate it beyond the initially observed
        // movement or turn it into a correction in the opposite direction.
        int const sign = initialMargin > 0 ? 1 : -1;
        int const remaining = std::clamp(sign * (now.leadDifference()-originalDifference),0,std::abs(initialMargin));
        if (!remaining) { clearRevision(); return; }
        // Even the FP endpoint cannot move more ballots away from the other
        // finalist than are present there. Allow for outstanding FP without
        // treating a primary already counted in TCP as a transferable vote.
        int const pending = std::max(0,now.fpTotal-now.tcpTotal());
        int const donorTcp = sign > 0 ? now.tcpSecond : now.tcpFirst;
        int const donorFp = sign > 0 ? now.fpSecond : now.fpFirst;
        double const movable = std::max(0,donorTcp-std::max(0,donorFp-pending));
        revision = Revision{now.finalists, sign * std::min(.5*remaining,movable)};
    }
    std::optional<Revision> revision;

private:
    std::optional<Account> previous;
    int initialMargin = 0, originalDifference = 0, originalSize = 0;
    void clearRevision() { initialMargin = 0; revision.reset(); }
};

// A modelling assumption for an unresolved discrepancy: one third of draws
// retain the published TCP, one third apply the full FP-implied movement,
// and the middle third spreads uniformly between those two allocations.
inline double fraction(double quantile) {
    return std::clamp(3*quantile-1,0.,1.);
}
}
