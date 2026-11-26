#pragma once

#include "CoalitionPreference.h"
#include "LivePartialCount.h"
#include "LiveTppComposition.h"

#include <array>
#include <cstdint>

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
inline constexpr double LeakageMean = CoalitionPreference::CentralLeakage;
inline constexpr double LocalFlowSd = .06/.25; // Roughly six preference points near an even split.

// A positive classic count identifies its own opponent. At a booth still
// awaiting TCP use the historical reference, including for zero-filled feeds.
inline int referenceOpponent(Counts const& accepted, bool nationalReference, int national) {
    if (LivePartialCount::total(accepted) > 0) return accepted.contains(national) ? national : 1;
    return nationalReference ? national : 1;
}

// A source represents a group of original primaries. Finalists' primaries
// cannot transfer. The excluded partner has its own leakage rate and the
// other candidates retain their individual pre-count Labor preference rates.
enum class SourceGroup { Labor, Liberal, National, Other };
struct Source {
    SourceGroup group;
    double votes, logOdds; // Votes in this batch, and prior log odds of going to Labor.
    int party = -1;
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
    int sourceId = -1;
    CoalitionPreference::Shares minorPrimaries; // Received plus projected remaining FP, counted once per party.
    int liveBoothIndex = -1; // Identifies this location in the diagnostic export.
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
struct FlowEvidence {
    Observation observation;
    Vector gain;
    double fittedLogOdds, samplingSd;
};
struct Prepared {
    std::vector<Booth> booths;
    std::vector<int> categories;
    Vector centre;
    Matrix precisionRoot;
    // The original two-sided fit measures deviations either side of their
    // midpoint. With an identified historical opponent, minor-party priors
    // describe that opponent directly (LIB = 0, NAT = 1).
    double referenceCoordinate = .5;
    CoalitionPreference::Shares geographicTotals;
    CoalitionPreference::Shares countedFp;
    double broadLogOdds = 0; // Measured election and regional movement in minor preferences to Labor.
    std::vector<FlowEvidence> evidence;
    template<class Normal>
    void update(CoalitionPreference::Scenario&,CoalitionPreference::Shares const&,int,int,double,Normal) const;
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

// Estimate how many of this booth's known primaries will finish with Labor
// against the specified Coalition partner. Use the local preference fit
// from the counts available at this forecast. The diagnostic describes its
// central rates before the individual simulations vary those rates.
// A partly or fully measured TCP would describe a different set of ballots.
inline std::optional<Estimate> pendingBenchmark(Prepared const& model, std::size_t index, std::size_t pair) {
    auto const& booth = model.booths.at(index);
    auto const& account = booth.pairs.at(pair);
    if (account.counted > 0 || account.pending.empty() || model.centre.empty()) return {};
    auto const district = model.booths.size();
    auto const& fit = model.centre;
    double const other = fit[index]+fit[district+2]+(double(pair)-model.referenceCoordinate)*fit[district+3]
        +fit[district+4+categoryIndex(model,booth)];
    double votes = 0;
    for (auto const& source : account.pending) votes += source.votes;
    return Estimate{allocate(account.pending,pair,fit[district+1-pair],other),votes,0.};
}

// Fit the uncertain rates once per district. A matched TCP measures the
// combined destination of its excluded primaries. Comparing it with the
// expected total on log odds preserves valid rates while letting each party's
// vote size determine how much it can explain. The normal approximation about
// the fitted rates keeps their dependence when simulation scenarios draw them.
inline Prepared prepare(std::vector<Booth> booths, std::vector<Observation> const& observations,
    std::optional<bool> nationalReference = {}) {
    Prepared model;
    model.booths = std::move(booths);
    if (nationalReference.has_value()) model.referenceCoordinate = *nationalReference ? 1. : 0.;
    for (auto const& booth : model.booths) {
        model.categories.push_back(booth.category);
        for (auto [party,voters] : booth.minorPrimaries) model.geographicTotals[party] += voters;
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
    double const leakageSd = CoalitionPreference::LeakageLogOddsSd;
    double const localSd = LocalFlowSd;
    double const districtSd = .10/.25;
    constexpr double Correlation = CoalitionPreference::LeakageCorrelation; // Prior correlation between the two leakage rates.
    auto prior = Matrix(size,Vector(size));
    for (std::size_t i = 0; i < size; ++i) prior[i][i] = 1/(localSd*localSd);
    prior[district][district] = prior[district+1][district+1] =
        1/(leakageSd*leakageSd*(1-Correlation*Correlation));
    prior[district][district+1] = prior[district+1][district] = -Correlation*prior[district][district];
    prior[district+2][district+2] = 1/(districtSd*districtSd);
    prior[district+3][district+3] = 1/std::pow(CoalitionPreference::OpponentDifferenceSd,2);
    model.centre.resize(size);
    // Bounds on the number of refinements and on a single step are numerical
    // safeguards for nearly unanimous or very sparse preference samples.
    for (int pass = 0; pass <= 15; ++pass) {
        model.evidence.clear();
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
            double const direction = double(observation.pair)-model.referenceCoordinate;
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
            // Save the local slope at this fit. The final pass supplies the
            // response to an individual simulation's anchored transfer rates.
            Vector gain(size);
            for (auto const& [i,x] : slopes) gain[i] = weight*x;
            model.evidence.push_back({observation,std::move(gain),LivePartialCount::logOdds(share),1./std::sqrt(weight)});
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
    for (auto& e : model.evidence)
        e.gain = LiveTppComposition::covarianceSolve(model.precisionRoot,std::move(e.gain));
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
                    +(double(pair)-referenceCoordinate)*draw[district+3]
                    : LivePartialCount::logOdds(LeakageMean)+draw[district+1-pair];
                result.labor += votes*LivePartialCount::inverseLogOdds(logOdds);
            }
        }
    }
    for (std::size_t index = 0; index < booths.size(); ++index) {
        auto const& booth = booths[index];
        double other = draw[index]+draw[district+2]+(double(pair)-referenceCoordinate)*draw[district+3]
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
// Draw local departures from each party's district preference rate. A postal
// batch can lean differently from ordinary votes, and two ordinary booths can
// differ. Before any evidence, each party's local departures must average to
// zero across its own voters: the simulation already drew the district rate.
// When an unrepresentative booth reports, uncertainty about its local lean
// limits what that return can establish about preferences in other locations.
struct GeographicFlowDraw {
    Vector local;
    CoalitionPreference::Shares partyMean;

    double rate(int party, double districtRate, std::size_t booth) const {
        auto found = partyMean.find(party);
        if (found == partyMean.end()) return districtRate;
        double const mean = found->second;
        // The centred contrast is between -1 and 1, so even a very
        // partisan source stays within 0--100%. Near the centre the slope
        // is the usual log-odds slope. The same voters inform both matchups.
        double const contrast = (local[booth]-mean)/(1.+std::abs(mean));
        return districtRate+districtRate*(1.-districtRate)*contrast;
    }
};

template<class Normal>
GeographicFlowDraw drawGeographicFlows(Prepared const& model, Normal normal) {
    GeographicFlowDraw draw;
    draw.local.reserve(model.booths.size());
    // Use commission booth IDs and declaration categories rather than their
    // position in this capture. A new location or a changed pair should
    // not assign an existing location a different draw. The high bits
    // separate these keys from each other and from count/recheck draws.
    std::map<int,double> categoryLean;
    for (int category : model.categories)
        categoryLean[category] = LocalFlowSd*normal((1ULL<<32)+std::uint32_t(category));
    for (auto const& booth : model.booths) {
        auto key = booth.sourceId == -1 ? (2ULL<<32)+std::uint32_t(booth.category)
            : (3ULL<<32)+std::uint32_t(booth.sourceId);
        double const lean = std::tanh(categoryLean.at(booth.category)+LocalFlowSd*normal(key));
        draw.local.push_back(lean);
        for (auto [party,voters] : booth.minorPrimaries) draw.partyMean[party] += voters*lean;
    }
    for (auto& [party,mean] : draw.partyMean) mean /= model.geographicTotals.at(party);
    return draw;
}

// Apply positive TCP evidence to the two FP-based forecasts. For each pair,
// count its measured ballots once, transfer the received primaries still
// awaiting TCP, then allocate the remaining district FP voters by source.
// Only matched FP/TCP can teach us about other booths or the other matchup.
// A label change supplies neither ballots nor a preference observation.
template<class Normal>
void Prepared::update(CoalitionPreference::Scenario& scenario, CoalitionPreference::Shares const& current,
    int national, int others, double turnout, Normal normal) const {
    // The ordinary seat calculation can learn about preferences from
    // comparable counts elsewhere before any local TCP. Give both matchups
    // that same evidence for their minor voters. The local fit below then
    // measures departures from these updated expectations.
    if (broadLogOdds != 0) {
        double const otherRate = scenario.rates.at(others);
        for (auto [party,n] : current) scenario.rates.try_emplace(party,otherRate);
        std::array<double,2> movement{};
        for (auto& [party,rate] : scenario.rates) {
            if (party == 0 || party == 1 || party == national) continue;
            double revised = CoalitionPreference::shiftedRate(rate,broadLogOdds);
            auto fp = current.find(party);
            if (fp != current.end()) {
                for (int p = 0; p < 2; ++p)
                    movement[p] += fp->second*(scenario.minorRate(revised,p != 0)-scenario.minorRate(rate,p != 0));
            }
            rate = revised;
        }
        for (int p = 0; p < 2; ++p)
            scenario.results.labor[p] = std::clamp(scenario.results.labor[p]+movement[p],0.,100.);
    }
    if (countedTotals[0]+countedTotals[1] == 0 && evidence.empty()) return;
    double total = std::max(turnout,minimumFormal);
    if (total <= 0) return;
    auto district = booths.size();
    auto const& prior = scenario.leakage;
    auto const geography = drawGeographicFlows(*this,normal);
    auto priorOdds = [&](Source const& source, std::size_t pair, std::size_t booth) {
        double rate = source.group == SourceGroup::Liberal ? prior.liberal : prior.national;
        if (source.group == SourceGroup::Other) {
            auto it = scenario.rates.find(source.party);
            rate = it == scenario.rates.end() ? scenario.rates.at(others) : it->second;
            rate = scenario.minorRate(rate,pair != 0);
            if (booth < booths.size()) rate = geography.rate(source.party,rate,booth);
        }
        return LivePartialCount::logOdds(rate);
    };
    auto otherShift = [&](Vector const& shifts, std::size_t b, std::size_t pair) {
        return shifts[b]+shifts[district+2]+(double(pair)-referenceCoordinate)*shifts[district+3]
            +shifts[district+4+categoryIndex(*this,booths[b])];
    };
    // The fit was made once at central party rates. Adjust its fitted log
    // odds for this draw's leakage, difference between opponents, local
    // preferences and sampling error. If a draw already predicts the
    // observed return its correction is small. This conditions the original
    // uncertainties once and preserves the variation the count cannot resolve.
    Vector shifts = centre;
    for (auto const& e : evidence) {
        auto const& obs = e.observation;
        double expected = 0;
        for (auto const& source : obs.donors) {
            auto shift = source.group == SourceGroup::Other ? otherShift(centre,obs.booth,obs.pair)
                : centre[district+1-obs.pair];
            expected += source.votes*LivePartialCount::inverseLogOdds(priorOdds(source,obs.pair,obs.booth)+shift);
        }
        double residual = e.fittedLogOdds-LivePartialCount::logOdds(expected/obs.votes)
            +e.samplingSd*normal(centre.size()+booths.size()+2*obs.booth+obs.pair);
        for (std::size_t i = 0; i < shifts.size(); ++i) shifts[i] += e.gain[i]*residual;
    }
    auto votes = [](auto const& counts,int id) {
        auto it = counts.find(id);
        return it == counts.end() ? 0. : it->second;
    };
    for (std::size_t p = 0; p < 2; ++p) {
        if (!countedTotals[p] && evidence.empty()) continue;
        auto finalist = p ? SourceGroup::National : SourceGroup::Liberal;
        double const outstanding = std::max(0.,total-countedTotals[p]-pendingTotals[p]);
        double labor = 0.;
        CoalitionPreference::Shares futureByParty, futureLabor;
        auto rate = [&](Source const& source,double other,std::size_t booth) {
            if (source.group == SourceGroup::Labor) return 1.;
            if (source.group == finalist) return 0.;
            double change = source.group == SourceGroup::Other ? other : shifts[district+1-p];
            return LivePartialCount::inverseLogOdds(priorOdds(source,p,booth)+change);
        };
        for (std::size_t b = 0; b < booths.size(); ++b) {
            auto const& account = booths[b].pairs[p];
            double countedLabor = account.labor;
            if (account.revision && account.revision->uncertainVotes() > 0) {
                auto const& revision = *account.revision;
                double q = (revision.first-revision.lower)/revision.uncertainVotes();
                countedLabor = revision.lower+revision.uncertainVotes()*LivePartialCount::inverseLogOdds(
                    LivePartialCount::logOdds(q)+2*normal(centre.size()+b));
            }
            labor += countedLabor;
            double local = otherShift(shifts,b,p);
            for (auto const& source : account.pending) labor += source.votes*rate(source,local,b);
            for (auto const& source : booths[b].future) {
                futureByParty[source.party] += source.votes;
                futureLabor[source.party] += source.votes*rate(source,local,b);
            }
        }
        // Match the uncounted pool to the simulation's updated party FP
        // shares. Observed FP voters remain in their actual booth. When a
        // party has no projected booth yet, use its district flow.
        double remaining = 0., remainingLabor = 0.;
        for (auto [id,share] : current) {
            double n = std::max(0.,share*.01*total-votes(countedFp,id));
            auto group = id == 0 ? SourceGroup::Labor : id == 1 ? SourceGroup::Liberal :
                id == national ? SourceGroup::National : SourceGroup::Other;
            double f = votes(futureByParty,id);
            double q = f > 0 ? votes(futureLabor,id)/f :
                rate({group,n,0,id},shifts[district+2]+(double(p)-referenceCoordinate)*shifts[district+3],booths.size());
            remaining += n;
            remainingLabor += n*q;
        }
        if (remaining > 0) labor += outstanding*remainingLabor/remaining;
        else if (outstanding > 0) labor += outstanding*scenario.results.labor[p]*.01;
        scenario.results.labor[p] = std::clamp(100.*labor/total,0.,100.);
        scenario.results.counted[p] = countedTotals[p] > 0;
    }
    // The ranking calculation also needs the two updated leakage rates,
    // as it may exclude a partner several steps before selecting the finalists.
    scenario.leakage.liberal = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(prior.liberal)+shifts[district]);
    scenario.leakage.national = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(prior.national)+shifts[district+1]);
    double laborFp = votes(current,0), liberalFp = votes(current,1), nationalFp = votes(current,national);
    double minorFp = std::max(0.,100.-laborFp-liberalFp-nationalFp);
    scenario.leakage.liberal = CoalitionPreference::feasibleLeakage(scenario.leakage.liberal,liberalFp,minorFp,
        scenario.results.labor[1]-laborFp);
    scenario.leakage.national = CoalitionPreference::feasibleLeakage(scenario.leakage.national,nationalFp,minorFp,
        scenario.results.labor[0]-laborFp);
}
} // namespace LiveCoalitionTcp
