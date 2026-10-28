#include "../LiveData.h"
#include "../LiveSimulationMath.h"
#include "../IndependentCandidacy.h"
#include "../CoalitionCandidacy.h"
#include "../LivePartyMapping.h"
#include "../LivePartialCount.h"
#include "../LiveNewVotes.h"
#include "../LiveFpEvidence.h"
#include "../LiveFpIntegration.h"
#include "../LiveEstimatedTpp.h"
#include "../LiveTppSensitivity.h"
#include "../LiveTurnoutMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

// Reused numeric party/candidate IDs must not transfer votes or affiliations
// between elections. These tiny source records exercise the same resolver and
// registered-party cache used when LiveV2 builds its FP/TCP accounts.
void testElectionScopedPartyMapping()
{
    using namespace LivePartyMapping;
    using Source = SourceElection;
    struct Candidate { int party; };
    struct Party { std::string shortCode; };
    struct Election {
        std::map<int, Candidate> candidates;
        std::map<int, Party> parties;
    };
    Election current{{{101,{4}}, {102,{8}}, {103,{12}}, {104,{11}}, {105,{-1}}, {106,{40}}},
        {{4,{"GRN"}}, {8,{"PHON"}}, {12,{"AJP"}}, {11,{"Libertarian"}}, {6,{"NATS"}}}};
    Election previous{{{101,{4}}, {202,{8}}, {203,{12}}, {204,{11}}, {205,{-1}}},
        {{4,{"NAT"}}, {8,{"LIB"}}, {12,{"ALP"}}, {11,{"GRN"}}, {40,{"ALP"}}}};
    CommissionIds ids;
    std::map<std::string, int> codes{{"ALP",0}, {"LIB",1}, {"GRN",2},
        {"PHON",3}, {"NAT",4}, {"NATS",4}};
    auto seed = [&](Election const& election, Source source) {
        for (auto const& [id, party] : election.parties)
            if (codes.contains(party.shortCode)) ids[{source,id}] = codes.at(party.shortCode);
    };
    seed(current,Source::Current);
    seed(previous,Source::Previous);
    auto map = [&](int candidateId, Source source) {
        auto party = candidateParty(candidateId,source,previous,current);
        assert(party);
        if (party->partyId == -1) return candidateId + 100000;
        return mapParty({party->source,party->partyId},party->abbreviation,7,100000,ids,codes);
    };
    assert(map(101,Source::Current) == 2);  // Current party 4 is Greens.
    assert(map(101,Source::Previous) == 4); // Historical party 4 is Nationals.
    assert(map(102,Source::Current) == 3);
    assert(map(202,Source::Previous) == 1);
    assert(map(203,Source::Previous) == 0);
    assert(map(204,Source::Previous) == 2);
    int const animalJustice = map(103,Source::Current);
    int const libertarian = map(104,Source::Current);
    assert(animalJustice >= 7 && libertarian >= 7 && animalJustice != libertarian);
    assert(map(105,Source::Current) == 100105);
    assert(map(205,Source::Previous) == 100205);

    // Candidate fallback must carry the source election with it, including for
    // unconfigured parties. A missing party record must not borrow the other
    // election's Labor record with the same numeric ID.
    assert(map(202,Source::Current) == 1);
    assert(map(103,Source::Previous) == animalJustice);
    auto missingParty = candidateParty(106,Source::Current,previous,current);
    assert(missingParty && !missingParty->hasPartyRecord && missingParty->abbreviation.empty());
    assert(map(106,Source::Current) >= 7);
    assert(!candidateParty(999,Source::Current,previous,current));
    assert(!candidateParty(999,Source::Previous,previous,current));

    // Uncached historical metadata must also come from the historical source.
    // Sharing a numeric ID with a current unconfigured party is not an alias.
    previous.candidates[206] = {12};
    previous.parties[12].shortCode = "OLD";
    ids.erase({Source::Previous,12});
    int const historicalMinor = map(206,Source::Previous);
    assert(historicalMinor >= 7 && historicalMinor != animalJustice);
    assert(codes.at("OLD") == historicalMinor);
    assert(mapParty({Source::Previous,99},"AJP",7,100000,ids,codes) == animalJustice);
    int const unnamedCurrent = mapParty({Source::Current,77},"",7,100000,ids,codes);
    int const unnamedPrevious = mapParty({Source::Previous,77},"",7,100000,ids,codes);
    assert(unnamedCurrent != unnamedPrevious);
    assert(!codes.contains(""));
}

void testJointTppIntegration()
{
    using namespace LiveTppIntegration;
    // Ten independent seat priors and a perfectly shared error in the count.
    // A local 50:50 blend would WIDEN the national SD (.316 -> .524).
    Moments prior{Vector(11,0),Matrix(11,Vector(11,0))};
    for (int i=0;i<11;++i) prior.covariance[i][i]=1;
    std::vector<Observation> rows;
    for (int i=0;i<10;++i) {
        Observation row{Vector(11,0),2,1e-8};
        row.h[i]=1; row.h[10]=1; rows.push_back(row);
    }
    auto joint=prepare(prior,rows);
    double nationalVariance=0;
    for (int i=0;i<10;++i) for (int j=0;j<10;++j)
        nationalVariance+=joint.posterior.covariance[i][j]/100;
    assert(std::abs(nationalVariance-1./11)<1e-8);
    assert(std::abs(joint.posterior.mean[0]-2./11)<1e-8);

    // T = total TPP, P = PPVC-minus-ordinary, D = declaration-minus-ordinary.
    // Ordinary counting constrains T without assuming P/D are already known.
    Moments category{{0,0,0},{{1.79*1.79,0,0},{0,25,0},{0,0,25}}};
    double previous=category.covariance[0][0];
    for (double sd : {20.,4.,2.,1.,.25}) {
        auto update=prepare(category,{{{1,-.31,-.24},-2,sd*sd}});
        double variance=update.posterior.covariance[0][0];
        assert(variance<previous); previous=variance;
        assert(update.posterior.mean[0]<0 && update.posterior.mean[0]>-2);
        // Identical cumulative inputs always start from the fixed prior.
        assert(prepare(category,{{{1,-.31,-.24},-2,sd*sd}}).posterior.mean==update.posterior.mean);
    }
    auto ordinary=prepare(category,{{{1,-.31,-.24},-2,.0625}});
    assert(std::abs(std::sqrt(ordinary.posterior.covariance[0][0])-1.327)<.002);
    auto more=prepare(category,{{{1,-.31,-.24},-2,.0625},{{1,.69,-.24},1,1}});
    assert(more.posterior.covariance[0][0]<ordinary.posterior.covariance[0][0]);
    assert(ordinary.posterior.covariance[1][1]>16); // Most PPVC uncertainty remains.
    auto noData=prepare(category,{});
    assert(condition(noData,{3,4,5},[](auto){assert(false);return 0.;})==Vector({3,4,5}));

    // Verify the actual draw transform, including all covariance entries, with
    // an exact finite orthogonal ensemble (no Monte Carlo tolerance/seed luck).
    Moments two{{2,-1,0},{{4,2,0},{2,4,0},{0,0,9}}};
    std::vector<Observation> observations{{{1,0,-.3},-3,4},{{0,1,-.2},2,1}};
    auto update=prepare(two,observations);
    Accumulator measured;
    for (int axis=0;axis<5;++axis) for (double sign : {-1.,1.}) {
        Vector z(5,0);z[axis]=sign*std::sqrt(5.);
        Vector draw{2+2*z[0],-1+z[0]+std::sqrt(3.)*z[1],3*z[2]};
        measured.add(condition(update,draw,[&](std::size_t i){return z[3+i];}));
    }
    auto actual=measured.finish();
    for (int i=0;i<3;++i) {
        assert(std::abs(actual.mean[i]-update.posterior.mean[i])<1e-10);
        for (int j=0;j<3;++j)
            assert(std::abs(actual.covariance[i][j]-update.posterior.covariance[i][j])<1e-10);
    }
    std::reverse(observations.begin(),observations.end());
    auto reversed=prepare(two,observations);
    for (int i=0;i<3;++i) {
        assert(std::abs(reversed.posterior.mean[i]-update.posterior.mean[i])<1e-10);
        for (int j=0;j<3;++j)
            assert(std::abs(reversed.posterior.covariance[i][j]-update.posterior.covariance[i][j])<1e-10);
    }

    CategoryEvidence empty, tiny, many, one, complete, inconsistent;
    empty.add(10000,0,1,0,0,10); assert(empty.weight==0 && empty.expected==10000);
    tiny.add(10000,80,1,3,1,10);
    assert(tiny.variance()>100); // One tiny location cannot override the prior.
    one.add(10000,5000,1,3,0,10);
    for(int i=0;i<10;++i) many.add(1000,500,1,3,0,10);
    assert(many.variance()<one.variance()/9);
    complete.add(10000,10000,1,3,0,10);assert(complete.variance()==.0001);
    inconsistent.add(5000,2000,1,-30,0,10);
    inconsistent.add(5000,2000,1,30,0,10);
    assert(inconsistent.variance()>one.variance());
    bool rejected=false;
    try {prepare(two,{{{1,0,0},0,-1}});} catch(std::logic_error const&){rejected=true;}
    assert(rejected);

    // FP ahead of preferences remains evidence: preserve the 100 actual TCP
    // votes and the retained cohort's allocation of the other 900 primaries.
    auto partial=receivedComparison(1000,100,40,0,540,false).value();
    assert(partial.votes==1000 && partial.inferredFraction==.9);
    assert(std::abs(partial.transformedShare-25*std::log(580.25/420.25))<1e-10);
    auto exhausted=receivedComparison(1000,800,400,0,0,true).value();
    assert(exhausted.votes==800 && exhausted.inferredFraction==0 && exhausted.transformedShare==0);
    auto fpOnly=receivedComparison(80,0,0,3,{},false).value();
    assert(fpOnly.votes==80 && fpOnly.inferredFraction==1 && fpOnly.transformedShare==3);
    assert(!receivedComparison(0,0,0,0,{},false));
    auto permittedExcess=receivedComparison(995,1000,500,0,{},false).value();
    assert(permittedExcess.votes==1000 && permittedExcess.inferredFraction==0);
    auto genuineZero=receivedComparison(100,100,0,{}, {},false).value();
    assert(std::isfinite(genuineZero.transformedShare) && genuineZero.transformedShare<0);
    assert(genuineZero.samplingVariance>2500);
    assert(receivedComparison(100,100,50,{}, {},false)->samplingVariance<25);
    LiveTurnoutMath::PartyAccount fixed;
    fixed.counted={{0,60},{1,40}};fixed.projected={{0,60},{1,40}};
    auto constrained=LiveFpIntegration::constrainToCounted(fixed,{{0,1},{1,99}});
    assert(constrained.at(0)==60 && constrained.at(1)==40);
}

