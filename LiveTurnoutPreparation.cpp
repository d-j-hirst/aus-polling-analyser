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
}

void LiveV2::Election::prepareTurnout(Results2::Election const& currentElection) {
    // This is the only live-to-count-model input boundary. The prior contains
    // pre-election expectations and service identities; checked current FP
    // counts supply observations. Party composition is prepared separately.
    auto started = std::chrono::steady_clock::now();
    auto artifact = TurnoutModelIO::loadForElection(project.paths().root(), run.getTermCode());
    auto const& prior = artifact.prior;
    if (currentElection.sourceTime.empty())
        throw std::runtime_error("Live turnout needs the current result source's timestamp. "
            "Retain timestamp metadata in this feed's loader or collector before using it for live counting.");
    double now = TurnoutModel::sourceHour(currentElection.sourceTime);
    std::map<std::string, std::size_t> liveSeats;
    for (std::size_t s = 0; s < seats.size(); ++s) liveSeats.emplace(seats[s].name, s);
    if (liveSeats.size() != prior.seats.size()) throw std::runtime_error("Turnout prior and live district populations differ.");
    auto units = prior.units;
    std::vector<int> boothIndexes;
    std::vector<bool> finalised(prior.seats.size());
    std::set<int> matchedBooths;
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        if (!liveSeats.contains(prior.seats[s])) throw std::runtime_error("Turnout district is absent: " + prior.seats[s]);
        auto raw = std::find_if(currentElection.seats.begin(), currentElection.seats.end(), [&](auto const& row) { return row.second.name == prior.seats[s]; });
        if (raw == currentElection.seats.end()) throw std::runtime_error("Turnout has no raw district account.");
        if (raw->second.enrolment > 0 && raw->second.enrolment != prior.enrolment[s])
            throw std::runtime_error("Turnout enrolment changed; regenerate the prior: " + prior.seats[s]);
        finalised[s] = raw->second.fpFinalised;
    }
    TurnoutModel::Observation current; current.hour = now;
    for (auto& unit : units) {
        auto const& seat = seats[liveSeats.at(prior.seats[unit.seat])];
        int match = -1;
        for (int index : seat.booths) {
            auto const& booth = booths[index];
            if (booth.name != unit.name) continue;
            bool declaration = booth.voteType != Results2::VoteType::Ordinary;
            bool ppvc = !declaration && booth.boothType == Results2::Booth::Type::Ppvc;
            if (declaration != (unit.kind == "declaration") || (!declaration && ppvc != (unit.kind == "ppvc")))
                throw std::runtime_error("Turnout booth role changed: " + seat.name + "/" + unit.name);
            if (match != -1) throw std::runtime_error("Ambiguous turnout booth: " + seat.name + "/" + unit.name);
            match = index;
        }
        // A missing service is not a measured zero. Only a recorded closure
        // permits its identity to be absent from the current feed.
        if (match == -1 && !unit.closed) throw std::runtime_error("Missing turnout unit: " + seat.name + "/" + unit.name);
        unit.counted = match >= 0 ? booths[match].node.totalFpVotesCurrent() : 0;
        boothIndexes.push_back(match);
        if (match >= 0 && !matchedBooths.insert(match).second) throw std::runtime_error("Duplicate turnout booth match.");
        if (unit.kind == "declaration") current.counts[{seat.name, unit.category}] += unit.counted;
        if (prior.groups[unit.group] != "postal")
            current.counts[{seat.name, "allocation:" + prior.groups[unit.group]}] += unit.counted;
    }
    auto directory = TurnoutModelIO::historyDirectory(project.paths().root(), run.getTermCode());
    auto mapping = TurnoutModelIO::observationMapping(prior);
    auto history = TurnoutModelIO::loadHistory(directory, run.getTermCode(), currentElection.sourceTime, mapping);
    history.push_back(current);
    turnout = LiveTurnout::Prepared::prepare(std::move(artifact), std::move(units), std::move(boothIndexes),
        booths.size(), finalised, history, currentElection.sourceTime);
    // Persist only received measured counts. It is safe to start with an empty
    // directory during a live election; future replay sources are filtered out.
    TurnoutModelIO::recordObservation(directory, run.getTermCode(), currentElection.sourceTime, current, mapping);
    auto diagnostic = turnout->diagnostic();
    diagnostic["history_observations_used"] = history.size();
    diagnostic["observation_mapping"] = mapping;
    diagnostic["history_policy"] = "Earlier received source observations only; current checked counts replace this source time.";
    diagnostic["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    logger << "Turnout preparation: " << diagnostic["seconds"] << " seconds\n";
    turnoutDiagnostic = std::make_shared<nlohmann::json const>(std::move(diagnostic));
}

