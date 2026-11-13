#include "LiveV2.h"
#include "LiveTurnout.h"
#include "LiveTurnoutMath.h"
#include "PollingProject.h"
#include "SimulationRun.h"
#include "RandomGenerator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
bool hasClassicPair(std::map<int, int> const& votes, int nationals) {
    return votes.size() == 2 && votes.count(0) && (votes.count(1) || votes.count(nationals));
}
double total(auto const& votes) {
    double result = 0; for (auto const& [party, count] : votes) result += count; return result;
}

// These inputs exist only during preparation. They join the fixed prior's
// service identities to checked LiveV2 counts before the portable model runs.
struct TurnoutObservations {
    std::vector<TurnoutModel::Unit> units;
    std::vector<int> boothIndexes;
    std::vector<bool> finalised;
    TurnoutModel::Observation current;
    nlohmann::json enrolmentRevisions = nlohmann::json::array();
};

std::map<std::string, std::size_t> inspectTurnoutDistricts(TurnoutModel::Prior const& prior,
    Results2::Election const& currentElection, std::vector<LiveV2::Seat> const& seats,
    TurnoutObservations& observations) {
    // District identities and finalisation come from the received source.
    // Revised enrolment is diagnostic information, not a new prior denominator.
    std::map<std::string, std::size_t> liveSeats;
    for (std::size_t s = 0; s < seats.size(); ++s) liveSeats.emplace(seats[s].name, s);
    if (liveSeats.size() != prior.seats.size()) throw std::runtime_error("Turnout prior and live district populations differ.");
    observations.finalised.resize(prior.seats.size());
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        if (!liveSeats.contains(prior.seats[s])) throw std::runtime_error("Turnout district is absent: " + prior.seats[s]);
        auto raw = std::find_if(currentElection.seats.begin(), currentElection.seats.end(), [&](auto const& row) { return row.second.name == prior.seats[s]; });
        if (raw == currentElection.seats.end()) throw std::runtime_error("Turnout has no raw district account.");
        // Authorities can revise roll figures after voting has finished. The
        // prior's count outcomes and turnout rates were prepared against its
        // original enrolment, so changing that denominator here would alter
        // expectations without evidence of additional votes. Retain the fixed
        // prior and record the source revision for inspection instead.
        if (raw->second.enrolment > 0 && raw->second.enrolment != prior.enrolment[s]) {
            observations.enrolmentRevisions.push_back({{"seat", prior.seats[s]},
                {"prior_enrolment", prior.enrolment[s]}, {"source_enrolment", raw->second.enrolment},
                {"difference", raw->second.enrolment - prior.enrolment[s]}});
        }
        observations.finalised[s] = raw->second.fpFinalised;
    }
    return liveSeats;
}

void matchTurnoutBooths(TurnoutModel::Prior const& prior,
    LiveInputRecovery::RecordStatuses const& statuses,
    std::vector<LiveV2::Seat> const& seats, std::vector<LiveV2::Booth> const& booths,
    std::map<std::string, std::size_t> const& liveSeats, TurnoutObservations& observations) {
    // Match each prior service exactly once and collect only measured FP votes.
    // The same category totals also become this source time's history record.
    std::set<int> matchedBooths;
    for (auto& unit : observations.units) {
        auto const& seat = seats[liveSeats.at(prior.seats[unit.seat])];
        int match = -1;
        for (int index : seat.booths) {
            auto const& booth = booths[index];
            if (booth.name != unit.name) continue;
            bool declaration = booth.voteType != Results2::VoteType::Ordinary;
            bool ppvc = !declaration && booth.boothType == Results2::Booth::Type::Ppvc;
            if (declaration != (unit.kind == "declaration") || (!declaration && ppvc != (unit.kind == "ppvc")))
                throw std::runtime_error("Turnout booth role changed: " + seat.name + "/" + unit.name);
            if (unit.incremental != booth.incrementalFp)
                throw std::runtime_error("Turnout batch reporting changed; regenerate inputs: " + seat.name + "/" + unit.name);
            if (match != -1) throw std::runtime_error("Ambiguous turnout booth: " + seat.name + "/" + unit.name);
            match = index;
        }
        // A missing service is not a measured zero. Only a recorded closure
        // permits its identity to be absent from the current feed.
        if (match == -1 && !unit.closed) throw std::runtime_error("Missing turnout unit: " + seat.name + "/" + unit.name);
        unit.counted = match >= 0 ? booths[match].node.totalFpVotesCurrent() : 0;
        auto status = statuses.find(seat.name + "/" + unit.kind + "/" + unit.name);
        if (status == statuses.end()) throw std::logic_error("Turnout has no validated count status: " + seat.name + "/" + unit.name);
        unit.acceptedComplete = status->second.first.complete;
        observations.boothIndexes.push_back(match);
        if (match >= 0 && !matchedBooths.insert(match).second) throw std::runtime_error("Duplicate turnout booth match.");
    }
}

