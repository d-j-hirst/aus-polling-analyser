#include "../LiveInputHistory.h"
#include "../LiveInputRecovery.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace {
using json = nlohmann::json;
using Path = std::filesystem::path;
using namespace LiveInputRecovery;

std::string timeAt(int minute) {
    std::ostringstream out;
    out << "2026-03-30T" << std::setw(2) << std::setfill('0') << 8 + minute / 60
        << ':' << std::setw(2) << minute % 60 << ":00";
    return out.str();
}
std::string stem(std::string time) {
    std::replace(time.begin(), time.end(), ':', '-');
    return time;
}
json read(Path const& path) {
    std::ifstream stream(path); json result; stream >> result; return result;
}
void write(Path const& path, json const& value) {
    std::filesystem::create_directories(path.parent_path());
    auto previous = std::filesystem::exists(path) ? std::filesystem::last_write_time(path) :
        std::filesystem::file_time_type::clock::now();
    { std::ofstream stream(path); stream << value; }
    // Simulate an external replacement without relying on filesystem clock
    // resolution or sleeps. Internal commits have explicit revision notification.
    std::filesystem::last_write_time(path, previous + std::chrono::seconds(1));
}
void validate(json const& value) {
    if (value.at("version") != 1 || !value.at("records").is_array())
        throw std::runtime_error("Unsupported test source.");
}
json source(int minute, int votes = 100) {
    auto time = timeAt(minute);
    json records = json::array();
    for (auto const& district : {"North", "South"}) {
        json fp{{"v", {{1, votes}, {2, 70}, {3, 30}}}, {"t", time}, {"s", 0}, {"complete", false}, {"stage", 1}};
        auto tcp = fp; tcp["v"] = {{1, votes + 20}, {2, 80}};
        records.push_back({{"k", std::string(district) + "/ordinary/School"}, {"d", district}, {"n", "School"},
            {"r", "ordinary"}, {"i", std::string(district) + "/candidate-identities"}, {"c", {1, 2, 3}},
            {"e", 400.0}, {"closed", false}, {"fp", fp}, {"tcp", tcp}});
    }
    return {{"version", 1}, {"election", "fictional"}, {"source_time", time}, {"source_fingerprint", time},
        {"opv", false}, {"districts", {{"North", {1000, false}}, {"South", {1000, false}}}}, {"records", records}};
}
Path save(Path const& folder, json const& value, bool notify = false) {
    auto code = stem(value.at("source_time"));
    auto path = folder / (code + ".json");
    write(path, value);
    if (notify) LiveInputHistory::committed(folder, code, validate);
    return path;
}

void compareAll(Path const& folder, int beforeMinute = 60) {
    auto cutoff = stem(timeAt(beforeMinute));
    std::map<std::string, json> expected;
    LiveInputHistory::Selection selection;
    for (auto const& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".json" || entry.path().stem().string() >= cutoff) continue;
        auto data = read(entry.path());
        expected[entry.path().stem().string()] = data;
        for (auto const& row : data.at("records"))
            selection[row.at("d").get<std::string>()].insert(row.at("k").get<std::string>());
    }
    std::map<std::string, std::set<std::size_t>> seen;
    LiveInputHistory::visitEarlier(folder, cutoff, selection, validate,
        [&](std::string const& code, std::size_t position, json const& observed) {
            auto original = expected.at(code);
            original["records"] = json::array({original.at("records").at(position)});
            assert(original == observed); // Includes every field and original clock.
            assert(seen[code].insert(position).second);
            return true;
        });
    for (auto const& [code, original] : expected) assert(seen[code].size() == original.at("records").size());
}

