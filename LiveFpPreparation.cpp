#include "LiveV2.h"
#include "PollingProject.h"
#include "SimulationRun.h"
#include "Simulation.h"
#include "LiveFpIntegration.h"
#include "LiveOrdinaryUpdate.h"

// Standard swing comparisons and support gradients use the preceding state
// result. In Narracan the older, comparable election supplies ALP/GRN only
// at a matched ordinary site. A missing match supplies no measured support
// there, even when a Greens result exists on the supplementary ballot.
std::optional<std::pair<int,int>> LiveV2::Election::primarySupportHistory(int index, int ballot) const {
    auto const& booth=readBooths().at(index);
    if (boothGeography) {
        auto const& older=boothGeography->districts.at(booth.parentSeatId).olderPrimaries;
        if (older && older->covers(ballot)) {
            auto found=older->counts.find(index);
            if (found==older->counts.end()) return {};
            auto const& counts=found->second;
            return std::pair{counts[ballot==0 ? 0 : 1],counts[0]+counts[1]+counts[2]};
        }
    }
    // A reconstructed declaration split or a booth inherited from a
    // different electorate supplies no same-district support comparison.
    if (!booth.sameSeat || (booth.syntheticPreviousCategory && !booth.measuredFpInSyntheticCategory) ||
        !booth.node.fpVotesPrevious.contains(ballot)) return {};
    return std::pair{booth.node.fpVotesPrevious.at(ballot),booth.node.totalVotesPrevious()};
}

bool LiveV2::Election::hasComparableFpReturn(int index, int ballot) const {
    auto const& booth=readBooths().at(index);
    if (boothGeography) {
        auto const& older=boothGeography->districts.at(booth.parentSeatId).olderPrimaries;
        if (older && older->covers(ballot))
            return booth.node.totalFpVotesCurrent()>0 && booth.node.fpVotesCurrent.contains(ballot) && older->counts.contains(index);
    }
    return booth.node.fpSwings.contains(ballot);
}

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
    if (boothGeography) {
        auto const& older=boothGeography->districts.at(booth.parentSeatId).olderPrimaries;
        if (older) for (int id : {0,older->greens}) {
            if (hasComparableFpReturn(boothIndex,id)) unit.comparable.insert(id);
            else unit.comparable.erase(id);
        }
    }
    unit.relevance = booth.node.relevanceModifier;
    unit.group = int(booth.voteType)*100+int(booth.boothType);
    unit.ordinary = booth.voteType == Results2::VoteType::Ordinary && booth.boothType != Results2::Booth::Type::Ppvc;
    unit.aggregatedEarly = booth.incrementalFp || LiveData::aggregatedEarly(booth.voteType);
    unit.namedEarlyEstimate = booth.evcDistrictAccount >= 0;
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

namespace {
bool ordinarySupportReturn(LiveV2::Booth const& booth) {
    return booth.boothType == Results2::Booth::Type::Normal && !booth.receivesFpBatches() &&
        booth.node.totalFpVotesCurrent() > 0;
}

// Put the pre-election district means onto their expected booth geography.
// The last column holds everyone outside the named comparisons. Include early
// and declaration voters in the district total even though only election-day
// booths will supply observations for the slope. Zero-size venues contribute
// no population to this balance (and have no usable opening expectation).
std::vector<LiveOrdinaryUpdate::Mix> openingSupportShares(std::vector<int> const& booths,
    std::vector<std::map<int,double>> const& blind, std::vector<int> const& parties,
    LiveOrdinaryUpdate::Mix targets) {
    using namespace LiveOrdinaryUpdate;
    double named = sum(targets);
    if (named > 1.+1e-8) return {};
    targets.push_back(std::max(0.,1.-named));
    std::vector<Mix> rows;
    Mix sizes;
    for (int index : booths) {
        auto const& projection = blind.at(index);
        double size = LiveFpEvidence::total(projection);
        Mix row;
        for (int party : parties)
            row.push_back(size > 0 ? LiveFpIntegration::value(projection,party)/size : 0.);
        row.push_back(std::max(0.,1.-sum(row)));
        sizes.push_back(size);
        rows.push_back(std::move(row));
    }
    double total = sum(sizes);
    if (total <= 0.) return {};
    for (double& t : targets) t *= total;
    // fit() checks every row has some supported candidate. A place without
    // expected turnout can use the district mix as a placeholder of weight zero.
    for (size_t i=0; i<rows.size(); ++i) if (sizes[i]==0.) rows[i] = targets;
    auto balance = fit(rows,sizes,targets);
    if (!balance) return {};
    for (size_t i=0; i<rows.size(); ++i)
        rows[i] = sizes[i]>0. ? tilted(rows[i],*balance) : Mix(targets.size(),0.);
    return rows;
}
}

