#include "LiveV2.h"
#include "LiveResultsInput.h"
#include "SimulationRun.h"
#include "TurnoutModelIO.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <set>
#include <stdexcept>

// This bridge is deliberately separate from the portable count algorithm.
// It attaches LiveV2's already mapped/checked counts, then compares projections
// using the existing expected party composition of outstanding votes. It does
// not install new count or party uncertainty into the forecast sampler.
void LiveV2::Election::prepareTurnoutShadow(Results2::Election const& currentElection) {
    auto setting = std::getenv("POLLING_ANALYSER_TURNOUT_SHADOW");
    if (!setting || !*setting) return;
    using json = nlohmann::json;
    auto started = std::chrono::steady_clock::now();
    auto artifact = TurnoutModelIO::load(LiveResultsInput::pathFromUtf8(setting));
    auto const& prior = artifact.prior;
    if (prior.election != run.getTermCode()) throw std::runtime_error("Turnout shadow election does not match the live run.");
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
    diagnostic["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    logger << "Turnout shadow preparation: " << diagnostic["seconds"] << " seconds\n";
    turnoutShadow = std::move(result);
    turnoutShadowDiagnostic = std::make_shared<json const>(std::move(diagnostic));
}
