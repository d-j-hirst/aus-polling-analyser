#pragma once

#include "LiveData.h"
#include "LiveTurnoutMath.h"
#include "LiveBoothGeography.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

// Combine a district's pre-election draw, shared regional movement and local
// FP evidence. Ordinary returns can be compared with their own earlier booth
// expectations. Candidates without that comparison use the booth projection.
// Precision is inverse variance; variances here are measured on natural log odds.
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

// Divide the remaining anonymous vote once, after honouring named priors.
// With four candidates in Others, two newly identified parties each receive
// one quarter of the original pool. Splitting successively would make their
// forecasts depend on which name came first. Use consistent units for every
// share: percentages in a simulation draw, or vote fractions in preparation.
inline void assignMissingPartyPriors(std::map<int,double>& prior, int others,
    std::vector<int> const& parties, int poolSize, double reservedIndependentShare) {
    if (parties.empty()) return;
    double const available = value(prior, others) - reservedIndependentShare;
    if (available <= 0) return;
    double const perCandidate = available / std::max(poolSize, int(parties.size()));
    int assigned = 0;
    for (int party : parties) {
        if (value(prior, party) > 0) continue;
        prior[party] = perCandidate;
        ++assigned;
    }
    if (assigned > 0)
        prior[others] = reservedIndependentShare + std::max(0.0, available - assigned * perCandidate);
}