static_assert(int(LiveData::VoteType::Invalid) == 0);
static_assert(int(LiveData::VoteType::TIO) == 12);
static_assert(int(LiveData::VoteType::MarkedAsVoted) == 13);
static_assert(int(LiveData::BoothType::Normal) == 0);
static_assert(int(LiveData::BoothType::Invalid) == 6);

void testTppCategorySensitivity()
{
    using B = LiveData::BoothType;
    using V = LiveData::VoteType;
    using LiveTppSensitivity::prepare;
    // Fictional centres have 10,000 votes awaiting TPP. Ordinary booths do
    // not inherit the PPVC error, and a zero-sized/closed centre adds nothing.
    std::vector<LiveTppSensitivity::Unit> units{
        {B::Ppvc,V::Ordinary,6000,false},
        {B::Ppvc,V::Ordinary,4000,false},
        {B::Normal,V::Ordinary,2000,false},
        {B::Ppvc,V::Ordinary,0,false},
        {B::Hospital,V::Ordinary,100,false},
        {B::Other,V::Postal,3000,false},
    };
    auto first = prepare(units,false);
    assert(first.boothTypes.at(B::Ppvc) == 10000);
    assert(first.boothTypes.at(B::Hospital) == 100);
    assert(first.boothTypes.size() == 2 && first.voteTypes.empty());
    // FP evidence refreshes the pair projections after initial preparation.
    // Rebuilding at that boundary must replace, not double, their exposure.
    auto refreshed = first;
    for (int pass = 0; pass < 3; ++pass) refreshed = prepare(units,false);
    assert(refreshed.boothTypes == first.boothTypes);
    assert(refreshed.voteTypes == first.voteTypes);
    double categorySd = 5, previousSeatVotes = 50000;
    assert(std::abs(categorySd*refreshed.boothTypes.at(B::Ppvc)/previousSeatVotes-1) < 1e-9);

    // Once direct classic TCP arrives, retain the existing direct-count path
    // even if only part of that centre has preferences. Replace stale weights
    // as well when the turnout target for another centre changes.
    units[0].hasObservedTpp = true;
    units[1].expectedVotes = 3500;
    refreshed = prepare(units,false);
    assert(refreshed.boothTypes.at(B::Ppvc) == 3500);
    units[1].hasObservedTpp = true;
    refreshed = prepare(units,false);
    assert(!refreshed.boothTypes.contains(B::Ppvc));
    assert(refreshed.boothTypes.at(B::Hospital) == 100);

    // Optional-preferential declaration categories retain their old routing.
    // A special booth type takes precedence, so one unit never counts twice.
    auto optional = prepare(units,true);
    assert(optional.voteTypes.at(V::Postal) == 3000);
    std::vector<LiveTppSensitivity::Unit> unusual{
        {B::Ppvc,V::PrePoll,500,false}, {B::Invalid,V::Invalid,700,false}};
    optional = prepare(unusual,true);
    assert(optional.boothTypes.at(B::Ppvc) == 500 && optional.voteTypes.empty());
    auto empty = prepare({},false);
    assert(empty.boothTypes.empty() && empty.voteTypes.empty());
}

