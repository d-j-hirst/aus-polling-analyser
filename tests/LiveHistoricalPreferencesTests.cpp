// Exercise exact historical populations and the additional preference fit.
// No forecast engine, GUI, logging instance or network source is required.
#include "LiveHistoricalPreferences.h"
#include "LivePartyConversion.h"
#include "LiveTppComposition.h"
#include <cassert>
#include <iostream>

namespace {
using namespace LiveHistoricalPreferences;

void close(double actual, double expected, double tolerance = 1e-9) {
    assert(std::abs(actual-expected) < tolerance);
}

History fictional(std::string const& previous = "2019fed", std::string const& current = "2022fed") {
    std::istringstream input(
        "term,seat,kind,id,fp,labor,coalition\n"
        "2019fed,Hill,booth,91,11:300;12:300;13:200;14:200,540,460\n"
        "2019fed,Hill,declaration,5,11:300;12:300;13:200;14:200,550,450\n"
        "2022fed,Hill,booth,91,11:300;12:300;13:200;14:200,600,400\n");
    return readHistory(input,previous,current);
}

void testIdentityAndPopulation() {
    auto history = fictional();
    Counts raw{{11,300},{12,300},{13,200},{14,200}};
    auto booth = history.find("Hill","booth",91,raw);
    assert(booth && booth->labor == 540 && history.accounts.size() == 2);
    assert(history.find("Hill","declaration",5,raw)->labor == 550);
    assert(!history.find("New Hill","booth",91,raw));
    assert(!history.find("Hill","booth",92,raw));
    Counts different = raw;
    different[13] += 10; different[14] -= 10;
    assert(!history.find("Hill","booth",91,different)); // Same total, changed composition.
    different = raw; different[13] += 1;
    assert(!history.find("Hill","booth",91,different)); // Different batch.
    different = raw; different[99] = 0;
    assert(history.find("Hill","booth",91,different)); // An omitted genuine zero is harmless.
    assert(fictional("2022fed","2022fed").accounts.empty());
    assert(fictional("2025fed","2022fed").accounts.empty());
    assert(fictional("2019fed","2022vic").accounts.empty());
    assert(fictional("2016fed").accounts.empty());
    auto nationals = mappedTpp(*booth,{{0,300},{10,300},{2,400}},10);
    assert(nationals.contains(10) && !nationals.contains(1));
    auto liberals = mappedTpp(*booth,{{0,300},{1,300},{2,400}},10);
    assert(liberals.contains(1) && !liberals.contains(10));
}

void testExactMixtureAndNoDoubleCorrection() {
    Counts fp{{0,300},{1,300},{2,200},{3,200}}, tpp{{0,540},{1,460}};
    Flows rates{{0,1},{1,0},{2,.8},{3,.2}};
    auto exact = fitOffset(fp,tpp,rates,0,1);
    assert(exact);
    double smoothed = 300+400*240.5/401;
    close(LivePartialCount::expectedFirst(fp,rates,*exact),smoothed);
    auto legacy = LivePartialCount::measure(fp,tpp,rates,0,1);
    assert(legacy && smoothed-LivePartialCount::expectedFirst(fp,rates,legacy->offset) > 13);

    // Already informed candidate priors do not acquire the full old discrepancy
    // twice. The additional fit learns only what remains after those rates.
    auto calibrated = rates;
    for (int party : {2,3}) calibrated[party] = LivePartialCount::preferenceRate(party,rates,*exact);
    auto remainder = fitOffset(fp,tpp,calibrated,0,1);
    assert(remainder); close(*remainder,0);

    // Comparing the same old and current mixture gives zero measured movement.
    // Subtracting an exact old fit from a pooled current approximation would
    // fail this check even with identical ballots, so the new path uses both
    // exact fits. Existing classic comparisons still use both pooled fits.
    auto current = fitOffset(fp,tpp,rates,0,1);
    close(*current-*exact,0);
    assert(std::abs(legacy->offset-*exact) > .1);
    close(LivePartialCount::preferenceRate(0,rates,*exact),1);
    close(LivePartialCount::preferenceRate(1,rates,*exact),0);
    assert(!fitOffset(fp,{{0,540},{1,459}},rates,0,1));
    assert(!fitOffset(fp,{{0,200},{1,800}},rates,0,1));
    assert(!fitOffset(fp,tpp,{{0,1},{1,0},{2,.8}},0,1));
    assert(!fitOffset({{0,300},{1,700}},{{0,300},{1,700}},{{0,1},{1,0}},0,1));
}

void testReferenceAndCategoryConsistency() {
    Counts fp{{0,300},{1,300},{2,200},{3,200}}, notional{{0,540},{1,460}};
    LivePartialCount::Flows configured{{-1,50},{2,80},{3,20}};
    auto generic = LivePartyConversion::historicalCategoryTpp(fp,{}, {},configured,{},10);
    close(generic.first,500);
    auto measured = LivePartyConversion::historicalCategoryTpp(fp,{}, {},configured,{},10,notional);
    close(measured.first,540); close(measured.second,1000);
    // Actual classic TCP remains authoritative if a duplicate extra count is
    // present. Neither its denominator nor the existing correction is replaced.
    auto classic = LivePartyConversion::historicalCategoryTpp(fp,{{0,560},{1,440}}, {},configured,{},10,notional);
    close(classic.first,560);
    auto mismatch = LivePartyConversion::historicalCategoryTpp(fp,{}, {},configured,{},10,{{0,540},{1,459}});
    close(mismatch.first,generic.first);
    // Using the same old account for booth and category comparisons removes
    // the conversion part of their mismatch without adding a second rate shift.
    auto fit = fitOffset(fp,notional,{{0,1},{1,0},{2,.8},{3,.2}},0,1);
    assert(fit);
    close(LivePartialCount::expectedFirst(fp,{{0,1},{1,0},{2,.8},{3,.2}},*fit),300+400*240.5/401);
    close(100*measured.first/measured.second,54);
    auto oldReference = LiveTppComposition::centre({50},{1000},54);
    auto matchedReference = LiveTppComposition::centre({54},{1000},54);
    double oldShift = LiveTppComposition::transform(oldReference.shares[0])-LiveTppComposition::transform(50);
    double matchedShift = LiveTppComposition::transform(matchedReference.shares[0])-LiveTppComposition::transform(54);
    assert(oldShift > 4);
    close(matchedShift,0); // The same old conversion error cannot also become reference centering.
}

void testExcludedCampaignCannotReturnThroughAggregateFit() {
    auto history = [](std::string const& weight) {
        std::istringstream input(
            "term,seat,party,candidate_key,fp,formal,alp,classic,forward_residual_weight\n"
            "2019fed,Coast,GRN,alex green,1000,10000,850,1,1\n"
            "2019fed,Hill,GRN,pat green,3000,10000,2400,0,"+weight+"\n");
        return LivePartyConversion::readHistory(input,"2019fed","2022fed");
    };
    Counts fp{{0,3000},{1,4000},{2,3000}}, oldTpp{{0,5400},{1,4600}};
    auto ordinary = history("1"), excluded = history("0"), half = history("0.5");
    double historical = *LivePartyConversion::historicalFlow(ordinary,"Hill","GRN");
    Flows rates{{0,1},{1,0},{2,historical}};
    auto usual = fitOffset(fp,oldTpp,LivePartyConversion::notionalCalibrationFlows(rates,ordinary,"Hill",2),0,1);
    auto stopped = fitOffset(fp,oldTpp,LivePartyConversion::notionalCalibrationFlows(rates,excluded,"Hill",2),0,1);
    auto partial = fitOffset(fp,oldTpp,LivePartyConversion::notionalCalibrationFlows(rates,half,"Hill",2),0,1);
    assert(usual && stopped && partial && *usual < 0);
    close(*stopped,0);
    close(*partial,.5* *usual);
    close(rates.at(2),historical); // The original historical/reference rate is not mutated.
    auto current = LivePartyConversion::greensFlow(.85,LivePartyConversion::logit(.3),0,excluded);
    close(LivePartialCount::preferenceRate(2,{{2,current}},*stopped),current);
}

void testMaintainedAccountsAndValidation() {
    std::size_t count = 0;
    for (auto const* term : {"2010fed","2013fed","2016fed","2019fed","2022fed","2025fed"}) {
        std::ifstream input("analysis/Data/historical-notional-tpp.csv");
        assert(input);
        auto history = readHistory(input,term,"2028fed");
        assert(!history.accounts.empty()); count += history.accounts.size();
        for (auto const& [key,account] : history.accounts) {
            assert(history.find(std::get<0>(key),std::get<1>(key),std::get<2>(key),account.fp));
            close(LivePartialCount::total(account.fp),double(account.labor)+account.coalition);
        }
    }
    assert(count == 7116);
    for (std::string const& row : {
        "2019fed,Hill,booth,91,11:100,60,39\n",
        "2019fed,Hill,booth,91,11:100;11:1,60,41\n",
        "2019fed,Hill,booth,91,11:-1,0,0\n",
        "2019fed,Hill,other,91,11:100,60,40\n",
        "2019fed,Hill,booth,91,11:100,60,40\n2019fed,Hill,booth,91,11:100,60,40\n"}) {
        std::istringstream input("term,seat,kind,id,fp,labor,coalition\n"+row);
        bool rejected = false;
        try { readHistory(input,"2019fed","2022fed"); } catch (std::exception const&) { rejected = true; }
        assert(rejected);
    }
}
}

int main() {
    testIdentityAndPopulation();
    testExactMixtureAndNoDoubleCorrection();
    testReferenceAndCategoryConsistency();
    testExcludedCampaignCannotReturnThroughAggregateFit();
    testMaintainedAccountsAndValidation();
    std::cout << "Historical preference tests passed.\n";
}
