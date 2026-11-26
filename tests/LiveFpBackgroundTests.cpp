// An incremental FP account must enter its own forecast through the local
// batch blend, not also through background movement or candidate counts.
#include "../LiveNewVotes.h"
#include "../LiveSimulationMath.h"
#include "../LiveFpEvidence.h"
#include "../LiveFederalDeclarations.h"
#include "../LiveVecCentreGeography.h"
#include "../LiveNarracanPrimaries.h"

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

void checkSharedOthersAtFirstDistrictReturn()
{
    // Two unprofiled candidates share what remains of the 14% Others prior
    // after 40 historically comparable votes have been assigned out of 1,000.
    std::map<int,double> projected{{7,40}};
    std::vector<UnprofiledCandidate> candidates{{8,{},0}, {9,{},0}};
    auto projectCandidates = [&](double median, double weight) {
        projectUnprofiledCandidates(projected, candidates, 1000, median, 40, weight);
        assert(projected.at(7) == 40);
    };
    projectCandidates(14,0);
    assert(projected.at(8) == 50 && projected.at(9) == 50);
    candidates[0].districtPercent = 5;
    projectCandidates(14,.01);
    assert(std::abs(projected.at(8)-50) < 1e-10 && projected.at(9) == 50);
    candidates[0].districtPercent = 0;
    candidates[0].boothVariation = 9;
    projectCandidates(14,0);
    assert(projected.at(8) == 50 && projected.at(9) == 50);
    candidates[0].boothVariation = 0;
    projectCandidates(14,.1);
    assert(projected.at(8) < 50 && projected.at(8) > 10 && projected.at(9) == 50);
    candidates[0].districtPercent = 12;
    projectCandidates(14,1);
    assert(std::abs(projected.at(8)-120) < 1e-10 && projected.at(9) == 50);
    for (double median : {0.,3.}) {
        projectCandidates(median,0);
        assert(projected.at(8) == 10 && projected.at(9) == 10);
    }
}

void checkFirstDeclarationBatchUncertainty()
{
    constexpr int target = 21598;
    Shares const context{{0,.307},{1,.450},{2,.243}};
    LivePartialCount::Counts const firstBatch{{0,112},{1,182},{2,32}};
    auto projectedLaborVotes = [&](LivePartialCount::Counts const& counted, double draw) {
        Shares variation{{0,draw * declarationFpLogOddsSd(20000)}};
        return project(counted,composition(counted,context,target,Category::AggregatedEarly,{},variation),target).at(0);
    };
    double const before = projectedLaborVotes({},1) - projectedLaborVotes({},-1);
    double const after = projectedLaborVotes(firstBatch,1) - projectedLaborVotes(firstBatch,-1);
    assert(after < before && after > .95 * before);
    assert(std::abs(projectedLaborVotes(firstBatch,0) - projectedLaborVotes({},0)) < .002 * target);
    LivePartialCount::Counts const complete{{0,7300},{1,9200},{2,target-16500}};
    assert(projectedLaborVotes(complete,1) == projectedLaborVotes(complete,-1));
}

