#include "LiveV2.h"

#include "PollingProject.h"
#include "RandomGenerator.h"
#include "SimulationRun.h"

#include <chrono>
#include <cmath>
#include <map>
#include <numeric>

void LiveV2::Election::preparePreferenceCorrections() {
    // Only the current source contributes to preference expectations. Previous
    // elections and frozen turnout origins are unnecessary; later snapshots
    // were used only to calibrate the fixed correction mixture offline.
    auto const started = std::chrono::steady_clock::now();
    using json = nlohmann::json;
    auto prepared = std::make_shared<PreferenceCorrectionPreparation>();
    prepared->seats.resize(seats.size());
    prepared->countedTpp.resize(seats.size());
    prepared->countedTcp.resize(seats.size());
    json diagnostic = {{"version", "live-preference-rechecking-2"},
        {"size_response_votes", LivePreferenceCorrections::SizeResponseScale},
        {"aggregate_samples", LivePreferenceCorrections::AggregateSamples},
        {"maximum_explicit_booths_per_seat", LivePreferenceCorrections::ExplicitBooths},
        {"calibration", "Shared Federal 2025 20 May fit: size-aware background rechecks and stronger regression-directed corrections"},
        {"calibration_comparison_sha256", "fec06e0f6327b58936a248069626108c9b9166957c1d6737ce67d9ecc142a15c"},
        {"calibration_model", "size_background_200_t5_landing"},
        {"components", {"unchanged", "routine_recheck", "background_recheck", "regression_directed_correction"}},
        {"seats", json::array()}, {"exclusions", json::object()}};
    auto exclude = [&](char const* reason) {
        auto& count = diagnostic["exclusions"][reason];
        count = count.is_null() ? 1 : count.get<int>() + 1;
    };
    std::size_t comparableBooths = 0;
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s];
        auto mapParty = [&](int party) { return party == seat.liveIndependentPartyIndex ? run.indPartyIndex : party; };
        // Retain all counted pair votes, including declarations that this
        // regression does not attempt to revise. The adjusted account is an
        // exact copy until a scenario draws an opposing preference transfer.
        for (int b : seat.booths) {
            auto const& tcp = booths[b].node.tcpVotesCurrent;
            if (tcp.size() == 2 && tcp.contains(0) && (tcp.contains(1) || tcp.contains(natPartyIndex)))
                for (auto const& [p, v] : tcp) prepared->countedTpp[s][p == natPartyIndex ? 1 : p] += v;
        }
        for (auto const& [p, v] : seat.node.tcpVotesCurrent) prepared->countedTcp[s][mapParty(p)] += v;
        if (seat.node.tcpVotesCurrent.size() != 2) continue;
        auto& state = prepared->seats[s];
        int const rawA = seat.node.tcpVotesCurrent.contains(0) ? 0
            : (seat.node.tcpVotesCurrent.begin()->first == 1 ? std::next(seat.node.tcpVotesCurrent.begin())->first
                : seat.node.tcpVotesCurrent.begin()->first);
        int const rawB = seat.node.tcpVotesCurrent.begin()->first == rawA
            ? std::next(seat.node.tcpVotesCurrent.begin())->first : seat.node.tcpVotesCurrent.begin()->first;
        state.classic = rawA == 0 && (rawB == 1 || rawB == natPartyIndex);
        state.partyA = state.classic ? 0 : mapParty(rawA);
        state.partyB = state.classic ? 1 : mapParty(rawB);
        auto const& projected = state.classic ? seat.node.tppVotesProjected : seat.node.tcpVotesProjected;
        if (!projected.contains(state.partyA) || !projected.contains(state.partyB)) continue;
        std::vector<LivePreferenceCorrections::Observation> observations;
        std::map<int, double> otherTotals;
        for (int b : seat.booths) {
            auto const& booth = booths[b];
            auto const& n = booth.node;
            if (booth.voteType != Results2::VoteType::Ordinary
                || (booth.boothType != Results2::Booth::Type::Normal && booth.boothType != Results2::Booth::Type::Ppvc)) {
                exclude("different_reporting_service"); continue;
            }
            if (n.tcpVotesCurrent.size() != 2 || !n.tcpVotesCurrent.contains(rawA) || !n.tcpVotesCurrent.contains(rawB)) {
                exclude("missing_or_different_pair"); continue;
            }
            // Exact parent agreement excludes unfinished preferences and OPV
            // exhaustion rather than mistaking them for a counting anomaly.
            if (n.totalFpVotesCurrent() != n.totalTcpVotesCurrent()) {
                exclude("fp_tcp_totals_differ"); continue;
            }
            if (!n.fpVotesCurrent.contains(rawA) || !n.fpVotesCurrent.contains(rawB)) {
                exclude("missing_fp_finalist"); continue;
            }
            double const fpA = n.fpVotesCurrent.at(rawA), fpB = n.fpVotesCurrent.at(rawB);
            double const formal = n.totalFpVotesCurrent(), pool = formal - fpA - fpB;
            if (!(pool > 0)) { exclude("no_other_candidate_preferences"); continue; }
            double const gain = n.tcpVotesCurrent.at(rawA) - fpA;
            if (gain < 0 || gain > pool) { exclude("tcp_contradicts_candidate_fp"); continue; }
            observations.push_back({std::size_t(b), formal, fpA, fpB, 0, gain, booth.boothType == Results2::Booth::Type::Ppvc});
            for (auto const& [p, v] : n.fpVotesCurrent) if (p != rawA && p != rawB) otherTotals[p] += v;
        }
        if (observations.empty()) continue;
        auto const largestOther = std::max_element(otherTotals.begin(), otherTotals.end(),
            [](auto const& a, auto const& b) { return a.second < b.second; })->first;
        for (auto& observation : observations) {
            auto const& fp = booths[observation.booth].node.fpVotesCurrent;
            if (fp.contains(largestOther)) observation.otherParty = fp.at(largestOther);
        }
        auto const mixtures = LivePreferenceCorrections::prepare(observations);
        auto const seed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0x50726566436f7272ULL, s);
        state.distribution = LivePreferenceCorrections::compress(mixtures, seed);
        comparableBooths += mixtures.size();
        json seatDiagnostic = {{"seat", seat.name}, {"party_a", state.partyA}, {"party_b", state.partyB},
            {"classic_pair", state.classic}, {"mean_revision_votes", state.distribution.expectedRevision},
            {"booths", json::array()}};
        for (auto const& mixture : mixtures) {
            bool const explicitDraw = std::any_of(state.distribution.explicitBooths.begin(), state.distribution.explicitBooths.end(),
                [&](auto const& item) { return item.booth == mixture.booth; });
            seatDiagnostic["booths"].push_back({{"booth", booths[mixture.booth].name},
                {"formal_votes", booths[mixture.booth].node.totalFpVotesCurrent()}, {"preference_pool", mixture.pool},
                {"reported_preference_gain_a", mixture.gain}, {"predicted_preference_share", 1 / (1 + std::exp(-mixture.prediction))},
                {"comparison_peers", mixture.peers}, {"size_matched_discrepancy", mixture.discrepancy},
                {"broad_probability", mixture.broadProbability}, {"routine_probability", mixture.routineProbability},
                {"background_probability", mixture.backgroundProbability},
                {"unchanged_probability", 1 - mixture.broadProbability - mixture.backgroundProbability - mixture.routineProbability},
                {"routine_log_odds_scale", mixture.routineScale}, {"background_log_odds_scale", mixture.backgroundScale},
                {"broad_log_odds_scale", mixture.broadScale},
                {"conditional_fraction_of_gap", mixture.fraction}, {"mean_revision_votes", mixture.expectedRevision},
                {"explicit_mixture_draw", explicitDraw}});
        }
        diagnostic["seats"].push_back(std::move(seatDiagnostic));
    }
    diagnostic["comparable_booths"] = comparableBooths;
    diagnostic["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    logger << "Preference rechecking preparation: " << diagnostic["seconds"] << " seconds\n";
    preferenceCorrectionDiagnostic = std::make_shared<json const>(std::move(diagnostic));
    // No reported comparable booth means an exact no-op, preserving the blind
    // baseline without allocating scenario accounts or consuming random draws.
    if (comparableBooths) preferenceCorrections = std::move(prepared);
}