void checkRepresentationAndUpdates(Path const& folder) {
    LiveInputHistory::clearMemoryCache();
    save(folder, source(0)); save(folder, source(10)); save(folder, source(20));
    compareAll(folder);
    auto initial = LiveInputHistory::statistics();
    assert(initial.sourceReads == 3 && initial.builds == 1);
    compareAll(folder);
    assert(LiveInputHistory::statistics().sourceReads == initial.sourceReads);

    // A backfill B between two unchanged A observations must end at the next A.
    auto middle = source(5, 150);
    save(folder, middle); compareAll(folder);
    assert(LiveInputHistory::statistics().sourceReads == initial.sourceReads + 1);
    middle["records"][0]["fp"]["stage"] = 2; // Same counts, different recheck authority.
    middle["records"][0]["fp"]["s"] = 1;
    middle["records"][0]["fp"]["t"] = timeAt(0); // Fixed restored clock, not source clock.
    middle["records"][0]["tcp_basis"] = {{"fp", middle["records"][0]["fp"]},
        {"tcp", middle["records"][0]["tcp"]}, {"transfers", json::array()}, {"rechecked_reconciled_pair", true}};
    save(folder, middle, true); compareAll(folder);
    assert(LiveInputHistory::statistics().builds == 1);

    auto absent = source(6);
    absent["records"].erase(0); absent["districts"].erase("North");
    save(folder, absent); compareAll(folder);
    auto returning = source(7); returning["records"][0]["i"] = "changed identity";
    returning["records"][0]["tcp"]["v"] = nullptr;
    returning["records"][0]["tcp"]["s"] = 2;
    save(folder, returning); compareAll(folder);
    auto legacy = source(8);
    legacy["records"][0]["fp"].erase("stage");
    legacy["records"][0]["fp"].erase("complete");
    save(folder, legacy); compareAll(folder);
    std::filesystem::remove(folder / (stem(timeAt(5)) + ".json")); compareAll(folder);
    std::filesystem::remove(folder / (stem(timeAt(20)) + ".json")); compareAll(folder);
    save(folder, source(30), true); compareAll(folder);

    // Reordering rows changes no candidate data but must retain source ordering.
    auto reordered = source(10);
    std::swap(reordered["records"][0], reordered["records"][1]);
    save(folder, reordered, true); compareAll(folder);
    compareAll(folder, 7); compareAll(folder, 5); compareAll(folder, 31);
    LiveInputHistory::clearMemoryCache(); // Application restart: no raw history reread.
    compareAll(folder);
    assert(LiveInputHistory::statistics().sourceReads == 0);
    assert(LiveInputHistory::statistics().builds == 0);
    LiveInputHistory::clearMemoryCache(1); // Force every partition to be evicted.
    compareAll(folder);
    assert(LiveInputHistory::statistics().retainedBytes == 0);
    assert(LiveInputHistory::statistics().sourceReads == 0);
    LiveInputHistory::clearMemoryCache();
}

void checkRepairAndCoverage(Path const& folder) {
    save(folder, source(0)); save(folder, source(10));
    auto future = source(120); future["version"] = 999;
    save(folder, future);
    compareAll(folder, 11); // A malformed future source cannot affect an earlier replay.
    auto index = folder / "change-index-v1";
    write(index / "manifest.json", {{"interrupted", true}});
    compareAll(folder, 11);
    auto manifest = read(index / "manifest.json").at("payload");
    std::string file = manifest.at("partitions").begin().value().at("file");
    write(index / file, {{"damaged", true}});
    LiveInputHistory::clearMemoryCache();
    compareAll(folder, 11); // A damaged partition rebuilds from original observations.
    write(index / "part-interrupted-unused.json", {{"unfinished", true}});
    write(index / "manifest.tmp", {{"unfinished", true}});
    compareAll(folder, 11); // Unpublished writes are invisible.
    save(folder, source(120)); compareAll(folder, 121);
    compareAll(folder, 1); compareAll(folder, 11);
    assert(LiveInputHistory::statistics().retainedBytes <= 16 * 1024 * 1024);
}

