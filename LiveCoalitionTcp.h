#pragma once

#include "LivePartialCount.h"
#include "LiveTppComposition.h"

#include <array>

// Parallel live vote accounts for a compulsory-preference district with both
// Coalition candidates. Each matchup fixes ballots counted for that pair.
// A count can also inform how the same sources might transfer in the other
// matchup. The two forecasts share this uncertain evidence and the turnout
// draw, without adding the same ballots twice to either result.
namespace LiveCoalitionTcp {
using LivePartialCount::Counts;
using LivePartialCount::Flows;
using LivePartialCount::value;
using LiveTppIntegration::Matrix;
using LiveTppIntegration::Vector;
inline constexpr double LeakageMean = .20;

// The common TPP can keep a real classic count, or use the local FP mix
// until one exists. A zero-filled pair conveys only the feed's intention.
inline int referenceOpponent(Counts const& accepted, Counts const& fp, int national) {
    if (LivePartialCount::total(accepted) > 0) return accepted.contains(national) ? national : 1;
    return value(fp,national) > value(fp,1) ? national : 1;
}

// A source represents a group of original primaries. Finalists' primaries
// cannot transfer. The excluded partner has its own leakage rate and the
// other candidates retain their individual pre-count Labor preference rates.
enum class SourceGroup { Labor, Liberal, National, Other };
struct Source {
    SourceGroup group;
    double votes, logOdds; // Votes in this batch, and prior log odds of going to Labor.
};
struct PairAccount {
    double counted = 0, labor = 0;
    std::optional<LivePartialCount::RevisedTcp> revision;
    std::vector<Source> pending;
};
struct Booth {
    int category;
    std::vector<Source> future;
    std::array<PairAccount,2> pairs;
};
struct Observation {
    std::size_t booth, pair; // Pair 0 is Labor–Liberal, pair 1 is Labor–National.
    std::vector<Source> donors;
    double votes, labor, weight; // Excluded primaries, those reaching Labor, and overlap reliability.
};

// Use this only to reserve the reported TCP ballots when the exact matching
// FP batch is missing. No allocation can assign more finalist primaries than
// that candidate already has in TCP. The inferred subset supplies no evidence
// about the actual transfer rate.
inline std::map<int,double> remainingPrimaries(Counts const& fp, Counts const& tcp,
    Counts const* basis, int opponent) {
    std::map<int,double> result(fp.begin(),fp.end());
    double const counted = LivePartialCount::total(tcp);
    if (basis) {
        for (auto& [party,votes] : result) votes -= value(*basis,party);
        return result;
    }
    double capacity = 0;
    for (auto const& [party,votes] : result)
        capacity += party == 0 || party == opponent ? std::min(votes,value(tcp,party)) : votes;
    // A count ahead of FP may already contain all the reported primaries.
    // Any incompatible residue is accounted for by the measured TCP alone.
    double fraction = capacity > 0 ? std::min(1.,counted/capacity) : 0;
    for (auto& [party,votes] : result) {
        double overlap = party == 0 || party == opponent ? std::min(votes,value(tcp,party)) : votes;
        votes -= overlap*fraction;
    }
    return result;
}

struct Estimate {
    double labor, total, counted; // Labor, formal and directly counted ballots.
};

// The first two district terms vary Liberal and National leakage separately.
// Non-Coalition flow has a shared term and a difference between pairings.
// A category and a booth term allow flow in one location to differ. The
// assumptions match the district-leakage prototype: 10 points of spread
// near the prior for leakage and the between-pair difference, and 6 near an
// even split for each local term. These are modelling assumptions.
struct Prepared {
    std::vector<Booth> booths;
    std::vector<int> categories;
    Vector centre;
    Matrix precisionRoot;
    std::array<double,4> futureTotals{}, countedPrimaries{};
    std::array<double,2> pendingTotals{}, countedTotals{};
    double future = 0, minimumFormal = 0, genericOtherLogOdds = 0;