void checkFederalReferenceAcrossDistrictLevels()
{
    namespace Geo = LiveBoothGeography;
    // Strong, weak and remote ordinary booths, two early centres and postal.
    // None of these weights come from the election-night return being tested.
    std::vector<Geo::Place> places{{"ordinary",1000,-.8},{"ordinary",2000,-2.2},
        {"ordinary",500,{}},{"early",1500,{}},{"early",1000,{}},{"postal",1000,{}}};
    for (double target : {.09,.22,.36}) {
        auto ref = Geo::baseline(places,target);
        assert(ref[0].share > target && ref[1].share < target && ref[2].share == target);
        assert(ref[3].share == target && ref[4].share == target && ref[5].share == target);
        Geo::District district;
        std::vector<Geo::Return> accepted(places.size());
        // A remote booth represents the full mix of ordinary voters. An
        // early batch is allowed to train its own category later in the count.
        accepted[2] = {200,800};
        accepted[3] = {900,1000};
        auto remoteFirst = Geo::prepareDistrict(places,accepted,target,district);
        assert(std::abs(remoteFirst[2].absoluteShare-.25) < 1e-12);
        assert(std::abs(remoteFirst[0].absoluteShare/3+2*remoteFirst[1].absoluteShare/3-.25) < 1e-12);
        accepted[2] = {};
        auto earlyOnly = Geo::prepareDistrict(places,accepted,target,district);
        assert(!district.ordinaryMovement);
        for (auto const& r : earlyOnly) assert(r.absoluteShare == r.share);
        // Missing 2022 candidacy and sharply rising support both preserve the
        // weighted district mean. A genuine historical difference between
        // postal and ordinary survives at the current polling level.
        auto historical = places;
        historical[0].previousOn = 10; historical[0].previousFormal = 1000;
        historical[1].previousOn = 120; historical[1].previousFormal = 2000;
        historical[5].previousOn = 80; historical[5].previousFormal = 1000;
        auto returning = Geo::baseline(historical,target);
        assert(returning[5].share > returning[2].share);
        historical[0].previousOn += 90; historical[1].previousOn -= 90;
        auto rearranged = Geo::baseline(historical,target);
        for (auto const* pattern : {&ref,&returning}) {
            double votes = 0, total = 0;
            for (size_t i = 0; i < places.size(); ++i) {
                votes += pattern->at(i).share*places[i].size; total += places[i].size;
                assert(returning[i].share == rearranged[i].share);
            }
            assert(std::abs(votes/total-target) < 1e-12);
            auto high = pattern->front().share;
            std::vector<Geo::Point> counted{{LivePartialCount::logOdds(high),400}};
            assert(std::abs(*Geo::movement(counted,400*high,400)) < 1e-12);
            assert(*Geo::movement(counted,400*(high+.02),400) > 0);
            assert(*Geo::movement(counted,400*(high-.02),400) < 0);
            assert(std::isfinite(*Geo::movement(counted,0,400)));
            assert(std::isfinite(*Geo::movement(counted,400,400)));
            // Two simultaneous reports of very different sizes and shares
            // recover the same shift from their combined actual ballots.
            counted.push_back({LivePartialCount::logOdds(pattern->at(1).share),1100});
            double observed = 0;
            for (auto const& p : counted) observed += p.weight*LivePartialCount::inverseLogOdds(p.odds+.35);
            assert(std::abs(*Geo::movement(counted,observed,1500)-.35) < 1e-12);
        }
    }
    assert(!Geo::movement({},0,0));
}

void checkOutstandingVoteUsesReportingLocations()
{
    namespace Fp = LiveFpEvidence;
    // A high ordinary booth has reported at its 40% expectation. The other
    // ordinary booth expects 10%. Postal voters expect 20%. Both sources of
    // FP evidence must give the outstanding voters those specific shares.
    auto unit = [](bool ordinary, double expectedShare, Fp::Shares counts, Fp::Shares projection) {
        Fp::Unit u;
        u.counted = counts; u.projected = projection; u.ordinary = ordinary;
        u.group = ordinary ? 0 : 1; u.geographicShare = {2,expectedShare}; return u;
    };
    auto strong = unit(true,.4,{{0,350},{1,250},{2,400}},{{0,350},{1,250},{2,400}});
    auto weak = unit(true,.1,{},{{0,500},{1,400},{2,100}});
    auto postal = unit(false,.2,{},{{0,300},{1,500},{2,200}});
    auto prepared = Fp::prepare({strong,weak,postal},.33);
    assert(std::abs(prepared.evidence.absolute.at(2)-300) < 1e-9);
    assert(std::abs(prepared.future.at(2)-.15) < 1e-10);
    assert(Fp::adjusted(strong,prepared.factors) == strong.counted);
    assert(std::abs(Fp::adjusted(weak,prepared.factors).at(2)
        + Fp::adjusted(postal,prepared.factors).at(2)-300) < .001);
    // A distinct postal batch then supplies its own composition. Only the
    // remainder is allocated. The same input without the opt-in reproduces
    // the legacy estimate from the raw ordinary share, and completion keeps
    // every observed candidate count in either case.
    postal.counted = {{0,50},{1,50},{2,100}};
    auto partial = Fp::prepare({strong,weak,postal},.4);
    assert(partial.evidence.absolute.at(2)>100+800*.2);
    for (auto const& [id,v] : Fp::adjusted(postal,partial.factors)) assert(v >= postal.counted.at(id));
    for (auto* u : {&strong,&weak,&postal}) u->geographicShare.reset();
    assert(Fp::collect({strong,weak,postal},.4).absolute.at(2)>600);
    weak.counted = weak.projected; postal.counted = postal.projected;
    assert(Fp::prepare({strong,weak,postal},1).factors.empty());
}

