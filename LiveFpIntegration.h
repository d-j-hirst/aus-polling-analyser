#pragma once

#include "LiveData.h"
#include "LiveTurnoutMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// Combine a pre-election draw (including accepted movement shared across seats)
// with an independently generated local count estimate. Precision is inverse
// variance. All variances here use natural log odds, not percentages or the
// analyser's log-odds-times-25 convention.
namespace LiveFpIntegration {
inline constexpr double ShareFloor = 1e-7;
// The tiny floor makes genuine zero shares finite for moment measurement. It
// represents 0.004 votes in a 40,000-vote seat and is not added to counted data.
inline double logOdds(double share) {
    share = std::clamp(share, ShareFloor, 1-ShareFloor);
    return std::log(share/(1-share));
}
inline double shareFromLogOdds(double value) {
    return value >= 0 ? 1/(1+std::exp(-value)) : std::exp(value)/(1+std::exp(value));
}
inline double value(std::map<int,double> const& values, int id) {
    auto it = values.find(id);
    return it == values.end() ? 0 : it->second;
}

// Subdivide an already integrated group for the ranking count. Give the
// named candidate their counted FP, then their projected share of the
// group's outstanding votes. The group's size is the original simulation
// draw in percent of district FP. Neither the district's other candidates
// nor its estimated turnout needs to be recalculated for this subdivision.
inline double splitCountedCandidate(double groupShare, double totalProjected,
	double groupCounted, double groupProjected, double candidateCounted, double candidateProjected) {
	if (groupShare <= 0 || totalProjected <= 0 || candidateCounted <= 0) return 0;
	double const groupRemaining = std::max(0.0, groupProjected - groupCounted);
	double const candidateRemaining = std::max(0.0, candidateProjected - candidateCounted);
	double const futureFraction = groupRemaining > 0 ? std::min(1.0, candidateRemaining / groupRemaining) : 0.0;
	double const fixedShare = (candidateCounted / totalProjected) * 100.0;
	double const uncountedShare = std::max(0.0, groupShare - (groupCounted / totalProjected) * 100.0);
	return std::min(groupShare, fixedShare + uncountedShare * futureFraction);
}

// Missing candidates in an iteration are genuine zeros. Keep their implicit
// observations too; otherwise a rarely emerging candidate would incorrectly
// acquire the variance of only the iterations in which it was present.
struct MomentAccumulator {
    std::size_t samples = 0;
    std::map<int,std::size_t> observations;
    std::map<int,double> shares, odds, squaredOdds;
    std::map<std::pair<int,int>,double> products;
    void add(std::map<int,double> const& percentages) {
        ++samples;
        for (auto const& [id,percent] : percentages) {
            double p = percent/100., z = logOdds(p);
            ++observations[id]; shares[id] += p;
            odds[id] += z; squaredOdds[id] += z*z;
            for (auto const& [other,otherPercent] : percentages)
                if (other >= id) products[{id,other}] += p*otherPercent/100.;
        }
    }
    LiveData::FpDistributionMoments finish() const {
        LiveData::FpDistributionMoments result;
        if (!samples) return result;
        double zero = logOdds(0);
        for (auto const& [id,n] : observations) {
            double mean = (value(odds,id)+(samples-n)*zero)/samples;
            result.logOddsMean[id] = mean;
            result.logOddsVariance[id] = std::max(0.,
                (value(squaredOdds,id)+(samples-n)*zero*zero)/samples-mean*mean);
            result.shareMean[id] = value(shares,id)/samples;
        }
        for (auto const& [a,unused] : observations)
            for (auto const& [b,unusedB] : observations) if (b >= a) {
                auto it = products.find({a,b});
                result.shareCovariance[{a,b}] = (it == products.end() ? 0 : it->second/samples)
                    -result.shareMean.at(a)*result.shareMean.at(b);
            }
        return result;
    }
};

struct Distribution {
    double mean = 0, variance = 0;
    bool present = false;
    // A candidate absent in every baseline draw has no measured prior width.
    // Keep this identity separate from a genuinely precise positive share;
    // subtracting log-odds moments can leave round-off even for all zeros.
    bool allZero = false;
};
template<class Classify>
Distribution group(LiveData::FpDistributionMoments const& moments, int id, Classify classify) {
    std::vector<int> members;
    double share = 0, variance = 0;
    for (auto const& [candidate,p] : moments.shareMean)
        if (classify(candidate) == id) { members.push_back(candidate); share += p; }
    if (members.empty()) return {};
    if (members.size() == 1) {
        int candidate = members.front();
        return {moments.logOddsMean.at(candidate), moments.logOddsVariance.at(candidate), true, share == 0};
    }
    // A named candidate uses its exact draw moments. For a combined group,
    // transform the summed shares and their covariance by the local derivative
    // of log odds. In particular, Liberal/National and generic candidate votes
    // must not be counted twice or have their variances added independently.
    for (int a : members) for (int b : members) {
        auto it = moments.shareCovariance.find(std::minmax(a,b));
        if (it != moments.shareCovariance.end()) variance += it->second;
    }
    double p = std::clamp(share,ShareFloor,1-ShareFloor);
    return {logOdds(p),std::max(0.,variance)/std::pow(p*(1-p),2),true,share == 0};
}
inline double countWeight(double baselineVariance, double countVariance, double gate) {
    // A finite count must earn influence through both precision and breadth.
    // Measure count variance BEFORE phase-in: shrinking its draws first would
    // make weak early evidence look artificially precise. Zero gate leaves the
    // prior unchanged; a deterministic prior yields only to hard counted facts.
    if (!std::isfinite(baselineVariance) || !std::isfinite(countVariance) || !std::isfinite(gate)
        || baselineVariance < 0 || countVariance < 0 || gate < 0 || gate > 1)
        throw std::runtime_error("Invalid FP integration precision.");
    double prior = gate*baselineVariance;
    double sum = prior+std::max(0.,countVariance);
    return prior > 0 && sum > 0 ? prior/sum : 0;
}

inline std::map<int,double> constrainToCounted(
    LiveTurnoutMath::PartyAccount const& account, std::map<int,double> target) {
    double total = 0, counted = 0;
    for (auto const& [id,votes] : account.projected) {
        if (!std::isfinite(votes) || votes < 0) throw std::runtime_error("Invalid FP projected account.");
        total += votes;
    }
    for (auto const& [id,votes] : account.counted) {
        if (!std::isfinite(votes) || votes < 0) throw std::runtime_error("Invalid FP counted account.");
        counted += votes; target.try_emplace(id,0);
    }
    for (auto const& [id,p] : target)
        if (!std::isfinite(p) || p < 0) throw std::runtime_error("Invalid FP integration target.");
    if (total <= 0) throw std::runtime_error("FP integration has no projected vote account.");
    // These are numerical checks on a validated double-precision account, not
    // recovery from a feed error. Allow only accumulated arithmetic round-off.
    double const roundoff = 32 * std::numeric_limits<double>::epsilon()
        * std::max(total,counted) * (account.projected.size()+account.counted.size()+1);
    if (counted > total+roundoff)
        throw std::runtime_error("FP integration exceeds its counted account.");
    total = std::max(total,counted);
    double targetSum = 0;
    bool boundsSatisfied = true;
    for (auto const& [id,p] : target) {
        targetSum += p;
        if (total * (p / 100) < value(account.counted,id)) boundsSatisfied = false;
    }
    if (!std::isfinite(targetSum)) throw std::runtime_error("Invalid FP integration target total.");
    // A percentage allocation that already sums to 100 and respects every
    // counted lower bound needs no redistribution. Preserve it bit for bit at
    // any count stage. Non-normalised targets and active bounds follow the
    // ordinary allocation below; no small electoral change is clipped away.
    if (targetSum == 100 && boundsSatisfied) return target;
    std::map<int,double> votes;
    std::set<int> fixed;
    double remaining = total;
    // Allocate the target proportions subject to counted lower bounds. Freeze
    // a candidate at its recorded count if scaling would take votes away, then
    // redistribute only the outstanding total among the remaining candidates.
    // Usually no bound is active; when counting finishes these bounds determine
    // the entire result regardless of any residual pre-election uncertainty.
    while (fixed.size() < target.size()) {
        double weight = 0;
        for (auto const& [id,p] : target) if (!fixed.contains(id)) weight += p;
        std::vector<int> constrained;
        for (auto const& [id,p] : target) {
            if (fixed.contains(id)) continue;
            double proposed = weight > 0 ? remaining*p/weight : remaining/(target.size()-fixed.size());
            double floor = value(account.counted,id);
            if (proposed < floor) constrained.push_back(id);
        }
        if (!constrained.empty()) {
            for (int id : constrained) {
                votes[id] = value(account.counted,id); remaining -= votes[id]; fixed.insert(id);
            }
            continue;
        }
        for (auto const& [id,p] : target) if (!fixed.contains(id))
            votes[id] = weight > 0 ? remaining*p/weight : remaining/(target.size()-fixed.size());
        break;
    }
    std::map<int,double> result;
    for (auto const& [id,n] : votes) result[id] = double(100*n/total);
    return result;
}

template<class PriorClassify, class CountClassify>
std::map<int,double> integrate(LiveTurnoutMath::PartyAccount const& account,
    std::map<int,double> const& prior, LiveData::FpIntegrationInformation const& information,
    PriorClassify priorClassify, CountClassify countClassify) {
    // The conditional prior has already received accepted election/regional
    // movement, which is also present in the count estimate. Only their local
    // difference is precision weighted: a broad local draw must not replace a
    // narrow prior, or suppress a shared shift already supported across seats.
    if (information.evidenceGate == 0) {
        double counted = 0;
        for (auto const& [id,n] : account.counted) counted += n;
        if (counted == 0) return prior;
        return constrainToCounted(account,std::map<int,double>(prior.begin(),prior.end()));
    }
    double total = 0;
    for (auto const& [id,n] : account.projected) total += n;
    std::map<int,double> target;
    std::set<int> candidates;
    for (auto const& [id,p] : prior) candidates.insert(id);
    for (auto const& [id,n] : account.projected) candidates.insert(id);
    for (int id : candidates) {
        auto baseline = group(information.baseline,id,priorClassify);
        auto count = group(information.count,id,countClassify);
        // An observed configured candidate can have no separate pre-election
        // slot. Its share was supplied from generic Others, so use that source's
        // width too. Do not infer infinite precision from an absent candidate.
        if (!baseline.present || baseline.allZero)
            baseline = group(information.baseline,-1,priorClassify);
        double p = prior.contains(id) ? prior.at(id)/100. : 0;
        double live = account.projected.contains(id) ? account.projected.at(id)/total : 0;
        double weight = baseline.present && count.present
            ? countWeight(baseline.variance,count.variance,information.evidenceGate) : 0;
        // A newly represented candidate takes its prior from the source group
        // before this call; the normal case uses the actual correlated draw.
        // An unused count estimate cannot introduce log-odds floors or rounding
        // into this candidate's prior. Use percentages on both branches so the
        // ordinary count constraint can also recognise an unchanged full draw.
        if (weight == 0 || p == live) {
            target[id] = prior.contains(id) ? prior.at(id) : 0;
        }
        else {
            target[id] = 100*shareFromLogOdds((1-weight)*logOdds(p)+weight*logOdds(live));
        }
    }
    return constrainToCounted(account,std::move(target));
}
} // namespace LiveFpIntegration