    template<class Normal>
    Estimate draw(std::size_t pair, double total, std::array<double,3> primaryShares, Normal normal) const;
};

inline double allocate(std::vector<Source> const& sources, std::size_t pair,
    double leakage, double other, std::array<double,4> const* futureScale = nullptr) {
    auto finalist = pair == 0 ? SourceGroup::Liberal : SourceGroup::National;
    double result = 0;
    for (auto const& source : sources) {
        double votes = source.votes;
        if (futureScale) votes *= futureScale->at(std::size_t(source.group));
        if (source.group == SourceGroup::Labor) result += votes;
        else if (source.group != finalist) {
            double shift = source.group == SourceGroup::Other ? other : leakage;
            result += votes*LivePartialCount::inverseLogOdds(source.logOdds+shift);
        }
    }
    return result;
}

inline std::size_t categoryIndex(Prepared const& model, Booth const& booth) {
    return std::size_t(std::lower_bound(model.categories.begin(),model.categories.end(),booth.category)
        - model.categories.begin());
}

// Fit the uncertain rates once per district. A matched TCP measures the
// combined destination of its excluded primaries. Comparing it with the
// expected total on log odds preserves valid rates while letting each party's
// vote size determine how much it can explain. The normal approximation about
// the fitted rates keeps their dependence when simulation scenarios draw them.
inline Prepared prepare(std::vector<Booth> booths, std::vector<Observation> const& observations) {
    Prepared model;
    model.booths = std::move(booths);
    for (auto const& booth : model.booths) {
        model.categories.push_back(booth.category);
        for (std::size_t pair = 0; pair < booth.pairs.size(); ++pair) {
            auto const& account = booth.pairs[pair];
            model.countedTotals[pair] += account.counted;
            for (auto const& source : account.pending) model.pendingTotals[pair] += source.votes;
        }
        for (auto const& source : booth.future) {
            model.futureTotals.at(std::size_t(source.group)) += source.votes;
            model.future += source.votes;
        }
    }
    model.minimumFormal = std::max(model.pendingTotals[0]+model.countedTotals[0],
        model.pendingTotals[1]+model.countedTotals[1]);
    std::sort(model.categories.begin(),model.categories.end());
    model.categories.erase(std::unique(model.categories.begin(),model.categories.end()),model.categories.end());
    // Private booth terms precede district/category terms. Each observation
    // touches only one private term, so the factor preserves that sparsity.
    std::size_t const district = model.booths.size();
    std::size_t const size = district+4+model.categories.size();
    double const leakageSd = .10/(LeakageMean*(1-LeakageMean));
    double const localSd = .06/.25;
    double const districtSd = .10/.25;
    constexpr double Correlation = .25; // Prior correlation between the two leakage rates.
    auto prior = Matrix(size,Vector(size));
    for (std::size_t i = 0; i < size; ++i) prior[i][i] = 1/(localSd*localSd);
    prior[district][district] = prior[district+1][district+1] =
        1/(leakageSd*leakageSd*(1-Correlation*Correlation));
    prior[district][district+1] = prior[district+1][district] = -Correlation*prior[district][district];
    prior[district+2][district+2] = prior[district+3][district+3] = 1/(districtSd*districtSd);
    model.centre.resize(size);
    // Bounds on the number of refinements and on a single step are numerical
    // safeguards for nearly unanimous or very sparse preference samples.
    for (int pass = 0; pass <= 15; ++pass) {
        Matrix precision = prior;
        Vector gradient(size);
        for (std::size_t i = 0; i < size; ++i) gradient[i] -= prior[i][i]*model.centre[i];
        gradient[district] -= prior[district][district+1]*model.centre[district+1];
        gradient[district+1] -= prior[district+1][district]*model.centre[district];
        for (auto const& observation : observations) {
            if (observation.weight <= 0 || observation.votes <= 0) continue;
            auto const b = observation.booth;
            auto const category = district+4+categoryIndex(model,model.booths.at(b));
            auto const leakage = district+1-observation.pair;
            double const direction = observation.pair == 0 ? -.5 : .5;
            double const other = model.centre[b]+model.centre[district+2]+
                direction*model.centre[district+3]+model.centre[category];
            double predicted = 0, leakageSlope = 0, otherSlope = 0;
            for (auto const& donor : observation.donors) {
                bool const minor = donor.group == SourceGroup::Other;
                double rate = LivePartialCount::inverseLogOdds(donor.logOdds+(minor ? other : model.centre[leakage]));
                predicted += donor.votes*rate;
                (minor ? otherSlope : leakageSlope) += donor.votes*rate*(1-rate);
            }
            double share = std::clamp(predicted/observation.votes,1e-6,1-1e-6);
            double slopeScale = 1/(observation.votes*share*(1-share));
            double a = leakageSlope*slopeScale, o = otherSlope*slopeScale;
            std::array<std::pair<std::size_t,double>,5> slopes{{
                {leakage,a},{b,o},{district+2,o},{district+3,direction*o},{category,o}}};
            // Half-ballot smoothing permits genuine zero or unanimous transfers.
            double measured = (observation.labor+.5)/(observation.votes+1);
            double residual = LivePartialCount::logOdds(measured)-LivePartialCount::logOdds(share);
            double weight = observation.weight*observation.votes*measured*(1-measured);
            for (auto const& [i,x] : slopes) {
                gradient[i] += weight*x*residual;
                for (auto const& [j,y] : slopes) precision[i][j] += weight*x*y;
            }
        }
        if (!LiveTppComposition::factor(precision,model.precisionRoot))
            throw std::logic_error("Invalid Coalition preference precision.");
        if (pass == 15 || observations.empty()) break;
        Vector step = LiveTppComposition::covarianceSolve(model.precisionRoot,std::move(gradient));
        double largest = 0;
        for (double change : step) largest = std::max(largest,std::abs(change));
        if (largest < 1e-6) break;
        for (std::size_t i = 0; i < size; ++i) model.centre[i] += step[i]/std::max(1.,largest);
    }
    return model;
}

template<class Normal>
Estimate Prepared::draw(std::size_t pair, double total, std::array<double,3> primaryShares, Normal normal) const {
    std::size_t const district = booths.size();
    Estimate result{0,std::max(total,minimumFormal),countedTotals.at(pair)};
    double const pending = pendingTotals.at(pair);
    for (std::size_t index = 0; index < booths.size(); ++index) {
        auto const& booth = booths[index];
        auto const& account = booth.pairs.at(pair);
        if (account.revision && account.revision->uncertainVotes() > 0) {
            // A primary revision leaves only these disputed transfers free
            // to move. The rest of the old count remains in the pair account.
            auto const& revised = *account.revision;
            double fraction = (revised.first-revised.lower)/revised.uncertainVotes();
            fraction = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(fraction)
                +2*normal(centre.size()+index));
            result.labor += revised.lower+revised.uncertainVotes()*fraction;
        } else result.labor += account.labor;
    }
    double const outstanding = std::max(0.,result.total-result.counted-pending);
    if (pending <= 0 && outstanding <= 0) return result;

