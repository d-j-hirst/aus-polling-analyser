#include "LiveV2.h"
#include "PollingProject.h"
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
    unit.aggregatedEarly = booth.incrementalFp || LiveData::aggregatedEarly(booth.voteType);
    if (boothGeography && boothGeography->units.at(boothIndex)) {
        auto const& reference = *boothGeography->units[boothIndex];
        int party = boothGeography->party;
        unit.geographicShare = {party,reference.absoluteShare};
        // These ordinary comparisons refer to a federal prediction. Even in
        // the five returning Victorian seats an old state swing supplies no
        // independent confidence in that prediction. Mobile teams share the
        // SA feed's ordinary label but form a separate voting population.
        if (reference.ordinary) unit.comparable.erase(party);
        else if (unit.ordinary) {
            unit.ordinary = false;
            unit.group = -1;
        }
    }
    return unit;
}

void LiveV2::Election::prepareFpEvidence(LiveFederalDeclarations::Prior const& declarationPrior) {
    // Prepare once from accepted counts and the historical projection. Every
    // scenario shares this small immutable state; no snapshots or JSON trees
    // are retained. Candidate-specific comparability prevents a new independent
    // from inheriting the major parties' confidence in historical swings.
    std::vector<LiveFpEvidence::Prepared> prepared;
    prepared.reserve(seats.size());
    int oneNation = InvalidPartyIndex;
    if (!declarationPrior.empty()) {
        // Where the booth comparison is active, both calculations describe
        // the same candidate even if the project recognises two party codes.
        oneNation = boothGeography ? boothGeography->party : project.parties().indexByShortCode("ON");
        if (oneNation < 0) oneNation = project.parties().indexByShortCode("ONP");
    }
    for (auto const& seat : seats) {
        std::vector<LiveFpEvidence::Unit> units;
        units.reserve(seat.booths.size());
        for (int booth : seat.booths) units.push_back(fpEvidenceUnit(booth));
        prepared.push_back(LiveFpEvidence::prepare(units,seat.node.fpConfidence));
        if (oneNation >= 0 && !seat.finalResult)
            prepareFederalDeclarations(seat, units, prepared.back().factors, declarationPrior, oneNation);
    }
    fpEvidence = std::make_shared<std::vector<LiveFpEvidence::Prepared> const>(std::move(prepared));
}

// Measure the district's in-person result after the ordinary FP evidence has
// been balanced across booths. The common reference covers exactly the same
// population as the federal ordinary vote. It includes the projected remainder
// at unreported booths and the fixed count at reported ones. The optional
// federal booth geography is already reflected in those projections.
void LiveV2::Election::prepareFederalDeclarations(Seat const& seat, std::vector<LiveFpEvidence::Unit> const& units,
    LiveFpEvidence::Shares const& factors, LiveFederalDeclarations::Prior const& prior, int party) {
    auto const election = run.getTermCode();
    LiveFederalDeclarations::HomeVote home;
    for (size_t j = 0; j < units.size(); ++j) {
        auto const& booth = readBooths()[seat.booths[j]];
        if (!LiveFederalDeclarations::homeInPerson(election, booth.voteType, booth.boothType, booth.name) ||
            !booth.node.runningParties.contains(party)) continue;
        auto projected = LiveFpEvidence::adjusted(units[j], factors);
        home.on += LiveFpEvidence::value(projected, party);
        home.formal += LiveFpEvidence::total(projected);
        home.counted += LiveFpEvidence::total(units[j].counted);
    }
    bool hasVictorianBaseline = election == "2026vic" && boothGeography;
    if (home.counted <= 0 && !hasVictorianBaseline) return;

    // Both feeds accumulate each absent, postal or early-away category in one district
    // account. Replace the central estimate for its future voters after both
    // the historical and current-share estimates have had their usual say.
    // This avoids adding the federal difference on top of an existing tilt
    // or feeding the same category return into its own estimate twice.
    for (size_t j = 0; j < units.size(); ++j) {
        auto& booth = preparationBooths()[seat.booths[j]];
        auto shift = prior.find(booth.voteType);
        if (shift == prior.end() || !booth.node.runningParties.contains(party)) continue;
        auto const& unit = units[j];
        auto projected = LiveFpEvidence::adjusted(unit, factors);
        double counted = LiveFpEvidence::total(unit.counted);
        double expected = LiveFpEvidence::total(projected);
        if (expected <= counted || projected.size() < 2 || !projected.contains(party)) continue;
        double countedOn = LiveFpEvidence::value(unit.counted, party);
        double original = std::clamp((projected.at(party) - countedOn) / (expected - counted), 0., 1.);
        if (home.counted > 0) {
            booth.federalDeclaration = LiveFederalDeclarations::estimate(party, home, shift->second,
                countedOn, counted, expected, original, booth.voteType);
        } else if (counted > 0) {
            // If this declaration is Victoria's first return in the district,
            // its pre-count share already includes the federal allowance.
            // Blend the batch once with that starting share. With no ballots
            // reported anywhere the centred district baseline stands as prepared.
            auto const& reference = boothGeography->units.at(seat.booths[j]).value();
            booth.federalDeclaration = LiveFederalDeclarations::estimateFromContext(party, reference.share,
                countedOn, counted, expected, original, booth.voteType);
        }
    }
}

