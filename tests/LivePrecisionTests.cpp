// Numerical identities at the live boundary: retain exact unchanged inputs,
// but keep genuine small updates and enforce received-count constraints.
#include "../General.h"
#include "../LiveFpIntegration.h"
#include "../LiveFpTpp.h"
#include "../LiveOrdinaryUpdate.h"
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

void testJointHistoricalSupport()
{
    using namespace LiveFpSupportGradient;
    std::vector<Observation> returns{
        {100,1000,320,1000,.16},{250,1000,380,1000,.28},{400,1000,430,1000,.4},
        {550,1000,450,1000,.53},{700,1000,480,1000,.65},{850,1000,520,1000,.79}};
    auto fitResult = fit(returns).value();
    // Independent Python prototype on these six returns. A loss concentrated
    // in strongholds leaves the weakest and strongest future places with
    // adjustments of opposite sign. Beyond those places the effect is capped.
    assert(std::abs(fitResult.slope + 1.8349504219924582) < 1e-10);
    assert(fitResult.tilt(0,1000) == fitResult.tilt(100,1000) && fitResult.tilt(0,1000)>0);
    assert(fitResult.tilt(1000,1000) == fitResult.tilt(850,1000) && fitResult.tilt(1000,1000)<0);
    std::reverse(returns.begin(),returns.end());
    assert(std::abs(fit(returns)->slope-fitResult.slope)<1e-10);
    returns.resize(3);
    assert(!fit(returns));
    returns = {{100,1000,950,1000,.5},{500,1000,500,1000,.5},{500,1000,500,1000,.5},{500,1000,500,1000,.5}};
    assert(!fit(returns)); // One unusual booth provides all the historical contrast.

    Mix votes{40,30,20,10,0};
    adjust(votes,{-.7,.4,0,0,0});
    assert(std::abs(votes[0]-24.7377568870772)<1e-10 && std::abs(votes[1]-42.1253047447687)<1e-10);
    assert(votes[4]==0 && votes[2]==2*votes[3]);
    assert(std::abs(LiveOrdinaryUpdate::sum(votes)-100)<1e-10);
    Mix two{50,50};
    adjust(two,{-.6,.6});
    assert(std::abs(std::log(two[1]/two[0])-.6)<1e-10);

    // The district integration independently fits its ordinary swing. A trial
    // tilt on the counted location cannot change the result; a historical
    // match on its remaining postal ballots can. Neither changes turnout.
    using LiveOrdinaryUpdate::Venue;
    std::vector<Venue> booths{{{.5,.5},{50,50},100,true,{-1.,1.}},{{.5,.5},{0,0},300,false}};
    auto predict = [&] { return LiveOrdinaryUpdate::remaining(booths,{.5,.5},{1,1}).value(); };
    assert(std::abs(predict()[0]-.5)<1e-10);
    booths[1].historicalSupportTilts = {-.6,.6};
    assert(std::abs(predict()[1]-two[1]/100.)<1e-10);
}

