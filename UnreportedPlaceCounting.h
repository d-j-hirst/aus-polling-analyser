#pragma once

#include <algorithm>
#include <cmath>
#include <string>

// An empty physical reporting account is not a small reported booth. It may
// eventually receive a normal-sized return, or never be used at this election.
// Keep that yes/no question separate from the number of formal votes if it
// does report. These portable rules do not estimate candidate shares or move
// a turnout shortfall into another voting category.
namespace UnreportedPlaceCounting {
inline constexpr char Version[] = "unreported-places-1";
inline constexpr double PublicSizeSd = .6; // Natural log odds of votes/enrolment.
inline constexpr double OfficeMedian = 40; // Formal votes, conditional on a return.
inline constexpr double OfficeSizeSd = 1.4;
inline constexpr double UnconfirmedOddsDivisor = 20;

enum class Role { None, Public, Office };

// Most ordinary booths and special services retain their existing treatment.
// Public PPVCs and federal Divisional Offices have distinct late-return evidence.
// BLV/EAV services and mixed early/day centres do not follow either procedure.
inline Role role(std::string const& kind, std::string const& name, bool eav, bool incremental) {
    if (eav || incremental || name.find("BLV") != std::string::npos
        || name.rfind("EAV ",0) == 0) return Role::None;
    if (kind != "declaration" && name.find("Divisional Office") != std::string::npos) return Role::Office;
    return kind == "ppvc" ? Role::Public : Role::None;
}

inline double logistic(double value) {
    return value >= 0 ? 1/(1+std::exp(-value)) : std::exp(value)/(1+std::exp(value));
}
inline double smoothPhase(double days, double duration) {
    double fraction = std::clamp(days/duration,0.,1.);
    return fraction*fraction*(3-2*fraction);
}

// Days are measured from polling close on the feed's source clock. The knots
// are conservative working curves informed by all empty accounts, including
// permanent placeholders, in the 2013/2016/2019 federal archives; 2022 provides
// a slower-reporting check. They are not precise fitted frequencies. A used
// historical centre (or an explicitly confirmed new public centre) retains a
// substantial chance of a late return: Woden 2016 and Greenfields 2013 first
// reported large accounts after nine or ten days. Offices that eventually
// report usually contain only a few dozen votes. See docs/live-turnout.md.
//
// Interpolate on log odds so the probability is continuous and stays between
// zero and one. Beyond four weeks the odds halve weekly, rather than leaving
// a permanent residual allowance. Unconfirmed public listings divide these
// odds by 20; their presence in a preload is not proof they will operate.
// During the first day, smoothly move from the initial account (probability 1)
// to the first knot. Thus a first tiny booth cannot switch the rules abruptly.
inline double returnProbability(Role type, bool confirmed, double days) {
    if (type == Role::None) return 1;
    constexpr double ages[] = {1,2,3,5,7,10,14,21,28};
    constexpr double publicProbabilities[] = {.95,.85,.70,.50,.40,.30,.15,.05,.01};
    constexpr double officeProbabilities[] = {.15,.08,.06,.05,.04,.02,.005,.001,.0002};
    auto const* values = type == Role::Office ? officeProbabilities : publicProbabilities;
    auto logOdds = [](double probability) { return std::log(probability/(1-probability)); };
    double location = logOdds(values[0]);
    for (int k = 1; k < 9; ++k) {
        if (days <= ages[k]) {
            double fraction = std::clamp((days-ages[k-1])/(ages[k]-ages[k-1]),0.,1.);
            location = logOdds(values[k-1])+fraction*(logOdds(values[k])-logOdds(values[k-1]));
            break;
        }
        location = logOdds(values[k]);
    }
    if (days > 28) location -= (days-28)*std::log(2.)/7;
    if (type == Role::Public && !confirmed) location -= std::log(UnconfirmedOddsDivisor);
    double phase = smoothPhase(days,1);
    return 1-phase+phase*logistic(location);
}

// An Office return uses a broad positive distribution, not the old ordinary
// booth's thousands of votes. In a large available count, its median is about
// 40 votes, mean about 107, 95th percentile about 400 and 99th about 1,040.
// These round, conservative settings reflect 15 final positive Office returns
// still empty at 48 hours in 2013/2016/2019 (2-378 votes; geometric mean 43,
// natural-log SD 1.41). Large temporary reclassified accounts are not extra
// final physical turnout. Expressing size as odds within reserved enrolment
// keeps every outcome physically possible even in a nearly complete seat.
inline double officeSize(double capacity, double normal) {
    if (capacity <= 0) return 0;
    return capacity*logistic(std::log(OfficeMedian/capacity)+OfficeSizeSd*normal);
}
}
