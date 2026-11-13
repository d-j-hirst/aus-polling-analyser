// An incremental FP account must enter its own forecast through the local
// batch blend, not also through background movement or candidate counts.
#include "../LiveNewVotes.h"
#include "../LiveSimulationMath.h"

#include <cassert>
#include <iostream>

namespace {
using namespace LiveNewVotes;
using Counts = LivePartialCount::Counts;

Shares background(FpEvidence evidence)
{
    auto region = aggregate({evidence}, evidence.expected);
    auto election = aggregate({region}, evidence.expected);
    condition(election, {}, LiveSimulationMath::observationWeight(election.confidence, 625));
    condition(region, {&election}, LiveSimulationMath::observationWeight(region.confidence));
    condition(evidence, {&election, &region}, LiveSimulationMath::observationWeight(evidence.confidence));
    Shares shares;
    for (int party : {0,1}) {
        double shift = election.specific[party] + region.specific[party] + evidence.specific[party];
        shares[party] = LivePartialCount::inverseLogOdds(shift / 25);
    }
    return normalize(std::move(shares));
}

void checkPartialBatchCannotTrainItsOwnBackground()
{
    double const deviation = 25 * std::log(.6/.4);
    Counts const received{{0,600},{1,400}};
    FpEvidence const partial{10000,1000,.1,.25,{{0,deviation},{1,-deviation}},{},{}};
    FpEvidence const missing{10000,0,0,1,{},{},{}};
    BackgroundFpSources corrected;
    corrected.add(partial, received, true);
    corrected.add(missing, {}, false);
    auto const context = background(aggregate(corrected.movementSources,20000));
    assert(context.at(0) == .5 && context.at(1) == .5);
    assert(corrected.currentVotes.empty());
    auto const future = composition(received, context, 10000, compositionCategory(true,false));
    assert(std::abs(future.at(0) - .50271478) < 1e-8);
    // Known votes remain fully present; only the other 9,000 use the estimate.
    auto const projected = project(received,future,10000);
    assert(std::abs(projected.at(0) - (600 + 9000 * future.at(0))) < 1e-10);
    assert(projected.at(0) >= 600 && projected.at(1) >= 400);

    // Reproduce the former overlap by deliberately allowing this source into
    // the booth-style pool. Its background already moves above 55%.
    BackgroundFpSources overlap;
    overlap.add(partial,received,false);
    overlap.add(missing,{},false);
    auto const contaminated = background(aggregate(overlap.movementSources,20000));
    assert(contaminated.at(0) > .55);
    assert(composition(received,contaminated,10000,compositionCategory(true,false)).at(0) > .55);
    // More received votes can still move the local estimate through its blend.
    auto const later = composition({{0,1800},{1,1200}},context,10000,compositionCategory(true,false));
    assert(later.at(0) > future.at(0) && later.at(0) < .6);
}

void checkDenominatorsAndCompletedBooths()
{
    Counts const day{{0,5000},{1,5000}};
    FpEvidence const ordinary{10000,10000,1,1,{{0,2},{1,-2}},{},{}};
    FpEvidence const early{10000,1000,.1,.25,{{0,20},{1,-20}},{},{}};
    BackgroundFpSources sources;
    sources.add(ordinary,day,false);
    sources.add(early,{{0,900},{1,100}},true);
    auto const evidence = aggregate(sources.movementSources,20000);
    assert(evidence.expected == 20000);
    assert(evidence.confidence == .5); // Exclusion must not make the district look complete.
    assert(evidence.deviations.at(0) == 2);
    assert(sources.currentVotes == day);

    // Completion does not change the source rule for an incremental place.
    BackgroundFpSources finished;
    finished.add(ordinary,day,false);
    finished.add({10000,10000,1,.25,{{0,20},{1,-20}},{},{}},{{0,9000},{1,1000}},true);
    assert(aggregate(finished.movementSources,20000).confidence == .5);
    assert(finished.currentVotes == day);
    assert((project({{0,9000},{1,1000}},{{0,.5},{1,.5}},10000) == Shares{{0,9000},{1,1000}}));

    // Ordinary federal PPVCs are booth-style accounts, so their evidence stays.
    BackgroundFpSources federal;
    federal.add(ordinary,day,false);
    federal.add(early,{{0,900},{1,100}},false);
    auto const original = aggregate({ordinary,early},20000);
    auto const retained = aggregate(federal.movementSources,20000);
    assert(retained.confidence == original.confidence);
    assert(retained.deviations == original.deviations);
    assert(retained.counted == original.counted);
    assert((federal.currentVotes == Counts{{0,5900},{1,5100}}));
}

void checkMissingCandidateFallbackUsesTheSameSources()
{
    Counts const ordinary{{0,800},{1,800},{100101,400},{100202,0}};
    Counts const ownBatch{{0,50},{1,50},{100101,900}};
    BackgroundFpSources sources;
    // The new candidate lacks historical movement but still has eligible raw
    // counts. Distinct independent identities must remain distinct here.
    sources.add({2000,2000,1,1,{{0,1},{1,-1}},{},{}},ordinary,false);
    sources.add({10000,1000,.1,.25,{{0,10},{1,-10}},{},{}},ownBatch,true);
    assert(sources.currentVotes == ordinary);
    assert(sources.currentVotes.contains(100202) && sources.currentVotes.at(100202) == 0);
    assert(!sources.currentVotes.contains(5));
    double const observed = double(sources.currentVotes.at(100101)) / LivePartialCount::total(sources.currentVotes);
    assert(observed == .2); // The own batch's 90% must not replace this share.
    BackgroundFpSources noEligibleEvidence;
    noEligibleEvidence.add({10000,1000,.1,.25,{{0,10}},{},{}},ownBatch,true);
    assert(noEligibleEvidence.currentVotes.empty());
    assert(aggregate(noEligibleEvidence.movementSources,10000).confidence == 0);

    // Candidate-specific pooling weights must respect the general exclusion.
    BackgroundFpSources pooling;
    pooling.add({1000,1000,1,1,{{100101,25}},{},{{100101,1000}}},ownBatch,true);
    auto const excluded = aggregate(pooling.movementSources,1000);
    assert(excluded.deviations.empty());
    assert(excluded.poolingVotes.at(100101) == 0);
}

void checkCompositionClassificationAndNoCountIdentity()
{
    assert(compositionCategory(false,false) == Category::General);
    assert(compositionCategory(true,false) == Category::AggregatedEarly); // WA Ordinary-labelled centre.
    assert(compositionCategory(false,true) == Category::AggregatedEarly); // Early declaration category.
    assert(compositionCategory(true,true) == Category::AggregatedEarly);
    double const early = observedWeight(1000,10000,compositionCategory(true,false));
    assert(early == 1000.0/(1000+4*9000+300));
    assert(early < observedWeight(1000,10000,compositionCategory(false,false)));
    Shares const prior{{0,.4723456789123},{1,1-.4723456789123}};
    assert(composition({},prior,10000,compositionCategory(true,false)) == prior);
}
} // namespace

int main()
{
    checkPartialBatchCannotTrainItsOwnBackground();
    checkDenominatorsAndCompletedBooths();
    checkMissingCandidateFallbackUsesTheSameSources();
    checkCompositionClassificationAndNoCountIdentity();
    std::cout << "Live FP background tests passed\n";
}
