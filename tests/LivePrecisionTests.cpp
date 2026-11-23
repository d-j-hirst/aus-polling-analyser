// Numerical identities at the live boundary: retain exact unchanged inputs,
// but keep genuine small updates and enforce received-count constraints.
#include "../General.h"
#include "../LiveFpIntegration.h"
#include "../LiveNewVotes.h"
#include "../SimulationAudit.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <type_traits>

void testProjectedVoteShareEndpoints()
{
    // A historical one-vote category can be 0% or 100%. Test the full numeric
    // route from that observed percentage through the log-odds transformation
    // and back to expected counts, including fractional turnout targets which
    // previously produced tiny negative remainders. The production projection
    // uses this same percentage-to-count conversion.
    for (double target : {0.,.1,.7,1.234,100.1,35000.123}) {
        for (double percent : {0.,1e-10,25.,50.,75.,100.-1e-10,100.}) {
            double transformed = transformVoteShare(percent);
            double votes = votesFromPercentage(detransformVoteShare(transformed),target);
            assert(std::isfinite(votes));
            assert(votes >= 0 && votes <= target);
            assert(target-votes >= 0);
            if (percent == 0) assert(votes == 0);
            if (percent == 100) assert(votes == target);
        }
    }
    // Preserve small genuine changes rather than replacing all near-endpoint
    // shares with endpoints, and leave invalid input for the caller to reject.
    assert(votesFromPercentage(100.-1e-10,100.) < 100.);
    assert(votesFromPercentage(1e-10,100.) > 0);
    assert(votesFromPercentage(100.+1e-10,100.) > 100.);
    assert(votesFromPercentage(-1e-10,100.) < 0);
    assert(std::isnan(votesFromPercentage(std::numeric_limits<double>::quiet_NaN(),100.)));
}