void checkEveryCandidateAndBoothTotalAtZeroCount()
{
    namespace Fp = LiveFpEvidence;
    // 400 One Nation votes move between the two ordinary booths. A third
    // candidate also stands in both places. Different rival mixes expose the
    // drift that proportional rescaling within each booth alone would cause.
    std::vector<Fp::GeographicRow> rows{
        {{{0,900},{1,200},{2,700}},300./1800},
        {{{0,100},{1,600},{2,100}},500./800},
        {{{0,110},{1,190},{2,100}},.25}};
    auto factors = Fp::opponentFactors(rows,2);
    Fp::Shares before, after;
    for (auto const& row : rows) {
        auto votes = row.blind;
        auto size = Fp::total(votes);
        Fp::add(before,votes);
        LiveBoothGeography::allocate(votes,2,row.share,size,factors);
        Fp::add(after,votes);
        assert(std::abs(Fp::total(votes)-size) < 1e-10);
        assert(std::abs(votes.at(2)-size*row.share) < 1e-10);
    }
    for (auto [id,votes] : before) assert(std::abs(after.at(id)-votes) < .001);
}
void checkSaFederalDeclarationPrior()
{
    namespace Fed = LiveFederalDeclarations;
    using enum LiveData::VoteType;
    auto path = Fed::inputPath(".", "2026sa");
    assert(path && !Fed::inputPath(".", "2022vic"));
    auto prior = Fed::readPrior(*path,"2026sa");
    assert(prior.size() == 3 && !prior.contains(Early) && !prior.contains(Provisional));
    using Booth = LiveData::BoothType;
    assert(Fed::homeInPerson("2026sa",Ordinary, Booth::Normal, "Central"));
    assert(Fed::homeInPerson("2026sa",Ordinary, Booth::Ppvc, "Central EVC"));
    assert(!Fed::homeInPerson("2026sa",Ordinary, Booth::Normal, "West Coast Mobile"));
    assert(!Fed::homeInPerson("2026sa",PrePoll, Booth::Other, "Early Absent"));

    // Exactly 22% smoothed in-person support. Before the first category
    // batch the predicted odds ratio must equal the transferred federal one.
    Fed::HomeVote home{2199.5, 9999, 2000};
    for (auto [mode, difference] : {std::pair{Absent, .231435490}, {PrePoll, .262142884}, {Postal, .114061102 / 2}}) {
        auto noBatch = Fed::estimate(4, home, prior.at(mode), 0, 0, 1000, .19, mode).value();
        assert(std::abs(LivePartialCount::logOdds(noBatch.future) - std::log(22./78) - difference) < 1e-9);
        auto partial = Fed::estimate(4, home, prior.at(mode), 150, 400, 1000, .19, mode).value();
        double w = mode == PrePoll ? 400./(400+4*600+300) : .4;
        double expected = (1-w)*noBatch.future + w*150.5/401.;
        assert(std::abs(partial.future - expected) < 1e-12);

        Counts counts{{0, 150}, {1, 100}, {4, 150}};
        Shares rest{{0, 324}, {1, 162}, {4, 114}};
        Fed::allocate(rest, 600, partial, counts, false);
        auto projected = project(counts, normalize(rest), 1000);
        assert(std::abs(projected.at(4) - (150 + 600*expected)) < 1e-9);
        assert(std::abs(rest.at(0) / rest.at(1) - 2) < 1e-12);
        assert(std::abs(LiveFpEvidence::total(projected) - 1000) < 1e-9);

        // Preserve the existing draw relative to the deterministic centre.
        Shares drawn{{0, 300}, {1, 150}, {4, 150}};
        Fed::allocate(drawn, 600, partial, counts, true);
        double before = LivePartialCount::logOdds(.25) - LivePartialCount::logOdds(.19);
        double after = LivePartialCount::logOdds(drawn.at(4)/600) - LivePartialCount::logOdds(expected);
        assert(std::abs(before - after) < 1e-12);
    }
    assert(!Fed::estimate(4, {2000, 10000, 0}, prior.at(Absent), 0, 0, 1000, .19, Absent));
    assert(!Fed::estimate(4, home, prior.at(Postal), 100, 400, 400, .19, Postal));
    // A reported zero is evidence. It differs from an unreported category,
    // and even a previously unanimous projection can give rivals future votes.
    auto zero = Fed::estimate(4, home, prior.at(Postal), 0, 100, 200, 1., Postal).value();
    assert(zero.future > 0 && zero.future < zero.context);
    Shares unanimous{{4, 100}, {2, 0}, {3, 0}};
    Fed::allocate(unanimous, 100, zero, {{4, 0}, {2, 25}, {3, 75}}, false);
    assert(std::abs(unanimous.at(4) - 100*zero.future) < 1e-10);
    assert(std::abs(unanimous.at(2)/unanimous.at(3) - 25.5/75.5) < 1e-10);
}

