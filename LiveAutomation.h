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
std::string digest(std::string const& text);
void validateRunId(std::string const& id);
void validateSettings(json const& settings);
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
    void arm(std::string const& inputRevision, bool acknowledgeChange, bool monitor = true);
    void setState(std::string const& state, std::string const& message = "");
    // First discover stable files, then parse their source clocks before sorting.
    // Reader parses commission counts only. Recovery and commits remain here.
    std::optional<Capture> ingest(Reader const& reader,
        std::function<void(LiveInputRecovery::Snapshot const&)> accepted = {});
    std::string reserveForecast(Capture const& capture);
    void finishForecast(Capture const& capture, bool success, std::string const& message);
    json status() const;
    // Also exposed for deterministic tests/manual scans. The production monitor
    // calls this independently while the calculation thread is occupied.
    void scanArrivals();
    json const& configuration() const { return settings; }
    std::filesystem::path const& directory() const { return folder; }
    std::string revision() const { return ledger.value("input_revision", ""); }
private:
    void save();
    std::filesystem::path root, folder;
    json settings, ledger;
    std::unique_ptr<ArrivalInbox> inbox;
};
}
