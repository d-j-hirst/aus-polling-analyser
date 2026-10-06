#include "LiveV2.h"
#include "LiveResultsInput.h"
#include "PollingProject.h"
#include "SimulationRun.h"
#include "TurnoutModelIO.h"
#include "LiveTurnoutMath.h"
#include "RandomGenerator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
bool hasClassicPair(std::map<int, int> const& votes, int nationals) {
    return votes.size() == 2 && votes.count(0) && (votes.count(1) || votes.count(nationals));
}
}

// The bridge attaches LiveV2's checked counts to the independent count model.
// An election with a prepared prior uses counts mode automatically. Shadow mode
// records a comparison without installing the new count estimates. Selection
// happens once for the live election; scenario copies share the prepared account.
// Party composition still uses the reduced LiveV2 preparation.
void LiveV2::Election::prepareTurnoutShadow(Results2::Election const& currentElection) {
    auto setting = std::getenv("POLLING_ANALYSER_TURNOUT_SHADOW");
    auto modeSetting = std::getenv("POLLING_ANALYSER_TURNOUT_MODE");
    std::optional<std::filesystem::path> overridePath;
    if (setting && *setting && std::string(setting) != "auto")
        overridePath = LiveResultsInput::pathFromUtf8(setting);
    auto selected = TurnoutModelIO::select(run.project.paths().root(),run.getTermCode(),
        modeSetting && *modeSetting ? modeSetting : "",overridePath);
    std::string const& mode = selected.mode;
    if (!selected.enabled) {
        // Missing priors are normal for elections not yet prepared. Record the
        // exact expected path so this fallback is visible during replay checks.
        if (mode != "off") logger << "No turnout prior for " << run.getTermCode()
            << "; retaining existing live vote-size rules. Expected file: " << selected.path.string() << "\n";
        return;
    }
    using json = nlohmann::json;
    auto started = std::chrono::steady_clock::now();
    auto artifact = TurnoutModelIO::load(selected.path);
    auto const& prior = artifact.prior;
    // Each turnout file contains one election's seats and vote-count prior.
    // Report the selected file and both election codes so a retained debugger
    // setting can be corrected without inspecting an optimized call stack.
    if (prior.election != run.getTermCode())
        throw std::runtime_error("Turnout file is for " + prior.election
            + ", but the live run is " + run.getTermCode() + ". File: " + LiveResultsInput::pathToUtf8(selected.path));
    if (currentElection.sourceTime.empty()) throw std::runtime_error("Turnout shadow needs the feed's source timestamp.");
    double now = TurnoutModel::sourceHour(currentElection.sourceTime);
    std::map<std::string, std::size_t> liveSeats;
    for (std::size_t s = 0; s < seats.size(); ++s) liveSeats.emplace(seats[s].name,s);
    if (liveSeats.size() != prior.seats.size()) throw std::runtime_error("Turnout shadow and live district populations differ.");
    auto units = prior.units;
    std::vector<int> boothIndexes;
    std::vector<bool> finalised(prior.seats.size());
    std::set<int> matchedBooths;
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        if (!liveSeats.contains(prior.seats[s])) throw std::runtime_error("Turnout shadow district is absent: "+prior.seats[s]);
        auto raw = std::find_if(currentElection.seats.begin(),currentElection.seats.end(),[&](auto const& row) { return row.second.name == prior.seats[s]; });
        if (raw == currentElection.seats.end()) throw std::runtime_error("Turnout shadow has no raw district account.");
        if (raw->second.enrolment > 0 && raw->second.enrolment != prior.enrolment[s])
            throw std::runtime_error("Turnout shadow enrolment changed; regenerate its prior: "+prior.seats[s]);
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
                throw std::runtime_error("Turnout shadow booth role changed: "+seat.name+"/"+unit.name);
            if (match != -1) throw std::runtime_error("Ambiguous turnout shadow booth: "+seat.name+"/"+unit.name);
            match = index;
        }
        // Only an explicitly known closed identity may be absent. A feed that
        // has no record for an expected service is not an observed zero count.
        if (match == -1 && !unit.closed) throw std::runtime_error("Missing turnout shadow unit: "+seat.name+"/"+unit.name);
        unit.counted = match >= 0 ? booths[match].node.totalFpVotesCurrent() : 0;
        boothIndexes.push_back(match);
        if (match >= 0 && !matchedBooths.insert(match).second) throw std::runtime_error("Duplicate turnout shadow booth match.");
        if (unit.kind == "declaration") current.counts[{seat.name,unit.category}] += unit.counted;
        // Mixed historical groups need their entire observed count, including
        // ordinary and PPVC booths. Postal already has its exact category total
        // above; adding it twice would corrupt both progress measurements.
        if (prior.groups[unit.group] != "postal")
            current.counts[{seat.name,"allocation:"+prior.groups[unit.group]}] += unit.counted;
    }
    // Detect a new/omitted reporting identity rather than dropping its votes.
    // The explicit prior mapping is part of the model's evidence contract.
    for (std::size_t b = 0; b < booths.size(); ++b) if (!matchedBooths.count(int(b)) && booths[b].node.totalFpVotesCurrent() > 0)
        throw std::runtime_error("Counted booth absent from turnout shadow prior: "+booths[b].name);
    std::vector<TurnoutModel::Observation> history;
    for (auto const& h : artifact.history) if (h.hour < now) history.push_back(h);
    history.push_back(std::move(current)); // current GUI counts replace this source time.
    auto options = artifact.options;
    if (artifact.decayPpvc) options.ppvcFactor = TurnoutModel::reportingFactor(now-*artifact.pollClose);
    auto result = std::make_shared<TurnoutModel::Result>(TurnoutModel::prepare(prior,units,finalised,history,options));
    json diagnostic = TurnoutModelIO::diagnostic(artifact,units,*result,currentElection.sourceTime);
    diagnostic["artifact_path"] = LiveResultsInput::pathToUtf8(selected.path);
    diagnostic["artifact_selection"] = selected.automatic ? "automatic" : "explicit_override";
    diagnostic["ppvc_reporting_factor"] = options.ppvcFactor;
    diagnostic["history_observations_used"] = history.size();
    diagnostic["history_policy"] = "Earlier source observations only; current LiveV2 counts replace the current source time.";

    // Mechanical FP comparison: preserve every counted party vote, and use the
    // existing LiveV2 remaining-vote mix for the changed remaining amount. Where
    // the legacy model expects no additions, its overall projected mix supplies
    // the composition assumption. This exposes count effects, not a new fit of
    // late-voter preferences or a combined forecast uncertainty distribution.
    std::vector<std::map<int,double>> projected(prior.seats.size());
    std::vector<bool> supported(prior.seats.size(),true);
    json unsupported = json::array();
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& u = units[j]; int index = boothIndexes[j];
        if (index < 0) continue;
        auto const& booth = booths[index];
        double addition = result->unitMeans[j]-u.counted;
        std::map<int,double> mix, counted;
        bool validMix = true;
        auto const& seat = seats[booth.parentSeatId];
        for (auto const& [party,votes] : booth.node.fpVotesCurrent) {
            int effective = party == seat.liveIndependentPartyIndex ? run.indPartyIndex : party;
            counted[effective] += votes;
        }
        double denominator = 0;
        for (auto const& [party,votes] : booth.node.fpVotesProjected) {
            double remaining = votes-counted[party];
            if (remaining < -1e-3 && addition > 1e-6) { validMix = false; continue; }
            mix[party] = std::max(0.,remaining); denominator += mix[party];
        }
        if (denominator <= 1e-6) {
            mix.clear(); denominator = 0;
            for (auto const& [party,votes] : booth.node.fpVotesProjected) { mix[party] = votes; denominator += votes; }
        }
        if (addition > 1e-6 && (denominator <= 0 || !validMix)) {
            supported[u.seat] = false; unsupported.push_back(prior.seats[u.seat]+"/"+u.name);
        }
        for (auto const& [party,votes] : counted) projected[u.seat][party] += votes;
        if (denominator > 0) for (auto const& [party,share] : mix) projected[u.seat][party] += addition*share/denominator;
    }
    diagnostic["fp_count_effects"] = json::array();
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        if (!supported[s]) continue;
        auto const& seat = seats[liveSeats.at(prior.seats[s])];
        double total = 0, legacyTotal = seat.node.totalFpVotesProjected();
        for (auto const& [party,votes] : projected[s]) total += votes;
        json row{{"seat",prior.seats[s]},{"legacy_total",legacyTotal},{"shadow_total",total}};
        row["parties"] = json::array();
        std::set<int> parties;
        for (auto const& [party,_] : projected[s]) parties.insert(party);
        for (auto const& [party,_] : seat.node.fpVotesProjected) parties.insert(party);
        for (int party : parties) {
            double share = total > 0 ? 100*projected[s][party]/total : 0;
            double legacyShare = legacyTotal > 0 && seat.node.fpVotesProjected.contains(party) ? 100*seat.node.fpVotesProjected.at(party)/legacyTotal : 0;
            row["parties"].push_back({{"party_index",party},{"mean_votes",projected[s][party]},
                {"share_percent",share},{"legacy_share_percent",legacyShare},{"change_percentage_points",share-legacyShare}});
        }
        diagnostic["fp_count_effects"].push_back(std::move(row));
    }
    diagnostic["unsupported_fp_units"] = std::move(unsupported);
    diagnostic["mode"] = mode;
    diagnostic["forecast_integration"] = {{"version","live-count-integration-3"},
        {"count_means_requested",mode == "counts"},{"count_means_active",false},
        {"count_uncertainty_integrated",false}};
    if (mode == "counts" && node.totalFpVotesCurrent() > 0) {
        // An uncounted new service also needs a model allocation. Silently
        // retaining its legacy size would break the finite district account.
        for (std::size_t b = 0; b < booths.size(); ++b) if (!matchedBooths.count(int(b)))
            throw std::runtime_error("Booth absent from integrated turnout prior: "+booths[b].name);
        auto means = std::make_shared<std::vector<double>>(booths.size());
        for (std::size_t j = 0; j < units.size(); ++j) if (boothIndexes[j] >= 0)
            means->at(boothIndexes[j]) = result->unitMeans[j];
        turnoutMeanFpTotals = std::move(means);
        auto composition = std::make_shared<TurnoutCountComposition>();
        composition->drawPlan = TurnoutModel::makeDrawPlan(*result,units,prior.enrolment);
        composition->boothIndexes = boothIndexes;
        turnoutCountComposition = std::move(composition);
        turnoutCountsActive = true;
        diagnostic["forecast_integration"]["count_means_active"] = true;
    }
    diagnostic["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    logger << "Turnout shadow preparation: " << diagnostic["seconds"] << " seconds\n";
    turnoutShadow = std::move(result);
    turnoutShadowDiagnostic = std::make_shared<json const>(std::move(diagnostic));
}