// Prepare once per capture, entirely from this election's accepted returns,
// previous FP counts and the count-blind booth projections. An ordinary
// district swing affects all places already. Here a candidate needs several
// ordinary booths showing a *systematic difference* between places where
// they previously polled strongly and weakly. Only the slope is carried to
// outstanding ballots with their own comparable history.
void LiveV2::Election::prepareFpSupportGradients(BlindFpProjections const& blind) {
    auto const& baseline = sim.getLiveBaselineReport().value();
    std::set<int> const trialParties{0,1,run.grnPartyIndex,run.natPartyIndex,run.indPartyIndex,
        project.parties().indexByShortCode("ON"),project.parties().indexByShortCode("ONP")};
    for (int s=0; s<static_cast<int>(seats.size()); ++s) {
        auto& seat = seats[s];
        if (seat.finalResult || std::count_if(seat.booths.begin(),seat.booths.end(),[&](int i) {
            return ordinarySupportReturn(readBooths().at(i));
        }) < LiveFpSupportGradient::MinimumBooths) continue;
        std::vector<int> parties;
        LiveOrdinaryUpdate::Mix priors;
        auto district = std::find(baseline.seatName.begin(),baseline.seatName.end(),seat.name);
        if (district == baseline.seatName.end()) continue;
        auto const& openingMeans = baseline.liveBaselineFpMoments.at(district-baseline.seatName.begin()).shareMean;
        for (auto const& [party,p] : openingMeans) {
            if (party < 0 || !(p>0. && p<1.) || !trialParties.contains(party)) continue;
            if (!seat.node.fpVotesCurrent.contains(fpEvidenceCandidate(s,party))) continue;
            // A new independent at an old polling place is a different
            // candidate. Their projection still shares any movement in rivals.
            if (party == run.indPartyIndex && (!seat.namedIndependentMatches ||
                seat.independentPartyIndex != seat.liveIndependentPartyIndex ||
                !returningIndependentBallotIds->contains(seat.independentPartyIndex))) continue;
            parties.push_back(party);
            priors.push_back(p);
        }
        if (parties.empty()) continue;
        auto opening = openingSupportShares(seat.booths,blind,parties,priors);
        if (opening.empty()) continue;
        for (size_t j=0; j<parties.size(); ++j) {
            int ballot = fpEvidenceCandidate(s,parties[j]);
            std::vector<LiveFpSupportGradient::Observation> returns;
            for (size_t b=0; b<seat.booths.size(); ++b) {
                auto const& booth = readBooths().at(seat.booths[b]);
                auto const& boothNode = booth.node;
                if (!ordinarySupportReturn(booth) ||
                    !boothNode.fpVotesCurrent.contains(ballot) || !(opening[b][j]>0. && opening[b][j]<1.)) continue;
                auto past=primarySupportHistory(seat.booths[b],ballot);
                if (!past || past->second<=0) continue;
                returns.push_back({past->first,past->second,
                    boothNode.fpVotesCurrent.at(ballot),boothNode.totalFpVotesCurrent(),opening[b][j]});
            }
            if (returns.size() < LiveFpSupportGradient::MinimumBooths) continue;
            auto fit = LiveFpSupportGradient::fit(returns);
            seat.historicalSupportFits.emplace(parties[j],
                Seat::SupportFit{static_cast<int>(returns.size()),fit ? fit->slope : 0.});
            if (!fit) continue;
            for (int index : seat.booths) {
                auto& booth = preparationBooths()[index];
                auto past=primarySupportHistory(index,ballot);
                if (past && past->second>0)
                    booth.historicalSupportTilts.emplace(ballot,fit->tilt(past->first,past->second));
            }
        }
    }
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
    if (factors.empty() && !booth.federalDeclaration && booth.historicalSupportTilts.empty()) return;
    auto unit = fpEvidenceUnit(boothIndex,createRandomVariation);
    auto adjusted = LiveFpEvidence::adjusted(unit,factors);
    unit.projected = adjusted;
    auto future = LiveFpEvidence::remainder(unit);
    double pool = LiveFpEvidence::total(future);
    if (pool > 0) {
        if (booth.federalDeclaration) {
            LiveFederalDeclarations::allocate(future, pool, *booth.federalDeclaration,
                booth.node.fpVotesCurrent, createRandomVariation);
        }
        if (!booth.historicalSupportTilts.empty()) {
            LiveOrdinaryUpdate::Mix votes, tilts;
            for (auto const& [id,v] : future) {
                votes.push_back(v);
                tilts.push_back(LiveFpIntegration::value(booth.historicalSupportTilts,id));
            }
            LiveFpSupportGradient::adjust(votes,tilts);
            size_t j = 0;
            for (auto& entry : future) entry.second = votes[j++];
        }
        if (booth.federalDeclaration || !booth.historicalSupportTilts.empty()) {
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

void LiveV2::Election::prepareFpBoothComparisons(int seatIndex, BlindFpProjections const& blind,
    LiveData::FpIntegrationInformation& information) const {
    // The blind pass used the same turnout units and ballot as this snapshot,
    // before local counting replaced their expected shares. This also works
    // on a cold start or after a skipped capture. A previous forecast file is
    // never needed. Keep one small comparison per named candidate.
    auto const& seat = seats.at(seatIndex);
    information.reportedFormal = seat.node.totalFpVotesCurrent();
    if (information.reportedFormal == 0 || seat.finalResult) return;
    auto const isOrdinary = [](Booth const& booth) {
        return booth.boothType == Results2::Booth::Type::Normal && !booth.receivesFpBatches();
    };
    if (std::none_of(seat.booths.begin(),seat.booths.end(),[&](int index) {
        auto const& booth = readBooths().at(index);
        return isOrdinary(booth) && booth.node.totalFpVotesCurrent() > 0;
    })) return;

    auto const& mean = turnout->mean(LiveData::CountKind::Fp,seatIndex);
    auto const& counted = turnout->counted(LiveData::CountKind::Fp,seatIndex);
    double const remaining = LiveFpEvidence::total(mean)-LiveFpEvidence::total(counted);
    if (remaining <= 0) return;

    // A named first-time candidate can have a pre-election forecast of their
    // own. Using its uncertainty needs no previous election at this booth.
    // Measuring a local swing below additionally requires comparable history.
    auto const candidatePriors = LiveFpIntegration::ordinaryCandidatePriors(information, OthersIndex, run.indPartyIndex);
    for (auto const& [party,votes] : mean) {
        // Unrepresented voters stay in their existing group until the
        // simulation allocates them. A named independent's counts are keyed
        // by their ballot identity, while the district prior uses the IND slot.
        if (party < 0 || party >= project.parties().count()) continue;
        int const ballot = party == run.indPartyIndex ? seat.liveIndependentPartyIndex : party;
        bool const samePerson = party != run.indPartyIndex ||
            (seat.namedIndependentMatches && ballot == seat.independentPartyIndex &&
             returningIndependentBallotIds->contains(ballot));
        double const prior = LiveFpIntegration::value(information.baseline.shareMean,party);
        std::vector<LiveFpIntegration::BoothReference> refs;
        refs.reserve(seat.booths.size());
        for (int index : seat.booths) {
            auto const& booth = readBooths().at(index);
            auto const& expected = blind.at(index);
            double const size = LiveFpEvidence::total(expected);
            if (size <= 0) continue;
            double const formal = booth.node.totalFpVotesCurrent();
            LiveFpIntegration::BoothReference ref{size,LiveFpIntegration::value(expected,party)/size,
                formal,std::nullopt,false};
            if (formal > 0 && isOrdinary(booth) && booth.node.fpVotesCurrent.contains(ballot)) {
                ref.ordinaryCandidateVotes = booth.node.fpVotesCurrent.at(ballot);
                // The federal geographic input already supplies this party's
                // booth estimate. An old state result at that venue is not a
                // second historical comparison to the chosen expectation.
                bool const geographic = boothGeography && party == boothGeography->party &&
                    boothGeography->units.at(index).has_value();
                ref.matched = prior > 0 && prior < 1 && samePerson && !geographic &&
                    hasComparableFpReturn(index,ballot);
            }
            refs.push_back(ref);
        }
        double const shared = LiveFpIntegration::value(information.sharedDeviation,party)/25.;
        double const candidatePrior = LiveFpIntegration::value(candidatePriors,party);
        std::optional<double> conditionalPrior;
        if (candidatePrior > 0 && candidatePrior < 1)
            conditionalPrior = LiveFpIntegration::shareFromLogOdds(LiveFpIntegration::logOdds(candidatePrior)+shared);
        double const boothRemaining = (votes-LiveFpIntegration::value(counted,party))/remaining;
        auto comparison = LiveFpIntegration::compareBooths(refs,conditionalPrior,boothRemaining);
        if (comparison.ordinary > 0) information.boothComparisons.emplace(party,comparison);
    }
}

void LiveV2::Election::prepareFpIntegration(BlindFpProjections const& blind) {
    // Identify baseline moments and accepted shared movements, measure raw
    // local uncertainty, then freeze the small handoff shared by all scenarios.
    auto information = fpIntegrationCentres();
    auto moments = measureFpCountMoments();
    for (std::size_t s = 0; s < information.size(); ++s) {
        information[s].count = std::move(moments[s]);
        prepareFpBoothComparisons(static_cast<int>(s),blind,information[s]);
        prepareOrdinaryFpFit(static_cast<int>(s),blind,information[s]);
    }
    fpIntegration = std::make_shared<std::vector<LiveData::FpIntegrationInformation> const>(std::move(information));
}

void LiveV2::Election::prepareOrdinaryFpFit(int seatIndex, BlindFpProjections const& blind,
    LiveData::FpIntegrationInformation& information) const {
    if (information.boothComparisons.empty()) return;
    auto const& seat = seats.at(seatIndex);
    auto const priors = LiveFpIntegration::ordinaryCandidatePriors(information,OthersIndex,run.indPartyIndex);
    std::vector<int> parties;
    LiveOrdinaryUpdate::Mix prior, odds;
    for (auto const& [id,b] : information.boothComparisons) {
        if (!b.priorCentre || (id==run.indPartyIndex && information.poolIndependents)) continue;
        parties.push_back(id);
        prior.push_back(priors.at(id));
        odds.push_back(std::exp(LiveFpIntegration::value(information.sharedDeviation,id)/25.));
    }
    if (parties.empty()) return;
    // Treat candidates without individual opening forecasts together when
    // measuring what named parties gained or lost. The simulation retains
    // their candidate-specific counts and variation when dividing that pool.
    double const residual = 1-LiveOrdinaryUpdate::sum(prior);
    if (residual < -1e-8) return;
    prior.push_back(std::max(0.,residual));
    odds.push_back(1.);
    std::vector<LiveOrdinaryUpdate::Venue> venues;
    double ordinary = 0;
    for (int index : seat.booths) {
        auto const& booth = readBooths().at(index);
        auto const& expected = blind.at(index);
        double size = LiveFpEvidence::total(expected), n = booth.node.totalFpVotesCurrent();
        if (size<=0) {
            if (n>0) return;
            continue;
        }
        bool const electionDay = booth.boothType==Results2::Booth::Type::Normal && !booth.receivesFpBatches();
        LiveOrdinaryUpdate::Mix shape,counts;
        for (int party : parties) {
            int const ballot = party==run.indPartyIndex ? seat.liveIndependentPartyIndex : party;
            auto found = booth.node.fpVotesCurrent.find(ballot);
            // An explicitly reported zero is evidence. An omitted count has
            // no observed candidate allocation for a joint fit at this venue.
            if (electionDay && n>0 && found==booth.node.fpVotesCurrent.end()) return;
            counts.push_back(found==booth.node.fpVotesCurrent.end() ? 0. : found->second);
            shape.push_back(LiveFpIntegration::value(expected,party)/size);
        }
        shape.push_back(std::max(0.,1.-LiveOrdinaryUpdate::sum(shape)));
        counts.push_back(std::max(0.,n-LiveOrdinaryUpdate::sum(counts)));
        venues.push_back({std::move(shape),std::move(counts),size,electionDay});
        // This independent district fit starts from the blind geography.
        // Give its outstanding ballots the same once-only adjustment as the
        // booth account, which separately supplies preference and turnout draws.
        if (!booth.historicalSupportTilts.empty()) {
            auto& tilts = venues.back().historicalSupportTilts;
            for (int party : parties)
                tilts.push_back(LiveFpIntegration::value(booth.historicalSupportTilts,fpEvidenceCandidate(seatIndex,party)));
            tilts.push_back(0.); // The unnamed residual has no historical slope.
        }
        if (electionDay) ordinary += n;
    }
    auto result = LiveOrdinaryUpdate::remaining(venues,prior,odds);
    if (!result) return;
    auto const conditioned = LiveOrdinaryUpdate::conditionedShares(prior,odds);
    for (size_t j=0; j<parties.size(); ++j) {
        auto& comparison = information.boothComparisons.at(parties[j]);
        comparison.priorCentre = LiveFpIntegration::logOdds(conditioned[j]);
        comparison.fittedRemaining = result->at(j);
    }
    // If the residual maps entirely to the simulator's Others pool it has
    // a joint centre too. A separately modelled candidate without a named
    // prior instead keeps the existing booth-based draw and uncertainty.
    auto const& mean = turnout->mean(LiveData::CountKind::Fp,seatIndex);
    auto const& counted = turnout->counted(LiveData::CountKind::Fp,seatIndex);
    double residualVotes = 0.;
    for (auto const& [id,n] : mean) {
        if (std::find(parties.begin(),parties.end(),id)!=parties.end()) continue;
        if (id>=0 && id<project.parties().count() && n>0) return;
        residualVotes += n-LiveFpIntegration::value(counted,id);
    }
    double const future = LiveFpEvidence::total(mean)-LiveFpEvidence::total(counted);
    if (residual<=0 || future<=0) return;
    auto& other = information.boothComparisons[OthersIndex];
    other.ordinary = ordinary;
    other.priorCentre = LiveFpIntegration::logOdds(conditioned.back());
    other.boothRemainingCentre = LiveFpIntegration::logOdds(residualVotes/future);
    other.fittedRemaining = result->back();
}
