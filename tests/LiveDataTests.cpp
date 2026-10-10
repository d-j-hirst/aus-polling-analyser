#include "../LiveData.h"
#include "../LiveSimulationMath.h"
#include "../LivePartyMapping.h"
#include "../LivePartialCount.h"
#include "../LiveNewVotes.h"

#include <cassert>
#include <cmath>
#include <iostream>

static_assert(int(LiveData::VoteType::Invalid) == 0);
static_assert(int(LiveData::VoteType::TIO) == 12);
static_assert(int(LiveData::VoteType::MarkedAsVoted) == 13);
static_assert(int(LiveData::BoothType::Normal) == 0);
static_assert(int(LiveData::BoothType::Invalid) == 6);

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

int main()
{
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