float LiveV2::Election::turnoutFpTarget(int boothIndex, float legacyTarget) const {
    if (!turnoutCountsActive) return legacyTarget;
    double expected = turnoutMeanFpTotals->at(boothIndex);
    LiveTurnoutMath::remaining(expected, booths.at(boothIndex).node.totalFpVotesCurrent());
    return float(expected);
}

float LiveV2::Election::turnoutPairTarget(int boothIndex, float legacyTarget) const {
    if (!turnoutCountsActive) return legacyTarget;
    auto const& booth = booths.at(boothIndex);
    return float(LiveTurnoutMath::pairTarget(turnoutMeanFpTotals->at(boothIndex),
        booth.node.totalFpVotesCurrent(), booth.node.totalTcpVotesCurrent()));
}

std::optional<LiveData::VoteCountAccount> LiveV2::Election::getSeatVoteCountAccount(
    std::string const& seatName, LiveData::CountKind kind) const {
    if (!turnoutCountsActive || !turnoutCountedParties) return {};
    auto found = turnoutCountedParties->seatIndexes.find(seatName);
    if (found == turnoutCountedParties->seatIndexes.end()) throw std::runtime_error("Missing integrated turnout district: " + seatName);
    std::size_t s = found->second;
    auto const& n = seats[s].node;
    if (kind == LiveData::CountKind::Fp) return LiveData::VoteCountAccount{&turnoutCountedParties->fp[s], &n.fpVotesProjected};
    if (kind == LiveData::CountKind::Tpp) return LiveData::VoteCountAccount{
        preferenceScenarioCounts ? &preferenceScenarioCounts->tpp[s] : &turnoutCountedParties->tpp[s], &n.tppVotesProjected};
    if (n.tcpVotesProjected.empty()) return {};
    return LiveData::VoteCountAccount{
        preferenceScenarioCounts ? &preferenceScenarioCounts->tcp[s] : &turnoutCountedParties->tcp[s], &n.tcpVotesProjected};
}

