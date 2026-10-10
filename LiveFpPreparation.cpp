#include "LiveV2.h"
#include "SimulationRun.h"

LiveFpEvidence::Unit LiveV2::Election::fpEvidenceUnit(int boothIndex, bool varied) const {
    auto const& booth = booths.at(boothIndex);
    auto const& seat = seats.at(booth.parentSeatId);
    LiveFpEvidence::Unit unit;
    for (auto const& [id,votes] : booth.node.fpVotesCurrent) unit.counted[id] = votes;
    auto const& projection = varied ? booth.node.tempFpVotesProjected : booth.node.fpVotesProjected;
    for (auto const& [id,votes] : projection)
        unit.projected[id == run.indPartyIndex && seat.liveIndependentPartyIndex != InvalidPartyIndex
            ? seat.liveIndependentPartyIndex : id] += votes;
    for (auto const& [id,swing] : booth.node.fpSwings) unit.comparable.insert(id);
    unit.relevance = booth.node.relevanceModifier;
    unit.group = int(booth.voteType)*100+int(booth.boothType);
    unit.ordinary = booth.voteType == Results2::VoteType::Ordinary && booth.boothType != Results2::Booth::Type::Ppvc;
    unit.aggregatedEarly = booth.voteType == Results2::VoteType::Early || booth.voteType == Results2::VoteType::PrePoll;
    return unit;
}

void LiveV2::Election::prepareFpEvidence() {
    // Prepare once from accepted counts and the historical projection. Every
    // scenario shares this small immutable state; no snapshots or JSON trees
    // are retained. Candidate-specific comparability prevents a new independent
    // from inheriting the major parties' confidence in historical swings.
    std::vector<LiveFpEvidence::Prepared> prepared;
    prepared.reserve(seats.size());
    for (auto const& seat : seats) {
        std::vector<LiveFpEvidence::Unit> units;
        units.reserve(seat.booths.size());
        for (int booth : seat.booths) units.push_back(fpEvidenceUnit(booth));
        prepared.push_back(LiveFpEvidence::prepare(units,seat.node.fpConfidence));
    }
    fpEvidence = std::make_shared<std::vector<LiveFpEvidence::Prepared> const>(std::move(prepared));
}

void LiveV2::Election::applyFpEvidence(int boothIndex) {
    if (!fpEvidence) return; // The initial historical composition is prepared first.
    auto& booth = booths.at(boothIndex);
    auto const& factors = fpEvidence->at(booth.parentSeatId).factors;
    if (factors.empty()) return; // Preserve zero-evidence and fully counted accounts.
    auto unit = fpEvidenceUnit(boothIndex,createRandomVariation);
    auto adjusted = LiveFpEvidence::adjusted(unit,factors);
    unit.projected = adjusted;
    auto future = LiveFpEvidence::remainder(unit);
    if (LiveFpEvidence::total(future)>0)
        booth.futureFpShares = LiveNewVotes::normalize(std::move(future));
    storeFpProjection(boothIndex,mapFpProjection(boothIndex,adjusted));
}

void LiveV2::Election::refreshFpEvidenceProjections() {
    // Move only the future FP pool, then let the existing preference-flow
    // calculations see that same composition before rebuilding parent accounts.
    // Already counted FP and matched FP/TCP cohorts remain unchanged.
    for (int index = 0; index < int(booths.size()); ++index) {
        applyFpEvidence(index);
        recomposeBoothTppVotes(true,index);
        recomposeBoothTcpVotes(index);
    }
    refreshTurnoutPairProgress();
    calculateTppEstimateBias();
    refreshProjectedVoteAggregates();
}

float LiveV2::Election::getSeatFpEvidenceWeight(std::string const& seatName) const {
    if (!fpEvidence) return 0;
    for (std::size_t s = 0; s < seats.size(); ++s)
        if (seats[s].name == seatName)
            return float(LiveFpEvidence::replacementWeight(fpEvidence->at(s).evidence));
    return 0;
}

float LiveV2::Election::fpEvidenceStdDev(int seatIndex, int partyId, float localStdDev) const {
    if (!fpEvidence) return localStdDev;
    auto const& seat = seats.at(seatIndex);
    int candidate = partyId == run.indPartyIndex && seat.liveIndependentPartyIndex != InvalidPartyIndex
        ? seat.liveIndependentPartyIndex : partyId;
    double variance = LiveFpEvidence::additionalVariance(fpEvidence->at(seatIndex),candidate);
    // Local preparation already describes projection uncertainty. Retain it,
    // adding disagreement and the early reporting-location allowance. The
    // variance helper uses natural log odds; LiveV2 uses log odds times 25.
    return float(std::sqrt(localStdDev*localStdDev+625*variance));
}