void LiveV2::Election::applyPreferenceCorrections(int iterationIndex) {
    if (!preferenceCorrections) return;
    auto adjusted = std::make_shared<PreferenceScenarioCounts>();
    adjusted->tpp = preferenceCorrections->countedTpp;
    adjusted->tcp = preferenceCorrections->countedTcp;
    auto const iterationSeed = RandomGenerator::mixKey(variabilityBaseSeed ^ 0x5265636865636b31ULL,
        std::uint64_t(iterationIndex));
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& state = preferenceCorrections->seats[s];
        if (state.distribution.aggregateRevisions.empty()) continue;
        double const revision = LivePreferenceCorrections::draw(state.distribution, RandomGenerator::mixKey(iterationSeed, s));
        auto& n = seats[s].node;
        auto& counted = state.classic ? adjusted->tpp[s] : adjusted->tcp[s];
        auto& projected = state.classic ? n.tppVotesProjected : n.tcpVotesProjected;
        LivePreferenceCorrections::transfer(counted, projected, state.partyA, state.partyB, revision);
        // Non-classic TCP corrections do not establish an observed classic TPP.
        // Refresh the published pair's shares, but retain the separately inferred
        // TPP and all completion metrics: this transfer adds no ballots.
        if (!state.classic) {
            double total = 0;
            for (auto const& [p, v] : projected) total += v;
            for (auto const& [p, v] : projected)
                n.tcpShares[p] = float(25 * std::log((double(v) + .25) / (total - v + .25)));
        }
    }
    preferenceScenarioCounts = std::move(adjusted);
}