void LiveV2::Election::refreshTurnoutPairProgress() {
    // All three projections share the future FP addition pool. Pair completion
    // also includes preferences still missing on already-counted FP votes, so
    // a partially reported batch cannot claim completion solely because FP is
    // complete. FP-derived TPP retains the existing half-confidence treatment
    // and is not promoted to a counted TPP batch.
    for (std::size_t b = 0; b < booths.size(); ++b) {
        auto& booth = booths[b]; auto& n = booth.node;
        double expected = turnoutMeanFpTotals->at(b);
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
    std::vector<double> seatWeights(seats.size()), regionWeights(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s)
        seatWeights[s] = refresh(seats[s].node, seats[s].booths, booths, *turnoutMeanFpTotals);
    for (std::size_t r = 0; r < largeRegions.size(); ++r)
        regionWeights[r] = refresh(largeRegions[r].node, largeRegions[r].seats, seats, seatWeights);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(), regions.end(), 0);
    refresh(node, regions, largeRegions, regionWeights);
}

void LiveV2::Election::recordTurnoutIntegrationDiagnostic() {
    // Run once after reduced preparation. Check the actual installed account,
    // then retain immutable counted-party maps for cheap bounded perturbations
    // in scenario copies. The legacy comparison remains in the same diagnostic.
    if (!turnoutShadowDiagnostic || !turnoutCountsActive) return;
    using json = nlohmann::json;
    auto value = *turnoutShadowDiagnostic;
    auto fixed = std::make_shared<TurnoutCountedParties>();
    fixed->fp.resize(seats.size()); fixed->tpp.resize(seats.size()); fixed->tcp.resize(seats.size());
    value["integrated_projections"] = json::array();
    double maximumError = 0, minimumMargin = std::numeric_limits<double>::infinity();
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s]; double expected = 0;
        double missingPreferences = 0, pairExcess = 0;
        fixed->seatIndexes.emplace(seat.name,s);
        for (int b : seat.booths) {
            auto const& booth = booths[b]; expected += turnoutMeanFpTotals->at(b);
            // Summing gaps per booth keeps an unrelated TCP excess from hiding
            // missing preferences elsewhere in the seat. These are checked
            // LiveV2 counts, not a correction to the authority's raw records.
            double gap = booth.node.totalFpVotesCurrent() - booth.node.totalTcpVotesCurrent();
            missingPreferences += std::max(0.,gap);
            pairExcess += std::max(0.,-gap);
            for (auto const& [party, votes] : booth.node.fpVotesCurrent)
                fixed->fp[s][party == seat.liveIndependentPartyIndex ? run.indPartyIndex : party] += votes;
            if (hasClassicPair(booth.node.tcpVotesCurrent, natPartyIndex))
                for (auto const& [party, votes] : booth.node.tcpVotesCurrent) fixed->tpp[s][party == natPartyIndex ? 1 : party] += votes;
        }
        for (auto const& [party, votes] : seat.node.tcpVotesCurrent)
            fixed->tcp[s][party == seat.liveIndependentPartyIndex ? run.indPartyIndex : party] += votes;
        auto check = [&](auto const& projected, auto const& counted) {
            if (projected.empty()) return; // no usable non-classic pair: FP path supplies its forecast.
            for (auto const& [party, votes] : counted) {
                auto found = projected.find(party);
                if (found == projected.end()) {
                    if (votes == 0) continue;
                    throw std::runtime_error("Integrated turnout projection lost a counted candidate in "+seat.name);
                }
                minimumMargin = std::min(minimumMargin, double(found->second) - votes);
                if (found->second < votes) throw std::runtime_error("Integrated turnout projection reduced counted votes in "+seat.name);
            }
        };
        check(seat.node.fpVotesProjected, fixed->fp[s]);
        check(seat.node.tppVotesProjected, fixed->tpp[s]);
        check(seat.node.tcpVotesProjected, fixed->tcp[s]);
        // Missing zero-valued candidates carry no count constraint in the final
        // sampler. A genuinely positive missing candidate was rejected above.
        for (auto* counts : {&fixed->fp[s], &fixed->tpp[s], &fixed->tcp[s]})
            for (auto it = counts->begin(); it != counts->end();) {
                if (it->second == 0) it = counts->erase(it); else ++it;
            }
        double projected = seat.node.totalFpVotesProjected();
        double tppProjected = 0;
        for (auto const& [party, votes] : seat.node.tppVotesProjected) tppProjected += votes;
        maximumError = std::max(maximumError, std::abs(projected - expected));
        if (std::abs(projected - expected) > std::max(.001, expected * 1e-6))
            throw std::runtime_error("Integrated turnout FP account does not reconcile in "+seat.name);
        value["integrated_projections"].push_back({{"seat",seat.name},{"counted_fp",seat.node.totalFpVotesCurrent()},
            {"model_mean_fp",expected},{"projected_fp",projected},{"projected_tcp",seat.node.totalTcpVotesProjected()},
            {"projected_tpp",tppProjected},{"fp_completion",seat.node.fpCompletion},
            {"fp_without_reported_tcp",missingPreferences},{"tcp_above_reported_fp",pairExcess},
            {"fp_confidence",seat.node.fpConfidence},{"tcp_completion",seat.node.tcpCompletion},
            {"tpp_completion",seat.node.tppCompletion},{"tpp_confidence",seat.node.tppConfidence}});
    }
    value["forecast_integration"]["maximum_fp_accounting_error"] = maximumError;
    value["forecast_integration"]["minimum_counted_party_margin"] = minimumMargin;
    turnoutCountedParties = std::move(fixed);
    prepareTurnoutCountComposition();
    value["forecast_integration"]["version"] = "live-count-integration-3";
    value["forecast_integration"]["count_uncertainty_integrated"] = true;
    value["forecast_integration"]["party_variation"] = "Joint prior count draws with an explicit no-addition outcome and conditional late batches; prepared composition variation transferred to the sampled remaining pool.";
    value["forecast_integration"]["unfinished_unit_responses"] = turnoutCountComposition->responses.size();
    turnoutShadowDiagnostic = std::make_shared<json const>(std::move(value));
}

