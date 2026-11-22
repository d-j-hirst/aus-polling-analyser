#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// A forecast may model a locally prominent campaign as an independent even
// when the ballot gives it a registered-party label. The authored candidate
// names identify that decision: Sam Romano,IND in Calare, for example, refers
// to that person only, not every Shooters, Fishers and Farmers candidate.
//
// Apply this to the current working election after reading its source. The
// original source files, historical election, candidate IDs and all vote
// counts stay unchanged. The ordinary independent path then handles this
// candidate's FP votes, TCP votes, preferences and baseline-name matching.
namespace LiveIndependentMapping {

// Commission names may be "ROMANO, Sam" while the project uses "Sam Romano".
// Require the complete name after reversing that comma form and normalising
// case/spacing. Do not use a surname-only or approximate match to reclassify
// somebody: a similar name is not sufficient evidence of intended identity.
inline std::string canonicalName(std::string name) {
    if (auto comma = name.find(','); comma != std::string::npos)
        name = name.substr(comma + 1) + " " + name.substr(0, comma);
    std::string result;
    bool gap = false;
    for (unsigned char character : name) {
        if (std::isspace(character)) { gap = !result.empty(); continue; }
        if (gap) result += ' ';
        result += char(std::tolower(character));
        gap = false;
    }
    return result;
}

using NamesBySeat = std::map<std::string, std::vector<std::string>>;

// The feed adapter may already have matched a historical candidate to this
// year's ballot ID (Victoria does this by name). A number on its own is not
// evidence that the person returned. Check the full name and independent
// affiliation in both rosters once, before projecting any booths.
template<class Candidates>
std::set<int> returningBallotIds(Candidates const& previous, Candidates const& current, int independentOffset) {
    std::set<int> returning;
    for (auto const& [id, candidate] : current) {
        if (candidate.party != candidate.Independent) continue;
        auto old = previous.find(id);
        if (old == previous.end() || old->second.party != candidate.Independent) continue;
        auto name = canonicalName(candidate.name);
        if (!name.empty() && name == canonicalName(old->second.name))
            returning.insert(id + independentOffset);
    }
    return returning;
}

// In a booth or district account, registered parties carry their vote across elections
// by party. An independent carries only their own votes, even when they
// occupy the forecast's main IND slot. A recorded zero is a booth result;
// an absent entry leaves the ordinary no-history estimate in place.
inline std::optional<int> previousFpVotes(std::map<int, int> const& counts, int ballotId,
    int independentOffset, std::set<int> const& returning) {
    if (ballotId >= independentOffset && !returning.contains(ballotId)) return std::nullopt;
    auto votes = counts.find(ballotId);
    if (votes == counts.end()) return std::nullopt;
    return votes->second;
}

// NamesBySeat contains only names explicitly assigned IND in sCandidateNames.
// Each seat's candidate-count keys restrict the match to its own ballot, even
// before any votes arrive. A future candidate absent from the current preload
// has no effect; duplicate full-name matches are an input error. Return the IDs
// actually reclassified so the caller can record what changed in its log.
template<class Election>
std::vector<int> apply(Election& election, NamesBySeat const& namedIndependents) {
    std::vector<int> changed;
    for (auto const& [_, seat] : election.seats) {
        auto names = namedIndependents.find(seat.name);
        if (names == namedIndependents.end()) continue;
        for (auto const& configuredName : names->second) {
            auto const expected = canonicalName(configuredName);
            if (expected.empty()) continue;
            std::vector<int> matches;
            for (auto const& [candidateId, counts] : seat.fpVotes) {
                (void)counts;
                auto candidate = election.candidates.find(candidateId);
                if (candidate != election.candidates.end() &&
                    canonicalName(candidate->second.name) == expected)
                    matches.push_back(candidateId);
            }
            if (matches.size() > 1)
                throw std::runtime_error("Ambiguous named independent '" + configuredName + "' in " + seat.name + ".");
            if (matches.empty()) continue;
            auto& candidate = election.candidates.at(matches.front());
            if (candidate.party == candidate.Independent) continue;
            candidate.party = candidate.Independent;
            changed.push_back(matches.front());
        }
    }
    return changed;
}

}