json cohort(std::optional<PreferenceCohort> const& value) {
    if (!value) return nullptr;
    return {{"fp", value->fp}, {"tcp", value->tcp}, {"fpTime", value->fpTime}, {"tcpTime", value->tcpTime},
        {"transfers", value->transfers}, {"rechecked", value->recheckedReconciledPair}};
}
json preferences(PreferenceHistories const& histories) {
    json result = json::object();
    for (auto const& [key, value] : histories)
        result[key] = {cohort(value.matchingCurrentTcp), cohort(value.previousAligned), cohort(value.tcpBasis)};
    return result;
}
Snapshot accepted(int minute, Counts fp, std::optional<Counts> tcp) {
    Snapshot result;
    result.election = "fictional"; result.sourceTime = timeAt(minute); result.sourceHash = result.sourceTime;
    result.districts["North"] = {10000, false};
    Record record;
    record.key = "North/ordinary/School"; record.name = "School"; record.district = "North";
    record.role = "ordinary"; record.identity = "North/candidates"; record.candidates = {1, 2, 3};
    record.fp = {fp, Status::Current, result.sourceTime};
    record.tcp = {tcp, tcp ? Status::Current : Status::Unavailable, result.sourceTime};
    result.records.push_back(record);
    return result;
}
void comparePreferences(Path const& folder, Snapshot const& now) {
    assert(preferences(loadPreferenceHistory(folder, now)) ==
        preferences(loadPreferenceHistory(folder, now, HistoryReadMode::SourceFiles)));
}
void checkMatchingEquivalence(Path const& folder) {
    auto fpOnly = accepted(0, {{1, 100}, {2, 80}, {3, 20}}, {});
    commit(folder, fpOnly);
    fpOnly.sourceTime = timeAt(10); fpOnly.records[0].fp.observedAt = timeAt(10);
    commit(folder, fpOnly);
    fpOnly.sourceTime = timeAt(20); fpOnly.records[0].fp.observedAt = timeAt(20);
    commit(folder, fpOnly);
    auto now = accepted(30, {{1, 150}, {2, 160}, {3, 40}}, Counts{{1, 116}, {2, 84}});
    now.records[0].tcp.observedAt = timeAt(15);
    comparePreferences(folder, now);
    // The latest change was at 08:00, but the latest ELIGIBLE observation is
    // 08:10; 08:20 is too late for TCP observed at 08:15. Neither may be confused.
    assert(loadPreferenceHistory(folder, now).at(now.records[0].key).matchingCurrentTcp->fpTime == timeAt(10));
    auto stats = LiveInputHistory::statistics();
    comparePreferences(folder, now);
    assert(LiveInputHistory::statistics().sourceReads == stats.sourceReads);
    now.records[0].identity = "other candidates"; comparePreferences(folder, now);
    now.records[0].identity = "North/candidates";
    now.sourceTime = timeAt(10); comparePreferences(folder, now);

    auto old = accepted(40, {{1, 480}, {2, 480}, {3, 40}}, Counts{{1, 500}, {2, 500}});
    commit(folder, old);
    now = accepted(50, {{1, 520}, {2, 480}, {3, 0}}, Counts{{1, 520}, {2, 480}});
    now.records[0].fp.stage = now.records[0].tcp.stage = CountStage::Rechecked;
    comparePreferences(folder, now);
    assert(loadPreferenceHistory(folder, now).at(now.records[0].key).previousAligned->fp.at(3) == 40);
    auto previous = accepted(45, {{1, 100}, {2, 80}, {3, 20}}, {});
    previous.records[0].fp.votes.reset(); commit(folder, previous);
    now.records[0].fp.votes.reset(); now.records[0].tcp.votes.reset();
    for (auto account : {HistoryAccount::FirstPreferences, HistoryAccount::TwoCandidatePreferred, HistoryAccount::Reconciled}) {
        auto a = loadEarlierCounts(folder, now, &previous, account);
        auto b = loadEarlierCounts(folder, now, &previous, account, HistoryReadMode::SourceFiles);
        assert(bool(a) == bool(b));
        if (a) {
            assert(a->records.size() == b->records.size());
            for (std::size_t i = 0; i < a->records.size(); ++i) {
                assert(a->records[i].key == b->records[i].key);
                assert(a->records[i].fp.votes == b->records[i].fp.votes);
                assert(a->records[i].fp.observedAt == b->records[i].fp.observedAt);
                assert(a->records[i].tcp.votes == b->records[i].tcp.votes);
                assert(a->records[i].tcp.observedAt == b->records[i].tcp.observedAt);
            }
        }
    }
    assert(LiveInputHistory::statistics().fallbacks == 0);
}