float LiveV2::Election::turnoutFpTarget(int boothIndex) const {
    double expected = turnout->fpTarget(boothIndex);
    LiveTurnoutMath::remaining(expected, booths.at(boothIndex).node.totalFpVotesCurrent());
    return float(expected);
}
float LiveV2::Election::turnoutPairTarget(int boothIndex) const {
    auto const& booth = booths.at(boothIndex);
    return float(LiveTurnoutMath::pairTarget(turnout->fpTarget(boothIndex),
        booth.node.totalFpVotesCurrent(), booth.node.totalTcpVotesCurrent()));
}

std::optional<LiveData::VoteCountAccount> LiveV2::Election::getSeatVoteCountAccount(
    std::string const& seatName, LiveData::CountKind kind) const {
    // Before any results, the ordinary simulation owns the whole forecast.
    // There is no observed count constraint to hand off at this stage.
    if (node.totalFpVotesCurrent() == 0) return {};
    std::size_t s = turnout->seatIndex(seatName);
    auto const& n = seats.at(s).node;
    if (kind == LiveData::CountKind::Fp) return LiveData::VoteCountAccount{&turnout->counted(kind, s), &n.fpVotesProjected};
    if (kind == LiveData::CountKind::Tpp && n.tppVotesProjected.size() == 2)
        return LiveData::VoteCountAccount{preferenceScenarioCounts ? &preferenceScenarioCounts->tpp[s] : &turnout->counted(kind, s), &n.tppVotesProjected};
    if (kind == LiveData::CountKind::Tcp && n.tcpVotesProjected.size() == 2)
        return LiveData::VoteCountAccount{preferenceScenarioCounts ? &preferenceScenarioCounts->tcp[s] : &turnout->counted(kind, s), &n.tcpVotesProjected};
    return {};
}

void LiveV2::Election::refreshTurnoutFpProgress() {
    // Completion and evidence weights use the same expected count that party
    // recomposition uses. Closed services contribute zero; declarations keep
    // their measured FP evidence separate from outstanding pair preferences.
    std::vector<float> boothWeights(booths.size());
    for (std::size_t b = 0; b < booths.size(); ++b) {
        auto& booth = booths[b];
        double expected = turnout->fpTarget(b);
        boothWeights[b] = float(expected);
        booth.node.fpCompletion = float(LiveTurnoutMath::completion(booth.node.totalFpVotesCurrent(), expected));
        if (booth.voteType != Results2::VoteType::Ordinary) booth.node.fpConfidence = booth.node.fpCompletion;
        if (expected == 0) booth.node.fpConfidence = 0;
    }
    auto refresh = [](Node& parent, auto const& indexes, auto const& children, auto const& weights) {
        double sum = 0, complete = 0, confidence = 0;
        for (int i : indexes) {
            auto const& n = children[i].node; double w = weights[i]; sum += w;
            complete += w * n.fpCompletion;
            if (!n.fpDeviations.empty()) confidence += w * n.fpConfidence * n.relevanceModifier;
        }
        parent.fpCompletion = sum > 0 ? float(complete / sum) : 0;
        parent.fpConfidence = sum > 0 ? float(confidence / sum) : 0;
        return sum;
    };
    std::vector<double> seatWeights(seats.size()), regionWeights(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s) seatWeights[s] = refresh(seats[s].node, seats[s].booths, booths, boothWeights);
    for (std::size_t r = 0; r < largeRegions.size(); ++r) regionWeights[r] = refresh(largeRegions[r].node, largeRegions[r].seats, seats, seatWeights);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    refresh(node, regions, largeRegions, regionWeights);
}

