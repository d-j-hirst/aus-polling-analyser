// Durable local queue for unattended forecasts. Arrival, validation, forecast
// success and upload acknowledgement are separate facts; a failed calculation
// must not erase a useful earlier TCP count or make a later retry look published.
#include "LiveAutomation.h"
#include "Date.h"
#include "LiveHistoryScope.h"
#include "LiveInputHistory.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <io.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace LiveAutomation {
void atomicJson(std::filesystem::path const& path, json const& value) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    // Finish writing the file to storage before replacing the last good
    // copy. Closing an ordinary stream can leave its contents in the OS cache.
    auto contents = value.dump(2);
    std::FILE* stream = nullptr;
#ifdef _WIN32
    _wfopen_s(&stream, temporary.c_str(), L"wb");
#else
    stream = std::fopen(temporary.c_str(), "wb");
#endif
    if (!stream) throw std::runtime_error("Cannot open automation output: " + temporary.string());
    bool flushed = std::fwrite(contents.data(), 1, contents.size(), stream) == contents.size() &&
        std::fflush(stream) == 0;
#ifdef _WIN32
    flushed = flushed && _commit(_fileno(stream)) == 0;
#else
    flushed = flushed && ::fsync(::fileno(stream)) == 0;
#endif
    if (std::fclose(stream) || !flushed) throw std::runtime_error("Cannot write automation state: " + path.string());
#ifdef _WIN32
    // Publish a complete replacement while keeping the previous state readable.
    // Windows readers can briefly prevent replacement even though neither file
    // is damaged. The replay helper and GUI poll this information during a run;
    // one such overlap must not stop an unattended election forecast. Retry
    // only access/locking errors, for at most two seconds. A persistent lock or
    // storage failure still stops the run, preserving the last published file
    // and the completed temporary file for inspection.
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD const error = GetLastError();
        bool const mayBeTemporary = error == ERROR_SHARING_VIOLATION ||
            error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED;
        if (!mayBeTemporary || std::chrono::steady_clock::now() >= deadline) {
            auto const explanation = std::error_code(static_cast<int>(error), std::system_category()).message();
            throw std::runtime_error("Cannot publish automation state: " + path.string() +
                " (Windows error " + std::to_string(error) + ": " + explanation + ")");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
#else
    std::filesystem::rename(temporary, path);
    int directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    bool synced = directory >= 0 && ::fsync(directory) == 0;
    if (directory >= 0) ::close(directory);
    if (!synced) throw std::runtime_error("Cannot persist automation directory: " + path.parent_path().string());
#endif
}
json readJson(std::filesystem::path const& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read automation state: " + path.string());
    // Close the file before interpreting its contents. Parsing a growing run
    // ledger should not prolong the Windows read lock seen by its writer.
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("Cannot read automation state: " + path.string());
    input.close();
    return json::parse(contents);
}
std::string digest(std::string const& text) {
    // Stable identity for local configuration changes, not authentication.
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char c : text) { value ^= c; value *= 1099511628211ULL; }
    std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}