void testSplitOrdinaryUpdate()
{
    using namespace LiveOrdinaryUpdate;
    std::vector<Venue> locations{
        {{.5,.5},{150,100},250,true}, {{.5,.5},{0,0},350,true},
        {{.5,.5},{0,0},3000,false}};
    auto predict = [&] { return remaining(locations,{.5,.5},{1,1}); };
    // All 250 votes are counted. For predicting other voters they contribute
    // 166 2/3 effective ballots against the two different opening estimates.
    auto first = predict().value();
    double const strong = (125+150./1.5)/(250+250./1.5);
    double const cautious = (500+150./1.5)/(1000+250./1.5);
    double const expected = (350*strong+3000*cautious)/3350;
    assert(std::abs(first[0]-expected)<1e-9 && std::abs(sum(first)-1)<1e-9);
    std::vector<Mix> venueShares;
    assert(remaining(locations,{.5,.5},{1,1},&venueShares) == first);
    assert(venueShares.size()==3 && venueShares.front().empty());
    assert(std::abs(venueShares[1][0]-strong)<1e-9 && std::abs(venueShares[2][0]-cautious)<1e-9);
    locations[2].turnout = 0;
    assert(std::abs(predict().value()[0]-strong)<1e-9);
    locations[2].turnout = 3000;
    locations[0].turnout = 500; // Only its remaining 250 receive the strong estimate.
    assert(std::abs(predict().value()[0]-(600*strong+3000*cautious)/3600)<1e-9);
    locations[0].turnout = 250;
    locations[1].turnout = 0;
    assert(std::abs(predict().value()[0]-cautious)<1e-9);
    locations[1].turnout = 350;
    locations[1].votes = {100,250};
    auto second = predict().value();
    assert(second[0]<.5);
    std::reverse(locations.begin(),locations.end());
    assert(std::abs(predict().value()[0]-second[0])<1e-10);
    locations.front().votes = {250,0}; // A postal return has its own evidence path.
    assert(std::abs(predict().value()[0]-second[0])<1e-10);
    locations[1].votes = {0,350};
    assert(predict().value()[0]<second[0]);
    for (auto& location : locations) location.ordinary = false;
    assert(!predict());
    assert(!remaining({{{1,0},{30,1},31,true},{{1,0},{0,0},100,false}},{1,0},{1,1}));

    // Cross-check the portable fit against the three-party Python prototype.
    // The venues lean differently and the second booth contradicts the first.
    std::vector<Venue> mixed{
        {{.35,.45,.20},{77,54,50},181,true}, {{.25,.60,.15},{26,60,39},125,true},
        {{.40,.39,.21},{0,0,0},10000,false}};
    Mix prior(3);
    for (auto const& v : mixed)
        for (size_t j=0; j<prior.size(); ++j) prior[j] += v.turnout*v.opening[j]/10306.;
    auto projected = remaining(mixed,prior,{1,1,1}).value();
    assert(std::abs(projected[0]-.40414846)<1e-7 && std::abs(projected[1]-.36430216)<1e-7);
    // Repeat with different opening shares and turnout for the outstanding
    // ordinary and non-ordinary venues. These are the Python split-fit shares.
    mixed[2].turnout = 7000;
    mixed[2].ordinary = true;
    mixed.push_back({{.30,.50,.20},{0,0,0},3000,false});
    prior.assign(3,0.);
    for (auto const& v : mixed)
        for (size_t j=0; j<prior.size(); ++j) prior[j] += v.turnout*v.opening[j]/10306.;
    projected = remaining(mixed,prior,{1,1,1}).value();
    assert(std::abs(projected[0]-.37692399)<1e-7 && std::abs(projected[1]-.37093084)<1e-7);
    assert(std::abs(sum(projected)-1)<1e-9);

    auto comparison = LiveFpIntegration::compareBooths({{250,.5,250,150.,true}},.5,.6);
    comparison.fittedRemaining = expected;
    for (double precision : {0.,.25,1.})
        assert(std::abs(LiveFpIntegration::ordinaryRemainingShare(comparison,.5,.6,precision)-expected)<1e-10);
}

void testTransferredArea()
{
    using namespace LiveSourceSeatTcp;
    std::vector<Venue> booths{
        {1000,600,600,600,"Old district",.5,1000,600},
        {1000,500,0,1000,"Old district",.5,0,0,true}};
    auto curve = prepare(booths).value();
    assert(curve.evidence.size()==1 && curve.evidence[0].in==1 && curve.evidence[0].pending==1);
    // A ten-point swing to Labor, weighted by .8 effective booths against
    // one prior, applies to half the electorate. Already counted votes stay fixed.
    assert(std::abs(curve.change(55)-5*.8/1.8)<1e-9);
    booths[1].lower = 250;
    booths[1].upper = 750;
    assert(std::abs(prepare(booths)->change(55)-.5*curve.change(55))<1e-9);
    booths[1].source = "Another district";
    assert(!prepare(booths));
    booths[1].source = booths[0].source;
    booths[1].counted = 1000;
    booths[1].countedFirst = 500;
    assert(!prepare(booths));

    using namespace LiveFpTpp;
    District account;
    account.sourceArea = curve;
    account.sourceArea->points.fill(30.);
    Shares primary{{0,40},{1,40},{2,20}}, rates{{2,.5}};
    auto result = project(account,primary,50.,primary,rates,1000,0,0).value();
    assert(std::abs(result.laborPercent-60)<1e-8);
    assert(std::abs(result.sources.at(2).first-200)<1e-6);
    account.covered = {{0,200},{1,200},{2,100}};
    result = project(account,primary,50.,primary,rates,1000,240,260).value();
    assert(std::abs(result.laborPercent-54)<1e-8 && std::abs(result.rates.at(2)-.7)<1e-8);
    result = project(account,primary,50.,primary,rates,1000,480,520).value();
    assert(result.laborPercent==48.);
}