void testRawTppSwing()
{
    LiveData::RawTppSwing raw;
    assert(!raw.value());
    // Fictional booths swing +10 and -10 points. Their current TCP sizes,
    // rather than the previous election's sizes or model estimates, weight
    // the result: (100*10 + 300*-10)/400 = -5 points.
    raw.add({{0,60},{1,40}},{{0,500},{1,500}},10);
    raw.add({{0,120},{10,180}},{{0,50},{10,50}},10);
    assert(std::abs(*raw.value()+5) < .00001);
    // Without a separately supplied FP comparison, non-classic pairings,
    // absent/zero records and missing comparisons supply no raw swing.
    raw.add({{0,90},{6,10}},{{0,50},{1,50}},10);
    raw.add({{0,90},{1,10}},{{0,50},{2,50}},10);
    raw.add({{0,0},{1,0}},{{0,50},{1,50}},10);
    raw.add({{0,90},{1,10}},{},10);
    assert(raw.countedVotes == 400);
    assert(std::abs(*raw.value()+5) < .00001);
    LiveData::RawTppSwing unanimous;
    unanimous.add({{0,0},{1,80}},{{0,40},{1,40}},-1);
    assert(*unanimous.value() == -50); // Genuine endpoints need no display smoothing.

    // Early booth updates normally contain FP before TCP. Their unblended
    // primary-derived TPP comparisons must be visible, with current FP weights.
    LiveData::RawTppSwing early;
    early.add({},{{0,500},{1,500}},10,100,60);
    early.add({{0,0},{1,0}},{{0,50},{1,50}},10,300,40);
    assert(early.countedVotes == 400 && *early.value() == -5);
    // Both historical and current non-classic pairings can use the existing
    // hypothetical Labor/Coalition shares derived from their measured FP.
    LiveData::RawTppSwing hypothetical;
    hypothetical.add({{6,70},{1,30}},{{0,20},{6,80}},10,200,55,52);
    assert(hypothetical.countedVotes == 200 && *hypothetical.value() == 3);
    // Direct TCP always takes precedence, without giving those votes a second
    // FP weight or replacing their recorded share with an inferred value.
    LiveData::RawTppSwing direct;
    direct.add({{0,45},{1,55}},{{0,50},{1,50}},10,300,80,70);
    assert(direct.countedVotes == 100 && *direct.value() == -5);
    LiveData::RawTppSwing historicalFp;
    historicalFp.add({{0,60},{1,40}},{},10,300,80,52);
    assert(historicalFp.countedVotes == 100 && *historicalFp.value() == 8);

    // A first 100-vote TCP batch must not replace a 10,000-vote FP account.
    // Reuse the forecast's pending-preference allocator, rather than putting
    // an arbitrary confidence weight on a whole-record TCP share.
    using namespace LivePartialCount;
    Counts fp{{0,4000},{1,4000},{2,2000}}, tinyTcp{{0,80},{1,20}};
    Flows flows{{0,1},{1,0},{2,.8}};
    double pending = pendingFirst(fp,tinyTcp,flows,0,{},0,1);
    LiveData::RawTppSwing partial;
    partial.add(tinyTcp,{{0,500},{1,500}},10,10000,80,{},pending);
    assert(partial.countedVotes == 10000);
    assert(std::abs(*partial.value()-6.24) < .00001); // 80 + 9,900*.56.
    // Other FP-only booths retain their own weight independently of that TCP.
    partial.add({},{{0,500},{1,500}},10,20000,50);
    assert(partial.countedVotes == 30000);
    assert(std::abs(*partial.value()-2.08) < .00001);
    // Retained matched cohorts use the same residual primary composition and
    // preference-flow evidence in this display as they do in the forecast.
    Counts cohortFp{{0,40},{1,40},{2,20}}, cohortTcp{{0,56},{1,44}};
    auto cohort = measure(cohortFp,cohortTcp,flows,0,1,true);
    auto allocation = pendingFirst(fp,cohortTcp,flows,0,cohort,0,1);
    LiveData::RawTppSwing matched;
    matched.add(cohortTcp,{{0,500},{1,500}},10,10000,56,{},allocation);
    assert(std::abs(*matched.value()-(56+allocation)/100+50) < .00001);
    // On completion the pending pool is empty; use recorded TCP exactly,
    // preserving a single 10,000-vote denominator and genuine FP/TCP differences.
    LiveData::RawTppSwing complete;
    complete.add({{0,5300},{1,4700}},{{0,50},{1,50}},10,10000,80,{},0);
    assert(complete.countedVotes == 10000 && *complete.value() == 3);
    LiveData::RawTppSwing smallExcess;
    smallExcess.add({{0,5301},{1,4700}},{{0,50},{1,50}},10,10000,80);
    assert(smallExcess.countedVotes == 10001);
    // No received votes or no historical comparison remains blank, rather
    // than presenting a baseline forecast as an observed result.
    LiveData::RawTppSwing absent;
    absent.add({},{{0,50},{1,50}},10,0,60);
    absent.add({},{{0,50},{1,50}},10,100);
    absent.add({},{},10,100,60);
    absent.add({},{{0,50},{1,50}},10,100,std::numeric_limits<double>::quiet_NaN());
    assert(!absent.value());
}

void testEstimatedTppSourceInfluence()
{
    using namespace LiveEstimatedTpp;
    auto evidenceFor = [](double fp, double tpp = 0, bool opv = false) {
        Evidence evidence;
        evidence.add({100000,fp,tpp,fp/100000,tpp/100000,1,true,opv});
        return evidence;
    };

    // Selected finalists with zero votes, including an ALP/NAT pair, contain
    // exactly the same evidence as an empty map. Non-classic TCP is never an
    // observed Labor/Coalition count; a genuine zero for one finalist is valid.
    for (std::map<int,int> tcp : {std::map<int,int>{},{{0,0},{1,0}},{{0,0},{10,0}},{{6,80},{1,120}}}) {
        Evidence none = evidenceFor(0,observedTppVotes(tcp,10));
        assert(none.influence() == 0);
        assert(evidenceFor(20000,observedTppVotes(tcp,10)).influence() == evidenceFor(20000).influence());
    }
    assert(observedTppVotes(std::map<int,int>{{0,0},{1,80}},10) == 80);
    assert(observedTppVotes(std::map<int,int>{{0,30},{10,50}},10) == 80);
    assert(observedTppVotes(std::map<int,int>{{1,30},{10,50}},10) == 0);

    // A first ordinary 80-vote booth in a 100,000-vote seat is not a full-strength
    // conversion estimate. Its completion/confidence is one locally, but every
    // unreported booth must remain in the seat denominator.
    Evidence tiny;
    tiny.add({80,80,0,1,0,1,true});
    tiny.add({99920,0,0,0,0,1,false});
    assert(std::abs(tiny.influence()-evidenceFor(80).influence()) < 1e-10);
    assert(tiny.influence() > 0 && tiny.influence() < .001);
    assert(evidenceFor(1).influence() < tiny.influence());
    assert(evidenceFor(1000).influence() > tiny.influence());
    assert(evidenceFor(20000).influence() > evidenceFor(1000).influence());

    // Partial declarations and FP ahead of preferences have the same source
    // distinction. Advancing actual classic TPP displaces inferred evidence
    // continuously; the first TCP vote cannot turn off the whole seat's error.
    double previous = evidenceFor(20000).influence();
    for (int tpp = 1; tpp <= 20000; ++tpp) {
        auto partial = evidenceFor(20000,tpp);
        assert(partial.influence() <= previous+1e-12);
        assert(previous-partial.influence() < .001);
        previous = partial.influence();
    }
    assert(previous == 0);
    auto midway = evidenceFor(20000,10000);
    assert(midway.inferredEvidenceVotes == 5000);
    assert(midway.observedEvidenceVotes == 10000);
    assert(midway.influence() > 0 && midway.influence() < evidenceFor(20000).influence());
    assert(evidenceFor(1000,1005).influence() == 0); // Permitted small TCP excess.
    assert(evidenceFor(0,1000).influence() == 0); // Direct TPP can arrive first.

    // Quality-weighted evidence survives aggregation without overwriting the
    // unreported denominator or letting a zero candidate declaration add weight.
    Evidence mixed;
    mixed.add({10000,10000,0,1,0,1,true});
    mixed.add({10000,10000,10000,1,1,1,true});
    mixed.add({80000,0,0,0,0,1,false});
    assert(mixed.expectedVotes == 100000);
    assert(mixed.inferredEvidenceVotes == 5000 && mixed.observedEvidenceVotes == 10000);
    assert(std::abs(mixed.inferredShare()-1./3) < 1e-12);
    Evidence discounted;
    discounted.add({100000,20000,0,.2,0,.1,true});
    assert(discounted.influence() < evidenceFor(20000).influence());
    Evidence noComparison;
    noComparison.add({100000,20000,0,.2,0,1,false});
    assert(noComparison.influence() == 0);
    Evidence closed;
    closed.add({0,0,0,0,0,1,false});
    assert(closed.expectedVotes == 0 && closed.influence() == 0);

    // Exhaustion is not pending preferences. The retained OPV path uses the
    // measured continuing-vote share when available and needs no extra FP gap.
    assert(evidenceFor(20000,18000,true).influence() == 0);
    assert(evidenceFor(20000,0,true).influence() > 0);
    assert(evidenceFor(100000).influence() > .99); // Complete FP still is not TPP.

    // Shared errors retain their common draw with seat-specific coefficients;
    // counted candidates and totals remain protected by the finite-pool handoff.
    LiveTurnoutMath::PartyAccount account{{{0,600},{1,400}},{{0,1200},{1,800}}};
    for (double error : {-20.,0.,20.}) {
        auto projected = LiveTurnoutMath::varyRemaining(account.projected,account.counted,
            {{0,error*midway.influence()}});
        assert(projected.at(0) >= 600 && projected.at(1) >= 400);
        assert(std::abs(projected.at(0)+projected.at(1)-2000) < .001);
    }
    auto fixed = LiveTurnoutMath::varyRemaining({{0,600},{1,400}},account.counted,{{0,20}});
    assert(fixed.at(0) == 600 && fixed.at(1) == 400);
}

void testHistoricalIndependentEligibility()
{
    using IndependentCandidacy::usePreviousResult;
    // A fictional defeated candidate won 24% last time. Some IND entries on
    // a configured ballot cannot preserve that campaign's prior, whether or
    // not a different named campaign has been marked prominent. This applies
    // before any sampled recontest or transfer to the emerging slot.
    assert(!usePreviousResult(true, false, false));
    // Explicitly returning candidates keep their historical distribution.
    assert(usePreviousResult(true, false, true));
    // Before the ballot is known, an unannounced return remains possible.
    assert(usePreviousResult(false, false, false));
    assert(usePreviousResult(false, false, true));
    // Incumbents continue through the separate retirement/recontest checks;
    // they do not need the unsuccessful-candidate flag.
    assert(usePreviousResult(true, true, false));
    assert(usePreviousResult(false, true, false));
}

