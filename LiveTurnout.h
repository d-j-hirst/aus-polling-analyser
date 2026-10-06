#pragma once

#include "LiveData.h"
#include "TurnoutModelIO.h"

#include <memory>

// Owns the vote-count account used by LiveV2. Preparation fixes the original
// prior, current counts and remaining-voter composition once per source feed.
// Simulation copies share that work and draw only counts and composition.
namespace LiveTurnout {
using Counted = std::map<int, double>;
using Projected = std::map<int, float>;

struct UnitComposition {
    std::size_t booth = 0, seat = 0;
    Counted fp, tpp, tcp;
    Projected projectedFp, projectedTpp, projectedTcp;
};
struct SeatComposition {
    std::string name;
    Counted fp, tpp, tcp;
    Projected projectedFp, projectedTpp, projectedTcp;
    float fpConfidence = 0, tppConfidence = 0, tcpConfidence = 0;
};
struct Projection {
    Projected fp, tpp, tcp;
};

class Prepared {
public:
    static std::shared_ptr<Prepared const> prepare(TurnoutModelIO::Artifact artifact,
        std::vector<TurnoutModel::Unit> units, std::vector<int> boothIndexes,
        std::size_t boothCount, std::vector<bool> const& finalised,
        std::vector<TurnoutModel::Observation> const& history, std::string const& sourceTime);

    // Attach the party model only after LiveV2 has prepared composition at the
    // count means. Count uncertainty then changes only the uncounted pool.
    std::shared_ptr<Prepared const> withComposition(std::vector<UnitComposition> const& units,
        std::vector<SeatComposition> const& seats) const;
    double fpTarget(std::size_t booth) const;
    std::vector<Projection> draw(unsigned long long seed) const;
    Projected vary(LiveData::CountKind kind, std::size_t seat,
        Projected const& sampled, std::map<int, double> const& changes) const;
    Projected const& mean(LiveData::CountKind kind, std::size_t seat) const;
    Counted const& counted(LiveData::CountKind kind, std::size_t seat) const;
    float confidence(LiveData::CountKind kind, std::size_t seat) const;
    std::size_t seatIndex(std::string const& name) const;
    nlohmann::json diagnostic() const;

private:
    struct Counts {
        TurnoutModelIO::Artifact artifact;
        std::vector<TurnoutModel::Unit> units;
        std::vector<int> boothIndexes;
        TurnoutModel::Result result;
        TurnoutModel::DrawPlan drawPlan;
        std::vector<double> boothMeans;
        std::string sourceTime;
    };
    struct Response {
        std::size_t unit, seat;
        std::vector<std::pair<int, double>> fp, tpp, tcp;
    };
    struct Composition {
        std::vector<SeatComposition> seats;
        std::map<std::string, std::size_t> indexes;
        std::vector<Counted> baseFp, baseTpp, baseTcp;
        std::vector<Response> responses;
    };
    std::shared_ptr<Counts const> counts;
    std::shared_ptr<Composition const> composition;
};
}
