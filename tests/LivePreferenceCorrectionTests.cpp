#include "../LivePreferenceCorrections.h"
#include "../LiveTurnoutMath.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>

namespace {
double logistic(double x) { return 1 / (1 + std::exp(-x)); }

std::vector<LivePreferenceCorrections::Observation> exampleSeat() {
    // Known primary/composition relationship with small genuine booth scatter.
    // The final large PPVC is deliberately reported far from that relationship.
    std::vector<LivePreferenceCorrections::Observation> rows;
    for (std::size_t i = 0; i < 24; ++i) {
        double const formal = 700 + 120 * i;
        double const fpA = formal * (.27 + .013 * i), fpB = formal * .22;
        double const pool = formal - fpA - fpB, other = pool * (.45 + .009 * i);
        double const prediction = .6 + .3 * std::log((fpA + .25) / (formal - fpA + .25))
            + .4 * std::log((other + .25) / (pool - other + .25)) + .025 * std::sin(double(i));
        rows.push_back({i, formal, fpA, fpB, other, pool * logistic(prediction), i % 4 == 0});
    }
    rows.push_back({24, 18000, 8000, 6000, 2600, 200, true});
    return rows;
}

void checkRetainedPairProtection() {
    using namespace LivePreferenceCorrections;
    using namespace LivePartialCount;
    // Cooper's 26 October 2024 23:01 matched accounts. The PPVC later gained
    // only 25 FP votes while retaining its erroneous TCP. Previously that
    // small revision removed its roughly 1,417-vote outlier correction.
    std::vector<Observation> rows{
        {520,6927,2508,2529,1729,180,true}, // Returning Officer Cooper
        {521,1141,410,357,334,323,false}, {522,1070,447,303,278,268,false},
        {523,1061,429,378,224,206,false}, {524,595,219,191,168,155,false},
        {526,934,328,289,297,280,false}, {527,1184,519,346,276,264,false},
        {528,903,352,272,265,247,false}, {529,339,125,71,122,127,false},
        {530,5184,2017,1908,1057,1007,true}, {531,378,178,112,77,72,false},
        {532,847,259,316,251,232,false}, {533,1268,493,344,385,366,false},
        {534,595,222,174,188,181,false}, {535,1233,521,328,344,325,false}};
    TcpBasis basis{{{0,2508},{1,2529},{2,1729},{4,102},{7,59}},{{0,2688},{1,4239}},{}};
    Counts revisedFp{{0,2516},{1,2542},{2,1733},{4,102},{7,59}};
    auto before = prepare(rows).front();
    assert(before.broadProbability > .999 && before.peers == 14);
    assert(before.expectedRevision > 1400 && before.expectedRevision < 1430);
    auto matched = matchedCounts(basis.fp,basis.tcp,nullptr,false);
    assert(matched && !matched->retainedBasis && !matched->limits(0,1,basis.fp));
    // A current official label must not certify the retained preliminary pair.
    auto retained = matchedCounts(revisedFp,basis.tcp,&basis,true);
    assert(retained && retained->retainedBasis && !retained->recheckedReconciledPair);
    assert(retained->fp == basis.fp && retained->tcp == basis.tcp);
    auto& row = rows.front();
    row.formal = total(retained->fp); row.fpA = retained->fp.at(0); row.fpB = retained->fp.at(1);
    row.otherParty = retained->fp.at(2); row.gainA = retained->tcp.at(0)-row.fpA;
    row.recheckedReconciledPair = retained->recheckedReconciledPair;
    row.revisionLimits = retained->limits(0,1,revisedFp);
    auto after = prepare(rows).front();
    assert(after.expectedRevision == before.expectedRevision);
    assert(after.broadProbability == before.broadProbability);
    assert(after.revisionLower == -180 && after.revisionUpper == 1710);

    // The correction must reach both scenario accounts. The four changed
    // non-finalist votes still have their independent revision uncertainty;
    // a preference correction neither adds voters nor changes that allowance.
    Flows flows{{0,1},{1,0},{2,.85},{4,.4},{7,.5}};
    auto distribution = compress({after},123);
    for (std::uint64_t seed=0; seed<200; ++seed) {
        auto revised = reviseTcp(revisedFp,basis.tcp,basis,flows,0,0,1,double(seed%7)-3);
        assert(revised && revised->uncertainVotes() == 4);
        auto counted = retained->guaranteed;
        std::map<int,double> projected{{0,revised->first},{1,revised->votes-revised->first}};
        auto additionsA = projected.at(0)-counted.at(0), additionsB = projected.at(1)-counted.at(1);
        transfer(counted,projected,0,1,draw(distribution,seed));
        assert(counted.at(0) >= revisedFp.at(0) && counted.at(1) >= revisedFp.at(1));
        assert(std::abs(projected.at(0)+projected.at(1)-6952) < 1e-8);
        assert(std::abs(projected.at(0)-counted.at(0)-additionsA) < 1e-8);
        assert(std::abs(projected.at(1)-counted.at(1)-additionsB) < 1e-8);
    }
    assert(basis.tcp.at(0) == 2688 && revisedFp.at(0) == 2516);

    // Candidate revisions can preserve the total yet invalidate the current
    // pairing. Continue to use the original mix; a corrected new TCP supersedes it.
    auto equalTotal = basis.fp; equalTotal.at(0) += 300; equalTotal.at(1) -= 300;
    auto sameSize = matchedCounts(equalTotal,basis.tcp,&basis,false);
    assert(sameSize && sameSize->fp.at(0) == 2508);
    Counts newTcp{{0,4144},{1,2808}};
    assert(!matchedCounts(revisedFp,newTcp,&basis,false));
    auto official = matchedCounts(revisedFp,newTcp,nullptr,true);
    assert(official && official->recheckedReconciledPair && !official->retainedBasis);
    assert(!matchedCounts(revisedFp,basis.tcp,nullptr,false));
    auto changedRoster = revisedFp; changedRoster.erase(7);
    assert(!matchedCounts(changedRoster,basis.tcp,&basis,false));

    // Large removals cannot leave the old pool free to move votes that have
    // disappeared. Clip moments and actual draws identically to the surviving
    // guaranteed preferences; bounds are symmetric when finalists are reversed.
    auto reducedFp = revisedFp; reducedFp.at(2) -= 500;
    auto reduced = matchedCounts(reducedFp,basis.tcp,&basis,false);
    assert(reduced);
    row.revisionLimits = reduced->limits(0,1,reducedFp);
    auto reverse = reduced->limits(1,0,reducedFp);
    assert(reverse->lower == -row.revisionLimits->upper && reverse->upper == -row.revisionLimits->lower);
    auto limited = prepare(rows).front();
    assert(limited.expectedRevision < after.expectedRevision);
    auto limitedDistribution = compress({limited},234);
    for (std::uint64_t seed=0; seed<200; ++seed) {
        double change = draw(limitedDistribution,seed);
        assert(change >= row.revisionLimits->lower && change <= row.revisionLimits->upper);
    }
    reducedFp.at(2) = reducedFp.at(4) = reducedFp.at(7) = 0;
    auto emptyPool = matchedCounts(reducedFp,basis.tcp,&basis,false);
    assert(emptyPool);
    row.revisionLimits = emptyPool->limits(0,1,reducedFp);
    auto none = prepare(rows).front();
    assert(none.expectedRevision == 0 && none.revisionSecondMoment == 0);
    assert(draw(compress({none},345),1) == 0);

    // A previously verified pair keeps its own recheck protection through a
    // subsequent revision, even when the current metadata lacks that status.
    basis.recheckedReconciledPair = true;
    auto verified = matchedCounts(revisedFp,basis.tcp,&basis,false);
    assert(verified && verified->recheckedReconciledPair);
    row.revisionLimits = verified->limits(0,1,revisedFp);
    row.recheckedReconciledPair = verified->recheckedReconciledPair;
    assert(prepare(rows).front().broadProbability == 0);
}
}

