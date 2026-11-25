#pragma once

#include "LiveInputRecovery.h"
#include "LiveResultsInput.h"
#include "json.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Persistent orchestration only: this class does not estimate votes. Every
// received capture is accepted by the ordinary recovery rules before the newest
// state is selected for a forecast. The serial worker owns the session, so no
// history writer can change the evidence while a forecast is being prepared.
namespace LiveAutomation {
using json = nlohmann::json;
void atomicJson(std::filesystem::path const& path, json const& value);
json readJson(std::filesystem::path const& path);
// Write a complete result before offering it to the publisher. A ready
// package belongs to its existing forecast, including the pre-count origin.
void publishNumerical(std::filesystem::path const& run, json const& numerical);
std::string digest(std::string const& text);
void validateRunId(std::string const& id);
void validateSettings(json const& settings);
// Return the entry that prevents starting a fresh run, if any. Operators can
// prepare a folder with project files, scripts and an incoming feed inbox.
std::optional<std::filesystem::path> newRunConflict(std::filesystem::path const& folder);
std::string suggestRunId(std::filesystem::path const& root, std::string const& election, std::string const& kind);

struct Capture {
    LiveResultsInput::CurrentFile file;
    std::string hash, deliveredAt, sourceTime;
};
class ArrivalInbox;
class Session {
public:
    using Reader = std::function<LiveInputRecovery::Snapshot(LiveResultsInput::CurrentFile const&)>;
    Session(std::filesystem::path root, json settings);
    ~Session();
    // Publish the operator's choices before any forecasting or API requests.
    void saveConfiguration(bool newRun);
    // Restart checks use content identity, not file modification dates. Explicit
    // resume after editing inputs may introduce a new documented input revision.
    // A reopened project can collect arrivals before its model/projection is
    // prepared. In that case there is no new calculation revision to record.
    void arm(std::optional<std::string> inputRevision, bool acknowledgeChange, bool monitor = true);
    void setState(std::string const& state, std::string const& message = "");
    // First discover stable files, then parse their source clocks before sorting.
    // Reader parses commission counts only. Recovery and commits remain here.
    std::optional<Capture> ingest(Reader const& reader,
        std::function<void(LiveInputRecovery::Snapshot const&)> accepted = {});
    std::string reserveForecast(Capture const& capture);
    void finishForecast(Capture const& capture, bool success, std::string const& message);
    bool needsOrigin() const;
    json status() const;
    // Also exposed for deterministic tests/manual scans. The production monitor
    // calls this independently while the calculation thread is occupied.
    void scanArrivals();
    json const& configuration() const { return settings; }
    std::filesystem::path const& directory() const { return folder; }
    std::string revision() const { return ledger.value("input_revision", ""); }
private:
    void loadState();
    void recoverHandoffs();
    std::string restoreReadyForecast(std::filesystem::path const& package);
    void save(bool checkpoint = false);
    std::filesystem::path root, folder;
    json settings, ledger;
    std::unique_ptr<ArrivalInbox> inbox;
};
}
