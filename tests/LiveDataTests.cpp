#include "../LiveData.h"
#include "../LiveSimulationMath.h"
#include "../IndependentCandidacy.h"
#include "../LivePartyMapping.h"
#include "../LivePartialCount.h"
#include "../LiveNewVotes.h"
#include "../LiveFpEvidence.h"
#include "../LiveFpIntegration.h"
#include "../LiveTurnoutMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

static_assert(int(LiveData::VoteType::Invalid) == 0);
static_assert(int(LiveData::VoteType::TIO) == 12);
static_assert(int(LiveData::VoteType::MarkedAsVoted) == 13);
static_assert(int(LiveData::BoothType::Normal) == 0);
static_assert(int(LiveData::BoothType::Invalid) == 6);

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

int main()
{
    testHistoricalIndependentEligibility();
    testIndependentPooling();
    testFpPrecisionIntegration();
    testFpDisagreementEndpoints();
    testCompetingFpEvidence();
    testNewVoteComposition();
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
	std::map<int, int> commissionIds{{11, 0}, {12, 1}, {13, 7}};
	std::map<std::string, int> abbreviations{{"ALP", 0}, {"LNP", 1}, {"NAT", 7}};
	auto mapParty = [&](int id, std::string const& code) {
		return LivePartyMapping::mapParty(id, code, 8, 100000,
			commissionIds, abbreviations);
	};
	assert(mapParty(12, "LNP") == 1);
	assert(mapParty(13, "NAT") == 7);
	assert(mapParty(14, "NAT") == 7); // Same party, another election's ID.
	int const unknownOne = mapParty(21, "");
	int const unknownTwo = mapParty(22, "");
	assert(unknownOne >= 8 && unknownTwo > unknownOne);
	assert(mapParty(21, "") == unknownOne);
	assert(!abbreviations.contains(""));
	int const minor = mapParty(23, "MIN");
	assert(mapParty(24, "MIN") == minor);
	std::map<int, int> combinedIds;
	std::map<std::string, int> combinedCodes{{"LNP", 1}, {"NAT", 1}};
	assert(LivePartyMapping::mapParty(31, "LNP", 2, 100000,
		combinedIds, combinedCodes) == 1);
	assert(LivePartyMapping::mapParty(32, "NAT", 2, 100000,
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