void checkUnavailableIndex(Path const& folder) {
    auto first = accepted(0, {{1, 100}, {2, 80}, {3, 20}}, {});
    commit(folder, first);
    // A read-only/busy/unwritable cache must not prevent a valid original history
    // from supplying its match. A regular file here prevents index-directory use.
    write(folder / "change-index-v1", {{"blocked", true}});
    auto now = accepted(30, {{1, 150}, {2, 160}, {3, 40}}, Counts{{1, 116}, {2, 84}});
    auto before = LiveInputHistory::statistics().fallbacks;
    comparePreferences(folder, now);
    assert(LiveInputHistory::statistics().fallbacks == before + 1);
    assert(loadPreferenceHistory(folder, now).at(now.records[0].key).matchingCurrentTcp->fpTime == timeAt(0));
}

void checkInterruptedPublication(Path const& folder) {
    save(folder, source(0)); compareAll(folder);
    auto index = folder / "change-index-v1";
    auto originalManifest = read(index / "manifest.json");
    // Prevent the final manifest write after new partition files have been
    // prepared. A successful source write survives; unpublished partitions do not.
    std::filesystem::create_directory(index / "manifest.tmp");
    auto failures = LiveInputHistory::statistics().updateFailures;
    save(folder, source(10, 150), true);
    assert(read(index / "manifest.json") == originalManifest);
    assert(LiveInputHistory::statistics().updateFailures == failures + 1);
    std::size_t partitions = 0;
    for (auto const& file : std::filesystem::directory_iterator(index))
        if (file.path().filename().string().starts_with("part-")) ++partitions;
    assert(partitions == originalManifest.at("payload").at("partitions").size());
    std::filesystem::remove(index / "manifest.tmp");
    compareAll(folder);
}

void checkMixedEdits(Path const& folder) {
    // Exercise overlapping backfills/deletions, not just an isolated insertion.
    // All comparisons use the original files, with periodic restart and replay
    // cutoffs before already-indexed data.
    save(folder, source(0)); save(folder, source(40));
    unsigned random = 7937;
    for (int step = 0; step < 35; ++step) {
        random = random * 1664525U + 1013904223U;
        int minute = 1 + int(random % 35);
        auto path = folder / (stem(timeAt(minute)) + ".json");
        if (random % 4 == 0 && std::filesystem::exists(path)) std::filesystem::remove(path);
        else {
            auto next = source(minute, 100 + int(random % 3) * 50);
            if (random % 5 == 0) next["records"].erase(1);
            if (random % 7 == 0) next["records"][0]["fp"]["v"] = nullptr;
            save(folder, next, step % 2 == 0);
        }
        if (step % 6 == 0) LiveInputHistory::clearMemoryCache(1024);
        compareAll(folder);
        compareAll(folder, 20);
    }
    LiveInputHistory::clearMemoryCache();
}
}

int main() {
    std::cout << std::unitbuf;
    try {
    auto root = std::filesystem::current_path() / "cli-build" / "live-input-history-tests";
    std::filesystem::remove_all(root);
    std::cout << "Checking change reconstruction and source updates...\n";
    checkRepresentationAndUpdates(root / "updates");
    std::cout << "Checking repair and timestamp coverage...\n";
    checkRepairAndCoverage(root / "repair");
    std::cout << "Checking matching equivalence...\n";
    checkMatchingEquivalence(root / "matching");
    checkUnavailableIndex(root / "unavailable");
    checkInterruptedPublication(root / "interrupted");
    checkMixedEdits(root / "mixed");
    LiveInputHistory::clearMemoryCache();
    std::filesystem::remove_all(root);
    std::cout << "Live input history checks passed.\n";
    } catch (std::exception const& error) {
        std::cerr << "History test failed: " << error.what() << '\n';
        return 1;
    }
}
