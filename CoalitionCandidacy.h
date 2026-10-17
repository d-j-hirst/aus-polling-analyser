#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace CoalitionCandidacy {

// Before nominations are settled, use the configured participation chances.
// A published ballot then establishes who actually runs, regardless of those
// earlier chances. The separate conditional vote-split parameter remains in
// use when both Coalition candidates are present.
inline float candidateChance(
    std::vector<std::string> const& knownBallot,
    std::map<std::string,float> const& possibilities,
    std::string const& code)
{
    if (!knownBallot.empty()) {
        bool present = std::any_of(knownBallot.begin(),knownBallot.end(),
            [&](std::string const& entry) {
                return entry.substr(0,entry.find('*')) == code;
            });
        return present ? 1.0f : 0.0f;
    }
    auto found = possibilities.find(code);
    return found == possibilities.end() ? 0.0f : found->second;
}

} // namespace CoalitionCandidacy