void LiveV2::Election::applyFpEvidence(int boothIndex) {
    if (!fpEvidence) return; // The initial historical composition is prepared first.
    auto& booth = preparationBooths().at(boothIndex);
    auto const& factors = fpEvidence->at(booth.parentSeatId).factors;
    if (factors.empty() && !booth.federalDeclaration) return;
    auto unit = fpEvidenceUnit(boothIndex,createRandomVariation);
    auto adjusted = LiveFpEvidence::adjusted(unit,factors);
    unit.projected = adjusted;
    auto future = LiveFpEvidence::remainder(unit);
    double pool = LiveFpEvidence::total(future);
    if (pool > 0) {
        if (booth.federalDeclaration) {
            LiveFederalDeclarations::allocate(future, pool, *booth.federalDeclaration,
                booth.node.fpVotesCurrent, createRandomVariation);
            for (auto const& [id, votes] : future) adjusted[id] = LiveFpEvidence::value(unit.counted, id) + votes;
        }
        // Preferences for the same outstanding voters and the turnout scenarios
        // must see the same composition as the seat's first-preference account.
        booth.futureFpShares = LiveNewVotes::normalize(std::move(future));
    }
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
    int seatIndex = findSeatIndex(seatName);
    return seatIndex != int(seats.size()) ? &fpIntegration->at(seatIndex) : nullptr;
}

int LiveV2::Election::fpEvidenceCandidate(int seatIndex, int partyId) const {
    // Simulation variability uses the principal-independent modelling slot;
    // competing FP evidence is keyed by the actual candidate on this ballot.
    auto const& seat = seats.at(seatIndex);
    return partyId == run.indPartyIndex && seat.liveIndependentPartyIndex != InvalidPartyIndex
        ? seat.liveIndependentPartyIndex : partyId;
}

double LiveV2::Election::fpEvidenceNormalStdDev(int seatIndex, int partyId, double localStdDev) const {
    if (!fpEvidence) return localStdDev;
    double variance = LiveFpEvidence::earlyVariance(fpEvidence->at(seatIndex),fpEvidenceCandidate(seatIndex,partyId));
    // Local preparation already describes projection uncertainty. Retain it,
    // adding the early reporting-location allowance. Disagreement is drawn
    // separately so it cannot add a second, unbounded normal tail. The helper
    // uses natural log odds; LiveV2 uses log odds times 25.
    return double(std::sqrt(localStdDev*localStdDev+625*variance));
}

double LiveV2::Election::fpEvidenceDisagreement(int seatIndex, int partyId, double quantile) const {
    if (!fpEvidence) return 0;
    return double(25*LiveFpEvidence::disagreementVariation(
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
        information[s].secondaryIndependent = seats[s].secondaryIndependent;
        information[s].namedIndependentMatches = seats[s].namedIndependentMatches;
        information[s].namedIncumbentMatches = seats[s].namedIncumbentMatches;
        information[s].poolIndependents = run.isLiveAutomatic() && run.preferenceModel &&
            run.preferenceModel->coversDistrict(static_cast<int>(s)) && seats[s].secondaryIndependent &&
            !seats[s].namedIndependentMatches;
        // A seat can include a minor party in its anonymous forecast even
        // though the project's party list recognises its code in the feed.
        // Count ballot identities, including explicit zero returns. The
        // reported vote share is the evidence used later to update this prior.
        auto& inputs = information[s];
        int otherBallotMembers = 0;
        for (auto const& [id, votes] : seats[s].node.fpVotesCurrent) {
            if (id == seats[s].liveIndependentPartyIndex) continue;
            if (id < 0 || id >= project.parties().count()) {
                ++otherBallotMembers;
            } else if (id >= PartyCollection::NumMajorParties && id != run.natPartyIndex &&
                id != run.indPartyIndex &&
                LiveFpIntegration::value(inputs.baseline.shareMean, id) == 0) {
                inputs.partiesFromOthers.push_back(id);
                ++otherBallotMembers;
            }
        }
        // OTH*** lists four unnamed party candidates. Some feeds initially
        // omit ballot members, so retain at least the configured number. If
        // no multiplicity is known, assume at least three share the pool.
        if (!inputs.partiesFromOthers.empty()) {
            int const projectSeat = project.seats().indexByName(seats[s].name);
            int const listedOthers = run.othCount.at(projectSeat);
            inputs.othersPoolSize = std::max(otherBallotMembers, listedOthers > 0 ? listedOthers : 3);
        }
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
            for (auto& [id,n] : projected) n = double(100*n/total);
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