void testKnownCoalitionBallot()
{
    using CoalitionCandidacy::candidateChance;
    // Unsettled nominations use the configured chances. Once the actual
    // ballot is known, retained estimates cannot remove or invent a candidate.
    std::map<std::string,float> chances{{"LNP",1.f},{"NAT",.2f}};
    assert(candidateChance({},chances,"NAT")==.2f);
    assert(candidateChance({"LNP","NAT"},chances,"NAT")==1);
    assert(candidateChance({"NAT"},chances,"LNP")==0);
    assert(candidateChance({"NAT"},chances,"NAT")==1);
    chances["LNP"]=0;
    assert(candidateChance({"LNP","NAT"},chances,"LNP")==1);
}

void testSharedFinalistTcpCoverage() {
    using LivePartialCount::SharedFinalistTcpEvidence;
    // Fictional district: a 200-vote booth has an old 300-vote comparison.
    // Another 79,800 votes belong to unreported or unmatched locations. A
    // complete first booth must not receive the whole-seat confidence cap.
    SharedFinalistTcpEvidence sparse;
    sparse.addBooth(200.0f, 2.0f, 300.0f);
    sparse.addBooth(40000.0f); // TCP not reported.
    sparse.addBooth(39800.0f); // No compatible historical pair.
    assert(sparse.available());
    assert(sparse.meanSwing() == 2.0f);
    assert(std::abs(sparse.confidence() - 0.006123724f) < 0.0000001f);
    assert(LiveSimulationMath::evidenceWeight(sparse.confidence(), 14.0f) < 0.04f);

    // Widespread observations strengthen the comparison normally. Unmatched
    // locations dilute its coverage without fabricating a zero measured swing.
    SharedFinalistTcpEvidence broad;
    broad.addBooth(30000.0f, 2.0f, 30000.0f);
    broad.addBooth(30000.0f, -4.0f, 30000.0f);
    broad.addBooth(20000.0f);
    assert(broad.meanSwing() == -1.0f);
    assert(std::abs(broad.confidence() - 0.08660254f) < 0.0000001f);
    assert(broad.confidence() > sparse.confidence());

    SharedFinalistTcpEvidence none;
    none.addBooth(80000.0f);
    none.addBooth(0.0f); // An unavailable service with no expected votes.
    assert(!none.available() && none.confidence() == 0.0f);
    SharedFinalistTcpEvidence complete;
    complete.addBooth(80000.0f, 0.0f, 90000.0f);
    assert(complete.available() && complete.meanSwing() == 0.0f);
    assert(complete.confidence() == SharedFinalistTcpEvidence::MaximumConfidence);
    SharedFinalistTcpEvidence empty;
    assert(!empty.available() && empty.confidence() == 0.0f);
}

void testHistoricalPreferenceReliability() {
    using namespace LivePartialCount;
    // Ordinary and legitimately strong low-90s flows keep their full weight.
    // A historical rate closer to unanimity loses only extrapolation weight;
    // the current measured preference cohort uses the usual calculation.
    for (double p : {.15,.5,.85,.90,.92}) assert(historicalFlowReliability(p)==1);
    assert(std::abs(historicalFlowReliability(.93)-25.0/27)<1e-12);
    assert(std::abs(historicalFlowReliability(.95)-.5)<1e-12);
    for (double p : {0.0,.01,.02,.98,.99,1.0}) assert(historicalFlowReliability(p)==0);
    double last = 1;
    for (int step = 0; step <= 600; ++step) {
        double p = .92+step*.0001;
        double reliability = historicalFlowReliability(p);
        assert(reliability>=0 && reliability<=last);
        assert(std::abs(reliability-historicalFlowReliability(1-p))<1e-12);
        last = reliability;
    }
    // The slope also vanishes at either join. Count revisions cannot create
    // a discontinuous switch just above 92% or just below 98%.
    assert(1-historicalFlowReliability(.920001)<1e-8);
    assert(historicalFlowReliability(.979999)<1e-8);

    // Fictional prior: a three-vote mismatch raises an already strong inferred
    // transfer from 97% to about 99.1%. The comparison is measurable but cannot
    // train extrapolation. No historical or current integer record is changed.
    Counts oldFp{{0,600},{1,300},{2,100}}, oldTcp{{0,697},{1,300}};
    auto old = measureTransfer(oldFp,oldTcp,0,1);
    assert(old && old->votes==100);
    assert(std::abs(old->received-(697000.0/997-600))<1e-12);
    assert(old->share()>.98 && historicalFlowReliability(old->share())==0);
    assert(!measureTransfer(oldFp,{},0,1));
    assert(!measureTransfer(oldFp,{{0,650},{1,300}},0,1)); // Beyond total-comparability allowance.
    assert(!measureTransfer({{0,600},{1,400}},{{0,600},{1,400}},0,1)); // No transferable pool.
    assert(!measureTransfer(oldFp,{{0,850},{1,150}},0,1)); // Gain exceeds available preferences.

    Flows flows{{0,1},{1,0},{2,.8},{3,.5}};
    Counts currentFp{{0,400},{1,400},{2,100},{3,100}}, currentTcp{{0,535},{1,465}};
    auto current = measure(currentFp,currentTcp,flows,0,1,true);
    assert(current && current->matchingCurrentTcp && current->preferenceVotes==200);
    double expectedOffset=logOdds(135.5/201)-logOdds(.65);
    assert(std::abs(current->offset-expectedOffset)<1e-12);
    // Even when the old comparison has zero reliability, retain the current
    // observation, its 500-vote shrinkage and the fully counted pair exactly.
    assert(std::abs(evidenceOffset(0,current,0,1)-200.0/700*expectedOffset)<1e-12);
    assert(pendingFirst(currentFp,currentTcp,flows,0,current,0,1)==0);
    assert(currentTcp.at(0)==535 && currentTcp.at(1)==465);
    auto oneWay = measure({{0,40},{1,40},{2,1}},{{0,41},{1,40}},flows,0,1,true);
    assert(oneWay && std::isfinite(oneWay->offset) && oneWay->preferenceVotes==1);
}

void testUnmatchedBoothFpProjection() {
    using namespace LivePartialCount;
    // A Baringa-sized FP-only booth. These illustrative flows give 3201 Labor
    // TPP votes; even a very low surrounding forecast cannot remove its 2555
    // counted Labor primaries or override the estimate when all FP is received.
    Counts fp{{0,2555},{1,1998},{2,1067}};
    Flows flows{{0,1},{1,0},{2,646.0/1067}};
    double n=total(fp);
    for (double context : {.01,.422464, .7,.99}) {
        assert(std::abs(projectFpOnly(fp,flows,0,n,context)-3201) < 1e-9);
        // Only 100 genuinely unreceived voters inherit the surrounding share.
        assert(std::abs(projectFpOnly(fp,flows,0,n+100,context)-3201-100*context) < 1e-9);
        // Preference scenarios retain both major-party primary lower bounds.
        for (double offset : {-100.,-2.,0.,2.,100.}) {
            double first=projectFpOnly(fp,flows,offset,n,context);
            assert(first >= fp.at(0) && n-first >= fp.at(1));
        }
    }
    // No FP gives the context alone; all-major FPs leave no preference pool.
    assert(projectFpOnly({},flows,0,1000,.42) == 420);
    assert(projectFpOnly({{0,60},{1,40}},flows,100,100,.01) == 60);
    // With a separate National finalist, Liberal primary votes may transfer;
    // the named finalists' own primaries remain fixed as in ordinary conversion.
    Flows splitCoalition{{0,1},{1,.1},{7,0},{2,.8}};
    Counts splitFp{{0,40},{1,20},{7,30},{2,10}};
    assert(projectFpOnly(splitFp,splitCoalition,0,100,.01) == 50);
}