TurnoutObservations collectTurnoutObservations(TurnoutModel::Prior const& prior,
    Results2::Election const& currentElection, std::vector<LiveV2::Seat> const& seats,
    std::vector<LiveV2::Booth> const& booths) {
    // Keep source interpretation separate from updating the count distribution.
    if (currentElection.sourceTime.empty())
        throw std::runtime_error("Live turnout needs the current result source's timestamp. "
            "Retain timestamp metadata in this feed's loader or collector before using it for live counting.");
    TurnoutObservations observations;
    observations.current.hour = TurnoutModel::sourceHour(currentElection.sourceTime);
    auto liveSeats = inspectTurnoutDistricts(prior, currentElection, seats, observations);
    observations.units = prior.units;
    matchTurnoutBooths(prior, currentElection.liveCountStatus, seats, booths, liveSeats, observations);
    observations.current = LiveInputRecovery::freshObservation(prior,observations.units,
        currentElection.liveCountStatus,currentElection.sourceTime);
    return observations;
}

nlohmann::json describeTurnoutPreparation(LiveTurnout::Prepared const& turnout,
    nlohmann::json enrolmentRevisions, std::size_t historySize, std::string const& mapping,
    std::chrono::steady_clock::time_point started) {
    // Expose source/history decisions alongside model diagnostics without
    // making reporting metadata part of the count update itself.
    auto diagnostic = turnout.diagnostic();
    diagnostic["history_observations_used"] = historySize;
    diagnostic["observation_mapping"] = mapping;
    diagnostic["history_policy"] = "Earlier received source observations only; current checked counts replace this source time.";
    diagnostic["enrolment_policy"] = "Use fixed pre-election prior enrolment; source roll revisions do not rescale vote expectations.";
    if (!enrolmentRevisions.empty())
        logger << "Turnout retains prior enrolment after source roll revisions in "
            << enrolmentRevisions.size() << " districts.\n";
    diagnostic["enrolment_revisions"] = std::move(enrolmentRevisions);
    diagnostic["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    logger << "Turnout preparation: " << diagnostic["seconds"] << " seconds\n";
    return diagnostic;
}

std::vector<TurnoutModel::Observation> loadReceivedTurnoutHistory(std::filesystem::path const& directory,
    std::string const& election, std::string const& sourceTime, std::string const& mapping,
    TurnoutModel::Observation const& current) {
    // Start with only earlier received observations, then include this source's
    // checked current counts. The same path works with an empty live history
    // and when replaying an earlier feed after later observations were saved.
    auto history = TurnoutModelIO::loadHistory(directory, election, sourceTime, mapping);
    history.push_back(current);
    return history;
}

struct TurnoutCompositionInputs {
    std::vector<LiveTurnout::UnitComposition> units;
    std::vector<LiveTurnout::SeatComposition> seats;
    nlohmann::json projections = nlohmann::json::array();
};

TurnoutCompositionInputs collectTurnoutComposition(std::vector<LiveV2::Seat> const& seats,
    std::vector<LiveV2::Booth> const& booths, LiveTurnout::Prepared const& countAccount,
    int natPartyIndex, int independentPartyIndex) {
    // Normal FP/TCP reports supply counted party votes. Match their identities
    // to the simulation's party groups while retaining preference work still
    // owed on FP votes that have arrived ahead of their TCP count.
    TurnoutCompositionInputs inputs;
    auto& unitInputs = inputs.units;
    auto& seatInputs = inputs.seats;
    seatInputs.resize(seats.size());
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s]; auto& input = seatInputs[s]; input.name = seat.name;
        input.finalised = seat.finalResult.has_value();
        input.projectedFp = seat.node.fpVotesProjected; input.projectedTpp = seat.node.tppVotesProjected; input.projectedTcp = seat.node.tcpVotesProjected;
        input.fpConfidence = seat.node.fpConfidence; input.tppConfidence = seat.node.tppConfidence; input.tcpConfidence = seat.node.tcpConfidence;
        auto mapParty = [&](int p) { return p == seat.liveIndependentPartyIndex ? independentPartyIndex : p; };
        double expected = 0, missingPreferences = 0, pairExcess = 0;
        for (int b : seat.booths) {
            auto const& booth = booths[b];
            expected += countAccount.fpTarget(b);
            double gap = booth.node.totalFpVotesCurrent() - booth.node.totalTcpVotesCurrent();
            missingPreferences += std::max(0., gap); pairExcess += std::max(0., -gap);
            LiveTurnout::UnitComposition unit; unit.booth = b; unit.seat = s;
            if (natPartyIndex != InvalidPartyIndex) unit.tppAliases[natPartyIndex] = 1;
            unit.projectedFp = booth.node.fpVotesProjected; unit.projectedTpp = booth.node.tppVotesProjected; unit.projectedTcp = booth.node.tcpVotesProjected;
            for (auto const& [p, v] : booth.node.fpVotesCurrent) if (v > 0) { unit.fp[mapParty(p)] += v; input.fp[mapParty(p)] += v; }
            bool classic = hasClassicPair(booth.node.tcpVotesCurrent, natPartyIndex);
            auto bounds = booth.tcpLowerBounds();
            for (auto const& [p, v] : bounds) if (v > 0) {
                if (!classic && !seat.node.tcpVotesProjected.empty()) unit.tcp[mapParty(p)] += v;
                input.tcp[mapParty(p)] += v;
            }
            // The same ballots may have a measured classic scrutiny and a
            // different published TCP. Each constrains its own comparison.
            for (auto const& [p, v] : booth.tppLowerBounds(natPartyIndex)) if (v > 0) {
                int const side = p == natPartyIndex ? 1 : p;
                unit.tpp[side] += v;
                input.tpp[side] += v;
            }
            unitInputs.push_back(std::move(unit));
        }
        if (seat.finalResult) {
            // Final district summaries are independent observations, not sums
            // of recovered service records. Preserve both for diagnostics; do
            // not spread an aggregate discrepancy back into invented booth votes.
            input.fp = seat.finalResult->fp; input.tcp = seat.finalResult->tcp;
            if (!seat.finalResult->tpp.empty()) input.tpp = seat.finalResult->tpp;
        }
        if (!input.finalised && std::abs(total(input.projectedFp) - expected) > std::max(.001, expected * 1e-6))
            throw std::runtime_error("Turnout FP account does not reconcile in " + seat.name);
        inputs.projections.push_back({{"seat",seat.name},{"finalised",input.finalised},{"counted_fp",seat.node.totalFpVotesCurrent()},
            {"model_mean_fp",expected},{"projected_fp",total(input.projectedFp)},{"projected_tcp",total(input.projectedTcp)},
            {"projected_tpp",total(input.projectedTpp)},{"fp_completion",seat.node.fpCompletion},
            {"fp_without_reported_tcp",missingPreferences},{"tcp_above_reported_fp",pairExcess},
            {"fp_confidence",seat.node.fpConfidence},{"tcp_completion",seat.node.tcpCompletion},
            {"tpp_completion",seat.node.tppCompletion},{"tpp_confidence",seat.node.tppConfidence}});
    }
    return inputs;
}