void publishNumerical(std::filesystem::path const& run, json const& numerical) {
    auto id = numerical.at("forecast_id").get<std::string>();
    auto package = run/"packages"/id;
    for (auto marker : {"ready-numerical.json", "publication.json", "ready-publication.json"}) {
        if (std::filesystem::exists(package/marker))
            throw std::runtime_error("Forecast already handed off. Its files were preserved: " + package.string());
    }
    atomicJson(package/"numerical.json", numerical);
    atomicJson(package/"ready-numerical.json", {{"version",1},{"forecast_id",id}});
}
void validateRunId(std::string const& id) {
    static std::regex const slug("[a-z0-9][a-z0-9-]*");
    static std::regex const deviceName("con|prn|aux|nul|com[1-9]|lpt[1-9]");
    if (id.size() > 255 || !std::regex_match(id, slug) || std::regex_match(id, deviceName))
        throw std::runtime_error("Use a run ID with lowercase letters, digits and hyphens, starting with a letter or digit.");
}
std::optional<std::filesystem::path> newRunConflict(std::filesystem::path const& folder) {
    auto const status = std::filesystem::symlink_status(folder);
    if (!std::filesystem::exists(status)) return {};
    if (!std::filesystem::is_directory(status)) return folder;
    auto const manifest = folder/"run.json";
    if (std::filesystem::exists(std::filesystem::symlink_status(manifest))) return manifest;

    // Check the paths written by the watcher, publisher and diagnostics. A
    // partial run may still contain counted votes or upload receipts even if
    // its run.json is missing. The inbox and captures.txt are operator inputs.
    static const std::set<std::string> runFiles = {
        "run.json", "state.json", "state-checkpoint.json", "status.json", "discovery.json", "arrival-status.json",
        "forecast.lock", "publisher.lock", "reporting-status.json", "trial-status.json",
        "commentary-budget.sqlite", "commentary-budget.sqlite-journal",
        "commentary-budget.sqlite-wal", "commentary-budget.sqlite-shm"
    };
    static const std::set<std::string> outputFolders = {
        "arrivals", "feeds", "history", "packages", "receipts", "daily-reports", "benchmark"
    };
    for (auto const& entry : std::filesystem::directory_iterator(folder)) {
        auto name = LiveResultsInput::pathToUtf8(entry.path().filename());
#ifdef _WIN32
        // A differently capitalised name can still be the same Windows file.
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
#endif
        if (outputFolders.contains(name)) {
            if (!std::filesystem::is_directory(entry.symlink_status())) return entry.path();
            for (auto const& output : std::filesystem::recursive_directory_iterator(entry.path())) {
                if (!std::filesystem::is_directory(output.symlink_status())) return output.path();
            }
        }
        // Atomic writers can leave their temporary output behind after a crash.
        if (name.ends_with(".tmp")) name.resize(name.size() - 4);
        if (runFiles.contains(name)) return entry.path();
        if (name.starts_with("snapshot_") && name.find("__run_") != std::string::npos &&
            (name.ends_with(".json") || name.ends_with(".analysis.json.gz")))
            return entry.path();
    }
    return {};
}
std::string suggestRunId(std::filesystem::path const& root, std::string const& election, std::string const& kind) {
    auto const base = election + "-" + kind;
    auto candidate = base;
    for (int number = 2; newRunConflict(root/"live_runs"/candidate); ++number)
        candidate = base + "-" + std::to_string(number);
    return candidate;
}
void validateSettings(json const& s) {
    if (s.value("version", 0) != 1) throw std::runtime_error("Unsupported live automation settings.");
    validateRunId(s.at("run_id").get<std::string>());
    if (!std::regex_match(s.at("election").get<std::string>(), std::regex("[a-z0-9][a-z0-9-]*")))
        throw std::runtime_error("Invalid automation election.");
    if (!Date::parseIso(s.at("election_date").get<std::string>())) throw std::runtime_error("Invalid election date.");
    if (s.at("source_directory").get<std::string>().empty()) throw std::runtime_error("Choose a feed directory.");
    if (s.value("kind", "replay") != "replay" && s.value("kind", "replay") != "live")
        throw std::runtime_error("Run kind must be live or replay.");
    if (s.value("visibility", "private") != "private" && s.value("visibility", "private") != "public")
        throw std::runtime_error("Run visibility must be public or private.");
    if (s.value("timezone", "").empty()) throw std::runtime_error("An election timezone is required.");
    int const cutoff = s.value("night_end_hour",6);
    if (cutoff < 0 || cutoff > 23) throw std::runtime_error("Comparison cutoff hour must be between 0 and 23.");
}

namespace {
void checkRunIdentity(std::filesystem::path const& manifest, json const& settings) {
    if (!std::filesystem::exists(manifest)) return;
    auto original = readJson(manifest);
    for (auto key : {"run_id","election","kind","timezone","election_date","display_name","visibility"})
        if (original.at(key) != settings.at(key))
            throw std::runtime_error(std::string("Run identity cannot change ") + key + ". Create a new run.");
}

std::filesystem::path sourceFolder(std::filesystem::path const& workspace, json const& settings) {
    auto source = LiveResultsInput::resolveDirectory(settings.at("source_directory").get<std::string>());
    return source.is_relative() ? workspace/source : source;
}

std::optional<std::string> captureCode(std::string const& name, std::string const& election) {
    std::regex pattern(election.ends_with("sa") ? "^el[0-9]{4}([0-9]{12})\\.xml$" :
        election.ends_with("wa") ? "^waec-la-verbose-([0-9]{14})\\.xml$" :
        ".*?([0-9]{8})_?([0-9]{6})(?:[^0-9].*)?$",std::regex::icase);
    std::smatch match;
    if (!std::regex_match(name,match,pattern)) return {};
    auto code = match[1].str();
    if (match.size() > 2) code += match[2].str();
    if (code.size() == 12) code = "20"+code;
    if (!Timestamp::parseCompactLocal(code)) return {};
    return code;
}
}