void LiveV2::Election::prepareTurnoutComposition() {
    // Translate LiveV2's candidate identities once. The portable turnout
    // component owns subsequent count draws, constraints and composition
    // variation, so other parts of the live model do not manage those caches.
    std::vector<LiveTurnout::UnitComposition> unitInputs;
    std::vector<LiveTurnout::SeatComposition> seatInputs(seats.size());
    auto diagnostic = *turnoutDiagnostic;
    diagnostic["integrated_projections"] = nlohmann::json::array();
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s]; auto& input = seatInputs[s]; input.name = seat.name;
        input.projectedFp = seat.node.fpVotesProjected; input.projectedTpp = seat.node.tppVotesProjected; input.projectedTcp = seat.node.tcpVotesProjected;
        input.fpConfidence = seat.node.fpConfidence; input.tppConfidence = seat.node.tppConfidence; input.tcpConfidence = seat.node.tcpConfidence;
        auto mapParty = [&](int p) { return p == seat.liveIndependentPartyIndex ? run.indPartyIndex : p; };
        double expected = 0, missingPreferences = 0, pairExcess = 0;
        for (int b : seat.booths) {
            auto const& booth = booths[b];
            expected += turnout->fpTarget(b);
            double gap = booth.node.totalFpVotesCurrent() - booth.node.totalTcpVotesCurrent();
            missingPreferences += std::max(0., gap); pairExcess += std::max(0., -gap);
            LiveTurnout::UnitComposition unit; unit.booth = b; unit.seat = s;
            unit.projectedFp = booth.node.fpVotesProjected; unit.projectedTpp = booth.node.tppVotesProjected; unit.projectedTcp = booth.node.tcpVotesProjected;
            for (auto const& [p, v] : booth.node.fpVotesCurrent) if (v > 0) { unit.fp[mapParty(p)] += v; input.fp[mapParty(p)] += v; }
            bool classic = hasClassicPair(booth.node.tcpVotesCurrent, natPartyIndex);
            for (auto const& [p, v] : booth.node.tcpVotesCurrent) if (v > 0) {
                if (classic) { unit.tpp[p == natPartyIndex ? 1 : p] += v; input.tpp[p == natPartyIndex ? 1 : p] += v; }
                else if (!seat.node.tcpVotesProjected.empty()) unit.tcp[mapParty(p)] += v;
            }
            unitInputs.push_back(std::move(unit));
        }
        for (auto const& [p, v] : seat.node.tcpVotesCurrent) if (v > 0) input.tcp[mapParty(p)] += v;
        if (std::abs(total(input.projectedFp) - expected) > std::max(.001, expected * 1e-6))
            throw std::runtime_error("Turnout FP account does not reconcile in " + seat.name);
        diagnostic["integrated_projections"].push_back({{"seat",seat.name},{"counted_fp",seat.node.totalFpVotesCurrent()},
            {"model_mean_fp",expected},{"projected_fp",total(input.projectedFp)},{"projected_tcp",total(input.projectedTcp)},
            {"projected_tpp",total(input.projectedTpp)},{"fp_completion",seat.node.fpCompletion},
            {"fp_without_reported_tcp",missingPreferences},{"tcp_above_reported_fp",pairExcess},
            {"fp_confidence",seat.node.fpConfidence},{"tcp_completion",seat.node.tcpCompletion},
            {"tpp_completion",seat.node.tppCompletion},{"tpp_confidence",seat.node.tppConfidence}});
    }
    turnout = turnout->withComposition(unitInputs, seatInputs);
    turnoutDiagnostic = std::make_shared<nlohmann::json const>(std::move(diagnostic));
}

void LiveV2::Election::drawTurnoutCounts(int iterationIndex) {
    if (iterationIndex < 0) throw std::runtime_error("Negative turnout iteration index.");
    // No measured votes means exact agreement with the standard simulation.
    if (node.totalFpVotesCurrent() == 0) return;
    auto seed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0xa0761d6478bd642fULL, std::uint64_t(iterationIndex));
    auto projected = turnout->draw(seed);
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto& n = seats[s].node;
        n.fpVotesProjected = std::move(projected[s].fp); n.tppVotesProjected = std::move(projected[s].tpp); n.tcpVotesProjected = std::move(projected[s].tcp);
        if (!n.tcpShares.empty()) {
            double sum = total(n.tcpVotesProjected);
            for (auto const& [p, v] : n.tcpVotesProjected) n.tcpShares[p] = float(25 * std::log((v + .25) / (sum - v + .25)));
        }
    }
}

