#pragma once

// Test whether a party is systematically gaining or losing more at its old
// strong booths than at its weak ones. The seat-wide movement is already
// forecast elsewhere. Only the *difference between places* is used here.
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

namespace LiveFpSupportGradient {
using Mix = std::vector<double>;
inline constexpr int MinimumBooths = 4;

// Half a ballot on each side keeps a measured zero or unanimous count usable.
// Callers separately check that the candidate was present and votes were counted.
inline double measuredShare(int candidate, int formal) {
    return (candidate + .5) / (formal + 1.);
}

struct Observation {
    int pastVotes, pastFormal, currentVotes, currentFormal;
    double openingShare;
};

struct Fit {
    double slope, centre, minimum, maximum;
    double tilt(int votes, int formal) const {
        // Beyond the strongest or weakest place already seen, carry the last
        // measured difference forward instead of extending the line indefinitely.
        return slope * (std::clamp(measuredShare(votes, formal), minimum, maximum) - centre);
    }
};

inline std::optional<Fit> fit(std::vector<Observation> const& booths) {
    struct Point { double strength, change, weight; };
    std::vector<Point> points;
    double weights = 0., weightedStrength = 0., weightedChange = 0.;
    double minimum = 1., maximum = 0.;
    for (auto const& b : booths) {
        if (b.pastFormal <= 0 || b.currentFormal <= 0 || !(b.openingShare > 0. && b.openingShare < 1.)) continue;
        double x = measuredShare(b.pastVotes, b.pastFormal);
        double q = measuredShare(b.currentVotes, b.currentFormal);
        double y = std::log(q / (1-q)) - std::log(b.openingShare / (1-b.openingShare));
        // Even a very large count describes only one place. The trial allows
        // 0.30 log odds of booth variation as well as counting noise in both
        // elections. At 50% this is about seven percentage points.
        double w = 1. / (.30*.30 + 1./(b.currentFormal*q*(1-q)) + 1./(b.pastFormal*x*(1-x)));
        points.push_back({x,y,w});
        weights += w;
        weightedStrength += w*x;
        weightedChange += w*y;
        minimum = std::min(minimum,x);
        maximum = std::max(maximum,x);
    }
    if (points.size() < MinimumBooths) return {};
    double centre = weightedStrength/weights, meanChange = weightedChange/weights;
    double spread = 0., covariance = 0.;
    for (auto const& p : points) {
        double dx = p.strength-centre;
        spread += p.weight*dx*dx;
        covariance += p.weight*dx*(p.change-meanChange);
    }
    if (spread < 1e-8) return {};
    Mix slopes;
    for (auto const& p : points) {
        // Refit with this booth removed. If just one booth supplies all the
        // contrast in historical support, the remaining data cannot confirm it.
        double dx = p.strength-centre, w = p.weight*weights/(weights-p.weight);
        double remainingSpread = spread-w*dx*dx;
        if (remainingSpread <= 1e-8) return {};
        slopes.push_back((covariance-w*dx*(p.change-meanChange))/remainingSpread);
    }
    double n = static_cast<double>(slopes.size());
    double mean = std::accumulate(slopes.begin(),slopes.end(),0.)/n;
    double variation = 0.;
    for (double s : slopes) variation += (s-mean)*(s-mean);
    double error = std::max(1./std::sqrt(spread),std::sqrt(variation*(n-1)/n));
    double raw = covariance/spread;
    // Trial settings: subtract a 90% normal uncertainty allowance and shrink
    // the remainder towards zero using a prior SD of two log odds per unit
    // of historical share. This is an evidence screen for the central forecast.
    double magnitude = (std::abs(raw)-1.645*error)/(1.+error*error/4.);
    if (magnitude <= 0.) return {};
    return Fit{std::copysign(magnitude,raw),centre,minimum,maximum};
}

// Apply all candidates' changes to the same outstanding ballots. One signal
// alone moves that candidate's odds against all rivals, in proportion to
// their support. Opposite signals partly describe the same transfer between
// two rivals: use the larger of those two signals in their mutual comparison.
// Input can be votes or shares. Turnout and zero starting shares are preserved.
inline void adjust(Mix& votes, Mix const& tilts) {
    if (tilts.empty()) return;
    bool hasSignal = false;
    for (size_t i=0; i<votes.size(); ++i) {
        if (votes[i]>0. && tilts[i]!=0.) { hasSignal = true; break; }
    }
    if (!hasSignal) return;
    double total = std::accumulate(votes.begin(),votes.end(),0.);
    Mix changes(votes.size(),0.);
    double maxChange = std::numeric_limits<double>::lowest();
    for (size_t i=0; i<votes.size(); ++i) {
        if (votes[i]==0.) continue;
        for (size_t j=0; j<votes.size(); ++j) {
            double difference = tilts[i]-tilts[j];
            if (tilts[i]*tilts[j] < 0.)
                difference -= std::copysign(std::min(std::abs(tilts[i]),std::abs(tilts[j])),tilts[i]);
            changes[i] += difference*votes[j]/total;
        }
        maxChange = std::max(maxChange,changes[i]);
    }
    double adjustedTotal = 0.;
    for (size_t i=0; i<votes.size(); ++i) {
        if (votes[i]==0.) continue;
        votes[i] *= std::exp(changes[i]-maxChange);
        adjustedTotal += votes[i];
    }
    for (double& v : votes) v *= total/adjustedTotal;
}
}
