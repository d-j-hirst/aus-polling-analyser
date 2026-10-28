#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace LivePartyMapping {

// Translate commission identities into the parties used by the forecast.
// Numeric IDs belong to one election: WA's current Greens and historical
// Nationals can both be party 4. Only official abbreviations (including aliases
// supplied by the project) connect registered parties across elections.
enum class SourceElection { Current, Previous };
using CommissionPartyId = std::pair<SourceElection, int>;
using CommissionIds = std::map<CommissionPartyId, int>;

struct CandidateParty {
    SourceElection source;
    int partyId;
    std::string abbreviation;
    bool hasPartyRecord;
};

// Resolve a candidate using the election whose votes are being read. Normally
// both the candidate and their party are present there. Preserve the existing
// fallback for a missing candidate record, but then use the fallback election
// for BOTH the party description and numeric-ID cache. A missing party record
// must stay unknown; the same number in the other election is not evidence of
// affiliation. The caller handles the independent-candidate sentinel separately.
template<class Election>
std::optional<CandidateParty> candidateParty(int candidateId, SourceElection source,
    Election const& previous, Election const& current)
{
    auto const* election = source == SourceElection::Previous ? &previous : &current;
    auto candidate = election->candidates.find(candidateId);
    if (candidate == election->candidates.end()) {
        source = source == SourceElection::Previous
            ? SourceElection::Current : SourceElection::Previous;
        election = source == SourceElection::Previous ? &previous : &current;
        candidate = election->candidates.find(candidateId);
        if (candidate == election->candidates.end()) return std::nullopt;
    }
    int const partyId = candidate->second.party;
    auto party = election->parties.find(partyId);
    bool const known = party != election->parties.end();
    return CandidateParty{source, partyId, known ? party->second.shortCode : "", known};
}

// The project supplies mappings for configured parties first. Other registered
// parties receive distinct internal IDs, shared across elections only when a
// nonempty official abbreviation matches. Unknown abbreviations remain separate.
// The returned ID determines how these votes contribute to FP and preferences.
inline int mapParty(CommissionPartyId commissionId, std::string const& abbreviation,
    int configuredPartyCount, int independentIdOffset,
    CommissionIds& commissionIds, std::map<std::string, int>& abbreviations)
{
    if (auto known = commissionIds.find(commissionId); known != commissionIds.end()) {
        return known->second;
    }
    if (!abbreviation.empty()) {
        if (auto alias = abbreviations.find(abbreviation); alias != abbreviations.end()) {
            commissionIds[commissionId] = alias->second;
            return alias->second;
        }
    }

    // Unconfigured parties follow the project parties and must stay below the
    // separately reserved range for individual independent candidates. Check
    // allocations from BOTH elections so unrelated unconfigured parties cannot
    // acquire the same internal ID merely by being read from different sources.
    int maximumId = configuredPartyCount - 1;
    for (auto const& [_, mappedId] : commissionIds) maximumId = std::max(maximumId, mappedId);
    if (maximumId + 1 >= independentIdOffset) {
        throw std::runtime_error("No internal party IDs remain below the independent-candidate range.");
    }
    int const newId = maximumId + 1;
    commissionIds[commissionId] = newId;
    if (!abbreviation.empty()) abbreviations[abbreviation] = newId;
    return newId;
}

}