void testNewVoteComposition() {
    using namespace LiveNewVotes;
    using LivePartialCount::Counts;
    Shares context{{0,.4},{1,.4},{2,.2}};
    Counts tiny{{0,20},{1,20},{2,5}};
    assert(std::abs(smoothedShare(0,4,4)-1.0/12) < 1e-12);
    assert(smoothedShare(0,1000,4) < .0005);
    auto unreported = composition({},context,2700,Category::General);
    for (auto const& [p,v] : context) assert(std::abs(unreported.at(p)-v) < 1e-12);
    auto blended = composition(tiny,context,2700,Category::General);
    assert(std::abs(blended.at(0)-context.at(0)) < .01);
    assert(blended.at(2) > .19);
    assert(!blended.contains(42)); // A candidate absent from this ballot stays absent.
    assert(observedWeight(0,2700,Category::General) == 0);
    assert(observedWeight(2700,2700,Category::General) == 1);
    assert(std::abs(observedWeight(45,2700,Category::General)-45.0/2700) < 1e-12);
    assert(observedWeight(1000,2000,Category::AggregatedEarly) < .2);

    // Integer observations remain exact, including genuine zeros. All new
    // allocations are nonnegative and share the turnout account's finite total.
    for (int n : {1,4,45,500,2700}) {
        Counts votes{{0,n},{1,0},{2,0}};
        auto future = composition(votes,context,2700,Category::General);
        auto projected = project(votes,future,2700);
        double total = 0;
        for (auto const& [p,v] : projected) {
            assert(std::isfinite(v) && v >= votes.at(p));
            total += v;
        }
        assert(std::abs(total-2700) < 1e-9);
        assert(votes.at(1) == 0);
        if (n < 45) assert(future.at(1) > .38);
        if (n == 2700) assert(projected.at(1) == 0);
    }
    // Postal adjustment belongs to the observed FP side. Its influence fades
    // continuously at zero observations, and TPP uses the adjusted mix itself.
    Shares postalShift{{1,PostalCoalitionLogOddsShift}};
    auto postal = composition(tiny,context,2700,Category::General,postalShift);
    assert(postal.at(1) < blended.at(1));
    auto emptyPostal = composition({},context,2700,Category::General,postalShift);
    assert(std::abs(emptyPostal.at(1)-context.at(1)) < 1e-12);
    LivePartialCount::Flows flows{{0,1},{1,0},{2,.8}};
    double tpp = firstShare(blended,flows,0);
    assert(std::abs(tpp-blended.at(0)-.8*blended.at(2)) < 1e-12);
    assert(tpp >= blended.at(0) && tpp <= 1-blended.at(1));
    // The measured flow uses the same evidence weight for future voters and
    // pending cohorts, while changed finalists cannot inherit that measurement.
    LivePartialCount::Evidence measured; measured.first = 0; measured.second = 1;
    measured.offset = 1; measured.preferenceVotes = 500;
    assert(LivePartialCount::evidenceOffset(0,measured,0,1) == .5);
    assert(LivePartialCount::evidenceOffset(.2,measured,0,7) == .2);
    assert(firstShare(blended,flows,.5) > tpp);

    // A four-vote ordinary record must not impersonate its historical size.
    // Complete ordinary counts retain their usual full observed evidence.
    FpEvidence partial{700,4,1,1,{{1,-100.0f}},{}}, complete{700,700,1,1,{{1,0.0f}},{}};
    assert(evidenceWeight(partial) == 4);
    assert(evidenceWeight(complete) == 700);
    auto parent = aggregate({partial,complete},1400);
    assert(std::abs(parent.deviations.at(1)+400.0/704) < 1e-6);
    assert(std::abs(parent.confidence-704.0/1400) < 1e-12);
    FpEvidence declaration{3000,1000,0,1,{{1,100.0f}},{}};
    auto ordinaryOnly = aggregate({complete,declaration},3700);
    assert(ordinaryOnly.deviations.at(1) == 0);
    assert(std::abs(ordinaryOnly.confidence-700.0/3700) < 1e-12);
    condition(parent,{},.5);
    assert(std::abs(parent.specific.at(1)-parent.deviations.at(1)*.5) < 1e-7);
}

void testIndependentPooling() {
    using namespace LiveNewVotes;
    assert(independentPoolingWeight(0) == 0);
    assert(std::abs(independentPoolingWeight(.05)-.5) < 1e-12);
    assert(independentPoolingWeight(.01) < .04);
    assert(independentPoolingWeight(.2) > .94);
    // Fictional small campaigns rise from low bases while established
    // candidates fall. Preserve each seat's own evidence and reduce only
    // the movement it contributes to other districts.
    FpEvidence small{10000,5000,.5,1,{{0,4.0f},{3,20.0f}},{},{}};
    FpEvidence strong{10000,5000,.5,1,{{0,4.0f},{3,-10.0f}},{},{}};
    prepareIndependentPooling(small,3,.01);
    prepareIndependentPooling(strong,3,.2);
    assert(small.deviations.at(3) == 20 && small.confidence == .5);
    std::vector<FpEvidence> sources(10,small);
    sources.push_back(strong); sources.push_back(strong);
    auto combined = aggregate(sources,120000);
    assert(combined.deviations.at(3) < -4);
    assert(combined.deviations.at(0) == 4 && combined.confidence == .5);
    assert(poolingConfidence(combined,3) < .1);

    // Region size must not replenish lost independent evidence. Splitting
    // these sources into regions preserves the flat candidate mean, weight
    // and confidence, without applying the share discount again.
    auto weakRegion = aggregate(std::vector<FpEvidence>(10,small),100000);
    auto strongRegion = aggregate({strong,strong},20000);
    auto nested = aggregate({weakRegion,strongRegion},120000);
    assert(std::abs(nested.deviations.at(3)-combined.deviations.at(3)) < 1e-6);
    assert(std::abs(nested.poolingVotes.at(3)-combined.poolingVotes.at(3)) < 1e-9);
    assert(poolingConfidence(nested,3) == poolingConfidence(combined,3));
    FpEvidence absent{10000,5000,.5,1,{{0,4.0f}},{},{}};
    prepareIndependentPooling(absent,3,.2);
    assert(candidateEvidenceWeight(absent,3) == 0);
    auto sparse = aggregate({absent,strong},20000);
    assert(sparse.deviations.at(3) == -10);
    assert(sparse.poolingVotes.at(3) == strong.poolingVotes.at(3));
    assert(sparse.confidence == .5);
    auto noIndependent = aggregate({absent,absent},20000);
    assert(!noIndependent.deviations.contains(3));
    assert(poolingConfidence(noIndependent,3) == 0);
    condition(combined,{},.5,{{3,.1}});
    assert(combined.specific.at(0) == 2);
    assert(std::abs(combined.specific.at(3)-combined.deviations.at(3)*.1) < 1e-6);

    // Broad, genuine improvement among competitive campaigns still transfers.
    // Neither weight nor confidence uses the first booth's observed share.
    strong.deviations[3] = 8;
    auto broad = aggregate({strong,strong,strong},30000);
    assert(broad.deviations.at(3) == 8);
    assert(poolingConfidence(broad,3) > .47);
}

void testCompetingFpEvidence() {
    using namespace LiveFpEvidence;
    Unit ordinary{{{0,400},{1,350},{900,250}},{{0,400},{1,350},{900,250}},{0,1},.1,0,true,false};
    Unit unreported{{},{{0,5000},{1,4000},{900,1000}},{0,1},1,0,true,false};
    Unit early{{{0,10},{1,80},{900,10}},{{0,1400},{1,1300},{900,300}},{0,1},1,1,false,true};
    std::vector<Unit> units{ordinary,unreported,early};
    auto weak = prepare(units,.01);
    auto strong = prepare(units,.8);
    // Another candidate's valid swing cannot give a newly appearing candidate
    // historical confidence. Completion and breadth remain distinct evidence.
    assert(value(weak.evidence.historicalConfidence,900) == 0);
    assert(weak.weight.at(900) > weak.weight.at(0));
    assert(weak.weight.at(0) > strong.weight.at(0));
    assert(weak.future.at(900) > .1);
    assert(weak.future.at(1) < .5); // tiny, one-sided early batch is not extrapolated
    Shares combined;
    for (auto const& unit : units) {
        auto projected = adjusted(unit,weak.factors);
        assert(std::abs(total(projected)-total(unit.projected)) < 1e-8);
        for (auto const& [id,v] : projected) assert(v >= value(unit.counted,id)-1e-9);
        add(combined,projected);
    }
    double pool = total(weak.evidence.historical);
    for (auto const& [id,share] : weak.future)
        assert(std::abs(combined.at(id)-value(weak.evidence.counted,id)-pool*share)<.002);
    assert(adjusted(ordinary,weak.factors) == ordinary.projected);
    auto none = prepare({unreported},0);
    assert(none.factors.empty());
    assert(breadthGate(none.evidence) == 0);
    assert(adjusted(unreported,none.factors) == unreported.projected);
    assert(prepare({ordinary},.1).factors.empty());
    assert(prepare({},0).factors.empty());
    assert(additionalVariance(none,900) == 0);
    assert(additionalVariance(weak,900) > disagreementVariance(weak,900));
    auto agreement = weak;
    agreement.evidence.absolute = agreement.evidence.historical;
    assert(disagreementVariance(agreement,900) == 0);
    // Evidence arrives gradually, including a genuine candidate zero. The
    // first vote is not a reason to replace the whole unreported account.
    auto tiny = early;
    tiny.counted={{0,1},{1,0},{900,0}};
    auto first = prepare({unreported,tiny},0);
    auto initialComposition = LiveNewVotes::normalize(first.evidence.historical);
    for (auto const& [id,share] : initialComposition)
        assert(std::abs(first.future.at(id)-share)<.001);
    assert(breadthGate(first.evidence)<.001);


}

