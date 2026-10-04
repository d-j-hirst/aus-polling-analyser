#pragma once
#include "TurnoutModel.h"
#include "json.h"
#include <filesystem>

namespace TurnoutModelIO {
inline constexpr int SchemaVersion = 1;
struct Artifact {
    TurnoutModel::Prior prior;
    TurnoutModel::Options options;
    std::optional<double> pollClose;
    bool decayPpvc = false;
    std::vector<TurnoutModel::Observation> history;
    nlohmann::json provenance, sensitivities;
};
Artifact read(nlohmann::json const& value);
Artifact load(std::filesystem::path const& path);
std::vector<TurnoutModel::Unit> units(nlohmann::json const& rows);
std::vector<TurnoutModel::Observation> history(nlohmann::json const& rows);
nlohmann::json diagnostic(Artifact const& artifact, std::vector<TurnoutModel::Unit> const& units,
    TurnoutModel::Result const& result, std::string const& sourceTime);
}
