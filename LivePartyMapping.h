#pragma once

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

namespace LivePartyMapping {

// The project supplies mappings for configured parties first. Other registered
// parties keep their commission identity, with a shared nonempty abbreviation
// allowing the same party to be recognised in different elections. A missing
// abbreviation is unknown information, not an alias for all unnamed parties.
inline int mapParty(int commissionId, std::string const& abbreviation,
    int configuredPartyCount, int independentIdOffset,
    std::map<int, int>& commissionIds, std::map<std::string, int>& abbreviations)
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
    // separately reserved range for individual independent candidates.
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