void testFpDisagreementEndpoints() {
    using namespace LiveFpEvidence;
    // Fictional weakly comparable candidate: counted votes are fixed, while
    // historical and absolute descriptions disagree about the future pool.
    // The same contract also applies when history predicts the higher result.
    Prepared p;
    p.evidence.observed=1000; p.evidence.completion=.1; p.evidence.breadth=4;
    p.evidence.counted={{0,500},{1,420},{900,80}};
    p.evidence.historical={{0,4400},{1,4400},{900,200}};
    p.evidence.absolute={{0,4000},{1,4100},{900,900}};
    p.future={{0,.47},{1,.46},{900,.07}};
    double const gap=std::log(980.5/9020.5)-std::log(280.5/9720.5);
    assert(std::abs(disagreementGap(p,900)-gap)<1e-12);
    for (bool reverse : {false,true}) {
        if (reverse) std::swap(p.evidence.historical,p.evidence.absolute);
        double const direction=reverse ? -gap : gap;
        for (double w : {0.,.001,.05,.5,.95,.999,1.}) {
            p.weight[900]=w;
            double historical=disagreementVariation(p,900,1);
            double absolute=disagreementVariation(p,900,0);
            double mean=(1-w)*historical+w*absolute;
            double variance=(1-w)*historical*historical+w*absolute*absolute;
            assert(std::abs(mean)<1e-12);
            assert(std::abs(variance-disagreementVariance(p,900))<1e-12);
            assert(std::abs(variance-w*(1-w)*gap*gap)<1e-12);
            // Once a method has all the weight there is no disagreement; when
            // both have weight, the draw never extends beyond either endpoint.
            for (double u : {0.,.0005,.025,.25,.5,.75,.975,.9995,1.}) {
                double offset=disagreementVariation(p,900,u);
                double centre=w*direction;
                assert(offset+centre>=std::min(0.,direction)-1e-12);
                assert(offset+centre<=std::max(0.,direction)+1e-12);
                if (w==0 || w==1) assert(offset==0);
            }
            double normal=earlyVariance(p,900);
            assert(normal>0);
            assert(std::abs(additionalVariance(p,900)-variance-normal)<1e-12);
        }
    }
    auto agreement=p;
    agreement.evidence.absolute=agreement.evidence.historical;
    assert(disagreementVariation(agreement,900,.5)==0);
    assert(earlyVariance(agreement,900)==earlyVariance(p,900));

    // The ordinary count-preservation path is shared with the new draw. A
    // choice changes future composition, keeps the seat total, and permits
    // ordinary normal variation in addition to the endpoint contribution.
    p.weight[900]=.7;
    std::map<int,float> projected{{0,4730.f},{1,4560.f},{900,710.f}};
    for (double u : {.1,.9}) {
        double offset=25*disagreementVariation(p,900,u);
        for (double normal : {-2.,0.,2.}) {
            auto varied=LiveTurnoutMath::varyRemaining(projected,p.evidence.counted,
                {{900,offset+normal}},true);
            double sum=0;
            for (auto const& [id,v] : varied) {
                assert(std::isfinite(v) && v>=p.evidence.counted.at(id));
                sum+=v;
            }
            assert(std::abs(sum-10000)<.002);
        }
    }
    // No observations and no future voters are exact no-ops. The first vote
    // still receives the existing smooth progress phase-in rather than a new
    // threshold at which the entire distribution changes.
    auto empty=p; empty.evidence.observed=0;
    assert(disagreementVariation(empty,900,.1)==0);
    auto complete=p; complete.evidence.historical.clear();
    assert(disagreementVariation(complete,900,.1)==0);
    assert(earlyVariance(complete,900)==0);
    Unit unreported{{},{{0,5000},{1,4000},{900,1000}},{0,1},1,0,true,false};
    Unit first{{{0,1},{1,0},{900,0}},{{0,1400},{1,1300},{900,300}},{0,1},1,1,false,true};
    auto tiny=prepare({unreported,first},0);
    double phase=-std::expm1(-50*tiny.evidence.completion);
    for (double u : {0.,.5,1.})
        assert(std::abs(phase*disagreementVariation(tiny,900,u))<.01);
}

