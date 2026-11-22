#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

// Conditional Labor shares in a compulsory-preference district with both
// Coalition candidates. Each scenario uses the same primary voters in both
// counts. Only voters whose candidate is excluded can change destination.
namespace CoalitionPreference {
using Shares = std::map<int,double>;
inline constexpr double CentralLeakage = .20;
inline constexpr double LeakageLogOddsSd = .10/(CentralLeakage*(1-CentralLeakage));
inline constexpr double LeakageCorrelation = .25;
// Spread of the difference in minor-party flows between the two opponents,
// on log odds (about ten percentage points at an even split). This is the
// existing live fit's assumption, now carried from the pre-election draw.
inline constexpr double OpponentDifferenceSd = .10/.25;

inline double shiftedRate(double rate, double logOddsChange) {
    if (logOddsChange == 0. || rate == 0. || rate == 1.) return rate;
    double const multiplier = std::exp(logOddsChange);
    return rate*multiplier/(1.-rate+rate*multiplier);
}

struct Leakage {
    double liberal = CentralLeakage, national = CentralLeakage;
    bool nationalReference = false;
    double contribution(bool againstNational, double liberalFp, double nationalFp) const {
        return againstNational ? liberal*liberalFp : national*nationalFp;
    }
    double reference(double liberalFp, double nationalFp) const {
        return contribution(nationalReference,liberalFp,nationalFp);
    }
};

// Leakage is the proportion of an excluded partner's original voters who
// prefer Labor to the other partner. The two rates have a 20% centre and
// roughly ten points of spread near that centre. A modest positive correlation
// allows both parties' voters to be more willing to cross together. These are
// the assumptions previously used for the live pair-specific counts.
inline Leakage sample(bool nationalReference, double liberalNormal, double nationalNormal) {
    auto rate = [](double normal) {
        double const oddsAgainstLabor = (1.-CentralLeakage)/CentralLeakage;
        return 1./(1.+oddsAgainstLabor*std::exp(-LeakageLogOddsSd*normal));
    };
    return {rate(std::sqrt(1-LeakageCorrelation*LeakageCorrelation)*liberalNormal
        +LeakageCorrelation*nationalNormal),rate(nationalNormal),nationalReference};
}

// Identify the opponent in the historical TPP used to set the margin.
// A named classic count is the strongest evidence. Where only a non-classic
// final is recorded, use the sole or stronger historical Coalition candidate.
// A new seat with no comparison falls back to its configured major opponent.
inline bool historicalNational(bool countedLabor, bool countedLiberal, bool countedNational,
    double liberalPrimary, double nationalPrimary, bool configuredNational) {
    if (countedLabor && (countedLiberal != countedNational)) return countedNational;
    if (liberalPrimary != nationalPrimary) return nationalPrimary > liberalPrimary;
    return configuredNational;
}

// Once a TCP and FP estimate are available, the excluded partner cannot
// supply more Labor preferences than all its transfers combined. Conversely
// the other minor candidates cannot supply more preferences than their FP.
inline double feasibleLeakage(double rate, double partnerFp, double otherDonors, double laborTransfers) {
    if (partnerFp <= 0) return rate;
    double lower = std::max(0.,(laborTransfers-otherDonors)/partnerFp);
    double upper = std::min(1.,laborTransfers/partnerFp);
    // Mutually inconsistent reports cannot identify a feasible leakage rate.
    if (lower > upper) return rate;
    return std::clamp(rate,lower,upper);
}

struct Matchups {
    std::array<double,2> labor{}; // Percent of all formal votes, against LIB then NAT.
    std::array<bool,2> counted{}; // A positive TCP for that exact pair, including retained votes.
};
struct Scenario {
    Leakage leakage;
    Shares rates; // Fraction of each non-Coalition source preferring Labor.
    Shares priorFp; // Separate Liberal and National primaries in percentage points.
    Matchups results;
    double opponentDifference = 0.; // Change in log odds towards Labor against NAT relative to LIB.

    double minorRate(double referenceRate, bool againstNational) const {
        double const direction = int(againstNational)-int(leakage.nationalReference);
        return shiftedRate(referenceRate,direction*opponentDifference);
    }
};

// Start with the original Labor TPP against its fixed opponent. Changing
// the finalist swaps the excluded partner. A new primary return then moves
// each estimate by what the changed voters contribute at these same rates.
inline Matchups projectPrimaries(Scenario const& scenario, Shares const& current, int national,
    double referenceTpp, int others) {
    auto value = [](Shares const& shares, int id) {
        auto found = shares.find(id);
        return found == shares.end() ? 0. : found->second;
    };
    auto const& fp = scenario.priorFp;
    auto const& leakage = scenario.leakage;
    double referenceLeak = leakage.reference(value(fp,1),value(fp,national));
    // Count differences in both directions so a source disappearing from
    // the ballot removes exactly its original contribution.
    double movement = 0, alternateMovement = 0;
    auto rate = [&](int id) {
        if (id == 0) return 1.;
        if (id == 1 || id == national) return 0.;
        auto it = scenario.rates.find(id);
        return it == scenario.rates.end() ? scenario.rates.at(others) : it->second;
    };
    for (auto [id,n] : current) {
        double const referenceRate = rate(id);
        movement += (n-value(fp,id))*referenceRate;
        if (id != 0 && id != 1 && id != national) {
            // These voters are present even before any local count. The
            // historical margin pins their rate against one opponent only.
            alternateMovement += n*(scenario.minorRate(referenceRate,!leakage.nationalReference)-referenceRate);
        }
    }
    for (auto [id,n] : fp) if (!current.contains(id)) movement -= n*rate(id);
    Matchups result;
    for (int p = 0; p < 2; ++p) {
        double change = leakage.contribution(p != 0,value(current,1),value(current,national))-referenceLeak;
        if (p != int(leakage.nationalReference)) change += alternateMovement;
        result.labor[p] = std::clamp(referenceTpp+(movement+change),0.,100.);
    }
    return result;
}
}