void LiveV2::Election::prepareTurnoutCountComposition() {
    // Run once after mean-count composition preparation. Cache each unit's
    // mix of future voters, keeping measured candidate counts separate from
    // estimated preferences on FP votes whose pair count has not yet reported.
    auto cache = std::make_shared<TurnoutCountComposition>(*turnoutCountComposition);
    cache->baseFp.resize(seats.size()); cache->baseTpp.resize(seats.size()); cache->baseTcp.resize(seats.size());
    for (auto const& seat : seats) {
        cache->meanFp.push_back(seat.node.fpVotesProjected);
        cache->meanTpp.push_back(seat.node.tppVotesProjected);
        cache->meanTcp.push_back(seat.node.tcpVotesProjected);
        cache->fpConfidence.push_back(seat.node.fpConfidence);
        cache->tppConfidence.push_back(seat.node.tppConfidence);
        cache->tcpConfidence.push_back(seat.node.tcpConfidence);
    }
    for (std::size_t j = 0; j < cache->boothIndexes.size(); ++j) {
        int b = cache->boothIndexes[j]; if (b < 0) continue;
        auto const& booth = booths[b]; auto const& seat = seats[booth.parentSeatId];
        auto s = std::size_t(booth.parentSeatId);
        auto mapParty = [&](int party) { return party == seat.liveIndependentPartyIndex ? run.indPartyIndex : party; };
        std::map<int,double> fp, tpp, tcp;
        for (auto const& [p,v] : booth.node.fpVotesCurrent) if (v > 0) fp[mapParty(p)] += v;
        bool classic = hasClassicPair(booth.node.tcpVotesCurrent,natPartyIndex);
        for (auto const& [p,v] : booth.node.tcpVotesCurrent) if (v > 0) {
            if (classic) tpp[p == natPartyIndex ? 1 : p] += v;
            else if (!seat.node.tcpVotesProjected.empty()) tcp[mapParty(p)] += v;
        }
        double counted = cache->drawPlan.counted[j];
        auto fpMix = LiveTurnoutMath::prepareUnitComposition(booth.node.fpVotesProjected,fp,counted);
        auto tppMix = LiveTurnoutMath::prepareUnitComposition(booth.node.tppVotesProjected,tpp,counted);
        auto tcpMix = LiveTurnoutMath::prepareUnitComposition(booth.node.tcpVotesProjected,tcp,counted);
        for (auto const& [p,v] : fpMix.base) cache->baseFp[s][p] += v;
        for (auto const& [p,v] : tppMix.base) cache->baseTpp[s][p] += v;
        for (auto const& [p,v] : tcpMix.base) cache->baseTcp[s][p] += v;
        if (!turnoutShadow->broad.complete[j]) {
            if (fpMix.additionShares.empty()) throw std::runtime_error("No composition for unfinished turnout unit: "+seat.name+"/"+booth.name);
            cache->responses.push_back({j,s,std::move(fpMix.additionShares),std::move(tppMix.additionShares),std::move(tcpMix.additionShares)});
        }
    }
    // Reconstruction must agree with the installed means before any stochastic
    // drawing is allowed. Float maps can contribute only small rounding error.
    auto check = [&](auto const& bases, auto const& means, auto member) {
        auto rebuilt = bases;
        for (auto const& r : cache->responses) {
            double addition = turnoutShadow->unitMeans[r.unit]-cache->drawPlan.counted[r.unit];
            for (auto const& [p,w] : r.*member) rebuilt[r.seat][p] += addition*w;
        }
        for (std::size_t s = 0; s < means.size(); ++s) for (auto const& [p,v] : means[s])
            if (std::abs(rebuilt[s][p]-v) > std::max(.02,double(v)*2e-6))
                throw std::runtime_error("Turnout composition cache does not reconstruct its mean in "+seats[s].name);
    };
    check(cache->baseFp,cache->meanFp,&TurnoutCountComposition::Response::fp);
    check(cache->baseTpp,cache->meanTpp,&TurnoutCountComposition::Response::tpp);
    check(cache->baseTcp,cache->meanTcp,&TurnoutCountComposition::Response::tcp);
    turnoutCountComposition = std::move(cache);
}