// Starting vote fractions whose variation can be used with ordinary-booth FP.
// A registered party may already have a share assigned to it out of Others.
// For an independent the pre-election draws must belong to the person on the
// ballot. Draws for a possible emerging independent include elections where
// nobody emerged. They cannot measure the spread for a person now receiving
// votes. Without their own prior, that candidate keeps the booth model's draw.
inline std::map<int,double> ordinaryCandidatePriors(LiveData::FpIntegrationInformation const& information,
    int others, int independent) {
    auto priors = information.baseline.shareMean;
    assignMissingPartyPriors(priors, others, information.partiesFromOthers, information.othersPoolSize, 0.);
    if (independent >= 0 && !information.namedIndependentMatches) priors.erase(independent);
    return priors;
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

// Recover a candidate from a group that has already been updated against the
// pre-count forecast. All candidates in the group use the same projected
// and counted totals, so each outstanding vote is scaled by the same factor.
// The account contains one simulation's votes. The group share and result
// are percentages of the whole district, including its counted votes.
template<class IsPooled>
double splitCountedCandidate(LiveData::VoteCountAccount const& account, double groupShare,
    int candidate, IsPooled isPooled) {
    double totalProjected = 0, groupProjected = 0, groupCounted = 0;
    for (auto const& [id, votes] : *account.projected) {
        totalProjected += votes;
        if (isPooled(id)) groupProjected += votes;
    }
    for (auto const& [id, votes] : *account.counted)
        if (isPooled(id)) groupCounted += votes;
    return splitCountedCandidate(groupShare, totalProjected, groupCounted, groupProjected,
        value(*account.counted, candidate), value(*account.projected, candidate));
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

// The blind composition pass supplies a size and candidate share for every
// venue, including declarations. An ordinary return may give a recorded zero.
// An absent candidate count cannot establish that zero or a historical swing.
struct BoothReference {
    double size, share, counted;
    std::optional<double> ordinaryCandidateVotes;
    bool matched;
};

inline LiveData::FpBoothComparison compareBooths(std::vector<BoothReference> const& booths,
    std::optional<double> priorShare, double boothRemainingShare) {
    LiveData::FpBoothComparison result;
    double observed = 0;
    for (auto const& b : booths) if (b.ordinaryCandidateVotes) {
        observed += *b.ordinaryCandidateVotes;
        result.ordinary += b.counted;
        if (b.matched && priorShare) {
            result.matched += b.counted;
            result.largestMatched = std::max(result.largestMatched,b.counted);
        }
    }
    // Both centres are needed to measure how far each simulated draw varies
    // from its usual level, even when no reporting booth has a historical match.
    if (priorShare) result.priorCentre = logOdds(*priorShare);
    result.boothRemainingCentre = logOdds(boothRemainingShare);
    if (result.matched == 0) return result;

    // Compare with expectations for the actual reporting venues. First put
    // all locations on the same district level as the pre-election forecast
    // conditioned on shared movement. A common odds shift keeps strong and
    // weak locations in order. With a 9% district forecast, a separately
    // prepared set of booth estimates averaging 1% must not interpret a 3%
    // return as a surge simply because it started on a different level.
    std::vector<LiveBoothGeography::Point> geography;
    for (auto const& b : booths) geography.push_back({logOdds(b.share),b.size});
    double const shift = LiveBoothGeography::commonShift(geography,priorShare.value());
    double expected = 0, futureVotes = 0, futureFormal = 0;
    for (size_t i = 0; i < booths.size(); ++i) {
        auto const& b = booths[i];
        double share = shareFromLogOdds(geography[i].odds+shift);
        if (b.ordinaryCandidateVotes) expected += b.counted*share;
        double remaining = std::max(0.,b.size-b.counted);
        futureVotes += remaining*share;
        futureFormal += remaining;
    }
    // The half ballot on either side makes an actual 0% or 100% count usable
    // on the log-odds scale. It never enters the counted ballot account.
    result.localMovement = std::log((observed+.5)/(result.ordinary-observed+.5))
        - std::log((expected+.5)/(result.ordinary-expected+.5));
    result.priorRemainingCentre = logOdds(futureFormal > 0 ? futureVotes/futureFormal : priorShare.value());
    return result;
}

inline double breadthResponse(LiveData::FpBoothComparison const& b, double precision) {
    double const breadth = b.matched > 0 ? 1-b.largestMatched/b.matched : 0;
    // Only ballots outside the largest comparable venue confirm that more
    // than one place has reported. The trial allowance of one half tapers
    // twice with the unused response: it has little effect once the ordinary
    // precision update already follows most of the observed movement.
    return precision + .5*breadth*(1-precision)*(1-precision);
}

inline double ordinaryRemainingShare(LiveData::FpBoothComparison const& b,
    double prior, double boothRemaining, double precision) {
    // A ballot candidate with no prior of their own or allocated from Others
    // has only the booth distribution. Usually both sources supply a draw.
    if (!b.priorCentre) return boothRemaining;
    double const boothOdds = logOdds(boothRemaining);
    double const drawDeviation = (1-precision)*(logOdds(prior)-*b.priorCentre)
        + precision*(boothOdds-b.boothRemainingCentre);

    // Historical coverage chooses the central estimate. Both estimates use
    // the same precision-weighted variation. Only the central response to a
    // matched result gains strength when several booths have reported.
    double matchedShare = 0;
    if (b.matched > 0) {
        double const localCentre = b.priorRemainingCentre + breadthResponse(b,precision)*b.localMovement;
        matchedShare = shareFromLogOdds(localCentre+drawDeviation);
        if (b.matched == b.ordinary) return matchedShare;
    }
    // A single unmatched booth can set the expected vote without imposing
    // its very wide range on the district. A genuinely empty or unanimous
    // outstanding pool keeps its endpoint.
    bool const fixedEndpoint = (boothRemaining == 0 || boothRemaining == 1) && boothOdds == b.boothRemainingCentre;
    double const unmatchedShare = precision == 1 || fixedEndpoint ? boothRemaining
        : shareFromLogOdds(b.boothRemainingCentre + drawDeviation);
    return b.matched == 0 ? unmatchedShare : std::lerp(unmatchedShare,matchedShare,b.matched/b.ordinary);
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

struct OrdinaryTarget {
    double remainingShare, fractionReported;
};

inline std::map<int,double> allocateOutstanding(LiveTurnoutMath::PartyAccount const& account,
    std::map<int,double> integrated, std::map<int,OrdinaryTarget> const& ordinaryTargets,
    double total, double counted) {
    if (ordinaryTargets.empty()) return integrated;
    // The trial measures ordinary venues. As other modes report, give their
    // established count integration its share of the evidence. Never treat
    // successive postal batches as different corroborating polling places.
    // Candidates can have different coverage, so balance the outstanding
    // ballots together before adding back this draw's accepted counts.
    double const future = total-counted;
    double sum = 0;
    for (auto& [id,percent] : integrated) {
        double share = std::max(0.,(percent*.01*total-value(account.counted,id))/future);
        if (auto o = ordinaryTargets.find(id); o != ordinaryTargets.end())
            share = std::lerp(share,o->second.remainingShare,o->second.fractionReported);
        percent = share;
        sum += share;
    }
    for (auto& [id,percent] : integrated) {
        // If the individually rounded targets leave no outstanding share,
        // the turnout draw still accounts for every projected ballot.
        double votes = sum > 0 ? value(account.counted,id) + future*percent/sum : value(account.projected,id);
        percent = 100*votes/total;
    }
    return integrated;
}

template<class PriorClassify, class CountClassify>
std::map<int,double> integrate(LiveTurnoutMath::PartyAccount const& account,
    std::map<int,double> const& prior, LiveData::FpIntegrationInformation const& information,
    PriorClassify priorClassify, CountClassify countClassify) {
    // The conditional prior has already received accepted election/regional
    // movement. Prepare the established precision update for declarations and
    // anonymous groups, and a comparison at the actual reporting locations
    // for named ordinary FP. The counted ballots are common to both.
    if (information.evidenceGate == 0) {
        double counted = 0;
        for (auto const& [id,n] : account.counted) counted += n;
        if (counted == 0) return prior;
        return constrainToCounted(account,std::map<int,double>(prior.begin(),prior.end()));
    }
    double total = 0;
    for (auto const& [id,n] : account.projected) total += n;
    double counted = 0;
    for (auto const& [id,n] : account.counted) counted += n;
    std::map<int,double> target;
    std::map<int,OrdinaryTarget> ordinaryTargets;
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
        // Named candidates can use the direct comparison. Keep anonymous
        // groups on their existing joint update; a different candidate in a
        // shared slot cannot inherit the match or its booth coverage.
        auto comparison = information.boothComparisons.find(id);
        if (id < 0 && id != -1) {
            // A single principal independent can occupy the emerging IND
            // slot in this draw. Follow the same mapping as its count account.
            comparison = std::find_if(information.boothComparisons.begin(), information.boothComparisons.end(),
                [&](auto const& row) { return countClassify(row.first) == id; });
        }
        if (comparison != information.boothComparisons.end() && countClassify(comparison->first) == id
            && total > counted && account.projected.contains(id)) {
            double const boothRemaining = std::clamp((account.projected.at(id)-value(account.counted,id))
                /(total-counted),0.,1.);
            ordinaryTargets[id] = {ordinaryRemainingShare(comparison->second,p,boothRemaining,weight),
                comparison->second.ordinary/information.reportedFormal};
        }
    }
    return allocateOutstanding(account,constrainToCounted(account,std::move(target)),ordinaryTargets,total,counted);
}
} // namespace LiveFpIntegration