void testOrdinaryFpBlend()
{
    using namespace LiveFpIntegration;
    // Both ordinary booths were expected to be 30% after aligning the 10%
    // blind venue estimates to the candidate's district prior. Agreement of
    // signs is not required. Equal sized independent locations earn breadth.
    std::vector<BoothReference> booths{
        {400,.1,400,180.,true},{400,.1,400,100.,true},{9200,.1,0,std::nullopt,false}};
    auto b = compareBooths(booths,.3,.34);
    double const surprise = std::log((280.5/520.5)/(240.5/560.5));
    assert(std::abs(b.localMovement-surprise) < 1e-10);
    assert(std::abs(b.priorRemainingCentre-logOdds(.3)) < 1e-10);
    assert(std::abs(breadthResponse(b,.2)-.36) < 1e-12);
    auto unequal = b;
    unequal.largestMatched = .9 * b.matched;
    assert(std::abs(breadthResponse(unequal,.2)-.232) < 1e-12);
    assert(breadthResponse(unequal,1) == 1);
    double const centre = shareFromLogOdds(logOdds(.3)+.36*surprise);
    assert(std::abs(ordinaryRemainingShare(b,.3,.34,.2)-centre) < 1e-10);
    // The breadth bonus moves the centre. Reusing the same input draws
    // retains both sources of uncertainty at their original precision weights.
    assert(std::abs(logOdds(ordinaryRemainingShare(b,.4,.38,.2))-logOdds(centre)
        -.8*(logOdds(.4)-logOdds(.3))-.2*(logOdds(.38)-logOdds(.34))) < 1e-10);
    booths[1].matched = false;
    b = compareBooths(booths,.3,.34);
    assert(breadthResponse(b,.2) == .2);
    assert(std::abs(ordinaryRemainingShare(b,.3,.34,.2)
        - .5*(.34+shareFromLogOdds(logOdds(.3)+.2*surprise))) < 1e-10);
    // In a mixed return both centres use the same deviations about the prior
    // and booth forecasts. The ballot weights then combine their vote shares.
    double const deviation = .8*(logOdds(.4)-logOdds(.3)) + .2*(logOdds(.47)-logOdds(.34));
    double const variedBooth = shareFromLogOdds(logOdds(.34)+deviation);
    double const variedDirect = shareFromLogOdds(logOdds(.3)+.2*surprise+deviation);
    assert(std::abs(ordinaryRemainingShare(b,.4,.47,.2) - .5*(variedBooth+variedDirect)) < 1e-10);
    booths[0].matched = false;
    auto unmatched = compareBooths(booths,.3,.34);
    assert(std::abs(ordinaryRemainingShare(unmatched,.4,.47,.2)-variedBooth) < 1e-10);
    for (double w : {0.,.004,.2,.9,1.})
        assert(std::abs(ordinaryRemainingShare(unmatched,.3,.34,w)-.34) < 1e-12);
    assert(std::abs(ordinaryRemainingShare(unmatched,.3,.80,0)-.34) < 1e-12);
    assert(ordinaryRemainingShare(unmatched,.3,.80,1) == .80);
    auto noPrior = compareBooths(booths,std::nullopt,.34);
    assert(ordinaryRemainingShare(noPrior,0,.47,.2) == .47);
    for (double endpoint : {0.,1.})
        assert(ordinaryRemainingShare(compareBooths(booths,.3,endpoint),.4,endpoint,.2) == endpoint);
    booths[0].ordinaryCandidateVotes = 0;
    booths[1].ordinaryCandidateVotes.reset();
    booths[0].matched = true;
    auto zero = compareBooths(booths,.3,.34);
    assert(zero.ordinary == 400 && zero.matched == 400 && std::isfinite(zero.localMovement) && zero.localMovement < 0);

    // Exercise the simulator entry point with a partially matched candidate,
    // a differently matched opponent and a distinct turnout draw. Only the
    // remaining ballots can be rebalanced across candidates.
    LiveData::FpIntegrationInformation info;
    info.evidenceGate = 1;
    info.reportedFormal = 800;
    info.baseline.shareMean = {{0,.3},{1,.7}};
    info.baseline.logOddsMean = {{0,logOdds(.3)},{1,logOdds(.7)}};
    info.baseline.logOddsVariance = {{0,.01},{1,.01}};
    info.count = info.baseline;
    info.count.logOddsVariance = {{0,.04},{1,.04}};
    auto opponent = compareBooths({{800,.7,800,520.,false}},.7,.66);
    info.boothComparisons = {{0,b},{1,opponent}};
    LiveTurnoutMath::PartyAccount account{{{0,280},{1,520}},{{0,3680},{1,7120}}};
    auto identity = [](int id) { return id; };
    auto draws = integrate(account,{{0,30},{1,70}},info,identity,identity);
    double a = ordinaryRemainingShare(b,.3,.34,.2);
    double expected = 100*(280+10000*a/(a+.66))/10800;
    assert(std::abs(draws.at(0)-expected) < 1e-9);
    assert(std::abs(draws.at(0)+draws.at(1)-100) < 1e-10);
    // The emerging IND alias must follow the same candidate. If the slot
    // instead joins anonymous Others, that group's own update still applies.
    auto alias = [](int id) { return id == 0 ? -2 : id; };
    auto relabelled = LiveTurnoutMath::partitionAccount({&account.counted,&account.projected},alias);
    auto namedAlias = integrate(relabelled,{{-2,30},{1,70}},info,alias,alias);
    assert(std::abs(namedAlias.at(-2)-draws.at(0)) < 1e-10);
    auto anonymous = [](int id) { return id == 0 ? -1 : id; };
    auto pooled = LiveTurnoutMath::partitionAccount({&account.counted,&account.projected},anonymous);
    auto pooledResult = integrate(pooled,{{-1,30},{1,70}},info,anonymous,anonymous);
    auto withoutNamed = info;
    withoutNamed.boothComparisons.erase(0);
    assert(pooledResult == integrate(pooled,{{-1,30},{1,70}},withoutNamed,anonymous,anonymous));
    // Grow the declaration account while holding the ordinary comparison
    // fixed. Only the ordinary fraction gets its alternative future share.
    auto mixed = allocateOutstanding(account,{{0,30},{1,70}},{{0,{.70,.4}}},10800,800);
    double oldFuture = (.30*10800-280)/10000;
    double mixedFuture = .4*.70 + .6*oldFuture;
    assert(std::abs(mixed.at(0)-100*(280+10000*mixedFuture/(mixedFuture+1-oldFuture))/10800) < 1e-9);
    auto finished = account;
    finished.projected = finished.counted;
    assert(integrate(finished,{{0,30},{1,70}},info,identity,identity).at(0) == 35);
}

void testOrdinaryIndependentPrior()
{
    using namespace LiveFpIntegration;
    constexpr int independent = 3, emerging = -2, others = -1;
    LiveData::FpIntegrationInformation info;
    info.baseline.shareMean = {{0,.5},{others,.12},{emerging,.004}};
    info.partiesFromOthers = {5};
    info.othersPoolSize = 4;
    auto candidatePriors = [&] { return ordinaryCandidatePriors(info,others,independent); };
    // A name on the ballot, or the existence of an emerging IND distribution,
    // does not give this candidate a prior. A named forecast for someone else
    // cannot do so either. Ordinary parties keep their usual prior sources.
    for (bool nameMatches : {false,true}) {
        info.namedIndependentMatches = nameMatches;
        auto priors = candidatePriors();
        assert(!priors.contains(independent));
        assert(priors.at(0) == .5 && priors.at(5) == .03);
    }
    info.baseline.shareMean[independent] = .35;
    info.namedIndependentMatches = false;
    assert(!candidatePriors().contains(independent));
    info.namedIndependentMatches = true;
    assert(candidatePriors().at(independent) == .35);
}

