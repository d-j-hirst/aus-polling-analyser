#pragma once

#include <algorithm>
#include <cmath>

// Reconcile two pre-count estimates for a likely Labor–Greens final. The
// ordinary TPP estimates Labor against Liberal. Moving one Liberal primary
// point also gives Labor some of that point against the Greens, depending on
// the local Liberal ticket. Both contests therefore inform the same three FPs.
namespace SoftPairing {
struct Shift { double labor = 0, greens = 0, tpp = 0; }; // District percentage points.
struct Centre {
    double labor, liberal, greens, tpp; // percentages of all formal votes
    double laborSd, liberalSd, greensSd; // typical FP variation in points
    double otherToLabor; // points of TPP from everyone outside these three
    double greenIntercept, greenSlope; // Greens' Labor preference on log odds
    double liberalToLaborOverGreen; // fraction of Liberal FP voting Labor over Greens
};

inline double weight(double pairFraction) {
    // A 65% chance gives half weight. This is an eligibility assumption:
    // a rare simulated matchup supplies little information about the district.
    return std::clamp((pairFraction - .30) / .70, 0., 1.);
}

inline double logOdds(double p) {
    p = std::clamp(p, 1e-7, 1.-1e-7);
    return std::log(p/(1.-p));
}
inline double probability(double z) { return 1./(1.+std::exp(-z)); }

// Compare plausible FP revisions to their normal spreads. A three-point
// revision to either final share costs one unit of evidence, a trial choice.
// For each possible Greens share the cheapest Labor/Liberal split is the
// minimum of a quadratic. Search just the remaining Greens dimension once per
// district during preparation, keeping the three-party total fixed.
inline Shift fit(Centre const& c, double pairFraction) {
    double const credibility = weight(pairFraction);
    if (credibility == 0.) return {};
    double const liberalFlow = c.liberalToLaborOverGreen;
    double const laborPrecision = 1. / (c.laborSd * c.laborSd);
    double const liberalPrecision = 1. / (c.liberalSd * c.liberalSd);
    constexpr double finalShareVariance = 3. * 3.;
    struct Fit { double cost; Shift shift; };
    auto evaluate = [&](double greensChange) {
        double const greens = c.greens + greensChange;
        double const greenFlow = probability(c.greenIntercept + c.greenSlope * logOdds(greens * .01));
        double const tppGap = c.labor + greens * greenFlow + c.otherToLabor - c.tpp;
        double const pairLaborEffect = 1. - liberalFlow;
        double const pairPrecision = credibility / finalShareVariance;
        double const numerator = greensChange * liberalPrecision + tppGap / finalShareVariance
            - pairPrecision * pairLaborEffect * liberalFlow * greensChange;
        double const denominator = laborPrecision + liberalPrecision + 1. / finalShareVariance
            + pairPrecision * pairLaborEffect * pairLaborEffect;
        double const laborChange = std::clamp(-numerator / denominator, -c.labor,
            std::max(-c.labor, c.liberal - greensChange));
        double const liberalChange = -laborChange - greensChange;
        double const tppChange = tppGap + laborChange;
        double const pairChange = laborChange + liberalFlow * liberalChange;
        double const cost = laborPrecision * laborChange * laborChange
            + liberalPrecision * liberalChange * liberalChange
            + greensChange * greensChange / (c.greensSd * c.greensSd * credibility)
            + tppChange * tppChange / finalShareVariance + pairPrecision * pairChange * pairChange;
        // Taper the whole revision as well as the influence of the second
        // contest in the fit. This keeps every output continuous at 30%.
        return Fit{cost, {credibility * laborChange, credibility * greensChange, credibility * tppChange}};
    };
    double lo = -c.greens, hi = c.labor+c.liberal;
    for (int i=0; i<80; ++i) {
        double const left = (2*lo+hi)/3., right = (lo+2*hi)/3.;
        if (evaluate(left).cost < evaluate(right).cost) hi = right;
        else lo = left;
    }
    return evaluate((lo+hi)*.5).shift;
}

}