nlohmann::json describeIntegratedTurnout(nlohmann::json const& previous, nlohmann::json projections) {
    // Keep source/count diagnostics and add the party-account reconciliation
    // without exposing diagnostic mutation to the preparation flow.
    auto diagnostic = previous;
    diagnostic["integrated_projections"] = std::move(projections);
    return diagnostic;
}

std::vector<double> refreshFpBoothProgress(std::vector<LiveV2::Booth>& booths,
    LiveTurnout::Prepared const& countAccount) {
    // Reported ordinary booths normally supply completed counts. Declaration
    // confidence instead follows its partial count; zero-size closed services
    // receive no evidence weight. All targets come from the same count model.
    std::vector<double> boothWeights(booths.size());
    for (std::size_t b = 0; b < booths.size(); ++b) {
        auto& booth = booths[b];
        double expected = countAccount.fpTarget(b);
        boothWeights[b] = double(expected);
        booth.node.fpCompletion = double(LiveTurnoutMath::completion(booth.node.totalFpVotesCurrent(), expected));
        if (booth.receivesFpBatches()) booth.node.fpConfidence = booth.node.fpCompletion;
        if (expected == 0) booth.node.fpConfidence = 0;
    }
    return boothWeights;
}

void aggregateFpProgress(LiveV2::Node& node, std::vector<LiveV2::Seat>& seats,
    std::vector<LiveV2::LargeRegion>& largeRegions, std::vector<LiveV2::Booth> const& booths,
    std::vector<double> const& boothWeights) {
    // Weight completion by expected votes, rather than the number of booths.
    // Confidence also retains the live model's discount for less useful matches.
    auto refresh = [](LiveV2::Node& parent, auto const& indexes, auto const& children, auto const& weights) {
        double sum = 0, complete = 0, confidence = 0;
        for (int i : indexes) {
            auto const& n = children[i].node; double w = weights[i]; sum += w;
            complete += w * n.fpCompletion;
            if (!n.fpDeviations.empty()) confidence += w * n.fpConfidence * n.relevanceModifier;
        }
        parent.fpCompletion = sum > 0 ? double(complete / sum) : 0;
        parent.fpConfidence = sum > 0 ? double(confidence / sum) : 0;
        return sum;
    };
    std::vector<double> seatWeights(seats.size()), regionWeights(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s) seatWeights[s] = refresh(seats[s].node, seats[s].booths, booths, boothWeights);
    for (std::size_t r = 0; r < largeRegions.size(); ++r) regionWeights[r] = refresh(largeRegions[r].node, largeRegions[r].seats, seats, seatWeights);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    refresh(node, regions, largeRegions, regionWeights);
}