void testFpPrecisionIntegration() {
    using namespace LiveFpIntegration;
    auto identity = [](int id) { return id; };
    // Record unrounded draws and the implicit zero when a candidate is absent.
    MomentAccumulator baseline;
    baseline.add({{0,40.f},{1,50.f},{2,10.f}});
    baseline.add({{0,50.f},{1,50.f}});
    auto moments = baseline.finish();
    double expected = (logOdds(.1)+logOdds(0))/2;
    assert(std::abs(moments.logOddsMean.at(2)-expected)<1e-10);
    assert(std::abs(moments.logOddsVariance.at(2)-std::pow(logOdds(.1)-logOdds(0),2)/4)<1e-9);
    // Anti-correlated candidates sum to a constant share. Adding their separate
    // variances would invent uncertainty in this anonymous candidate group.
    auto combined = group(moments,0,[](int id) { return id == 2 ? 0 : id; });
    assert(combined.present && combined.variance<1e-12);

    // Many zero-only draws can leave a tiny positive log-odds variance and a
    // mean just above the numerical floor. The candidate is still absent from
    // that prior and must borrow its source group's uncertainty when observed.
    MomentAccumulator zeroOnly;
    for (int sample = 0; sample < 10000; ++sample) {
        float offset = sample % 2 ? 3.f : -3.f;
        zeroOnly.add({{0,40.f},{1,0.f},{7,45.f-offset},{-1,15.f+offset}});
    }
    auto zeroMoments = zeroOnly.finish();
    auto missing = group(zeroMoments,1,identity);
    assert(missing.present && missing.allZero);
    assert(!group(zeroMoments,0,identity).allZero);
    // Tiny but positive support remains a real distribution, even if the
    // finite transform uses the same floor as a zero observation.
    MomentAccumulator tinyPositive;
    tinyPositive.add({{1,0.0000001f}});
    assert(!group(tinyPositive.finish(),1,identity).allZero);

    LiveData::FpIntegrationInformation newcomer;
    newcomer.baseline = zeroMoments;
    newcomer.count.shareMean = {{0,.4},{1,.14},{7,.32},{-1,.14}};
    for (auto const& [party,p] : newcomer.count.shareMean) {
        newcomer.count.logOddsMean[party] = logOdds(p);
        newcomer.count.logOddsVariance[party] = .01;
    }
    newcomer.evidenceGate = 1;
    LiveTurnoutMath::PartyAccount newcomerAccount{
        {{0,40.f},{1,100.f},{7,50.f},{-1,10.f}},
        {{0,4000.f},{1,1400.f},{7,3200.f},{-1,1400.f}}};
    auto learned = integrate(newcomerAccount,
        {{0,40.f},{1,1.f},{7,44.f},{-1,15.f}},newcomer,identity,identity);
    assert(learned.at(1)>5); // Not frozen at its 1% counted lower bound.

    // Broad early count draws earn less weight than a narrow pre-election prior.
    // This single precision weight also bounds the independent blended variance.
    for (double rawVariance : {.001,.04,1.,10.}) {
        for (double gate : {0.,.01,.1,.9,1.}) {
            double w = countWeight(.04,rawVariance,gate);
            double variance = std::pow(1-w,2)*.04+w*w*rawVariance;
            assert(variance<=.04+1e-12);
            assert(w>=0 && w<=1);
        }
    }
    double tinyGate = 80./580*.5;
    assert(countWeight(.04,1,tinyGate)<.003);
    assert(countWeight(.04,1,0)==0);
    assert(countWeight(0,1,1)==0);
    assert(countWeight(.04,0,1)==1);

    LiveData::FpIntegrationInformation information;
    information.baseline.logOddsMean={{0,logOdds(.3)},{1,logOdds(.7)}};
    information.baseline.logOddsVariance={{0,.04},{1,.04}};
    information.baseline.shareMean={{0,.3},{1,.7}};
    information.count.logOddsMean={{0,logOdds(.45)},{1,logOdds(.55)}};
    information.count.logOddsVariance={{0,1.f},{1,1.f}};
    information.count.shareMean={{0,.45},{1,.55}};
    std::map<int,float> prior{{0,30.f},{1,70.f}};
    LiveTurnoutMath::PartyAccount early{{{0,40.f},{1,40.f}},{{0,18000.f},{1,22000.f}}};
    // No observations preserves the exact prior; eighty votes move it smoothly.
    auto unreported=early; unreported.counted={{0,0.f},{1,0.f}};
    assert(integrate(unreported,prior,information,identity,identity)==prior);
    information.evidenceGate=tinyGate;
    auto first=integrate(early,prior,information,identity,identity);
    assert(first.at(0)>30 && first.at(0)<30.1);
    assert(std::abs(first.at(0)+first.at(1)-100)<1e-5);

    // Shared movement survives even when local uncertainty is very broad. The
    // caller supplies the same conditional prior shift as the count's centre.
    auto shifted=early; shifted.projected={{0,14000.f},{1,26000.f}};
    auto common=integrate(shifted,{{0,35.f},{1,65.f}},information,identity,identity);
    assert(std::abs(common.at(0)-35)<1e-5);

    // Tight count evidence replaces an implausible prior tail, while fixed
    // counts always bound the result, even if a precise prior contradicts them.
    information.evidenceGate=.9;
    information.count.logOddsVariance={{0,.00001},{1,.00001}};
    auto strong=integrate(early,{{0,90.f},{1,10.f}},information,identity,identity);
    assert(strong.at(0)>44.9 && strong.at(0)<45.1);
    LiveTurnoutMath::PartyAccount late{{{0,490.f},{1,450.f},{2,50.f}},{{0,500.f},{1,450.f},{2,50.f}}};
    auto bounded=constrainToCounted(late,{{0,.01},{1,.98},{2,.01}});
    assert(bounded.at(0)>=49 && bounded.at(1)>=45 && bounded.at(2)>=5);
    assert(std::abs(bounded.at(0)+bounded.at(1)+bounded.at(2)-100)<1e-5);
    auto complete=late; complete.counted={{0,500.f},{1,450.f},{2,50.f}};
    auto exact=constrainToCounted(complete,{{0,.8},{1,.1},{2,.1}});
    assert(exact.at(0)==50 && exact.at(1)==45 && exact.at(2)==5);
    // Identical moments under an independent proxy or Liberal/National
    // partition produce identical results; array position has no meaning here.
    auto proxy=information;
    auto relabel = [](auto const& values) {
        std::map<int,double> out;
        for (auto const& [id,v] : values) out[id == 1 ? 900 : id]=v;
        return out;
    };
    proxy.count.shareMean=relabel(information.count.shareMean);
    proxy.count.logOddsMean=relabel(information.count.logOddsMean);
    proxy.count.logOddsVariance=relabel(information.count.logOddsVariance);
    LiveTurnoutMath::PartyAccount alias{{{0,40.f},{900,40.f}},{{0,18000.f},{900,22000.f}}};
    auto partition=[](int id) { return id == 900 ? 1 : id; };
    auto renamed=integrate(LiveTurnoutMath::partitionAccount({&alias.counted,&alias.projected},partition),
        {{0,90.f},{1,10.f}},proxy,identity,partition);
    assert(renamed==strong);
}

void testRevisedTcpAccount() {
    using namespace LivePartialCount;
    // Currimundi's FP reallocated 300 primaries while TCP still described the
    // preliminary batch. Only -1 ON and +2 Greens need estimated transfers.
    TcpBasis basis{{{0,1122},{1,2862},{2,466},{4,299}},{{0,1557},{1,3192}},
        {{2,{{0,362},{1,104}}},{4,{{0,73},{1,226}}}}};
    Counts fp{{0,1422},{1,2562},{2,468},{4,298}};
    Flows flows{{0,1},{1,0},{2,.8},{4,.25}};
    assert(validBasis(basis));
    assert(!reviseTcp(basis.fp,basis.tcp,basis,flows,0,0,1));
    auto revised = reviseTcp(fp,basis.tcp,basis,flows,0,0,1);
    assert(revised && revised->votes == 4750);
    assert(std::abs(revised->first-(1857-73.0/299+2*362.0/466)) < 1e-9);
    assert(revised->uncertainVotes() == 3);
    auto bounds = revisedLowerBounds(fp,basis.tcp,basis);
    assert(bounds.at(0) == 1856 && bounds.at(1) == 2891);
    auto composition = LiveTurnoutMath::prepareUnitComposition(
        {{0,float(revised->first)},{1,float(revised->votes-revised->first)}},bounds,revised->votes);
    assert(std::abs(composition.base.at(0)-revised->first) < .001);
    assert(std::abs(composition.base.at(0)+composition.base.at(1)-4750) < .001);
    for (int count : {1,7,300}) {
        auto changed = basis.fp;
        changed[0] += count; changed[1] -= count;
        auto exact = reviseTcp(changed,basis.tcp,basis,flows,0,0,1,3);
        assert(exact && exact->first == 1557+count && exact->uncertainVotes() == 0);
    }
    auto removed = basis.fp; removed[2] = 0;
    for (double draw : {-100.0,0.0,100.0}) {
        auto r = reviseTcp(removed,basis.tcp,basis,flows,0,0,1,draw);
        assert(r && r->first == 1557-362 && r->uncertainVotes() == 0);
        auto varied = reviseTcp(fp,basis.tcp,basis,flows,0,0,1,draw);
        assert(varied->first >= varied->lower && varied->first <= varied->upper);
        assert(varied->first >= fp.at(0) && varied->votes-varied->first >= fp.at(1));
    }
    assert(!reviseTcp(fp,{{0,1850},{1,2900}},basis,flows,0,0,1)); // New TCP supersedes old.
    auto malformed = basis; malformed.transfers[2][0]++;
    assert(!validBasis(malformed));
    auto generic = basis; generic.transfers.clear();
    auto oldEvidence = measure(generic.fp,generic.tcp,flows,0,1);
    assert(oldEvidence);
    auto estimated = reviseTcp(fp,generic.tcp,generic,flows,oldEvidence->offset,0,1);
    assert(estimated && estimated->first > 1856 && estimated->first < 1859);
    auto added = basis.fp; added[0] += 500;
    auto addition = reviseTcp(added,basis.tcp,basis,flows,0,0,1);
    assert(addition && addition->first == 2057 && addition->uncertainVotes() == 0);
    auto reverse = reviseTcp(fp,basis.tcp,basis,{{0,0},{1,1},{2,.2},{4,.75}},0,1,0);
    assert(reverse && std::abs(reverse->first+revised->first-4750) < 1e-9);
}