void LiveV2::Election::drawTurnoutCounts(int iterationIndex) {
    if (iterationIndex < 0) throw std::runtime_error("Negative turnout iteration index.");
    auto const& cache = *turnoutCountComposition;
    auto seed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0xa0761d6478bd642fULL,
        std::uint64_t(iterationIndex));
    auto counts = TurnoutModel::drawUnitCounts(*turnoutShadow,cache.drawPlan,seed);
    auto fp = cache.baseFp, tpp = cache.baseTpp, tcp = cache.baseTcp;
    for (auto const& r : cache.responses) {
        double addition = LiveTurnoutMath::remaining(counts[r.unit],cache.drawPlan.counted[r.unit]);
        for (auto const& [p,w] : r.fp) fp[r.seat][p] += addition*w;
        for (auto const& [p,w] : r.tpp) tpp[r.seat][p] += addition*w;
        for (auto const& [p,w] : r.tcp) tcp[r.seat][p] += addition*w;
    }
    auto install = [](auto const& source, auto& target) {
        target.clear(); for (auto const& [p,v] : source) target[p] = float(v);
    };
    for (std::size_t s = 0; s < seats.size(); ++s) {
        install(fp[s],seats[s].node.fpVotesProjected);
        install(tpp[s],seats[s].node.tppVotesProjected);
        install(tcp[s],seats[s].node.tcpVotesProjected);
        if (!seats[s].node.tcpShares.empty()) {
            double total = 0; for (auto const& [p,v] : tcp[s]) total += v;
            for (auto const& [p,v] : tcp[s])
                seats[s].node.tcpShares[p] = float(25*std::log((v+.25)/(total-v+.25)));
        }
    }
}