void applyTurnoutProjections(std::vector<LiveV2::Seat>& seats, std::vector<LiveTurnout::Projection>& projected) {
    // Publish the sampled count/composition account together. TCP log odds use
    // a quarter-vote equivalent so possible zero shares remain transformable.
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto& n = seats[s].node;
        n.fpVotesProjected = std::move(projected[s].fp); n.tppVotesProjected = std::move(projected[s].tpp); n.tcpVotesProjected = std::move(projected[s].tcp);
        if (!n.tcpShares.empty()) {
            double sum = total(n.tcpVotesProjected);
            for (auto const& [p, v] : n.tcpVotesProjected) n.tcpShares[p] = double(25 * std::log((v + .25) / (sum - v + .25)));
        }
    }
}

void refreshScenarioSeatProgress(std::vector<LiveV2::Seat>& seats, LiveTurnout::Prepared const& countAccount,
    LiveTurnout::ScenarioCounts const* revised) {
    // Completion uses sampled expected counts. Preserve the evidence discount
    // measured at the mean count, including preferences inferred from FP votes.
    auto confidence = [](double value, double mean, double sampled) { return sampled > 0 ? double(std::clamp(value * mean / sampled, 0., 1.)) : 0.; };
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto& n = seats[s].node;
        double fp = total(n.fpVotesProjected), tpp = total(n.tppVotesProjected), tcp = total(n.tcpVotesProjected);
        n.fpCompletion = double(LiveTurnoutMath::completion(total(revised ? revised->fp[s] : countAccount.counted(LiveData::CountKind::Fp, s)), fp));
        n.tppCompletion = double(LiveTurnoutMath::completion(total(revised ? revised->tpp[s] : countAccount.counted(LiveData::CountKind::Tpp, s)), tpp));
        n.tcpCompletion = tcp > 0 ? double(LiveTurnoutMath::completion(total(revised ? revised->tcp[s] : countAccount.counted(LiveData::CountKind::Tcp, s)), tcp)) : 0;
        n.fpConfidence = confidence(countAccount.confidence(LiveData::CountKind::Fp, s), total(countAccount.mean(LiveData::CountKind::Fp, s)), fp);
        n.tppConfidence = confidence(countAccount.confidence(LiveData::CountKind::Tpp, s), total(countAccount.mean(LiveData::CountKind::Tpp, s)), tpp);
        n.tcpConfidence = confidence(countAccount.confidence(LiveData::CountKind::Tcp, s), total(countAccount.mean(LiveData::CountKind::Tcp, s)), tcp);
    }
}

void aggregateScenarioProgress(LiveV2::Node& node, std::vector<LiveV2::Seat> const& seats,
    std::vector<LiveV2::LargeRegion>& largeRegions) {
    // Each draw carries its own expected FP weights through the hierarchy.
    // Confidence requires usable share evidence as well as counted-vote coverage.
    auto aggregate = [&](LiveV2::Node& parent, auto const& indexes, auto const& children) {
        double sum = 0, fpComplete = 0, tppComplete = 0, fpConfidence = 0, tppConfidence = 0;
        for (int i : indexes) {
            auto const& n = children[i].node; double w = total(n.fpVotesProjected); sum += w;
            fpComplete += w * n.fpCompletion; tppComplete += w * n.tppCompletion;
            if (!n.fpDeviations.empty()) fpConfidence += w * n.fpConfidence * n.relevanceModifier;
            if (n.tppDeviation) tppConfidence += w * n.tppConfidence * n.relevanceModifier;
        }
        parent.fpCompletion = sum > 0 ? double(fpComplete / sum) : 0;
        parent.tppCompletion = sum > 0 ? double(tppComplete / sum) : 0;
        parent.fpConfidence = sum > 0 ? double(fpConfidence / sum) : 0;
        parent.tppConfidence = sum > 0 ? double(tppConfidence / sum) : 0;
    };
    for (auto& region : largeRegions) aggregate(region.node, region.seats, seats);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    aggregate(node, regions, largeRegions);
}