int main()
{
    testElectionScopedPartyMapping();
    testRevisedTcpAccount();
    testJointTppIntegration();
    testTppCategorySensitivity();
	testRawTppSwing();
	testEstimatedTppSourceInfluence();
	testSharedFinalistTcpCoverage();
    testHistoricalIndependentEligibility();
	testHistoricalPreferenceReliability();
    testKnownCoalitionBallot();
    testIndependentPooling();
    testFpPrecisionIntegration();
    testFpDisagreementEndpoints();
    testCompetingFpEvidence();
    testNewVoteComposition();
    testUnmatchedBoothFpProjection();
    // Fictional batches: earlier urban primaries favour the first finalist;
    // later rural primaries favour the second. Transfer only the measured
    // preference relationship, retaining each later candidate's own primaries.
    using namespace LivePartialCount;
    Counts oldFp{{0,400},{1,300},{2,300}}, pair{{0,640},{1,360}};
    Counts currentFp{{0,600},{1,1000},{2,400}};
    Flows flows{{0,1},{1,0},{2,.8}};
    auto evidence = measure(oldFp,pair,flows,0,1,true);
    assert(evidence && evidence->matchingCurrentTcp);
    auto remaining = remainingCohort(currentFp,*evidence);
    assert(remaining && total(*remaining) == 1000);
    double inferred = pendingFirst(currentFp,pair,flows,0,evidence,0,1);
    assert(inferred > 275 && inferred < 285); // ~200 primaries + 80 preferences.
    assert(std::abs(pendingFirst(currentFp,pair,flows,0,{},0,1)-460) < 1e-9);
    // A small later TCP release must not discard the earlier matched primary
    // cohort. Only its counted votes and the size of the unfinished pool move;
    // the estimate of that pool's composition remains the same.
    auto retained = evidence; retained->matchingCurrentTcp = false;
    Counts laterPair{{0,645},{1,375}};
    double laterPending = pendingFirst(currentFp,laterPair,flows,0,retained,0,1);
    assert(std::abs(laterPending-inferred*.98) < 1e-9);
    assert(std::abs(pendingFirst(currentFp,pair,flows,0,retained,0,1)-inferred) < 1e-9);
    // In a near-complete count the whole-FP estimate may fall below the TCP
    // already received. That is not evidence that the unfinished voters all
    // favour the other finalist: their own estimate remains well inside [0,1].
    Counts latePair{{0,1100},{1,850}};
    double late = pendingFirst(currentFp,latePair,flows,0,{},0,1);
    assert(std::abs(late-23) < 1e-9);
    Counts revised = currentFp; revised[0] = 399;
    assert(!remainingCohort(revised,*evidence));
    assert(pendingFirst(oldFp,pair,flows,0,evidence,0,1) == 0);
    assert(!measure(oldFp,Counts{{0,900},{1,100}},flows,0,1));
    assert(!measure(oldFp,Counts{{0,300},{1,700}},flows,0,1));
    assert(!measure(oldFp,Counts{{0,320},{1,180}},flows,0,1));
    // The same accounting applies to a non-classic finalist pair. Its seat
    // preference assumption supplies the non-finalist flow instead of TPP's
    // party-specific flows; no Labor/Coalition identities are required.
    Counts otherFp{{4,40},{1,50},{2,10}}, otherPair{{4,46},{1,54}};
    Counts otherCurrent{{4,70},{1,90},{2,40}};
    Flows otherFlows{{4,1},{1,0},{2,.6}};
    auto otherEvidence = measure(otherFp,otherPair,otherFlows,4,1,true);
    assert(otherEvidence);
    double otherPending = pendingFirst(otherCurrent,otherPair,otherFlows,0,otherEvidence,4,1);
    assert(otherPending > 47.9 && otherPending < 48.1);
	assert(LiveData::voteTypeName(LiveData::VoteType::PrePoll) == "PrePoll");
	assert(LiveData::voteTypeName(LiveData::VoteType::IVote) == "iVote");
	assert(LiveData::voteTypeName(LiveData::VoteType::MarkedAsVoted) == "Marked as voted");
	assert(LiveData::boothTypeName(LiveData::BoothType::Ppvc) == "PPVC");

	// Configured coalition aliases may deliberately combine parties, while a
	// project with separate Nationals retains both identities. Missing codes on
	// other registered parties must never act as a shared alias.
	using PartySource = LivePartyMapping::SourceElection;
	LivePartyMapping::CommissionIds commissionIds{
		{{PartySource::Current,11},0}, {{PartySource::Current,12},1}, {{PartySource::Current,13},7}};
	std::map<std::string, int> abbreviations{{"ALP", 0}, {"LNP", 1}, {"NAT", 7}};
	auto mapParty = [&](int id, std::string const& code) {
		return LivePartyMapping::mapParty({PartySource::Current,id}, code, 8, 100000,
			commissionIds, abbreviations);
	};
	assert(mapParty(12, "LNP") == 1);
	assert(mapParty(13, "NAT") == 7);
	assert(LivePartyMapping::mapParty({PartySource::Previous,14}, "NAT", 8, 100000,
		commissionIds, abbreviations) == 7);
	int const unknownOne = mapParty(21, "");
	int const unknownTwo = mapParty(22, "");
	assert(unknownOne >= 8 && unknownTwo > unknownOne);
	assert(mapParty(21, "") == unknownOne);
	assert(!abbreviations.contains(""));
	int const minor = mapParty(23, "MIN");
	assert(mapParty(24, "MIN") == minor);
	LivePartyMapping::CommissionIds combinedIds;
	std::map<std::string, int> combinedCodes{{"LNP", 1}, {"NAT", 1}};
	assert(LivePartyMapping::mapParty({PartySource::Current,31}, "LNP", 2, 100000,
		combinedIds, combinedCodes) == 1);
	assert(LivePartyMapping::mapParty({PartySource::Previous,32}, "NAT", 2, 100000,
		combinedIds, combinedCodes) == 1);

	LiveData::BoothSnapshot snapshot;
	assert(snapshot.boothType == LiveData::BoothType::Invalid);
	assert(snapshot.voteType == LiveData::VoteType::Invalid);

	LiveData::Internals internals;
	assert(internals.projected2pp == 0.0f);
	assert(internals.raw2ppDeviation == 0.0f);

	assert(LiveSimulationMath::evidenceWeight(0.0f) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(
		LiveSimulationMath::EvidenceEpsilon) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(0.000001f) > 0.0f);
	assert(LiveSimulationMath::evidenceWeight(1.0f) == 1.0f);
	assert(LiveSimulationMath::evidenceWeight(0.0f, 14.0f) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(1.0f, 14.0f) == 1.0f);
	float previousWeight = 0.0f;
	for (int step = 1; step <= 100; ++step) {
		float const weight = LiveSimulationMath::evidenceWeight(
			float(step) * 0.01f);
		assert(weight >= previousWeight);
		previousWeight = weight;
	}

	auto noActivation = LiveSimulationMath::activateFromSource(
		12.0f, 0.0f, 8.0f);
	assert(noActivation.partyShare == 0.0f);
	assert(noActivation.sourceShare == 8.0f);
	assert(!noActivation.requiresNormalisation);

	auto partialActivation = LiveSimulationMath::activateFromSource(
		12.0f, 0.25f, 8.0f);
	assert(std::abs(partialActivation.partyShare - 3.0f) < 0.00001f);
	assert(std::abs(partialActivation.sourceShare - 5.0f) < 0.00001f);
	assert(std::abs(
		partialActivation.partyShare + partialActivation.sourceShare -
		8.0f) < 0.00001f);
	assert(!partialActivation.requiresNormalisation);

	auto limitedActivation = LiveSimulationMath::activateFromSource(
		12.0f, 1.0f, 8.0f);
	assert(limitedActivation.partyShare == 12.0f);
	assert(limitedActivation.sourceShare == 0.0f);
	assert(limitedActivation.requiresNormalisation);

	auto reservedActivation = LiveSimulationMath::activateFromSource(
		12.0f, 1.0f, 8.0f, 2.0f);
	assert(reservedActivation.partyShare == 12.0f);
	assert(reservedActivation.sourceShare == 2.0f);
	assert(reservedActivation.requiresNormalisation);

	float const equalStrengthReserve = LiveSimulationMath::othersReserve(
		10.0f, 4, {2.0f});
	float const strongPartyReserve = LiveSimulationMath::othersReserve(
		10.0f, 4, {6.0f});
	assert(std::abs(equalStrengthReserve - 8.0f) < 0.00001f);
	assert(std::abs(strongPartyReserve - (40.0f / 7.0f)) < 0.00001f);
	assert(strongPartyReserve < equalStrengthReserve);

	std::cout << "Live data tests passed\n";
}