int main() {
    using namespace LivePreferenceCorrections;
    checkRetainedPairProtection();
    assert(prepare({}).empty());
    assert(draw(compress({}, 1), 2) == 0);
    auto rows = exampleSeat();
    auto const originalGain = rows.back().gainA;
    auto mixtures = prepare(rows);
    assert(rows.back().gainA == originalGain);
    auto const& outlier = mixtures.back();
    assert(outlier.peers == 24 && outlier.discrepancy > 5);
    assert(outlier.broadProbability > .95 && outlier.fraction > .95);
    assert(outlier.expectedRevision > 1500);
    // An explicit reconciled recheck keeps its measured flow even when the
    // regression strongly disagrees. It still trains every other booth's fit.
    auto recheckedRows = rows;
    recheckedRows.back().recheckedReconciledPair = true;
    auto rechecked = prepare(recheckedRows);
    assert(rechecked.back().directedCorrectionSuppressed);
    assert(rechecked.back().broadProbability == 0);
    assert(rechecked.back().routineProbability > 0 && rechecked.back().backgroundProbability > 0);
    assert(rechecked.back().routineScale == outlier.routineScale);
    assert(rechecked.back().backgroundScale == outlier.backgroundScale);
    assert(std::abs(rechecked.back().expectedRevision) < outlier.expectedRevision / 10);
    for (std::size_t i=0; i+1<rows.size(); ++i) {
        assert(rechecked[i].prediction == mixtures[i].prediction);
        assert(rechecked[i].broadProbability == mixtures[i].broadProbability);
    }
    recheckedRows.back().recheckedReconciledPair = false;
    assert(prepare(recheckedRows).back().expectedRevision == outlier.expectedRevision);
    for (auto const& mixture : mixtures) {
        assert(mixture.broadProbability >= 0 && mixture.routineProbability >= 0 && mixture.backgroundProbability > 0);
        assert(mixture.broadProbability + mixture.backgroundProbability + mixture.routineProbability <= 1 + 1e-12);
        assert(mixture.backgroundScale > mixture.routineScale);
        assert(std::isfinite(mixture.expectedRevision));
    }
    // No count thresholds are required for sparse peers or booth size. With no
    // peer there is no claimed regression signal, but both rechecks remain.
    auto single = prepare({{0, 42, 20, 12, 5, 0, false}});
    assert(single[0].broadProbability == 0 && single[0].routineProbability > 0 && single[0].backgroundProbability > 0);
    auto sparse = prepare({rows[0], rows[1]});
    assert(sparse[0].peers == 1 && std::isfinite(sparse[0].discrepancy));
    auto changed = rows;
    changed.back().formal += .0001;
    assert(std::abs(prepare(changed).back().broadProbability - outlier.broadProbability) < 1e-6);

    // A compelling small-booth discrepancy must overcome weaker size evidence.
    // Multiplying by a booth-size taper used to impose a probability ceiling,
    // even for an arbitrarily implausible preference allocation.
    auto smallOutlierRows = rows;
    smallOutlierRows.back() = {24, 841, 300, 220, 200, 20, true};
    auto const smallOutlier = prepare(smallOutlierRows).back();
    assert(smallOutlier.discrepancy > 5 && smallOutlier.broadProbability > .95);

    auto distribution = compress(mixtures, 1234);
    assert(distribution.explicitBooths.size() == ExplicitBooths);
    assert(distribution.aggregateRevisions.size() == AggregateSamples);
    assert(draw(distribution, 5678) == draw(distribution, 5678));
    auto zeroDistribution = compress(single, 1234);
    bool sawUnchanged = false, sawPositive = false;
    for (std::uint64_t seed = 1; seed < 2000; ++seed) {
        double const revision = draw(zeroDistribution, seed);
        assert(revision >= 0 && revision <= single[0].pool);
        sawUnchanged = sawUnchanged || revision == 0;
        sawPositive = sawPositive || revision > 0;
    }
    assert(sawUnchanged && sawPositive);

    // Even without a regression discrepancy, a large booth needs nontrivial
    // tails for a recheck. Balanced reported preferences make the distribution
    // symmetric, so broad tails must appear on both sides without a mean shift.
    auto const balanced = prepare({{0, 15000, 6000, 4000, 2000, 2500, true}});
    assert(balanced[0].broadProbability == 0 && balanced[0].backgroundProbability > .2);
    assert(std::abs(balanced[0].expectedRevision) < 1e-8);
    auto const balancedDistribution = compress(balanced, 9123);
    std::size_t positiveTail = 0, negativeTail = 0, unchanged = 0;
    double sum = 0, secondMoment = 0;
    constexpr std::uint64_t draws = 20000;
    for (std::uint64_t seed = 1; seed <= draws; ++seed) {
        double const revision = draw(balancedDistribution, seed);
        assert(revision >= -2500 && revision <= 2500);
        positiveTail += revision > 100;
        negativeTail += revision < -100;
        unchanged += revision == 0;
        sum += revision;
        secondMoment += revision * revision;
    }
    assert(positiveTail > draws / 200 && negativeTail > draws / 200);
    assert(unchanged > draws / 4 && unchanged < draws / 2);
    assert(std::abs(sum / draws) < 2);
    // Contributor ranking must account for the new component as well as the
    // sampler: compare its prepared second moment with actual scenario draws.
    assert(std::abs(secondMoment / draws / balanced[0].revisionSecondMoment - 1) < .25);

    // This is the critical full-simulator handoff: an opposing recheck persists
    // when there are no additions, despite the prior proposing another share.
    std::map<int, double> const reported{{0, 8000}, {1, 6000}};
    auto counted = reported;
    std::map<int, float> projected{{0, 8000}, {1, 6000}};
    transfer(counted, projected, 0, 1, -1000);
    assert(counted.at(0) == 7000 && counted.at(1) == 7000);
    assert(reported.at(0) == 8000 && reported.at(1) == 6000);
    LiveData::VoteCountAccount source{&counted, &projected};
    auto account = LiveTurnoutMath::partitionAccount(source, [](int p) { return p; });
    auto shares = LiveTurnoutMath::reconcileForecast(account, {{0, 70}, {1, 30}});
    assert(shares.at(0) == 50 && shares.at(1) == 50);

    // With outstanding ballots, the recheck leaves that allowance unchanged,
    // and subsequent remainder variation cannot erase the adjusted counts.
    projected = {{0, 7250}, {1, 7150}};
    transfer(counted, projected, 0, 1, 100.375);
    assert(std::abs(LiveTurnoutMath::remaining(projected.at(0), counted.at(0)) - 250) < .001);
    assert(std::abs(LiveTurnoutMath::remaining(projected.at(1), counted.at(1)) - 150) < .001);
    account = LiveTurnoutMath::partitionAccount(source, [](int p) { return p; });
    shares = LiveTurnoutMath::reconcileForecast(account, {{0, 20}, {1, 80}});
    assert(shares.at(0) * 144 >= counted.at(0) - .001);
    assert(shares.at(1) * 144 >= counted.at(1) - .001);

    // Real rechecking draws have arbitrary fractional parts. Float storage can
    // round both projected candidates below their double-precision counted
    // allocation, unlike the exactly representable transfers tested above.
    // Reproduce the progress refresh and then the final forecast handoff.
    for (double revision : {.3, -.3, 27.812341, -127.812341}) {
        std::map<int, double> fractionalCounts{{0, 8000}, {1, 1000}};
        std::map<int, float> fractionalProjection{{0, 8000}, {1, 1000}};
        transfer(fractionalCounts, fractionalProjection, 0, 1, revision);
        double const total = double(fractionalProjection.at(0)) + double(fractionalProjection.at(1));
        double const progress = LiveTurnoutMath::completion(9000, total);
        assert(progress <= 1 && std::abs(progress - 1) < 1e-7);
        LiveData::VoteCountAccount fractionalSource{&fractionalCounts, &fractionalProjection};
        auto fractionalAccount = LiveTurnoutMath::partitionAccount(fractionalSource, [](int p) { return p; });
        auto fractionalShares = LiveTurnoutMath::reconcileForecast(fractionalAccount, {{0, 50}, {1, 50}});
        assert(std::abs(fractionalShares.at(0) - 100 * fractionalCounts.at(0) / 9000) < 1e-5);
    }
    bool rejectedShortfall = false;
    try { LiveTurnoutMath::remaining(8999, 9000); }
    catch (std::runtime_error const&) { rejectedShortfall = true; }
    assert(rejectedShortfall);
    std::cout << "Preference correction tests passed\n";
}