// Arrival collection has no access to forecast state or accepted history. Its
// only job is to preserve completed deliveries while the simulation is busy.
// The OS releases the run lock on a crash, so a stale lock filename is harmless.
class ArrivalInbox {
public:
    ArrivalInbox(std::filesystem::path sourceDirectory,std::filesystem::path runDirectory,std::string term,
        bool newRun = false)
        : source(std::move(sourceDirectory)),folder(std::move(runDirectory)),election(std::move(term)) {
        std::filesystem::create_directories(folder);
        // A fresh run must claim the name exactly once, even when two GUIs
        // both inspected the prepared folder before either saved its settings.
#ifdef _WIN32
        lease = CreateFileW((folder/"forecast.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
            newRun ? CREATE_NEW : OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (lease == INVALID_HANDLE_VALUE) throw std::runtime_error("Another forecasting process owns this run, or its state is not writable.");
#else
        lease = ::open((folder/"forecast.lock").c_str(),O_RDWR|O_CREAT|(newRun ? O_EXCL : 0),0600);
        if (lease < 0 || flock(lease,LOCK_EX|LOCK_NB)) {
            if (lease >= 0) ::close(lease);
            throw std::runtime_error("Another forecasting process owns this run, or its state is not writable.");
        }
#endif
        try {
            std::filesystem::create_directories(folder/"arrivals");
            discovery = std::filesystem::exists(folder/"discovery.json") ? readJson(folder/"discovery.json") : json::object();
            for (auto const& entry : std::filesystem::directory_iterator(folder/"arrivals"))
                if (entry.path().extension() == ".json") ++collected;
        }
        catch (...) { release(); throw; }
    }
    ~ArrivalInbox() { stop(); release(); }
    void start() {
        worker = std::thread([this] {
            for (;;) {
                try { scan(); }
                catch (std::exception const& error) { std::lock_guard lock(mutex); failure = error.what(); }
                std::unique_lock lock(mutex);
                if (wake.wait_for(lock,std::chrono::seconds(1),[this] { return stopping; })) return;
            }
        });
    }
    void stop() {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }
    json status() const {
        std::lock_guard lock(mutex);
        return {{"latest_received",latest},{"collector_error",failure}};
    }
    void scan() {
        std::lock_guard lock(mutex);
        bool changed = false;
        if (!std::filesystem::is_directory(source)) throw std::runtime_error("Feed folder is unavailable: "+source.string());
        for (auto const& entry : std::filesystem::directory_iterator(source)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".xml" && ext != ".zip") continue;
            auto name = LiveResultsInput::pathToUtf8(entry.path().filename());
            auto stamp = std::to_string(entry.last_write_time().time_since_epoch().count())+"/"+std::to_string(entry.file_size());
            auto& seen = discovery[name];
            if (!seen.is_object() || seen.value("stamp", "") != stamp) {
                seen = {{"stamp",stamp},{"delivered_at",Timestamp::now().formatIsoUtc()},{"stable",false}};
                changed = true;
                continue;
            }
            if (!entry.file_size() || seen.value("stable",false)) continue;
            auto hash = LiveInputRecovery::fingerprint(entry.path());
            auto code = captureCode(name,election);
            // WA lacks a feed-wide source clock: its capture filename supplies
            // a new observation even when the candidate counts are unchanged.
            auto identity = digest(hash+"/"+code.value_or(""));
            auto retained = folder/"feeds"/hash/entry.path().filename();
            if (!std::filesystem::exists(retained)) {
                std::filesystem::create_directories(retained.parent_path());
                auto temporary = retained; temporary += ".part";
                std::filesystem::copy_file(entry.path(),temporary,std::filesystem::copy_options::overwrite_existing);
                if (LiveInputRecovery::fingerprint(temporary) != hash || stamp !=
                    std::to_string(entry.last_write_time().time_since_epoch().count())+"/"+std::to_string(entry.file_size())) {
                    std::filesystem::remove(temporary);
                    continue;
                }
                std::filesystem::last_write_time(temporary,entry.last_write_time());
                std::filesystem::rename(temporary,retained);
            }
            auto arrival = folder/"arrivals"/(identity+".json");
            if (!std::filesystem::exists(arrival)) {
                json record{{"file",LiveResultsInput::pathToUtf8(retained)},{"hash",identity},{"content_hash",hash},
                    {"delivered_at",seen.at("delivered_at")},{"state","discovered"}};
                if (code) record["capture_code"] = *code;
                atomicJson(arrival,record);
                ++collected;
            }
            latest = seen.at("delivered_at"); seen["stable"] = true;
            changed = true;
        }
        if (changed) {
            atomicJson(folder/"discovery.json",discovery);
            atomicJson(folder/"arrival-status.json",{{"latest_received",latest},{"collected",collected},{"error",failure}});
        }
    }
private:
    void release() {
#ifdef _WIN32
        CloseHandle(lease);
#else
        ::close(lease);
#endif
    }
    std::filesystem::path source,folder;
    std::string election,latest,failure;
    json discovery;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::size_t collected = 0;
    std::thread worker;
#ifdef _WIN32
    HANDLE lease = INVALID_HANDLE_VALUE;
#else
    int lease = -1;
#endif
};

Session::Session(std::filesystem::path workspace, json config) : root(std::move(workspace)), settings(std::move(config)) {
    validateSettings(settings);
    folder = root/"live_runs"/settings.at("run_id").get<std::string>();
    checkRunIdentity(folder/"run.json", settings);
    loadState();
}
Session::~Session() = default;
void Session::loadState() {
    // The project file supplies the run identity. Progress is always read
    // from this run's disk records, even if the open project predates counting.
    auto primary = folder/"state.json";
    auto checkpoint = folder/"state-checkpoint.json";
    auto load = [&](std::filesystem::path const& path) {
        auto state = readJson(path);
        if (state.at("version") != 1 || !state.at("run_id").is_string() || !state.at("captures").is_object() ||
            !state.at("next_forecast").is_number_integer() ||
            state.at("next_forecast").get<int>() < 1 || !state.at("state").is_string())
            throw std::runtime_error("Invalid forecasting state: " + path.string());
        return state;
    };
    if (std::filesystem::exists(primary)) {
        try { ledger = load(primary); }
        catch (std::exception const&) {
            if (!std::filesystem::exists(checkpoint)) throw;
            ledger = load(checkpoint);
        }
    } else if (std::filesystem::exists(checkpoint)) {
        ledger = load(checkpoint);
    } else {
        // An empty prepared folder is a normal new run. Evidence of previous
        // operation requires recovery, never a fresh forecast sequence at 1.
        for (auto name : {"arrivals", "feeds", "packages", "receipts", "history"}) {
            if (!std::filesystem::exists(folder/name)) continue;
            for (auto const& item : std::filesystem::recursive_directory_iterator(folder/name)) {
                if (item.is_regular_file())
                    throw std::runtime_error("Missing forecasting state. Restore state.json or state-checkpoint.json in " + folder.string());
            }
        }
        ledger = {{"version",1},{"run_id",settings.at("run_id")},{"state","paused"},
            {"captures",json::object()},{"seen",json::object()},{"next_forecast",1}};
    }
    if (ledger.at("run_id") != settings.at("run_id"))
        throw std::runtime_error("Automation state belongs to a different run.");
}
void Session::saveConfiguration(bool newRun) {
    auto source = sourceFolder(root, settings);
    bool const defaultInbox = source.lexically_normal() == (folder/"inbox").lexically_normal();
    if (newRun) {
        if (auto conflict = newRunConflict(folder))
            throw std::runtime_error("Cannot start a new run here. Choose another ID or inspect: " + conflict->string());
    }
    // An unavailable delivery location can be corrected without claiming a
    // new ID. A missing default inbox will be created once we own the run.
    if (!std::filesystem::is_directory(source) &&
        (!defaultInbox || std::filesystem::exists(std::filesystem::symlink_status(source))))
        throw std::runtime_error("Feed folder is unavailable: " + source.string());
    // Use the same forecast lock as Watch / Resume. This protects an existing
    // run from a configuration edit while another program instance is writing.
    ArrivalInbox lease(source, folder, settings.at("election"), newRun);
    auto manifest = folder/"run.json";
    checkRunIdentity(manifest, settings);
    if (defaultInbox)
        std::filesystem::create_directories(source);
    if (!newRun) {
        loadState();
        if (ledger.value("state", "paused") == "armed") {
            // The local lease still protects both files. An interrupted run
            // needs an explicit Resume after its configuration has changed.
            ledger["state"] = "paused";
            ledger["message"] = "";
            save(true);
        }
    }
    settings.erase("input_revision");
    if (ledger.contains("input_revision")) settings["input_revision"] = ledger.at("input_revision");
    atomicJson(manifest, settings);
}
void Session::save(bool checkpoint) {
    atomicJson(folder/"state.json",ledger);
    // One additional ledger covers the last completed attempt and explicit
    // arm/pause decisions. Feed observations and forecast payloads stay single-copy.
    if (checkpoint) atomicJson(folder/"state-checkpoint.json",ledger);
    json counts = json::object();
    int awaitingForecast = 0;
    for (auto const& [hash,record] : ledger["captures"].items()) {
        auto state = record.value("state", "unknown");
        counts[state] = counts.value(state,0)+1;
        if (state == "accepted" && record.at("source_time").get<std::string>() > ledger.value("last_attempt_source", ""))
            ++awaitingForecast;
    }
    json summary{{"state",ledger.value("state","paused")},{"counts",counts},
        {"latest_received",ledger.value("latest_received","")},
        {"latest_accepted",ledger.value("latest_accepted","")},
        {"latest_forecast",ledger.value("latest_forecast","")},
        {"known_arrivals",ledger["captures"].size()},{"awaiting_forecast",awaitingForecast},
        {"last_forecast_error",ledger.value("last_forecast_error","")},
        {"last_rejection",ledger.value("last_rejection","")},
        {"error",ledger.value("message","")}};
    atomicJson(folder/"status.json",summary);
}
namespace {
void checkUploadedPackages(std::filesystem::path const& folder) {
    auto packages = folder/"packages";
    // A destination can already have accepted a package the operator's local
    // copy has lost. Recreating it would produce a different immutable report.
    if (std::filesystem::is_directory(folder/"receipts")) {
        for (auto const& item : std::filesystem::directory_iterator(folder/"receipts")) {
            if (item.path().extension() != ".json") continue;
            auto receipts = readJson(item.path());
            if (!receipts.contains("forecasts")) continue;
            for (auto const& [id, receipt] : receipts.at("forecasts").items())
                if (receipt.value("accepted", false) && !std::filesystem::is_regular_file(packages/id/"publication.json"))
                    throw std::runtime_error("Restore the locally missing, already uploaded forecast " + id + " in " + packages.string());
        }
    }
}
}
std::string Session::restoreReadyForecast(std::filesystem::path const& package) {
    auto id = package.filename().string();
    auto report = readJson(package/"numerical.json");
    if (report.at("forecast_id") != id || report.at("run_id") != settings.at("run_id") ||
        readJson(package/"ready-numerical.json").at("forecast_id") != id)
        throw std::runtime_error("Inconsistent forecast identity at " + package.string());
    auto hash = report.at("source_fingerprint").get<std::string>();
    auto time = report.at("source_time").get<std::string>();
    if (hash.empty() || time.empty() || std::filesystem::path(hash).filename() != hash || hash == "." || hash == "..")
        throw std::runtime_error("Missing or invalid source identity at " + package.string());
    auto& records = ledger.at("captures");
    if (!records.contains(hash)) {
        // The capture arrived after the backup. Ingestion will
        // revalidate its original delivery and retain its count history.
        auto arrival = readJson(folder/"arrivals"/(hash+".json"));
        if (arrival.at("hash") != hash) throw std::runtime_error("Forecast and original delivery disagree: " + id);
        records[hash] = std::move(arrival);
    }
    auto& record = records.at(hash);
    if (record.value("source_time", time) != time ||
        (ledger.contains("in_flight") && ledger["in_flight"].at("id") == id && ledger["in_flight"].at("hash") != hash))
        throw std::runtime_error("Forecast and retained feed disagree at " + package.string());
    record["source_time"] = time;
    // Older runs could deliberately recalculate one feed after an edit.
    // Preserve both archived IDs and remember the last successful one.
    if (record.value("forecast", "") != "completed" || record.value("forecast_id", "") < id)
        record["forecast_id"] = id;
    record["forecast"] = "completed";
    record["forecast_message"] = "Recovered completed numerical handoff after restart.";
    return time;
}
void Session::recoverHandoffs() {
    // Usually the ledger already records every completion. A power loss can
    // leave a finished package ahead of the ledger, or a partial directory
    // whose number must remain reserved. Examine the large numerical report
    // only when its completion is missing from the ledger.
    std::set<std::string> completed;
    int next = ledger.at("next_forecast").get<int>();
    static const std::regex numbered("f[0-9]{8}");
    auto attempted = ledger.value("last_attempt_source", "");
    auto latest = ledger.value("latest_forecast", "");
    for (auto const& record : ledger.at("captures")) {
        auto id = record.value("forecast_id", "");
        if (std::regex_match(id, numbered)) next = std::max(next, std::stoi(id.substr(1)) + 1);
        auto outcome = record.value("forecast", "");
        if (outcome != "completed" && outcome != "failed") continue;
        auto time = record.at("source_time").get<std::string>();
        attempted = std::max(attempted, time);
        if (outcome == "completed") {
            completed.insert(record.at("forecast_id").get<std::string>());
            latest = std::max(latest, time);
        }
    }
    auto packages = folder/"packages";
    if (std::filesystem::is_directory(packages)) {
        for (auto const& item : std::filesystem::directory_iterator(packages)) {
            auto package = item.path();
            auto id = package.filename().string();
            if (!item.is_directory() || (id != "origin" && !std::regex_match(id, numbered))) continue;
            if (id != "origin") next = std::max(next, std::stoi(id.substr(1)) + 1);
            if (!std::filesystem::exists(package/"ready-numerical.json")) {
                if (std::filesystem::exists(package/"publication.json") || std::filesystem::exists(package/"ready-publication.json"))
                    throw std::runtime_error("Published forecast is missing its numerical handoff. Restore the package at " + package.string());
                continue;
            }
            if (!std::filesystem::is_regular_file(package/"numerical.json"))
                throw std::runtime_error("Ready forecast is missing numerical.json at " + package.string());
            if (id == "origin" || completed.contains(id)) continue;
            auto time = restoreReadyForecast(package);
            completed.insert(id);
            attempted = std::max(attempted, time);
            latest = std::max(latest, time);
        }
    }
    if (ledger.contains("in_flight")) {
        auto const& attempt = ledger.at("in_flight");
        auto const& source = ledger.at("captures").at(attempt.at("hash").get<std::string>());
        if (completed.contains(attempt.at("id").get<std::string>()) || source.value("forecast", "") == "completed")
            ledger.erase("in_flight");
    }
    checkUploadedPackages(folder);
    ledger["next_forecast"] = next;
    if (!latest.empty()) ledger["latest_forecast"] = latest;
    if (!attempted.empty()) ledger["last_attempt_source"] = attempted;
    if (!latest.empty() && latest == attempted) ledger["last_forecast_error"] = "";
}
void Session::arm(std::optional<std::string> revision, bool acknowledgeChange, bool monitor) {
    // Acquire ownership before recovering a completion or changing the ledger.
    // Another GUI process may already be operating on the same saved project.
    bool startMonitor = !inbox;
    if (!inbox) {
        inbox = std::make_unique<ArrivalInbox>(sourceFolder(root, settings),folder,settings.at("election"));
        try { loadState(); }
        catch (...) { inbox.reset(); throw; }
    }
    if (!acknowledgeChange && std::filesystem::exists(folder/"run.json")) {
        auto previous = readJson(folder/"run.json");
        auto current = settings;
        previous.erase("input_revision"); current.erase("input_revision");
        if (previous != current)
            throw std::runtime_error("Watcher settings changed since this run stopped. Review them and explicitly Resume.");
    }
    try { recoverHandoffs(); }
    catch (...) { inbox.reset(); throw; } // Preserve both copies for recovery.
    if (revision && ledger.contains("input_revision") && ledger["input_revision"] != *revision && !acknowledgeChange)
        throw std::runtime_error("Forecast inputs changed since this run stopped. Review them and explicitly Resume.");
    if (revision && ledger.value("input_revision", "") != *revision) {
        if (!ledger.contains("revisions")) ledger["revisions"] = json::array();
        ledger["revisions"].push_back({{"revision",*revision},{"at",Timestamp::now().formatIsoUtc()}});
        ledger["input_revision"] = *revision;
    }
    // Older sessions used this flag to repeat the latest forecast after an
    // input edit. A revision applies from the next distinct feed observation.
    ledger.erase("needs_forecast");
    settings.erase("input_revision");
    if (ledger.contains("input_revision")) settings["input_revision"] = ledger.at("input_revision");
    atomicJson(folder/"run.json",settings);
    setState("armed");
    if (monitor && startMonitor) inbox->start();
}
void Session::setState(std::string const& state, std::string const& message) {
    if (!inbox) throw std::runtime_error("This process does not own the forecasting run; its state was not changed.");
    ledger["state"] = state; ledger["message"] = message; save(true);
    if (state != "armed") inbox.reset();
}
void Session::scanArrivals() { if (inbox) inbox->scan(); }
bool Session::needsOrigin() const {
    if (std::filesystem::is_regular_file(folder/"packages"/"origin"/"ready-numerical.json")) return false;
    // Every counting forecast is compared to the same pre-count result. A
    // rebuilt projection must never supply a new origin halfway through a run.
    if (ledger.contains("in_flight") || !ledger.value("last_attempt_source", "").empty())
        throw std::runtime_error("Restore the original start-of-counting forecast in " + (folder/"packages"/"origin").string());
    return true;
}
json Session::status() const {
    auto result = ledger;
    if (inbox) result.update(inbox->status());
    return result;
}

std::optional<Capture> Session::ingest(Reader const& reader,
    std::function<void(LiveInputRecovery::Snapshot const&)> accepted) {
    if (ledger.value("state", "paused") != "armed") return {};
    if (inbox && !inbox->status().value("collector_error", "").empty())
        throw std::runtime_error(inbox->status().at("collector_error").get<std::string>());
    // Snapshot the arrival journal at this boundary. Arrivals copied later are
    // deliberately left for the next calculation, keeping this history fixed.
    bool changed = false;
    for (auto const& entry : std::filesystem::directory_iterator(folder/"arrivals")) {
        if (entry.path().extension() != ".json") continue;
        auto record = readJson(entry.path());
        std::string hash = record.at("hash");
        if (!ledger["captures"].contains(hash)) { ledger["captures"][hash] = std::move(record); changed = true; }
    }
    if (changed) save();
    std::vector<Capture> parsed;
    std::optional<LiveInputRecovery::Snapshot> singleSnapshot;
    std::map<std::string,std::string> acceptedAtTime;
    for (auto const& [hash,record] : ledger["captures"].items())
        if (record.value("state", "") == "accepted") acceptedAtTime[record.at("source_time")] = hash;
    // Revisit discovered captures after a crash. Parsing errors reject this
    // capture; storage/history errors below pause the entire session instead.
    for (auto& [hash,record] : ledger["captures"].items()) {
        if (record.value("state", "") != "discovered") continue;
        changed = true;
        Capture capture{{LiveResultsInput::pathFromUtf8(record.at("file").get<std::string>()),{}},
                        hash,record.at("delivered_at"),""};
        if (record.contains("capture_code")) capture.file.timestamp = record.at("capture_code").get<std::string>();
        try {
            auto snapshot = reader(capture.file);
            if (snapshot.election != settings.at("election") || snapshot.sourceTime.empty())
                throw std::runtime_error("Feed has the wrong election or no source time.");
            capture.sourceTime = snapshot.sourceTime;
            parsed.push_back(capture);
            record["source_time"] = capture.sourceTime;
            record["parsed_content_hash"] = snapshot.sourceHash;
            // Keep only one parsed capture. A long downtime backlog must not
            // hold hundreds of complete elections in memory simultaneously.
            if (parsed.size() == 1) singleSnapshot = std::move(snapshot);
            else singleSnapshot.reset();
        } catch (LiveResultsInput::ProcessingError const&) {
            throw;
        } catch (std::filesystem::filesystem_error const&) {
            throw;
        } catch (std::exception const& error) {
            record["state"] = "rejected"; record["error"] = error.what();
            ledger["last_rejection"] = capture.file.path.filename().string()+": "+error.what();
        }
    }
    std::sort(parsed.begin(),parsed.end(),[](auto const& a,auto const& b) {
        return std::tie(a.sourceTime,a.deliveredAt,a.hash) < std::tie(b.sourceTime,b.deliveredAt,b.hash);
    });
    auto history = folder/"history"/"accepted-counts";
    for (auto& item : parsed) {
        auto& record = ledger["captures"][item.hash];
        auto found = acceptedAtTime.find(item.sourceTime);
        if (found != acceptedAtTime.end() && found->second != item.hash) {
            auto const& previousRecord = ledger["captures"][found->second];
            // Different ZIP packaging can contain the exact same XML. Compare
            // the parsed source bytes as well as retaining the original archive.
            bool same = previousRecord.value("parsed_content_hash",previousRecord.at("content_hash")) ==
                record.value("parsed_content_hash",record.at("content_hash"));
            record["state"] = same ? "duplicate" : "rejected";
            record["error"] = same ? "Identical source observation already accepted." : "Conflicting contents at an accepted source time.";
            if (!same) ledger["last_rejection"] = item.file.path.filename().string()+": "+record["error"].get<std::string>();
            continue;
        }
        auto snapshot = singleSnapshot ? std::move(*singleSnapshot) : reader(item.file);
        auto previous = LiveInputRecovery::loadAcceptedCompatiblePrevious(history,snapshot);
        auto recovered = LiveInputRecovery::recover(std::move(snapshot),previous,
            [&](auto const& current,auto old,auto account) { return LiveInputRecovery::loadEarlierCounts(history,current,old,account); });
        auto preferences = LiveInputRecovery::loadPreferenceHistory(history,recovered.snapshot);
        LiveInputRecovery::retainPreferenceHistory(recovered.snapshot,preferences);
        LiveInputRecovery::commit(history,recovered.snapshot);
        if (accepted) accepted(recovered.snapshot);
        record["state"] = "accepted"; record["source_time"] = item.sourceTime;
        record["issues"] = recovered.issues.summary();
        acceptedAtTime[item.sourceTime] = item.hash;
        if (item.deliveredAt > ledger.value("latest_received", "")) ledger["latest_received"] = item.deliveredAt;
        if (item.sourceTime > ledger.value("latest_accepted", "")) ledger["latest_accepted"] = item.sourceTime;
        save();
    }
    if (changed) save();
    std::optional<Capture> newest;
    for (auto const& [hash,record] : ledger["captures"].items()) {
        if (record.value("state", "") != "accepted") continue;
        std::string time = record.at("source_time");
        if (time <= ledger.value("last_attempt_source", "") || record.value("forecast", "") == "completed") continue;
        if (!newest || time > newest->sourceTime)
            newest = Capture{{LiveResultsInput::pathFromUtf8(record.at("file").get<std::string>()),{}},hash,record.at("delivered_at"),time};
    }
    if (newest) {
        std::string code;
        for (unsigned char c : newest->sourceTime.substr(0,19)) if (std::isdigit(c)) code += char(c);
        newest->file.timestamp = code;
        newest->file.sourceObservationTime = newest->sourceTime;
    }
    return newest;
}
std::string Session::reserveForecast(Capture const& capture) {
    if (capture.sourceTime <= ledger.value("last_attempt_source", "") ||
        ledger.at("captures").at(capture.hash).value("forecast", "") == "completed")
        throw std::runtime_error("This feed observation has already been attempted in this run: " + capture.sourceTime);
    if (ledger.contains("in_flight") && ledger["in_flight"].at("hash") == capture.hash &&
        ledger["in_flight"].value("input_revision", "") == revision())
        return ledger["in_flight"].at("id"); // An unfinished attempt retains its identity after a crash.
    if (ledger.contains("in_flight")) {
        std::string interrupted = ledger["in_flight"].at("hash");
        ledger["captures"].at(interrupted)["forecast"] = "interrupted_superseded";
    }
    int sequence = ledger.value("next_forecast",1); ledger["next_forecast"] = sequence+1;
    std::ostringstream id; id << "f" << std::setfill('0') << std::setw(8) << sequence;
    for (auto& [hash,record] : ledger["captures"].items())
        if (record.value("state", "") == "accepted" && !record.contains("forecast") &&
            record.at("source_time").get<std::string>() < capture.sourceTime) record["forecast"] = "skipped_newer_available";
    auto& record = ledger["captures"].at(capture.hash);
    record["forecast_id"] = id.str(); record["forecast"] = "running";
    ledger["in_flight"] = {{"id",id.str()},{"hash",capture.hash},{"source_time",capture.sourceTime},
        {"input_revision",revision()}};
    save(); return id.str();
}
void Session::finishForecast(Capture const& capture,bool success,std::string const& message) {
    ledger["last_attempt_source"] = capture.sourceTime;
    auto& record = ledger["captures"][capture.hash];
    record["forecast"] = success ? "completed" : "failed";
    record["forecast_message"] = message;
    ledger["last_forecast_error"] = success ? "" : message;
    if (success) ledger["latest_forecast"] = capture.sourceTime;
    ledger.erase("in_flight"); save(true);
}
}
