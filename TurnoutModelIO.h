#pragma once
#include "TurnoutModel.h"
#include "json.h"
#include <filesystem>

namespace TurnoutModelIO {
inline constexpr int SchemaVersion = 1;
struct InactiveContest {
    std::string status, reason;
};
struct Artifact {
    TurnoutModel::Prior prior;
    TurnoutModel::Options options;
    std::optional<double> pollClose;
    bool decayPpvc = false;
    // Counting procedure, not an election-specific recovery exception. Under
    // optional preferential voting, exhausted ballots legitimately reduce TCP.
    bool optionalPreferential = false;
    nlohmann::json provenance, sensitivities;
    // A postponed contest remains in the forecast, but has no count account
    // for this election's active counting period.
    std::map<std::string, InactiveContest> inactiveContests;
};
// Every live election has an explicit count prior. Missing inputs are an input
// error, rather than a request to select a different vote-size model.
std::filesystem::path priorPath(std::filesystem::path const& workspaceRoot, std::string const& election);
std::filesystem::path historyDirectory(std::filesystem::path const& workspaceRoot, std::string const& election);
Artifact loadForElection(std::filesystem::path const& workspaceRoot, std::string const& election);
// Require every active source district to have a prior. Explicitly postponed
// districts can be absent, but cannot silently discard any reported votes.
void validateDistrictPopulation(Artifact const& artifact,
    std::map<std::string, double> const& reportedCounts);
// Identify how service counts are grouped. Changed definitions must not turn
// an older cached allocation into apparent voter/counting behaviour.
std::string observationMapping(TurnoutModel::Prior const& prior);
// Retained observations are a cache of feeds already received, not posterior
// forecasts. Replay reads only observations strictly earlier than its source.
std::vector<TurnoutModel::Observation> loadHistory(std::filesystem::path const& directory,
    std::string const& election, std::string const& sourceTime, std::string const& mapping);
void recordObservation(std::filesystem::path const& directory, std::string const& election,
    std::string const& sourceTime, TurnoutModel::Observation const& observation, std::string const& mapping);
Artifact read(nlohmann::json const& value);
Artifact load(std::filesystem::path const& path);
std::vector<TurnoutModel::Unit> units(nlohmann::json const& rows);
std::vector<TurnoutModel::Observation> history(nlohmann::json const& rows);
nlohmann::json diagnostic(Artifact const& artifact, std::vector<TurnoutModel::Unit> const& units,
    TurnoutModel::Result const& result, std::string const& sourceTime);
}
