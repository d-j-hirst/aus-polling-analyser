#include "LivePartyConversion.h"
#include "LivePartialCount.h"

#include <cassert>
#include <iostream>

using namespace LivePartyConversion;

void close(double a, double b) { assert(std::abs(a-b) < 1e-9); }

History fictionalHistory(std::string previous = "2019fed", std::string current = "2022fed") {
    std::istringstream input(
        "term,seat,party,candidate_key,fp,formal,alp,classic\n"
        "2019fed,Coast,GRN,alex green,1000,10000,850,1\n"
        "2019fed,Hill,GRN,pat green,3000,10000,2760,0\n"
        "2019fed,Hill,IND,lee river,2000,10000,1400,0\n"
        "2022fed,Hill,GRN,pat green,4000,10000,3960,0\n");
    return readHistory(input, previous, current);
}

void testHistorySelection() {
    auto history = fictionalHistory();
    assert(history.records.size() == 3 && history.hasGreens);
    assert(fictionalHistory("2022fed", "2022fed").records.empty());
    assert(fictionalHistory("2025fed", "2022fed").records.empty());
    assert(fictionalHistory("2019fed", "2022vic").records.empty());
    assert(fictionalHistory("2016fed").records.empty());
    assert(history.find("Hill", "GRN")->fp == 3000);
    assert(!history.find("New Hill", "GRN"));
    assert(history.find("Hill", "IND", candidateKey("RIVER, Lee")));
    assert(!history.find("Hill", "IND", candidateKey("Sam River")));
    auto duplicate = *history.find("Hill", "IND", "lee river");
    history.records.push_back(duplicate);
    assert(!history.find("Hill", "IND", "lee river"));
}

void testStrengthAndHistoricalOffsets() {
    auto history = fictionalHistory();
    close(phasedStrength(.2,0,0),logit(.2));
    assert(std::abs(phasedStrength(.2,40,80)-logit(.2)) < .03);
    assert(phasedStrength(.2,15000,50000) > phasedStrength(.2,150,500));
    double low = greensFlow(.85,logit(.1),0,history);
    double high = greensFlow(.85,logit(.3),0,history);
    close(low,.85);
    assert(high > .91 && high < .92);
    close(greensFlow(.85,logit(.3),0,History{}),.85);
    close(greensFlow(0,logit(.3),0,history),0);
    close(greensFlow(1,logit(.3),0,history),1);
    auto row = history.find("Hill","GRN");
    double rate = greensFlow(.85,row->strength(),history.greensResidual(*row),history);
    assert(rate > high && rate < logistic(row->flow()));

    // A matched booth's historical residual must be recomputed after changing
    // the party prior: applying that residual reconstructs the same old TCP.
    LivePartialCount::Counts fp{{0,3000},{1,4000},{2,3000}}, tcp{{0,5760},{1,4240}};
    LivePartialCount::Flows flows{{0,1},{1,0},{2,rate}};
    auto evidence = LivePartialCount::measure(fp,tcp,flows,0,1);
    assert(evidence);
    close(LivePartialCount::expectedFirst(fp,flows,evidence->offset),3000+3000*2760.5/3001);

    auto independent = history.find("Hill","IND","lee river");
    close(returningIndependentFlow(*independent,.5,.5),logistic(independent->flow()));
    assert(returningIndependentFlow(*independent,.55,.5) > logistic(independent->flow()));
}

History weightedHistory(std::string const& weight, std::string const& party = "GRN") {
    std::istringstream input(
        "term,seat,party,candidate_key,fp,formal,alp,classic,forward_residual_weight\n"
        "2019fed,Coast,GRN,alex green,1000,10000,850,1,1\n"
        "2019fed,Hill," + party + ",pat green,3000,10000,2400,0," + weight + "\n");
    return readHistory(input,"2019fed","2022fed");
}