void LiveV2::Election::refreshTurnoutScenarioProgress() {
    // Sampled counts and completion travel together to the forecast handoff.
    // Retain the mean preparation's evidence discount for inferred preferences.
    auto confidence = [](double value, double mean, double sampled) { return sampled > 0 ? float(std::clamp(value * mean / sampled, 0., 1.)) : 0.f; };
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto& n = seats[s].node;
        double fp = total(n.fpVotesProjected), tpp = total(n.tppVotesProjected), tcp = total(n.tcpVotesProjected);
        n.fpCompletion = float(LiveTurnoutMath::completion(total(turnout->counted(LiveData::CountKind::Fp, s)), fp));
        n.tppCompletion = float(LiveTurnoutMath::completion(total(turnout->counted(LiveData::CountKind::Tpp, s)), tpp));
        n.tcpCompletion = tcp > 0 ? float(LiveTurnoutMath::completion(total(turnout->counted(LiveData::CountKind::Tcp, s)), tcp)) : 0;
        n.fpConfidence = confidence(turnout->confidence(LiveData::CountKind::Fp, s), total(turnout->mean(LiveData::CountKind::Fp, s)), fp);
        n.tppConfidence = confidence(turnout->confidence(LiveData::CountKind::Tpp, s), total(turnout->mean(LiveData::CountKind::Tpp, s)), tpp);
        n.tcpConfidence = confidence(turnout->confidence(LiveData::CountKind::Tcp, s), total(turnout->mean(LiveData::CountKind::Tcp, s)), tcp);
    }
    auto aggregate = [&](Node& parent, auto const& indexes, auto const& children) {
        double sum = 0, fpComplete = 0, tppComplete = 0, fpConfidence = 0, tppConfidence = 0;
        for (int i : indexes) {
            auto const& n = children[i].node; double w = total(n.fpVotesProjected); sum += w;
            fpComplete += w * n.fpCompletion; tppComplete += w * n.tppCompletion;
            if (!n.fpDeviations.empty()) fpConfidence += w * n.fpConfidence * n.relevanceModifier;
            if (n.tppDeviation) tppConfidence += w * n.tppConfidence * n.relevanceModifier;
        }
        parent.fpCompletion = sum > 0 ? float(fpComplete / sum) : 0;
        parent.tppCompletion = sum > 0 ? float(tppComplete / sum) : 0;
        parent.fpConfidence = sum > 0 ? float(fpConfidence / sum) : 0;
        parent.tppConfidence = sum > 0 ? float(tppConfidence / sum) : 0;
    };
    for (auto& region : largeRegions) aggregate(region.node, region.seats, seats);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    aggregate(node, regions, largeRegions);
}

void LiveV2::Election::refreshTurnoutPairProgress() {
    // All three projections share the future FP addition pool. Pair completion
    // also includes preferences still missing on already-counted FP votes, so
    // a partially reported batch cannot claim completion solely because FP is
    // complete. FP-derived TPP retains the existing half-confidence treatment
    // and is not promoted to a counted TPP batch.
    for (std::size_t b = 0; b < booths.size(); ++b) {
        auto& booth = booths[b]; auto& n = booth.node;
        double expected = turnout->fpTarget(b);
        bool classic = hasClassicPair(n.tcpVotesCurrent, natPartyIndex) && n.totalTcpVotesCurrent() > 0;
        double pairExpected = LiveTurnoutMath::pairTarget(expected, n.totalFpVotesCurrent(), n.totalTcpVotesCurrent());
        float pairProgress = float(LiveTurnoutMath::completion(n.totalTcpVotesCurrent(), pairExpected));
        n.tcpCompletion = pairProgress;
        n.tppCompletion = classic ? pairProgress : 0;
        if (classic) n.tppConfidence = pairProgress;
        else if (!classic) n.tppConfidence = n.fpConfidence * .5f;
        if (expected == 0) { n.tcpConfidence = 0; n.tppConfidence = 0; }
        if (n.preferenceFlowDeviation) n.preferenceFlowConfidence = std::min(n.fpConfidence, n.tppCompletion);
    }
    // Rebuild progress at each hierarchy level without recalculating the raw
    // observed swings. Both direct and estimated evidence retain their existing
    // relevance discount; every expected booth contributes to the denominator.
    auto refresh = [](Node& parent, auto const& indices, auto const& children, auto const& weights) {
        double total = 0, complete = 0, confidence = 0;
        for (int index : indices) {
            auto const& child = children[index].node; double weight = weights[index];
            total += weight; complete += weight * child.tppCompletion;
            if (child.tppDeviation) confidence += weight * child.tppConfidence * child.relevanceModifier;
        }
        parent.tppCompletion = total > 0 ? float(complete / total) : 0;
        parent.tppConfidence = total > 0 ? float(confidence / total) : 0;
        return total;
    };
    std::vector<double> boothWeights(booths.size());
    for (std::size_t b = 0; b < booths.size(); ++b) boothWeights[b] = turnout->fpTarget(b);
    std::vector<double> seatWeights(seats.size()), regionWeights(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s)
        seatWeights[s] = refresh(seats[s].node, seats[s].booths, booths, boothWeights);
    for (std::size_t r = 0; r < largeRegions.size(); ++r)
        regionWeights[r] = refresh(largeRegions[r].node, largeRegions[r].seats, seats, seatWeights);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    refresh(node, regions, largeRegions, regionWeights);
}