void LiveV2::Election::refreshTurnoutScenarioProgress() {
    // Progress is derived from the same sampled counts used for composition.
    // Scale the existing evidence numerator, retaining discounts for inferred
    // TPP and incomplete TCP coverage rather than promoting them to raw counts.
    auto const& cache = *turnoutCountComposition;
    auto sum = [](auto const& values) { double n = 0; for (auto const& [p,v] : values) n += v; return n; };
    auto confidence = [](double value, double mean, double sampled) {
        return sampled > 0 ? float(std::clamp(value*mean/sampled,0.,1.)) : 0.f;
    };
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto& n = seats[s].node;
        double fp = sum(n.fpVotesProjected), tpp = sum(n.tppVotesProjected), tcp = sum(n.tcpVotesProjected);
        n.fpCompletion = float(LiveTurnoutMath::completion(sum(turnoutCountedParties->fp[s]),fp));
        n.tppCompletion = float(LiveTurnoutMath::completion(sum(turnoutCountedParties->tpp[s]),tpp));
        n.tcpCompletion = tcp > 0 ? float(LiveTurnoutMath::completion(sum(turnoutCountedParties->tcp[s]),tcp)) : 0;
        n.fpConfidence = confidence(cache.fpConfidence[s],sum(cache.meanFp[s]),fp);
        n.tppConfidence = confidence(cache.tppConfidence[s],sum(cache.meanTpp[s]),tpp);
        n.tcpConfidence = confidence(cache.tcpConfidence[s],sum(cache.meanTcp[s]),tcp);
    }
    auto aggregate = [&](Node& parent, auto const& indexes, auto const& children) {
        double total = 0, fpComplete = 0, tppComplete = 0, fpConfidence = 0, tppConfidence = 0;
        for (int i : indexes) {
            auto const& n = children[i].node; double w = sum(n.fpVotesProjected); total += w;
            fpComplete += w*n.fpCompletion; tppComplete += w*n.tppCompletion;
            if (!n.fpDeviations.empty()) fpConfidence += w*n.fpConfidence*n.relevanceModifier;
            if (n.tppDeviation) tppConfidence += w*n.tppConfidence*n.relevanceModifier;
        }
        parent.fpCompletion = total > 0 ? float(fpComplete/total) : 0;
        parent.tppCompletion = total > 0 ? float(tppComplete/total) : 0;
        parent.fpConfidence = total > 0 ? float(fpConfidence/total) : 0;
        parent.tppConfidence = total > 0 ? float(tppConfidence/total) : 0;
    };
    for (auto& region : largeRegions) aggregate(region.node,region.seats,seats);
    std::vector<int> regions(largeRegions.size()); std::iota(regions.begin(),regions.end(),0);
    aggregate(node,regions,largeRegions);
}