void refreshPairBoothProgress(std::vector<LiveV2::Booth>& booths,
    LiveTurnout::Prepared const& countAccount, int natPartyIndex) {
    // Direct classic TCP counts supply TPP evidence. Other pairings retain the
    // existing half-confidence FP-derived estimate; it is not a counted TPP batch.
    // Future FP additions and already-counted votes awaiting preferences both
    // remain in the pair target, and unavailable services contribute no weight.
    for (std::size_t b = 0; b < booths.size(); ++b) {
        auto& booth = booths[b]; auto& n = booth.node;
        double expected = countAccount.fpTarget(b);
        double measured = n.totalTcpVotesCurrent();
        if (booth.hasTcpRevision()) measured = total(booth.tcpLowerBounds());
        double pairExpected = LiveTurnoutMath::pairTarget(expected, n.totalFpVotesCurrent(), measured);
        double pairProgress = double(LiveTurnoutMath::completion(measured, pairExpected));
        n.tcpCompletion = pairProgress;
        double const tppMeasured = total(booth.tppLowerBounds(natPartyIndex));
        double const tppExpected = LiveTurnoutMath::pairTarget(expected,n.totalFpVotesCurrent(),tppMeasured);
        n.tppCompletion = LiveTurnoutMath::completion(tppMeasured,tppExpected);
        n.tppConfidence = tppMeasured > 0 ? n.tppCompletion : n.fpConfidence * .5;
        if (expected == 0) { n.tcpConfidence = 0; n.tppConfidence = 0; }
        if (n.preferenceFlowDeviation) n.preferenceFlowConfidence = std::min(n.fpConfidence, n.tppCompletion);
    }
}

void aggregatePairProgress(LiveV2::Node& node, std::vector<LiveV2::Seat>& seats,
    std::vector<LiveV2::LargeRegion>& largeRegions, std::vector<LiveV2::Booth> const& booths,
    LiveTurnout::Prepared const& countAccount) {
    // Rebuild progress at each hierarchy level without recalculating the raw
    // observed swings. Both direct and estimated evidence retain their existing
    // relevance discount; every expected booth contributes to the denominator.
    auto refresh = [](LiveV2::Node& parent, auto const& indices, auto const& children, auto const& weights) {
        double total = 0, complete = 0, confidence = 0;
        for (int index : indices) {
            auto const& child = children[index].node; double weight = weights[index];
            total += weight; complete += weight * child.tppCompletion;
            if (child.tppDeviation) confidence += weight * child.tppConfidence * child.relevanceModifier;
        }
        parent.tppCompletion = total > 0 ? double(complete / total) : 0;
        parent.tppConfidence = total > 0 ? double(confidence / total) : 0;
        return total;
    };
    std::vector<double> boothWeights(booths.size());
    for (std::size_t b = 0; b < booths.size(); ++b) boothWeights[b] = countAccount.fpTarget(b);
    std::vector<double> seatWeights(seats.size()), regionWeights(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s)
        seatWeights[s] = refresh(seats[s].node, seats[s].booths, booths, boothWeights);
    for (auto& seat : seats) if (seat.finalResult && !seat.finalResult->tpp.empty())
        seat.node.tppCompletion = seat.node.tppConfidence = 1;
    for (std::size_t r = 0; r < largeRegions.size(); ++r)
        regionWeights[r] = refresh(largeRegions[r].node, largeRegions[r].seats, seats, seatWeights);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    refresh(node, regions, largeRegions, regionWeights);
}
}

void LiveV2::Election::configureInactiveTurnoutContests(TurnoutModelIO::Artifact const& artifact) {
    // The configuration records an exceptional status separately from voter
    // behaviour. Check that each named contest still has a standard forecast seat.
    inactiveContests = artifact.inactiveContests;
    for (auto const& [name, contest] : inactiveContests)
        if (project.seats().indexByName(name) == SeatCollection::InvalidIndex)
            throw std::runtime_error("Inactive turnout contest is not a forecast seat: " + name);
}

