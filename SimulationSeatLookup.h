#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Locate the configured seats used by one calculation run. A seat's identity,
// configuration and region stay fixed while iterations vary its possible vote
// shares. Keeping references and ordered region membership here avoids walking
// the editable project's map each time an iteration needs that information.
//
// This cache belongs to the calculation context, not the editable GUI project.
// Prepare it again after importing configuration, including for supporting runs
// and new snapshots. Its owner must keep that configuration alive and unchanged
// until every calculation worker has finished. No votes, confidence measures or
// other scenario-dependent quantities are stored here.
template<class Seat>
class SimulationSeatLookup {
public:
    template<class Seats, class RegionIndex>
    void prepare(Seats const& configuredSeats, int regionCount, RegionIndex regionIndex) {
        SimulationSeatLookup prepared;
        prepared.byRegion.resize(regionCount);
        // Iterate the original collection once. Appending in this order retains
        // the old seat indices and, within each region, the order of additions
        // used for vote-share totals. IDs may contain gaps after GUI deletions.
        for (auto const& entry : configuredSeats) {
            auto const& configuredSeat = entry.second;
            int region = regionIndex(configuredSeat.region);
            if (region < 0 || region >= regionCount)
                throw std::runtime_error("Invalid region in simulation seat lookup.");
            int index = int(prepared.orderedSeats.size());
            prepared.orderedSeats.push_back(std::addressof(configuredSeat));
            prepared.regions.push_back(region);
            prepared.byRegion[region].push_back(index);
        }
        *this = std::move(prepared);
    }

    Seat const& seat(std::size_t index) const { return *orderedSeats.at(index); }
    int region(std::size_t index) const { return regions.at(index); }
    std::vector<int> const& regionSeats(std::size_t index) const { return byRegion.at(index); }

private:
    std::vector<Seat const*> orderedSeats;
    std::vector<int> regions;
    std::vector<std::vector<int>> byRegion;
};
