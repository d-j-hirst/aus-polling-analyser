#pragma once

#include "LiveData.h"

#include <cmath>
#include <span>
#include <stdexcept>

// Category uncertainty is shared across seats: an uncertain PPVC swing moves
// every unreported centre together. These vote weights describe the final
// prepared accounts, independently of how often their projections were rebuilt.
namespace LiveTppSensitivity {

struct Unit {
    LiveData::BoothType boothType;
    LiveData::VoteType voteType;
    float expectedVotes;
    bool hasObservedTpp;
};

struct Sensitivities {
    std::map<LiveData::BoothType, float> boothTypes;
    std::map<LiveData::VoteType, float> voteTypes;
};

inline Sensitivities prepare(std::span<Unit const> units, bool optionalPreferential) {
    // Return a fresh account on every preparation pass. Replacing the previous
    // result also removes a centre's old weight when its direct count arrives.
    Sensitivities result;
    for (auto const& unit : units) {
        if (!std::isfinite(unit.expectedVotes) || unit.expectedVotes < 0)
            throw std::logic_error("Invalid TPP category sensitivity target.");
        if (unit.expectedVotes == 0) continue;
        // Normally ordinary booths use the common swing, while unreported
        // PPVCs and other special booth types also receive a category error.
        // Retain the existing eligibility: a positive classic TCP, including a
        // partial one, uses its direct-count projection. Non-classic TCP does
        // not directly observe TPP and therefore does not remove this weight.
        if (unit.hasObservedTpp) continue;
        // Compulsory-preferential declaration accounts use their separately
        // prepared composition response. OPV retains the existing category path.
        if (!optionalPreferential && unit.voteType != LiveData::VoteType::Ordinary) continue;
        if (unit.boothType != LiveData::BoothType::Normal
            && unit.boothType != LiveData::BoothType::Invalid
            && unit.boothType != LiveData::BoothType::Other) {
            result.boothTypes[unit.boothType] += unit.expectedVotes;
        }
        else if (unit.voteType != LiveData::VoteType::Ordinary
            && unit.voteType != LiveData::VoteType::Invalid) {
            result.voteTypes[unit.voteType] += unit.expectedVotes;
        }
    }
    return result;
}

}