    // Solve the transpose of the precision factor to draw correlated rates.
    // Between private booth terms the factor is diagonal.
    Vector draw(centre.size());
    for (std::size_t i = draw.size(); i-- > 0;) {
        double dev = normal(i);
        for (std::size_t j = std::max(i+1,district); j < draw.size(); ++j)
            dev -= precisionRoot[j][i]*draw[j];
        draw[i] = dev/precisionRoot[i][i];
    }
    for (std::size_t i = 0; i < draw.size(); ++i) draw[i] += centre[i];

    // Both matchups use precisely the same mix of votes yet to report. The
    // callers resolve the major-primary mix in the same simulation draw.
    double const othersShare = 100-primaryShares[0]-primaryShares[1]-primaryShares[2];
    std::array<double,4> share{primaryShares[0],primaryShares[1],primaryShares[2],othersShare};
    std::array<double,4> scale{}, balance{};
    double sum = 0;
    for (std::size_t group = 0; group < scale.size(); ++group) {
        balance[group] = std::max(0.,result.total*share[group]*.01-countedPrimaries[group]);
        sum += balance[group];
    }
    if (sum <= 0) {
        // A rounding adjustment or a count ahead of FP may leave no usable
        // difference. The prepared future-voter mix breaks that tie.
        balance = futureTotals;
        sum = future;
    }
    auto finalist = pair == 0 ? SourceGroup::Liberal : SourceGroup::National;
    for (std::size_t group = 0; group < scale.size(); ++group) {
        double votes = sum > 0 ? outstanding*balance[group]/sum : 0;
        if (futureTotals[group] > 0) scale[group] = votes/futureTotals[group];
        else if (votes > 0) {
            // The simulated electorate can include a source absent from the
            // mean forecast for unreported booths. Use the district rate.
            auto source = SourceGroup(group);
            if (source == SourceGroup::Labor) result.labor += votes;
            else if (source != finalist) {
                double logOdds = source == SourceGroup::Other ? genericOtherLogOdds+draw[district+2]
                    +(pair == 0 ? -.5 : .5)*draw[district+3]
                    : LivePartialCount::logOdds(LeakageMean)+draw[district+1-pair];
                result.labor += votes*LivePartialCount::inverseLogOdds(logOdds);
            }
        }
    }
    for (std::size_t index = 0; index < booths.size(); ++index) {
        auto const& booth = booths[index];
        double other = draw[index]+draw[district+2]+(pair == 0 ? -.5 : .5)*draw[district+3]
            +draw[district+4+categoryIndex(*this,booth)];
        double leakage = draw[district+1-pair];
        result.labor += allocate(booth.pairs[pair].pending,pair,leakage,other);
        result.labor += allocate(booth.future,pair,leakage,other,&scale);
    }
    // A preparation with no forecast remainder can still draw a few late
    // votes. Allocate those from the observed account rather than inventing
    // a distinct donor pool for either candidate.
    if (sum <= 0 && outstanding > 0 && result.counted+pending > 0)
        result.labor += outstanding*result.labor/(result.counted+pending);
    return result;
}
} // namespace LiveCoalitionTcp