void LiveV2::Election::prepareFinalResults(Results2::Election const& previousElection,
    Results2::Election const& currentElection) {
    // Commission candidate IDs become the same configured parties used by
    // ordinary live accounts. A principal independent retains its normal slot;
    // an unconfigured finalist uses an emerging slot so its known victory is
    // not discarded merely because the prior failed to model that candidate.
    for (auto& seat : seats) {
        auto raw = std::find_if(currentElection.seats.begin(),currentElection.seats.end(),
            [&](auto const& row) { return row.second.name == seat.name; });
        if (raw == currentElection.seats.end() || !raw->second.finalResult) continue;
        auto const& source = *raw->second.finalResult;
        auto mapCandidate = [&](int candidate) {
            int party = mapPartyId(candidate,false,previousElection,currentElection);
            if (party == seat.liveIndependentPartyIndex) return run.indPartyIndex;
            if (party >= 0 && party < project.parties().count()) return party;
            if (!source.tcp.contains(candidate)) return OthersIndex;
            return party >= IndependentPartyIdOffset ? EmergingIndIndex : EmergingPartyIndex;
        };
        LiveData::FinalSeatResult final;
        final.basis = source.basis;
        for (auto const& [c,v] : source.fp) final.fp[mapCandidate(c)] += v;
        for (auto const& [c,v] : source.tcp) final.tcp[mapCandidate(c)] += v;
        for (auto const& [coalition,v] : source.tpp) final.tpp[coalition == 1 ? 0 : 1] = v;
        if (source.winner) final.winner = mapCandidate(*source.winner);
        if (final.tcp.size() != 2)
            throw std::runtime_error("Final TCP candidates require distinct model identities in " + seat.name);
        seat.finalResult = std::move(final);
        hasFinalResults = true;
        // Keep the input seat/booth counts in their existing live identities.
        // They still supply cohort comparisons and inspectable booth evidence.
        // Ordinary configured parties usually have the same identity in both
        // accounts, but an unconfigured finalist can become Emerging Party (or
        // Emerging Independent) only in this simulator-facing final summary.
        // Replacing the input seat pair with that generic slot would make booth
        // matching search for a candidate absent from the actual booth records.
        // recomposeSeat* and the finite-count handoff separately install the
        // authoritative final projections/counts, so the forecast still uses
        // the final totals, pairing and winner without modelling them again.
    }
}