int main()
{
    testProjectedVoteShareEndpoints();
    testOrdinaryFpBlend();
    testOrdinaryIndependentPrior();
    // The live ballot identifies two parties that pre-election draws included
    // in a four-candidate Others pool. A separate independent prior still
    // belongs to that candidate, including while both use a pooled account.
    std::map<int,double> district{{0,90},{-1,10}};
    LiveFpIntegration::assignMissingPartyPriors(district,-1,{3,4},4,2);
    assert((district == std::map<int,double>{{0,90},{3,2},{4,2},{-1,6}}));
    std::map<int,double> reversed{{0,90},{-1,10}};
    LiveFpIntegration::assignMissingPartyPriors(reversed,-1,{4,3},4,2);
    assert(reversed == district);
    std::map<int,double> named{{3,5},{-1,6}};
    LiveFpIntegration::assignMissingPartyPriors(named,-1,{3,4},3,0);
    assert(named.at(3) == 5 && named.at(4) == 2 && named.at(-1) == 4);
    std::map<int,double> emptyPool{{0,100}};
    LiveFpIntegration::assignMissingPartyPriors(emptyPool,-1,{3},3,0);
    assert(emptyPool.size() == 1);
    // A Flinders-style first return gives the separately recognised party
    // zero ballots, while the booth model still expects some votes elsewhere.
    // Its seat estimate should sit between the prior and the booth estimate.
    LiveFpIntegration::MomentAccumulator baselineDraws, countDraws;
    baselineDraws.add({{0,94},{-1,6}});
    baselineDraws.add({{0,90},{-1,10}});
    countDraws.add({{0,92},{-1,7},{3,1}});
    countDraws.add({{0,92},{-1,7.8},{3,.2}});
    LiveData::FpIntegrationInformation firstBooths;
    firstBooths.baseline = baselineDraws.finish();
    firstBooths.count = countDraws.finish();
    firstBooths.evidenceGate = .2;
    LiveTurnoutMath::PartyAccount firstCount{
        {{0,90},{-1,10},{3,0}}, {{0,9200},{-1,750},{3,50}}};
    std::map<int,double> prior{{0,92},{-1,8}};
    LiveFpIntegration::assignMissingPartyPriors(prior,-1,{3},4,0);
    auto unchangedId = [](int id) { return id; };
    auto estimate = LiveFpIntegration::integrate(firstCount,prior,firstBooths,unchangedId,unchangedId);
    assert(estimate.at(3) > .5 && estimate.at(3) < 2);
    assert(std::abs(estimate.at(0) + estimate.at(-1) + estimate.at(3) - 100) < 1e-10);
    firstCount.counted = firstCount.projected;
    estimate = LiveFpIntegration::integrate(firstCount,prior,firstBooths,unchangedId,unchangedId);
    assert(estimate.at(3) == .5);
    // A 1,000-vote district has 50 counted Others, including 30 for the
    // second independent. The projection gives them 20 of 50 outstanding
    // Others. A draw that assigns 8% to the group gives them 3% fixed and
    // 40% of the remaining 3% in that draw only after subtracting all 5% fixed.
    assert(std::abs(LiveFpIntegration::splitCountedCandidate(8,1000,50,100,30,50) - 4.2) < 1e-10);
    assert(LiveFpIntegration::splitCountedCandidate(5,1000,50,50,30,30) == 3);
    assert(LiveFpIntegration::splitCountedCandidate(8,1000,50,100,0,50) == 0);

    // Both independents draw from one outstanding pool, regardless of
    // which raw candidate occupies the regular IND slot.
    constexpr int Independent = 6, OtherIndependent = 140002, OtherCandidate = 20;
    std::map<int,double> counted{{0,100},{Independent,30},{OtherIndependent,20},{OtherCandidate,50}};
    std::map<int,double> projected{{0,700},{Independent,110},{OtherIndependent,80},{OtherCandidate,110}};
    LiveData::VoteCountAccount pooledVotes{&counted,&projected};
    auto inPool = [](int id) { return id != 0; };
    double const poolShare = 25;
    auto pooledShareFor = [&](int id) { return LiveFpIntegration::splitCountedCandidate(pooledVotes,poolShare,id,inPool); };
    double const first = pooledShareFor(Independent), second = pooledShareFor(OtherIndependent);
    assert(std::abs(first-9) < 1e-10 && std::abs(second-6.5) < 1e-10);
    assert(std::abs(first+second+pooledShareFor(OtherCandidate)-poolShare) < 1e-10);
    std::swap(counted.at(Independent),counted.at(OtherIndependent));
    std::swap(projected.at(Independent),projected.at(OtherIndependent));
    assert(pooledShareFor(Independent) == second && pooledShareFor(OtherIndependent) == first);
    counted = projected;
    assert(LiveFpIntegration::splitCountedCandidate(pooledVotes,30,Independent,inPool) == 8);
    assert(LiveFpIntegration::splitCountedCandidate(pooledVotes,30,OtherIndependent,inPool) == 11);
    static_assert(std::is_same_v<decltype(LiveTurnoutMath::PartyAccount{}.projected)::mapped_type, double>);
    static_assert(std::is_same_v<decltype(getAt(std::map<int,double>{},0,0.f)), double>);
    std::map<int,double> precise{{0,50.000000000001}};
    assert(getAt(precise,0,0.f) == precise.at(0));
    assert(getAt(precise,1,0.f) == 0);

    LiveTurnoutMath::PartyAccount account{{{0,10},{1,20}},{{0,500},{1,500}}};
    for (int step = 0; step <= 10000; ++step) {
        double share = 10 + step*.008;
        std::map<int,double> target{{0,share},{1,100-share}};
        // This is not a zero-count special case: received votes are present,
        // but this allocation already respects their lower bounds.
        assert(LiveFpIntegration::constrainToCounted(account,target) == target);
        auto shifted = target;
        shifted[0] += 1e-10;
        shifted[1] -= 1e-10;
        auto result = LiveFpIntegration::constrainToCounted(account,shifted);
        assert(result.at(0) > target.at(0));
    }
    auto bounded = LiveFpIntegration::constrainToCounted(account,{{0,.1},{1,99.9}});
    assert(bounded.at(0) == 1);
    assert(bounded.at(1) == 99);
    account.counted = account.projected;
    assert((LiveFpIntegration::constrainToCounted(account,{{0,90},{1,10}})
        == std::map<int,double>{{0,50},{1,50}}));

    LiveData::FpIntegrationInformation unavailableWidths;
    unavailableWidths.evidenceGate = .1;
    LiveTurnoutMath::PartyAccount noLocalWeight{{{0,1},{1,1},{2,0}},{{0,400},{1,600},{2,0}}};
    std::map<int,double> unchangedPrior{{0,32.123456789123},{1,67.876543210877},{2,0}};
    auto identity = [](int candidate) { return candidate; };
    assert(LiveFpIntegration::integrate(noLocalWeight,unchangedPrior,unavailableWidths,identity,identity) == unchangedPrior);

    LiveNewVotes::Shares context{{0,.3},{1,.7},{2,0}};
    auto neutral = LiveNewVotes::composition({},context,1000,LiveNewVotes::Category::General);
    assert(neutral == context); // Includes the genuine zero, not a smoothed floor.
    for (auto const& invalid : {LiveNewVotes::Shares{}, LiveNewVotes::Shares{{0,.2},{1,.3}},
        LiveNewVotes::Shares{{0,std::numeric_limits<double>::quiet_NaN()}}}) {
        bool rejected = false;
        try { LiveNewVotes::composition({},invalid,1000,LiveNewVotes::Category::General); }
        catch (std::runtime_error const&) { rejected = true; }
        assert(rejected);
    }
    for (double change : {-1e-10,1e-10}) {
        auto varied = LiveNewVotes::composition({},context,1000,
            LiveNewVotes::Category::General,{},{{0,change}});
        assert((varied.at(0)-context.at(0))*change > 0);
        assert(varied.at(2) == 0);
    }
    auto observed = LiveNewVotes::composition({{0,1},{1,0},{2,0}},context,
        1000,LiveNewVotes::Category::General);
    assert(observed != context);
    assert(observed.at(2) > 0); // An observed zero has ordinary small-sample smoothing.

    // The blind reference and a declaration record containing only zero counts
    // take the same composition route. A separate extra normalisation on one
    // side would manufacture offsets even with double-precision arithmetic.
    for (int step = 1; step <= 10000; ++step) {
        LiveNewVotes::Shares counts{{0,1234.567+step*.01},{1,7654.321},{2,0}};
        auto fractions = LiveNewVotes::normalize(counts);
        auto reference = LiveNewVotes::project({},fractions,10000.123);
        LivePartialCount::Counts zeros{{0,0},{1,0},{2,0}};
        auto current = LiveNewVotes::project(zeros,LiveNewVotes::composition(
            zeros,fractions,10000.123,LiveNewVotes::Category::General),10000.123);
        assert(reference == current);
    }

    // The opt-in trace round-trips every double, including changes well below
    // display precision. Ordinary report formats need not change to expose it.
    double const next = std::nextafter(50.0,100.0);
    auto encoded = nlohmann::json({{"value",next}}).dump();
    assert(nlohmann::json::parse(encoded).at("value").get<double>() == next);
    std::cout << "Live precision tests passed\n";
}
