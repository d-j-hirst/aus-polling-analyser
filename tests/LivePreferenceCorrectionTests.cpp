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
}

int main() {
    using namespace LivePreferenceCorrections;
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