void LiveV2::Election::prepareTurnout(Results2::Election const& currentElection, TurnoutModelIO::Artifact artifact) {
    // This is the only live-to-count-model input boundary. Collect checked
    // observations, update the fixed prior, then retain the observation and
    // diagnostics. Party composition is prepared separately.
    auto started = std::chrono::steady_clock::now();
    auto observations = collectTurnoutObservations(artifact.prior, currentElection, seats, readBooths());
    // Usually the current positive account is sufficient. WA mixed early places
    // need their accepted batch sequence; empty public/Office accounts also need
    // to know whether a positive return was subsequently withdrawn. The latter
    // must not be mistaken for an unused preload. Reuse the compact change index,
    // querying only through this snapshot's source time, including on replays.
    bool unreportedPlaces = artifact.unreportedPlaces.value_or(artifact.decayPpvc);
    // No empty-unit adjustment runs before polling close or before any positive
    // FP. Avoid loading every district's empty history for a preload. The index
    // still retains those accepted sources for subsequent queries when needed.
    bool emptyHistoryNeeded = unreportedPlaces && artifact.pollClose
        && TurnoutModel::sourceHour(currentElection.sourceTime) > *artifact.pollClose
        && std::any_of(observations.units.begin(),observations.units.end(),[](auto const& unit) { return unit.counted > 0; });
    if (artifact.prior.election.ends_with("wa") || emptyHistoryNeeded) {
        std::map<std::string,std::set<std::string>> selected;
        for (auto const& unit : observations.units) {
            bool mixedWa = artifact.prior.election.ends_with("wa") && unit.kind == "ppvc" && unit.incremental;
            bool emptyPlace = emptyHistoryNeeded && unit.counted == 0 && !unit.closed && !unit.excluded
                && !observations.finalised[unit.seat]
                && UnreportedPlaceCounting::role(unit.kind,unit.name,unit.eav,unit.incremental)
                    != UnreportedPlaceCounting::Role::None;
            if (!mixedWa && !emptyPlace) continue;
            auto const& district = artifact.prior.seats[unit.seat];
            selected[district].insert(district+"/"+unit.kind+"/"+unit.name);
        }
        if (!selected.empty()) {
            auto changes = LiveInputRecovery::loadFpTotalHistory(
                LiveInputRecovery::directory(project.paths().root(),run.getTermCode()),
                run.getTermCode(),currentElection.sourceTime,selected);
            for (auto& unit : observations.units) {
                auto key = artifact.prior.seats[unit.seat]+"/"+unit.kind+"/"+unit.name;
                if (changes.contains(key)) unit.receivedFpTotals = std::move(changes.at(key));
            }
        }
    }
    auto directory = TurnoutModelIO::historyDirectory(project.paths().root(), run.getTermCode());
    auto mapping = TurnoutModelIO::observationMapping(artifact.prior);
    auto history = loadReceivedTurnoutHistory(directory, run.getTermCode(), currentElection.sourceTime,
        mapping, observations.current);
    turnout = LiveTurnout::Prepared::prepare(std::move(artifact), std::move(observations.units),
        std::move(observations.boothIndexes), readBooths().size(), observations.finalised, history, currentElection.sourceTime);
    // Reporting coverage uses the accepted source counts, not confidence-
    // discounted evidence or sampled positive/negative count revisions. Freeze
    // its denominator at the prepared mean final formal turnout for all services
    // represented in this forecast. Scenario copies keep this same display value.
    countProgress = currentElection.reportedCountProgress;
    if (countProgress) {
        std::map<std::string,double> finalTotals;
        for (auto const& [id,raw] : currentElection.seats) if (raw.finalResult)
            finalTotals[raw.name] = total(raw.finalResult->fp);
        for (std::size_t b = 0; b < readBooths().size(); ++b)
            if (!finalTotals.contains(seats.at(readBooths()[b].parentSeatId).name))
                countProgress->expectedFormalVotes += turnout->fpTarget(b);
        for (auto const& [name,votes] : finalTotals) countProgress->expectedFormalVotes += votes;
        if (!(countProgress->expectedFormalVotes > 0)) countProgress.reset();
    }
    // Persist only received measured counts. It is safe to start with an empty
    // directory during a live election; future replay sources are filtered out.
    run.pendingLiveCommits.push_back([directory, election = run.getTermCode(), time = currentElection.sourceTime,
        observation = observations.current, mapping] {
        TurnoutModelIO::recordObservation(directory, election, time, observation, mapping);
    });
    auto diagnostic = describeTurnoutPreparation(*turnout, std::move(observations.enrolmentRevisions), history.size(), mapping, started);
    turnoutDiagnostic = std::make_shared<nlohmann::json const>(std::move(diagnostic));
}

double LiveV2::Election::turnoutFpTarget(int boothIndex) const {
    double expected = turnout->fpTarget(boothIndex);
    LiveTurnoutMath::remaining(expected, readBooths().at(boothIndex).node.totalFpVotesCurrent());
    return double(expected);
}
double LiveV2::Election::turnoutPairTarget(int boothIndex) const {
    auto const& booth = readBooths().at(boothIndex);
    return double(LiveTurnoutMath::pairTarget(turnout->fpTarget(boothIndex),
        booth.node.totalFpVotesCurrent(), booth.hasTcpRevision() ? booth.node.totalFpVotesCurrent() : booth.node.totalTcpVotesCurrent()));
}

std::optional<LiveData::VoteCountAccount> LiveV2::Election::getSeatVoteCountAccount(
    std::string const& seatName, LiveData::CountKind kind) const {
    // Before any results, the ordinary simulation owns the whole forecast.
    // There is no observed count constraint to hand off at this stage.
    if (node.totalFpVotesCurrent() == 0 || inactiveContests.contains(seatName)) return {};
    std::size_t s = turnout->seatIndex(seatName);
    auto const& n = seats.at(s).node;
    if (kind == LiveData::CountKind::Fp) return LiveData::VoteCountAccount{scenarioCountedVotes ? &scenarioCountedVotes->fp[s] : &turnout->counted(kind, s), &n.fpVotesProjected};
    if (kind == LiveData::CountKind::Tpp && n.tppVotesProjected.size() == 2)
        return LiveData::VoteCountAccount{scenarioCountedVotes ? &scenarioCountedVotes->tpp[s] : &turnout->counted(kind, s), &n.tppVotesProjected};
    if (kind == LiveData::CountKind::Tcp && n.tcpVotesProjected.size() == 2)
        return LiveData::VoteCountAccount{scenarioCountedVotes ? &scenarioCountedVotes->tcp[s] : &turnout->counted(kind, s), &n.tcpVotesProjected};
    return {};
}