void testExceptionalHistoricalResidual() {
    // A fictional campaign depressed preferences to Labor. Stop carrying that
    // district offset forward, while retaining its real old account and the
    // historical reconstruction used for matched swings/reference alignment.
    auto retained=weightedHistory("1"), excluded=weightedHistory("0"), half=weightedHistory("0.5");
    auto old=retained.find("Hill","GRN"), row=excluded.find("Hill","GRN");
    assert(old && row);
    close(row->fp,old->fp); close(row->alp,old->alp); close(row->formal,old->formal);
    close(row->flow(),old->flow()); close(row->strength(),old->strength());
    close(excluded.greensResidual(*row),retained.greensResidual(*old));
    assert(excluded.greensResidual(*row)<0);
    close(excluded.forwardGreensResidual(*row),0);
    close(half.forwardGreensResidual(*half.find("Hill","GRN")),.5*retained.greensResidual(*old));
    auto historical=[](History const& h, Record const& r) {
        return greensFlow(logistic(h.greensLevel),r.strength(),h.greensResidual(r),h);
    };
    close(historical(retained,*old),historical(excluded,*row));
    auto future=[](History const& h, Record const& r) {
        return greensFlow(.85,logit(.4),h.forwardGreensResidual(r),h);
    };
    assert(future(excluded,*row)>future(retained,*old));
    close(future(excluded,*row),greensFlow(.85,logit(.4),0,excluded));
    // Excluding part of a redistributed source does not renormalise its votes
    // onto other districts. It contributes zero residual, not absent history.
    double mixed=(40*excluded.forwardGreensResidual(*row)+60*retained.forwardGreensResidual(*old))/100;
    close(mixed,.6*retained.forwardGreensResidual(*old));
    close(excluded.greensLevel,retained.greensLevel);
    close(excluded.greensStrength,retained.greensStrength);
    auto legacy=fictionalHistory();
    close(legacy.forwardGreensResidual(*legacy.find("Hill","GRN")),
        legacy.greensResidual(*legacy.find("Hill","GRN")));

    for (auto const* invalid : {"-0.1","1.1","nan","inf","0junk",""}) {
        bool rejected=false;
        try { weightedHistory(invalid); } catch (std::exception const&) { rejected=true; }
        assert(rejected);
    }
    bool rejected=false;
    try { weightedHistory("0","IND"); } catch (std::runtime_error const&) { rejected=true; }
    assert(rejected); // Named independent history has no Greens district residual.
}

void testPublishedAccounts() {
    // Validate the shipped data's denominators and identities, without using
    // any target-election TPP to calibrate a fictional replay's current flows.
    for (auto const& term : {"2019fed","2022fed","2025fed"}) {
        std::ifstream input("analysis/Data/historical-party-tpp-flows.csv");
        assert(input);
        auto history = readHistory(input,term,"2028fed");
        assert(history.hasGreens);
        std::size_t greens = 0;
        for (auto const& row : history.records) {
            assert(history.find(row.seat,row.party,row.candidate));
            if (row.party == "GRN") ++greens;
            bool exceptional=std::string(term)=="2019fed" && row.seat=="Melbourne" && row.party=="GRN";
            close(row.forwardResidualWeight,exceptional ? 0 : 1);
            if (exceptional) {
                assert(row.fp==45876 && row.alp==39004 && row.formal==93064);
                close(history.forwardGreensResidual(row),0);
                assert(history.greensResidual(row)<0);
            }
        }
        assert(greens == (std::string(term) == "2025fed" ? 149 : 151));
    }
}

void testMatchedCalibration() {
    // Different source strengths have different normal flows. Learning the
    // residual must not flatten 83% and 88% to a shared absolute flow.
    auto account = [](double expected, int actual, int greens = 1000, int others = 0) {
        MatchedAccount result;
        LivePartialCount::Counts fp{{0,3000},{1,6000},{2,greens},{3,others}};
        LivePartialCount::Counts tcp{{0,3000+actual},{1,6000+greens+others-actual}};
        result.add(fp,tcp,{{0,1},{1,0},{2,expected},{3,.5}},0,1,2,100000,1);
        return result;
    };
    auto low = account(.83,830), high = account(.88,880);
    auto neutral = calibrate({low,high},2,5);
    close(neutral.factors.mean[0],0);
    close(neutral.factors.mean[1],0);
    auto empty = calibrate({},2,5);
    close(empty.factors.covariance[0][0],1);
    close(empty.factors.covariance[1][1],1);
    auto positive = calibrate(std::vector<MatchedAccount>(50,account(.83,800)),2,5);
    auto negative = calibrate(std::vector<MatchedAccount>(50,account(.17,200)),2,5);
    assert(positive.factors.mean[0] > 0);
    close(positive.factors.mean[0],-negative.factors.mean[0]);
    assert(positive.factors.covariance[0][0] >= TransferVarianceFloor);
    close(positive.factors.mean[1],0); // No independent exposure, no attribution.
    close(positive.factors.covariance[1][1],1);
    // One district with hundreds of booths still supplies one district-level
    // observation. Replicating an identical account's vote size cannot erase
    // the 5-point district variation allowance or population-transfer floor.
    auto large = low;
    large.pool*=100;large.expected*=100;large.observed*=100;
    for (auto& g : large.groups) g*=100;
    auto largeFit=calibrate({large},2,5);
    assert(largeFit.factors.covariance[0][0]>.9);
    auto mixed=calibrate(std::vector<MatchedAccount>(50,account(.83,483,100,900)),2,5);
    // The mixed residual is predominantly unidentified other preferences,
    // not a direct observation that Greens flow fell by 10 percentage points.
    assert(std::abs(mixed.factors.mean[0])<.2);
    auto partial=low;
    partial.add({{0,3000},{1,6000},{2,1000}},{{0,380},{1,620}},
        {{0,1},{1,0},{2,.83}},0,1,2,100000,1);
    close(partial.pool,low.pool); // Partial/unmatched current cohorts excluded.
}

int main() {
    testMatchedCalibration();
    testHistorySelection();
    testStrengthAndHistoricalOffsets();
    testExceptionalHistoricalResidual();
    testPublishedAccounts();
    std::cout << "Live party conversion tests passed.\n";
}