void testAnonymousLiveFpAllocation()
{
    using Shares = std::map<int,double>;
    int const secondInd = LiveData::IndependentPartyIdOffset + 9;
    Shares counted, projected{{0,750},{3,60},{12,60},{15,50},{18,35},{secondInd,45}};
    LiveData::VoteCountAccount account{&counted,&projected};
    // The named IND already has its own share. The remaining parties and
    // independent share a 20% pool across a projected 1,000 formal votes.
    auto grouped = [](int id) { return id != 0 && id != 3; };
    Shares prior{{OthersIndex,2},{EmergingPartyIndex,12},{EmergingIndIndex,6}};
    auto split = [&]() {
        Shares result{{0,74},{3,6},{OthersIndex,20}};
        LiveFpIntegration::restoreAnonymousFpShares(result,prior,account,true,grouped);
        assert(std::abs(result.at(OthersIndex) + result.at(EmergingPartyIndex)
            + result.at(EmergingIndIndex) - 20.) < 1e-10);
        assert(result.at(3) == 6);
        return result;
    };
    auto early = split();
    assert(early.at(EmergingPartyIndex) == 12 && early.at(EmergingIndIndex) == 6);
    counted = {{0,500},{3,60},{12,40},{15,30},{18,20},{secondInd,10}};
    // 10 percentage points have identities. For those ballots the largest
    // eligible party has 4 points and the additional independent has one.
    // The remaining 10 points still use the 60% / 30% pre-count allocation.
    auto partial = split();
    assert(partial.at(EmergingPartyIndex) == 10 && partial.at(EmergingIndIndex) == 4);
    // A provider reporting 50 of the group's votes without a candidate
    // breakdown supplies no additional limit on any one ballot line.
    counted[OthersIndex] = 50;
    projected[OthersIndex] = 50;
    projected.at(0) -= 50;
    assert(split() == partial);
    projected.erase(OthersIndex);
    projected.at(0) += 50;
    counted = projected;
    // The projected group contains 190 identified votes. Use its exact share
    // now the count is complete; the best candidates have 60 and 45 votes.
    Shares complete{{0,75},{3,6},{OthersIndex,19}};
    LiveFpIntegration::restoreAnonymousFpShares(complete,prior,account,true,grouped);
    assert(complete.at(EmergingPartyIndex) == 6 && complete.at(EmergingIndIndex) == 4.5);
    assert(complete.at(OthersIndex) == 8.5);
    // In other seats the emerging IND slot carries the identified principal
    // independent. The separate six points must survive splitting Others.
    Shares proxy{{0,75},{EmergingIndIndex,6},{OthersIndex,19}};
    LiveFpIntegration::restoreAnonymousFpShares(proxy,prior,account,false,grouped);
    assert(proxy.at(EmergingIndIndex) == 6 && proxy.at(OthersIndex) == 13);
    // A genuine strong party can still support a large share even after all
    // ballots have been counted. Here it actually received 12% on one line.
    projected.at(12) += 60;
    projected.at(0) -= 60;
    counted = projected;
    Shares strongParty{{0,69},{3,6},{OthersIndex,25}};
    LiveFpIntegration::restoreAnonymousFpShares(strongParty,prior,account,true,grouped);
    assert(strongParty.at(EmergingPartyIndex) == 12);
}

int main()
{
    testAnonymousLiveFpAllocation();
    testJointHistoricalSupport();
    testSplitOrdinaryUpdate();
    testTransferredArea();
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