LiveData::FinalSeatResult const* LiveV2::Election::getFinalSeatResult(std::string const& seatName) const {
    // Election-night sources normally have no final districts. Avoid an extra
    // scan of every seat for every iteration; otherwise use the existing index.
    if (!hasFinalResults || inactiveContests.contains(seatName)) return nullptr;
    auto const& final = seats.at(turnout->seatIndex(seatName)).finalResult;
    return final ? &*final : nullptr;
}

std::optional<LiveData::AggregateVoteTotals> LiveV2::Election::getSeatAggregateVoteTotals(
    std::string const& seatName) const {
    // Aggregation needs sizes even in unreported districts, rather than just
    // received counts. Preparation holds mean projections; scenario generation
    // replaces them with its existing turnout draw. Summing those same party
    // accounts also retains any sampled count revisions and OPV exhaustion.
    // Inactive/postponed contests have no current-election vote account.
    if (inactiveContests.contains(seatName)) return LiveData::AggregateVoteTotals{};
    auto const& n = seats.at(turnout->seatIndex(seatName)).node;
    return LiveData::AggregateVoteTotals{total(n.fpVotesProjected),total(n.tppVotesProjected)};
}

void LiveV2::Election::refreshTurnoutFpProgress() {
    // The same target counts determine vote composition and completion. Update
    // booth evidence first, then combine it at district and election levels.
    auto weights = refreshFpBoothProgress(preparationBooths(), *turnout);
    aggregateFpProgress(node, seats, largeRegions, readBooths(), weights);
}

void LiveV2::Election::prepareTurnoutComposition() {
    // Translate checked party accounts, attach their reusable count responses,
    // then record reconciliation diagnostics before simulation copies share them.
    auto inputs = collectTurnoutComposition(seats, readBooths(), *turnout, natPartyIndex, run.indPartyIndex);
    turnout = turnout->withComposition(inputs.units, inputs.seats);
    auto diagnostic = *turnoutDiagnostic;
    diagnostic["count_revisions"] = turnout->revisionDiagnostic();
    turnoutDiagnostic = std::make_shared<nlohmann::json const>(
        describeIntegratedTurnout(diagnostic, std::move(inputs.projections)));
}

void LiveV2::Election::drawTurnoutCounts(int iterationIndex) {
    if (iterationIndex < 0) throw std::runtime_error("Negative turnout iteration index.");
    // Before any counts arrive the standard simulation supplies the forecast.
    // Once counting starts, use a reproducible draw and publish its party totals.
    if (node.totalFpVotesCurrent() == 0) return;
    auto seed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0xa0761d6478bd642fULL, std::uint64_t(iterationIndex));
    auto projected = turnout->draw(seed);
    applyTurnoutProjections(seats, projected);
}

void LiveV2::Election::refreshTurnoutScenarioProgress() {
    // Counts, completion and evidence confidence must refer to the same draw.
    // Update districts before combining their evidence at broader levels.
    refreshScenarioSeatProgress(seats, *turnout, scenarioCountedVotes.get());
    aggregateScenarioProgress(node, seats, largeRegions);
}

void LiveV2::Election::applyCountRevisions(int iterationIndex) {
    if (node.totalFpVotesCurrent() == 0) return;
    // Finish the scenario's account after ordinary share variation and existing
    // preference-only corrections. A correction can lower a reported total;
    // it does not create a negative outstanding pool or edit source history.
    auto adjusted = std::make_shared<LiveTurnout::ScenarioCounts>(
        scenarioCountedVotes ? *scenarioCountedVotes : turnout->scenarioCounts());
    std::vector<LiveTurnout::Projection> projected;
    projected.reserve(seats.size());
    for (auto& seat : seats) projected.push_back({std::move(seat.node.fpVotesProjected),
        std::move(seat.node.tppVotesProjected),std::move(seat.node.tcpVotesProjected)});
    auto seed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0x436f756e74526576ULL,std::uint64_t(iterationIndex));
    turnout->revise(seed,projected,*adjusted);
    applyTurnoutProjections(seats,projected);
    scenarioCountedVotes = std::move(adjusted);
}

void LiveV2::Election::refreshTurnoutPairProgress() {
    // A complete FP report normally also has its final-pair count. When it
    // does not, keep the outstanding preference work in the pair denominator.
    refreshPairBoothProgress(preparationBooths(), *turnout, natPartyIndex);
    aggregatePairProgress(node, seats, largeRegions, readBooths(), *turnout);
}
