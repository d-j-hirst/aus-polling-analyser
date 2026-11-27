#pragma once

#include "LiveIndependentMapping.h"
#include "LivePartialCount.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// When the same independent faces the same party again, their preference
// flows may differ systematically between booths. Estimate the change since
// the previous election at booths already counted, then carry that change
// to comparable locations still awaiting a TCP. FP and turnout are inputs to
// the downstream projection. This calculation estimates a preference rate.
namespace LiveIndependentFlow {

struct Contest {
    int independent, opponent; // Source election's candidate IDs.
    std::string independentName, opponentCode;
};

// Use the two individuals with positive district TCP counts. A candidate left
// listed with zero votes after a realignment is not evidence of a third finalist.
// Read each affiliation in its own election's roster: commission numbers can
// be reassigned, and they can also change when the same person stands again.
template<class Election, class Seat>
std::optional<Contest> contest(Election const& election, Seat const& seat) {
    Contest result{-1, -1, {}, {}};
    for (auto const& [id, counts] : seat.tcpVotesCandidate) {
        if (std::none_of(counts.begin(), counts.end(), [](auto const& row) { return row.second > 0; })) continue;
        auto candidate = election.candidates.find(id);
        if (candidate == election.candidates.end()) return {};
        if (candidate->second.party == candidate->second.Independent) {
            if (!result.independentName.empty()) return {};
            result.independent = id;
            result.independentName = LiveIndependentMapping::canonicalName(candidate->second.name);
            if (result.independentName.empty()) return {};
        } else {
            auto party = election.parties.find(candidate->second.party);
            if (!result.opponentCode.empty() || party == election.parties.end() || party->second.shortCode.empty()) return {};
            result.opponent = id;
            result.opponentCode = party->second.shortCode;
        }
    }
    if (result.independentName.empty() || result.opponentCode.empty()) return {};
    return result;
}

// The same party may have different commission abbreviations. Keep Nationals
// distinct from Liberals even in projects that group their primary votes.
inline std::string ballotParty(std::string code) {
    if (code == "LP") return "LIB";
    if (code == "LNQ") return "LNP";
    if (code == "NP") return "NAT";
    if (code == "LAB") return "ALP";
    if (code == "GVIC") return "GRN";
    if (code == "PHON" || code == "ONP") return "ON";
    return code;
}

struct Match { int independent, previousIndependent, opponent; }; // Forecast's ballot and party indices.

template<class Election, class Seat, class PartyLookup>
std::optional<Match> match(Election const& previous, Seat const& previousSeat,
    Election const& current, Seat const& currentSeat, PartyLookup partyIndex, int independentOffset) {
    auto const now = contest(current, currentSeat);
    if (!now) return {};
    auto const before = contest(previous, previousSeat);
    if (!before || before->independentName != now->independentName ||
        ballotParty(before->opponentCode) != ballotParty(now->opponentCode)) return {};

    int const opponent = partyIndex(now->opponentCode);
    if (opponent < 0 || opponent != partyIndex(before->opponentCode)) return {};
    // Booth FP counts are grouped by project party. If the ballot lists
    // two candidates who map to the opponent's slot, subtracting their
    // combined FP would count too few ballots as available for transfer.
    auto distinctOpponent = [&](Election const& source, Seat const& district, int finalist) {
        for (auto const& entry : district.fpVotes) {
            if (entry.first == finalist) continue;
            auto const candidate = source.candidates.find(entry.first);
            if (candidate == source.candidates.end()) continue;
            auto const party = source.parties.find(candidate->second.party);
            if (party != source.parties.end() && partyIndex(party->second.shortCode) == opponent) return false;
        }
        return true;
    };
    if (!distinctOpponent(current, currentSeat, now->opponent) ||
        !distinctOpponent(previous, previousSeat, before->opponent)) return {};
    return Match{now->independent + independentOffset, before->independent + independentOffset, opponent};
}

using LivePartialCount::PreferenceTransfer;
using LivePartialCount::Counts;

// The trial's main comparison used exactly reconciling accounts. Missing FP
// entries and ballots of unknown destination cannot measure this flow. A
// recorded zero for one finalist, or a unanimous transfer, can be genuine.
inline std::optional<PreferenceTransfer> exactTransfer(Counts const& fp, Counts const& tcp,
    int independent, int opponent) {
    if (!fp.contains(independent) || !fp.contains(opponent) ||
        LivePartialCount::total(fp) != LivePartialCount::total(tcp)) return {};
    return LivePartialCount::measureTransfer(fp, tcp, independent, opponent);
}

struct BoothObservation {
    int boothIndex;
    std::optional<PreferenceTransfer> previous, current;
    bool awaitingTcp;
    double transferWeight = 1; // Relevance of the current centre to voters elsewhere.
};

// Return the preference fraction to the independent at eligible *uncounted*
// booths, using only data available in this capture. Calculate it once before
// the simulations. The existing pooled rate (also a fraction) remains the
// estimate wherever the observations cannot identify a local adjustment.
inline std::map<int, double> estimate(std::vector<BoothObservation> const& booths, double pooled) {
    double historicalVotes = 0, historicalReceived = 0, currentVotes = 0;
    double matchedVotes = 0, matchedReceived = 0;
    bool hasTarget = false;
    for (auto const& booth : booths) {
        if (booth.current) currentVotes += booth.transferWeight * booth.current->votes;
        if (!booth.previous) continue;
        hasTarget |= booth.awaitingTcp;
        historicalVotes += booth.previous->votes;
        historicalReceived += booth.previous->received;
        if (booth.current) {
            matchedVotes += booth.transferWeight * booth.current->votes;
            matchedReceived += booth.transferWeight * booth.current->received;
        }
    }
    if (!matchedVotes || !hasTarget) return {};

    // Give small historical booths 50 preference ballots at the historical
    // district rate. Find the common movement in odds that reproduces the
    // independent's measured transfers at the matched booths, weighting
    // each by the number of other candidates' FP ballots there *this* time.
    constexpr double historicalAllowance = 50;
    double const historicalMean = historicalReceived / historicalVotes;
    auto historicalLogOdds = [&](PreferenceTransfer const& previous) {
        double share = (previous.received + historicalAllowance * historicalMean) /
            (previous.votes + historicalAllowance);
        // Finite starting odds let even unanimous history respond to new votes.
        share = std::clamp(share, 1e-10, 1.0 - 1e-10);
        return std::log(share / (1 - share));
    };
    std::vector<std::pair<double, double>> comparisons;
    for (auto const& booth : booths) {
        if (booth.previous && booth.current)
            comparisons.emplace_back(historicalLogOdds(*booth.previous), booth.transferWeight * booth.current->votes);
    }
    // These numerical limits cover near-unanimous counts in either direction.
    double lower = -32, upper = 32;
    for (int i = 0; i < 44; ++i) {
        double const change = (lower + upper) * .5;
        double received = 0;
        for (auto const& [odds, votes] : comparisons)
            received += votes * LivePartialCount::inverseLogOdds(odds + change);
        if (received < matchedReceived) lower = change;
        else upper = change;
    }

    // The allowances and half strength come from the returning-independent
    // booth-flow trial (analysis/reports/returning-independent-booth-flows.md).
    // With 100 comparable transfers and no unmatched ones, apply a quarter
    // of the implied difference. More matched evidence can lift this to half.
    constexpr double currentAllowance = 100;
    double const strength = .5 * matchedVotes / (currentVotes + currentAllowance);
    std::map<int, double> rates;
    for (auto const& booth : booths) {
        if (!booth.previous || !booth.awaitingTcp) continue;
        double const fitted = LivePartialCount::inverseLogOdds(historicalLogOdds(*booth.previous) + (lower + upper) * .5);
        rates.emplace(booth.boothIndex, pooled + strength * (fitted - pooled));
    }
    return rates;
}

}