void checkVictorianDeclarationMedian()
{
    namespace Geo = LiveBoothGeography;
    namespace Fed = LiveFederalDeclarations;
    using enum LiveData::VoteType;
    using Booth = LiveData::BoothType;
    auto path = Fed::inputPath(".", "2026vic").value();
    auto prior = Fed::readPrior(path,"2026vic");
    assert(prior.size() == 3 && prior.at(OtherEarly) == 0);
    assert(std::abs(prior.at(Absent) - .295566590) < 1e-9);
    assert(std::abs(prior.at(Postal) - .220832223/2) < 1e-9);
    assert(Fed::homeInPerson("2026vic",EvcEarly,Booth::Other,"EVC early"));
    assert(!Fed::homeInPerson("2026vic",MobileEarly,Booth::Other,"Mobile early"));
    assert(!Fed::homeInPerson("2026vic",OtherEarly,Booth::Other,"Other early"));
    Geo::ModeShifts shifts;
    for (auto [type,shift] : prior) shifts.emplace(LiveData::voteTypeName(type),shift);

    // Use both a new and a returning candidacy. Where home early and ordinary
    // differed last time their shares may differ again. The federal comparator
    // is their combined formal vote, including a remote ordinary booth.
    for (bool returning : {false,true}) for (double median : {.08,.20,.41}) {
        std::vector<Geo::Place> units{{"ordinary",1100,-1.1},{"ordinary",700,-2.4},{"ordinary",100,{}},
            {"EVC early",2450,{}},{"Mobile early",110,{}},{"Other early",650,{}},
            {"Absent",420,{}},{"Postal",800,{}},{"Provisional",45,{}}};
        if (returning) {
            units[0].previousOn = 40; units[0].previousFormal = 1400;
            units[3].previousOn = 210; units[3].previousFormal = 2000;
            units[6].previousOn = 80; units[6].previousFormal = 100;
        }
        Geo::District district;
        auto refs = Geo::prepareDistrict(units,std::vector<Geo::Return>(units.size()),median,district,shifts);
        assert(!district.ordinaryMovement && refs[0].share > refs[1].share);
        double ballots = 0, formal = 0, homeVotes = 0, homeFormal = 0;
        for (size_t j = 0; j < units.size(); ++j) {
            ballots += refs[j].share * units[j].size; formal += units[j].size;
            if (j < 4) { homeVotes += refs[j].share * units[j].size; homeFormal += units[j].size; }
        }
        double homeShare = homeVotes/homeFormal, homeOdds = LivePartialCount::logOdds(homeShare);
        assert(std::abs(ballots/formal - median) < 1e-12);
        assert(std::abs(refs[5].share - homeShare) < 1e-12);
        assert(std::abs(LivePartialCount::logOdds(refs[6].share) - homeOdds - prior.at(Absent)) < 1e-11);
        assert(std::abs(LivePartialCount::logOdds(refs[7].share) - homeOdds - prior.at(Postal)) < 1e-11);
        if (returning) assert(refs[3].share > refs[0].modeShare);
    }
    // A postal batch can arrive before home ballots. The frozen postal
    // share already includes its half federal weight. A later home count
    // giving the same expectation must change neither the blend nor its base.
    Fed::HomeVote home{1999.5,9999,1000};
    auto postal = Fed::estimate(4,home,prior.at(Postal),0,0,1000,.2,Postal).value();
    auto first = Fed::estimateFromContext(4,postal.context,50,200,1000,.2,Postal).value();
    auto later = Fed::estimate(4,home,prior.at(Postal),50,200,1000,.2,Postal).value();
    assert(std::abs(later.future - first.future) < 1e-12);
    assert(std::abs(first.future - (.8*postal.context + .2*50.5/201.)) < 1e-12);
    auto residual = Fed::estimate(4,home,0,90,200,1000,.2,OtherEarly).value();
    auto weight = 200./(200 + 4*800 + 300);
    assert(std::abs(residual.future - ((1-weight)*.2 + weight*90.5/201.)) < 1e-12);
}
void checkVictorianNamedEarlyCentres() {
    namespace V = LiveVecCentreGeography;
    std::vector<V::Centre> sites{
        {10,30,std::array{100,80,40},std::array{604,396}},
        {20,60,std::array{50,140,10},std::array{350,650}},
        {30,10,{},{}}};
    Shares district{{0,.33},{1,.25},{2,.1},{3,.1},{4,.22}};
    LivePartialCount::Flows flows{{0,1},{1,0},{2,.18},{3,.84},{4,.4}};
    auto factors=V::primaryFactors(sites,district,2,3);
    std::vector<Shares> local;
    for (auto const& f : factors) local.push_back(V::allocate(district,f,2,3));
    assert(local[0].at(0)>local[1].at(0) && local[0].at(3)>local[1].at(3));
    assert(local[2]==district);
    for (auto const& [id,share] : district) {
        double sum=0;
        for (size_t i=0; i<sites.size(); ++i) {
            assert(local[i].at(4)==district.at(4));
            assert(std::abs(local[i].at(1)/local[i].at(2)-2.5)<1e-12);
            sum+=sites[i].volume*local[i].at(id);
        }
        assert(std::abs(sum/100-share)<1e-8);
    }
    auto offsets=V::preferenceOffsets(sites,local,flows,.1);
    assert(offsets.back()==.1);
    double totalTpp=0;
    std::vector<double> tpp;
    for (size_t i=0; i<sites.size(); ++i) {
        tpp.push_back(firstShare(local[i],flows,offsets[i]));
        assert(tpp.back()>=local[i].at(0) && tpp.back()<=1-local[i].at(1));
        totalTpp+=sites[i].volume*tpp.back();
    }
    assert(tpp[0]>tpp[1]);
    assert(std::abs(totalTpp/100-firstShare(district,flows,.1))<1e-8);
    Shares expected{{0,100},{1,20},{2,10},{3,10},{4,10}};
    assert(V::fromOtherCentres(district,expected,expected,500)==district);
    auto measured=expected;
    measured[0]+=20; measured[1]-=20;
    assert(V::fromOtherCentres(district,measured,expected,500).at(0)>district.at(0));
}
// The comparison-year ballot IDs need not match the present ballot. Two
// matched sites can move differently, while a new site uses only the common
// adjustment. All three retain their own turnout after setting aside ON.
void checkOlderNarracanOrdinaryPrimaries()
{
    namespace H = LiveNarracanPrimaries;
    auto roster = nlohmann::json::parse(R"({
        "candidates":{"8":{"party":"AUSTRALIAN GREENS"},"2":{"party":"LIBERAL"},
                      "4":{"party":"AUSTRALIAN LABOR PARTY"}},
        "booths":{"East":{"fp":{"2":45,"4":45,"8":10}},
                  "West":{"fp":{"2":75,"4":23,"8":2}}}
    })");
    auto history = H::readHistory(roster);
    std::vector<H::Venue> sites{
        {{2.,12.,66.},history.at("East")}, {{3.,18.,99.},history.at("West")},
        {{1.,6.,33.},std::nullopt}
    };
    H::Mix targets{35.,15.,190.};
    auto fit = H::fit(sites,targets);
    assert(fit && fit->local.back()==fit->common);
    H::Mix total(3);
    std::vector<H::Mix> shares;
    for (size_t i=0;i<sites.size();++i) {
        auto local = LiveOrdinaryUpdate::tilted(sites[i].opening,fit->local[i]);
        assert(std::abs(LiveOrdinaryUpdate::sum(local)-1.)<1e-12);
        for (size_t j=0;j<3;++j) total[j]+=LiveOrdinaryUpdate::sum(sites[i].opening)*local[j];
        shares.push_back(std::move(local));
    }
    for (size_t j=0;j<3;++j) assert(std::abs(total[j]-targets[j])<1e-5);
    assert(shares[0][0]>shares[1][0] && shares[0][1]>shares[1][1]);
    for (auto& site : sites) site.history.reset();
    assert(!H::fit(sites,targets));
}
} // namespace

int main()
{
    checkPartialBatchCannotTrainItsOwnBackground();
    checkDenominatorsAndCompletedBooths();
    checkMissingCandidateFallbackUsesTheSameSources();
    checkSharedOthersAtFirstDistrictReturn();
    checkCompositionClassificationAndNoCountIdentity();
    checkFirstDeclarationBatchUncertainty();
    checkFederalReferenceAcrossDistrictLevels();
    checkOutstandingVoteUsesReportingLocations();
    checkEveryCandidateAndBoothTotalAtZeroCount();
    checkSaFederalDeclarationPrior();
    checkVictorianDeclarationMedian();
    checkVictorianNamedEarlyCentres();
    checkOlderNarracanOrdinaryPrimaries();
    std::cout << "Live FP background tests passed\n";
}
