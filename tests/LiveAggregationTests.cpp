// Portable checks of the reporting-only historical/current vote-weight blend.
// Synthetic counts exercise invariants without calculating an election forecast.
#include "../LiveAggregation.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <numeric>

namespace {
using namespace LiveAggregation;
using Totals = LiveData::AggregateVoteTotals;
using Progress = LiveData::CountProgress;

bool close(double a,double b) { return std::abs(a-b)<1e-10; }
double mean(std::vector<double> const& shares,std::vector<double> const& weights) {
    double numerator=0,denominator=0;
    for (std::size_t i=0;i<shares.size();++i) {
        numerator+=shares[i]*weights[i]; denominator+=weights[i];
    }
    return numerator/denominator;
}
template<class F> void rejects(F operation) {
    bool rejected=false;
    try { operation(); } catch(std::runtime_error const&) { rejected=true; }
    assert(rejected);
}

void testProgressAndLegacyScope() {
    std::vector<double> historical{60000,90000,75000};
    // Ordinary forecasts, supporting runs and legacy providers supply no live
    // progress. No live sizes are requested, and historical arithmetic is exact.
    auto unavailable=[](std::size_t)->Totals { assert(false); return {}; };
    for (auto progress : {std::optional<Progress>{},std::optional<Progress>{Progress{0,1000,10000}}}) {
        auto w=prepare(historical,progress,unavailable);
        assert(w.fp==historical && w.tpp==historical && w.turnoutFraction==0);
        assert(mean({49,54,43},w.tpp)==mean({49,54,43},historical));
    }
    assert(close(turnoutFraction(Progress{1000,0,10000}),.104));
    assert(close(turnoutFraction(Progress{2500,0,10000}),.5));
    assert(close(turnoutFraction(Progress{4000,0,10000}),.896));
    assert(turnoutFraction(Progress{5000,0,10000})==1);
    assert(turnoutFraction(Progress{11000,0,10000})==1);
    assert(turnoutFraction(Progress{1,0,10000})<1.3e-7);
    // TCP resets and FP-derived preference estimates cannot change the fade.
    assert(turnoutFraction(Progress{2500,0,10000})==turnoutFraction(Progress{2500,9000,10000}));
    // No latch: looking at a later feed cannot alter an earlier feed's result.
    double early=turnoutFraction(Progress{1000,0,10000});
    (void)turnoutFraction(Progress{9000,0,10000});
    assert(turnoutFraction(Progress{1000,0,10000})==early);
    rejects([]{ turnoutFraction(Progress{1,0,0}); });
    rejects([]{ turnoutFraction(Progress{-1,0,100}); });
    rejects([]{ turnoutFraction(Progress{1,0,std::numeric_limits<double>::quiet_NaN()}); });
}

void testNormalisedMixAndSameScenario() {
    std::vector<double> historical{100,100};
    std::vector<Totals> totals{{300,240},{100,60}};
    auto w=prepare(historical,Progress{25,0,100},[&](std::size_t i){ return totals[i]; });
    // Half historical (50:50), half current FP (75:25) => 62.5:37.5.
    // Current TPP is 80:20 because of differential OPV exhaustion.
    assert(w.turnoutFraction==.5);
    assert(close(w.fp[0]/200,.625) && close(w.tpp[0]/200,.65));
    assert(close(std::accumulate(w.fp.begin(),w.fp.end(),0.),200));
    assert(close(std::accumulate(w.tpp.begin(),w.tpp.end(),0.),200));
    auto original=w;
    for (auto& t:totals) { t.fp*=7; t.tpp*=7; }
    w=prepare(historical,Progress{25,0,100},[&](std::size_t i){ return totals[i]; });
    for(std::size_t i=0;i<2;++i) {
        assert(close(w.fp[i],original.fp[i])); assert(close(w.tpp[i],original.tpp[i]));
    }
    // Relative seat sizes in an existing scenario affect the weights, while
    // snapshot coverage remains fixed. No new random count draw is requested.
    totals[0]={350,280}; totals[1]={350,280};
    auto equal=prepare(historical,Progress{25,0,100},[&](std::size_t i){ return totals[i]; });
    assert(equal.turnoutFraction==original.turnoutFraction && equal.fp[0]!=original.fp[0]);
    assert(equal.fp==historical && equal.tpp==historical);
}

void testCompleteAndRegionalAccounting() {
    std::vector<double> historical{100,100,200};
    std::vector<Totals> totals{{300,240},{100,60},{0,0}};
    std::vector<double> seatShares{60,40,70};
    std::vector<int> winners{0,1,0};
    auto oldShares=seatShares; auto oldWinners=winners; auto oldTotals=totals;
    for(double counted : {0.,10.,25.,40.,50.,100.}) {
        auto w=prepare(historical,Progress{counted,0,100},[&](std::size_t i){ return totals[i]; });
        assert(close(mean(seatShares,w.fp),(1-w.turnoutFraction)*60+w.turnoutFraction*55));
        // Region A is seats 0,2; Region B is seat 1. Recombining the regions
        // with their weight masses exactly recovers the national aggregate.
        double regionAWeight=w.tpp[0]+w.tpp[2], regionBWeight=w.tpp[1];
        double regionA=(seatShares[0]*w.tpp[0]+seatShares[2]*w.tpp[2])/regionAWeight;
        double national=(regionA*regionAWeight+seatShares[1]*regionBWeight)/(regionAWeight+regionBWeight);
        assert(close(national,mean(seatShares,w.tpp)));
        if(counted>=50) {
            assert(w.fp[2]==0 && w.tpp[2]==0); // Explicit inactive contest.
            assert(close(mean(seatShares,w.fp),55));
            assert(close(mean(seatShares,w.tpp),56));
        }
    }
    assert(seatShares==oldShares && winners==oldWinners);
    for(std::size_t i=0;i<totals.size();++i) {
        assert(totals[i].fp==oldTotals[i].fp && totals[i].tpp==oldTotals[i].tpp);
    }
    rejects([&]{ prepare(historical,Progress{50,0,100},[](std::size_t){return Totals{-1,10};}); });
    rejects([&]{ prepare(historical,Progress{50,0,100},[](std::size_t){return Totals{10,0};}); });
    rejects([&]{ prepare(historical,Progress{50,0,100},[](std::size_t){return Totals{10,std::numeric_limits<double>::infinity()};}); });
}

void testPreparedInputsRetainScenarioSizes() {
    std::vector<double> historical{60000,70000,80000};
    auto original=historical;
    for(double counted : {0.,1.,1000.,2500.,4000.,5000.,10000.}) {
        Progress progress{counted,0,10000};
        double fraction=turnoutFraction(progress);
        for(int scenario=0;scenario<200;++scenario) {
            // Distinct FP/TPP totals include zero-sized inactive contests and
            // different remaining turnout across scenarios. Only the fraction
            // and historical counts may be reused, never these current sizes.
            std::vector<Totals> sizes{{60000.+scenario,58000.+scenario},
                {90000.-2*scenario,80000.-scenario},{0,0}};
            std::vector<std::size_t> requested;
            auto direct=prepare(historical,progress,[&](std::size_t s){return sizes[s];});
            auto cached=prepare(historical,fraction,[&](std::size_t s){
                requested.push_back(s); return sizes[s];
            });
            assert(cached.fp==direct.fp && cached.tpp==direct.tpp);
            assert(cached.turnoutFraction==direct.turnoutFraction);
            assert(requested==(fraction==0 ? std::vector<std::size_t>{}
                : std::vector<std::size_t>{0,1,2}));
        }
    }
    assert(historical==original);
}
}

int main() {
    testProgressAndLegacyScope();
    testNormalisedMixAndSameScenario();
    testCompleteAndRegionalAccounting();
    testPreparedInputsRetainScenarioSizes();
    std::cout<<"Live aggregation tests passed\n";
}
