#include "LiveV2.h"
#include "SimulationRun.h"
#include "Simulation.h"
#include "LiveFpIntegration.h"

LiveFpEvidence::Unit LiveV2::Election::fpEvidenceUnit(int boothIndex, bool varied) const {
    auto const& booth = readBooths().at(boothIndex);
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
    unit.aggregatedEarly = booth.incrementalFp || booth.voteType == Results2::VoteType::Early || booth.voteType == Results2::VoteType::PrePoll;
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
    auto& booth = preparationBooths().at(boothIndex);
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
    for (int index = 0; index < int(readBooths().size()); ++index) {
        applyFpEvidence(index);
        recomposeBoothTppVotes(true,index);
        recomposeBoothTcpVotes(index);
    }
    refreshTurnoutPairProgress();
    calculateTppEstimateBias();
    refreshProjectedVoteAggregates();
}

LiveData::FpIntegrationInformation const* LiveV2::Election::getSeatFpIntegrationInformation(
    std::string const& seatName) const {
    if (!fpIntegration) return nullptr;
    for (std::size_t s = 0; s < seats.size(); ++s)
        if (seats[s].name == seatName)
            return &fpIntegration->at(s);
    return nullptr;
}

int LiveV2::Election::fpEvidenceCandidate(int seatIndex, int partyId) const {
    // Simulation variability uses the principal-independent modelling slot;
    // competing FP evidence is keyed by the actual candidate on this ballot.
    auto const& seat = seats.at(seatIndex);
    return partyId == run.indPartyIndex && seat.liveIndependentPartyIndex != InvalidPartyIndex
        ? seat.liveIndependentPartyIndex : partyId;
}

float LiveV2::Election::fpEvidenceNormalStdDev(int seatIndex, int partyId, float localStdDev) const {
    if (!fpEvidence) return localStdDev;
    double variance = LiveFpEvidence::earlyVariance(fpEvidence->at(seatIndex),fpEvidenceCandidate(seatIndex,partyId));
    // Local preparation already describes projection uncertainty. Retain it,
    // adding the early reporting-location allowance. Disagreement is drawn
    // separately so it cannot add a second, unbounded normal tail. The helper
    // uses natural log odds; LiveV2 uses log odds times 25.
    return float(std::sqrt(localStdDev*localStdDev+625*variance));
}

float LiveV2::Election::fpEvidenceDisagreement(int seatIndex, int partyId, double quantile) const {
    if (!fpEvidence) return 0;
    return float(25*LiveFpEvidence::disagreementVariation(
        fpEvidence->at(seatIndex),fpEvidenceCandidate(seatIndex,partyId),quantile));
}

std::vector<LiveData::FpIntegrationInformation> LiveV2::Election::fpIntegrationCentres() const {
    // District names bind the baseline report to the live account, rather than
    // assuming the feed and project use the same ordering of districts.
    std::vector<LiveData::FpIntegrationInformation> information(seats.size());
    auto const& baseline = sim.getLiveBaselineReport().value();
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto found = std::find(baseline.seatName.begin(),baseline.seatName.end(),seats[s].name);
        if (found == baseline.seatName.end())
            throw std::runtime_error("Missing FP baseline district: "+seats[s].name);
        information[s].baseline = baseline.liveBaselineFpMoments.at(found-baseline.seatName.begin());
        information[s].sharedDeviation = finalSpecificFpDeviations;
        for (auto const& [party,deviation] : largeRegions.at(seats[s].parentRegionIndex).finalSpecificFpDeviations)
            information[s].sharedDeviation[party] += deviation;
        information[s].evidenceGate = LiveFpEvidence::breadthGate(fpEvidence->at(s).evidence);
    }
    return information;
}

std::vector<LiveData::FpDistributionMoments> LiveV2::Election::measureFpCountMoments() {
    // Sample this same unphased finite-pool response once during preparation,
    // rather than estimating its variance from rounded exported quantiles.
    // Holding turnout at its prepared mean measures composition uncertainty;
    // ordinary scenario turnout draws remain a separate source of uncertainty.
    // Only moments survive, shared by every scenario. Cost and memory do not
    // grow with the length of a replay or the number of simulation iterations.
    constexpr int MomentSamples = 256;
    std::vector<LiveFpIntegration::MomentAccumulator> accumulators(seats.size());
    auto previousIndex = variabilitySampleIndex;
    auto previousSeed = variabilityBaseSeed;
    variabilityBaseSeed ^= 0xf1357aea2e62a9c5ULL;
    for (int sample = 0; sample < MomentSamples; ++sample) {
        // A separate deterministic seed keeps moment measurement independent
        // of forecast draws even for unusually large iteration counts.
        variabilitySampleIndex = sample;
        generateSharedFpVariability();
        for (std::size_t s = 0; s < seats.size(); ++s) {
            auto const& mean = turnout->mean(LiveData::CountKind::Fp,s);
            double expected = 0;
            for (auto const& [id,n] : mean) expected += n;
            if (expected <= 0) continue; // An explicitly inactive contest has no account.
            auto projected = turnout->vary(LiveData::CountKind::Fp,s,mean,drawSeatFpChanges(int(s)));
            double total = 0;
            for (auto const& [id,n] : projected) total += n;
            if (total <= 0) continue;
            for (auto& [id,n] : projected) n = float(100*n/total);
            accumulators[s].add(projected);
        }
    }
    variabilitySampleIndex = previousIndex;
    variabilityBaseSeed = previousSeed;
    boothTypeFpIterationVariation.clear();
    voteTypeFpIterationVariation.clear();
    std::vector<LiveData::FpDistributionMoments> moments;
    for (auto const& accumulator : accumulators) moments.push_back(accumulator.finish());
    return moments;
}

void LiveV2::Election::prepareFpIntegration() {
    // Identify baseline moments and accepted shared movements, measure raw
    // local uncertainty, then freeze the small handoff shared by all scenarios.
    auto information = fpIntegrationCentres();
    auto moments = measureFpCountMoments();
    for (std::size_t s = 0; s < information.size(); ++s) information[s].count = std::move(moments[s]);
    fpIntegration = std::make_shared<std::vector<LiveData::FpIntegrationInformation> const>(std::move(information));
}
