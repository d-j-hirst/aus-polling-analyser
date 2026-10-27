#include "LiveInputHistory.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace LiveInputHistory {
namespace {
using json = nlohmann::json;
using Path = std::filesystem::path;
using Value = std::optional<std::string>; // Absent field differs from JSON null.
constexpr int FormatVersion = 1;
char const* IndexDirectory = "change-index-v1";
Statistics counters;
std::size_t memoryBudget = 16 * 1024 * 1024;
std::mutex indexMutex;

std::string hashText(std::string const& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : text) hash = (hash ^ c) * 1099511628211ULL;
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

// District content hashes identify unchanged observations during maintenance.
// They include ALL normalized record fields, not just vote totals or the raw
// feed fingerprint: accepted recovery/basis metadata can change on a replay.
json districtHashes(json const& data);

std::string readText(Path const& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw Unavailable("Cannot read live history index input: " + path.string());
    std::ostringstream text;
    text << stream.rdbuf();
    if (stream.bad()) throw Unavailable("Cannot finish reading live history index input.");
    return text.str();
}

void writeText(Path const& path, std::string const& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), std::streamsize(text.size()));
    stream.close();
    if (!stream) throw Unavailable("Cannot write the live history index.");
}

void replaceFile(Path const& temporary, Path const& target) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Unavailable("Cannot publish the live history index.");
#else
    std::filesystem::rename(temporary, target);
#endif
}

// An application-level mutex protects threads; this short-lived OS lock also
// prevents two application processes from publishing conflicting index manifests.
// A busy index falls back to the source reader instead of blocking another run.
class FileLock {
public:
    explicit FileLock(Path const& directory) {
        std::filesystem::create_directories(directory);
#ifdef _WIN32
        handle = CreateFileW((directory / "index.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw Unavailable("Live history index is in use.");
#else
        handle = ::open((directory / "index.lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (handle < 0) throw Unavailable("Cannot lock the live history index.");
        if (flock(handle, LOCK_EX | LOCK_NB) != 0) {
            ::close(handle); handle = -1;
            throw Unavailable("Live history index is in use.");
        }
#endif
    }
    ~FileLock() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (handle >= 0) { flock(handle, LOCK_UN); ::close(handle); }
#endif
    }
    FileLock(FileLock const&) = delete;
    FileLock& operator=(FileLock const&) = delete;
private:
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
};

json revision(Path const& path) {
    return {std::filesystem::file_size(path),
        std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count())};
}

// Source clocks advance even when candidate values do not. A symbolic reference
// preserves that observation time without turning every repeated capture into a
// data change. Fixed older clocks (restored counts and retained pairs) stay literal.
void transformClocks(json& value, std::string const& sourceTime, bool expand) {
    static json const reference = {{"$source", true}};
    if (value.is_object()) {
        for (auto& item : value.items()) {
            if (item.key() == "t" && ((!expand && item.value() == sourceTime) ||
                (expand && item.value() == reference)))
                item.value() = expand ? json(sourceTime) : reference;
            else transformClocks(item.value(), sourceTime, expand);
        }
    } else if (value.is_array()) {
        for (auto& child : value) transformClocks(child, sourceTime, expand);
    }
}

json districtHashes(json const& data) {
    std::map<std::string, std::string> contents;
    for (auto row : data.at("records")) {
        std::string district = row.at("d");
        transformClocks(row, data.at("source_time"), false);
        contents[district] += row.dump() + '\n';
    }
    json result = json::object();
    for (auto const& [district, text] : contents) result[district] = hashText(text);
    return result;
}

struct Change {
    std::string source;
    Value value;
};
using Timeline = std::vector<Change>;

Value const& at(Timeline const& changes, std::string const& source) {
    static Value const absent;
    auto next = std::upper_bound(changes.begin(), changes.end(), source,
        [](std::string const& key, Change const& change) { return key < change.source; });
    return next == changes.begin() ? absent : std::prev(next)->value;
}

void put(Timeline& changes, std::string const& source, Value value) {
    auto position = std::lower_bound(changes.begin(), changes.end(), source,
        [](Change const& change, std::string const& key) { return change.source < key; });
    if (position != changes.end() && position->source == source) position->value = std::move(value);
    else changes.insert(position, {source, std::move(value)});
}

void compact(Timeline& changes) {
    Value previous;
    changes.erase(std::remove_if(changes.begin(), changes.end(), [&](Change const& change) {
        if (change.value == previous) return true;
        previous = change.value;
        return false;
    }), changes.end());
}

// A middle insertion/replacement affects just that captured observation. Preserve
// the NEXT existing source's value even if it originally shared the preceding run.
// Deleting a source likewise must not erase what a later snapshot actually said.
void amend(Timeline& changes, std::string const& source, std::string const& next,
    Value value, bool deleting) {
    auto following = next.empty() ? Value{} : at(changes, next);
    if (deleting) {
        changes.erase(std::remove_if(changes.begin(), changes.end(), [&](Change const& change) {
            return change.source == source;
        }), changes.end());
    } else put(changes, source, std::move(value));
    if (!next.empty()) put(changes, next, std::move(following));
    compact(changes);
}

struct RecordHistory {
    Timeline present;
    std::map<std::string, Timeline> fields;
};
using Partition = std::map<std::string, RecordHistory>;

json encodeTimeline(Timeline const& changes) {
    json result = json::array();
    for (auto const& change : changes)
        result.push_back({change.source, bool(change.value), change.value ? json::parse(*change.value) : json()});
    return result;
}

Timeline decodeTimeline(json const& value, std::string const& cutoff = {}) {
    Timeline result;
    for (auto const& row : value) {
        std::string source = row.at(0);
        if (!cutoff.empty() && source >= cutoff) break;
        if (!result.empty() && result.back().source >= source) throw Unavailable("Unordered history changes.");
        result.push_back({std::move(source), row.at(1).get<bool>() ? Value(row.at(2).dump()) : Value{}});
    }
    return result;
}

std::string encodePartition(Partition const& partition) {
    json records = json::object();
    for (auto const& [key, record] : partition) {
        json fields = json::object();
        for (auto const& [name, changes] : record.fields) fields[name] = encodeTimeline(changes);
        records[key] = {{"present", encodeTimeline(record.present)}, {"fields", std::move(fields)}};
    }
    return json{{"version", FormatVersion}, {"records", std::move(records)}}.dump();
}

Partition decodePartition(std::string const& text, std::string const& cutoff = {}) {
    auto value = json::parse(text);
    if (value.at("version") != FormatVersion) throw Unavailable("Unsupported history partition.");
    Partition result;
    for (auto const& item : value.at("records").items()) {
        auto& record = result[item.key()];
        record.present = decodeTimeline(item.value().at("present"), cutoff);
        for (auto const& field : item.value().at("fields").items())
            record.fields[field.key()] = decodeTimeline(field.value(), cutoff);
    }
    return result;
}

std::size_t retainedSize(Partition const& partition) {
    // Account for owned string/vector capacity and map-node overhead. JSON parse
    // trees are temporary; only these compact strings/timelines enter the cache.
    std::size_t bytes = sizeof(partition);
    auto timelineSize = [](Timeline const& changes) {
        std::size_t size = sizeof(changes) + changes.capacity() * sizeof(Change);
        for (auto const& change : changes) size += change.source.capacity() +
            (change.value ? change.value->capacity() : 0);
        return size;
    };
    for (auto const& [key, record] : partition) {
        bytes += sizeof(RecordHistory) + key.capacity() + 64 + timelineSize(record.present);
        for (auto const& [field, changes] : record.fields)
            bytes += field.capacity() + 96 + timelineSize(changes);
    }
    return bytes;
}

json reconstruct(RecordHistory const& record, std::string const& source, std::string const& time) {
    if (at(record.present, source) != Value("true")) return {};
    json result = json::object();
    for (auto const& [field, changes] : record.fields)
        if (auto const& value = at(changes, source)) result[field] = json::parse(*value);
    transformClocks(result, time, true);
    return result;
}

struct CachedPartition {
    std::string file;
    std::string cutoff; // Empty means a complete partition used for maintenance.
    std::shared_ptr<Partition const> data;
    std::size_t bytes = 0;
};

// The manifest is published last, referencing immutable partition generations.
// Interrupted writes leave either the old complete index or harmless unreferenced
// files; they never expose a mixture of old and new district histories.
class Store {
public:
    explicit Store(Path root) : folder(std::move(root)), directory(folder / IndexDirectory) {}
    ~Store() { discardUnpublished(); }
    Path folder, directory;
    json manifest;
    std::optional<json> manifestRevision;
    std::list<CachedPartition> cache;
    std::vector<Path> unpublishedFiles;

    void refresh(std::string const& cutoff, Validate const& validate, std::string const& force = {});
    void build(std::map<std::string, json> const& sources, Validate const& validate);
    void update(std::string const& source, std::optional<json> const& stamp, Validate const& validate);
    void prepare(Selection const& selection, std::string const& cutoff);
    void visit(std::string const& cutoff, Selection const& selection, Visitor const& visitor, RequiredCount required);
    std::shared_ptr<Partition const> partition(std::string const& id, std::string const& cutoff = {});
    void publish(json next);
    void clearPartitions() { cache.clear(); counters.retainedBytes = 0; }
    void discardUnpublished() noexcept {
        for (auto const& path : unpublishedFiles) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        unpublishedFiles.clear();
    }
    json sourceInfo(std::string const& source, json const& stamp, json const& data, json& target,
        json const& hashes);
    json savePartition(std::string const& id, std::string const& district, Partition const& data);
};

std::unique_ptr<Store> currentStore;

std::string generation() {
    static std::uint64_t sequence = 0;
    return std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(++sequence);
}

std::map<std::string, json> inventory(Path const& folder, std::string const& cutoff) {
    std::map<std::string, json> result;
    if (std::filesystem::exists(folder)) for (auto const& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            auto source = entry.path().stem().string();
            if (source < cutoff) result[source] = revision(entry.path());
        }
    }
    return result;
}

json Store::sourceInfo(std::string const& source, json const& stamp, json const& data, json& target,
    json const& hashes) {
    json metadata = json::object();
    for (auto const& item : data.items()) if (item.key() != "records") metadata[item.key()] = item.value();
    json order = json::array();
    for (auto const& record : data.at("records")) order.push_back(record.at("k"));
    auto orderId = hashText(order.dump());
    if (target["orders"].contains(orderId) && target["orders"][orderId] != order)
        throw Unavailable("History record-order hash collision.");
    target["orders"][orderId] = std::move(order);
    return {{"source", source}, {"revision", stamp}, {"metadata", std::move(metadata)},
        {"order", orderId}, {"district_hashes", hashes}};
}

json Store::savePartition(std::string const& id, std::string const& district, Partition const& data) {
    auto text = encodePartition(data);
    auto file = "part-" + generation() + "-" + id + ".json";
    while (std::filesystem::exists(directory / file)) file = "part-" + generation() + "-" + id + ".json";
    unpublishedFiles.push_back(directory / file);
    writeText(directory / file, text);
    return {{"district", district}, {"file", file}, {"hash", hashText(text)}};
}

void Store::publish(json next) {
    next["generation"] = generation();
    auto temporary = directory / "manifest.tmp";
    writeText(temporary, json{{"payload", next}, {"hash", hashText(next.dump())}}.dump());
    replaceFile(temporary, directory / "manifest.json");
    // These partitions are now referenced by the authoritative index manifest.
    // Subsequent cleanup/stat failures must never delete a published generation.
    unpublishedFiles.clear();
    manifest = std::move(next);
    manifestRevision = revision(directory / "manifest.json");
    clearPartitions();
    // Only derived, recognizably named index files are eligible for cleanup. The
    // source directory and its accepted-count JSON files are never modified here.
    std::set<std::string> keep;
    for (auto const& item : manifest.at("partitions").items()) keep.insert(item.value().at("file").get<std::string>());
    for (auto const& entry : std::filesystem::directory_iterator(directory)) {
        auto name = entry.path().filename().string();
        if (entry.is_regular_file() && ((name.starts_with("part-") && entry.path().extension() == ".json" &&
            !keep.contains(name)) || (name.starts_with("work-") && entry.path().extension() == ".jsonl"))) {
            std::error_code ignored;
            std::filesystem::remove(entry.path(), ignored);
        }
    }
}

void Store::build(std::map<std::string, json> const& sources, Validate const& validate) {
    ++counters.builds;
    discardUnpublished();
    clearPartitions();
    json next = {{"version", FormatVersion}, {"sources", json::object()},
        {"orders", json::object()}, {"partitions", json::object()}};
    struct Latest { json row; bool present = false; };
    std::map<std::string, std::map<std::string, Latest>> latest;
    std::map<std::string, std::ofstream> streams;
    std::map<std::string, std::string> districts;
    auto workGeneration = generation();
    auto emit = [&](std::string const& district, std::string const& key, json const& field,
        std::string const& source, bool exists, json const& value) {
        auto id = hashText(district);
        if (districts.contains(id) && districts.at(id) != district)
            throw Unavailable("History district hash collision.");
        districts[id] = district;
        auto& stream = streams[id];
        if (!stream.is_open()) {
            auto path = directory / ("work-" + workGeneration + "-" + id + ".jsonl");
            unpublishedFiles.push_back(path);
            stream.open(path, std::ios::binary | std::ios::trunc);
        }
        stream << json::array({key, field, source, exists, value}).dump() << '\n';
        if (!stream) throw Unavailable("Cannot spool live history changes.");
    };
    // Read each complete source just once. Keep only its predecessor's field
    // values in memory, spooling changes by district. Then assemble one compact
    // partition at a time, rather than holding the election's full history.
    for (auto const& [source, stamp] : sources) {
        ++counters.sourceReads;
        auto data = json::parse(readText(folder / (source + ".json")));
        validate(data);
        next["sources"][source] = sourceInfo(source, stamp, data, next, districtHashes(data));
        std::set<std::pair<std::string, std::string>> present;
        for (auto row : data.at("records")) {
            std::string district = row.at("d"), key = row.at("k");
            transformClocks(row, data.at("source_time"), false);
            present.emplace(district, key);
            auto& previous = latest[district][key];
            if (!previous.present) emit(district, key, nullptr, source, true, true);
            for (auto const& field : row.items())
                if (!previous.row.contains(field.key()) || previous.row.at(field.key()) != field.value())
                    emit(district, key, field.key(), source, true, field.value());
            for (auto const& field : previous.row.items())
                if (!row.contains(field.key())) emit(district, key, field.key(), source, false, nullptr);
            previous = {std::move(row), true};
        }
        for (auto& [district, records] : latest) for (auto& [key, previous] : records) {
            if (previous.present && !present.contains({district, key})) {
                emit(district, key, nullptr, source, true, false);
                previous.present = false;
            }
        }
    }
    latest.clear();
    for (auto& [id, stream] : streams) {
        stream.close();
        if (!stream) throw Unavailable("Cannot finish spooling live history changes.");
        Partition partitionData;
        std::ifstream input(directory / ("work-" + workGeneration + "-" + id + ".jsonl"));
        std::string line;
        while (std::getline(input, line)) {
            auto event = json::parse(line);
            auto& record = partitionData[event.at(0).get<std::string>()];
            auto& changes = event.at(1).is_null() ? record.present : record.fields[event.at(1).get<std::string>()];
            changes.push_back({event.at(2), event.at(3).get<bool>() ? Value(event.at(4).dump()) : Value{}});
        }
        if (!input.eof()) throw Unavailable("Cannot read spooled live history changes.");
        next["partitions"][id] = savePartition(id, districts.at(id), partitionData);
    }
    publish(std::move(next));
}

std::shared_ptr<Partition const> Store::partition(std::string const& id, std::string const& cutoff) {
    if (!manifest.at("partitions").contains(id)) return std::make_shared<Partition>();
    auto const& info = manifest.at("partitions").at(id);
    std::string file = info.at("file");
    for (auto found = cache.begin(); found != cache.end(); ++found) if (found->file == file &&
        (found->cutoff.empty() || found->cutoff == cutoff)) {
        auto result = found->data;
        cache.splice(cache.begin(), cache, found);
        return result;
    }
    if (Path(file).filename() != Path(file) || !file.starts_with("part-") || Path(file).extension() != ".json")
        throw Unavailable("Invalid live history partition filename.");
    ++counters.partitionReads;
    auto text = readText(directory / file);
    if (hashText(text) != info.at("hash")) throw Unavailable("Damaged live history partition.");
    // During replay, trim future changes before retaining the district. FP, TCP
    // and preference queries often need different booths in the SAME district;
    // retaining its other past records avoids rereading it for each account.
    // Full maintenance requests keep the entire timeline. Oversized views still
    // obey the normal eviction rule, without dropping observations from queries.
    auto data = std::make_shared<Partition>(decodePartition(text, cutoff));
    auto bytes = retainedSize(*data);
    for (auto found = cache.begin(); found != cache.end();) {
        if (found->file == file && found->cutoff == cutoff) {
            counters.retainedBytes -= found->bytes;
            found = cache.erase(found);
        } else ++found;
    }
    while (!cache.empty() && counters.retainedBytes + bytes > memoryBudget) {
        counters.retainedBytes -= cache.back().bytes;
        cache.pop_back();
    }
    // An unusually large district can still be queried, but is not retained
    // beyond that operation if it exceeds the memory-cache budget on its own.
    if (bytes <= memoryBudget) {
        cache.push_front({file, cutoff, data, bytes});
        counters.retainedBytes += bytes;
        counters.peakRetainedBytes = std::max(counters.peakRetainedBytes, counters.retainedBytes);
    }
    return data;
}

void Store::update(std::string const& source, std::optional<json> const& stamp, Validate const& validate) {
    json data, next = manifest;
    json hashes = json::object();
    std::map<std::string, std::map<std::string, json>> rows;
    if (stamp) {
        ++counters.sourceReads;
        data = json::parse(readText(folder / (source + ".json")));
        validate(data);
        hashes = districtHashes(data);
        for (auto row : data.at("records")) {
            transformClocks(row, data.at("source_time"), false);
            std::string district = row.at("d"), key = row.at("k");
            rows[district][key] = std::move(row);
        }
    }
    // Source identifiers are stable filename clocks. A backfill does not require
    // renumbering later changes. Bound its effect at the next EXISTING source,
    // before updating the inventory, so multiple insertions also compose safely.
    auto const& sourceMap = manifest.at("sources").get_ref<json::object_t const&>();
    auto following = sourceMap.upper_bound(source);
    std::string nextSource = following == sourceMap.end() ? "" : following->first;
    auto preceding = sourceMap.lower_bound(source);
    json previousHashes = preceding == sourceMap.begin() ? json::object() :
        std::prev(preceding)->second.at("district_hashes");
    json oldHashes = sourceMap.contains(source) ? sourceMap.at(source).at("district_hashes") : previousHashes;
    std::set<std::string> affected;
    // A source may omit a whole district, not just one booth. Its absence must
    // stop earlier values carrying forward even when the district is not listed
    // in this source's metadata at all.
    for (auto const& item : manifest.at("partitions").items()) affected.insert(item.value().at("district").get<std::string>());
    for (auto const& [district, records] : rows) affected.insert(district);
    for (auto const& district : affected) {
        // Most captures change only a few districts. The existing source is the
        // comparison for replacement; its predecessor is the comparison for an
        // insertion. Deletion compares the removed source with its predecessor.
        // Identical normalized states need only a new source-clock inventory row.
        auto const& replacement = stamp ? hashes : previousHashes;
        if (oldHashes.value(district, std::string{}) == replacement.value(district, std::string{})) continue;
        auto id = hashText(district);
        if (manifest.at("partitions").contains(id) && manifest.at("partitions").at(id).at("district") != district)
            throw Unavailable("History district hash collision.");
        auto old = partition(id);
        Partition revised = *old;
        for (auto const& [key, row] : rows[district]) revised.try_emplace(key);
        for (auto& [key, record] : revised) {
            bool present = rows[district].contains(key);
            amend(record.present, source, nextSource, Value(present ? "true" : "false"), !stamp);
            auto const& row = present ? rows[district].at(key) : json::object();
            for (auto const& field : row.items()) record.fields.try_emplace(field.key());
            for (auto& [field, changes] : record.fields) {
                // Fields of an absent record are irrelevant, but clearing them
                // makes that absence explicit and keeps later restoration exact.
                Value value = row.contains(field) ? Value(row.at(field).dump()) : Value{};
                amend(changes, source, nextSource, std::move(value), !stamp);
            }
        }
        auto text = encodePartition(revised);
        if (!manifest.at("partitions").contains(id) || hashText(text) != manifest.at("partitions").at(id).at("hash"))
            next["partitions"][id] = savePartition(id, district, revised);
    }
    if (stamp) next["sources"][source] = sourceInfo(source, *stamp, data, next, hashes);
    else next["sources"].erase(source);
    publish(std::move(next));
}

void Store::refresh(std::string const& cutoff, Validate const& validate, std::string const& force) {
    auto sources = inventory(folder, cutoff);
    auto manifestPath = directory / "manifest.json";
    // A first run normally has no index. Build directly rather than throwing a
    // caught exception for that expected path (which also avoids debugger stops).
    if (!std::filesystem::exists(manifestPath)) {
        build(sources, validate);
        return;
    }
    try {
        auto stamp = revision(manifestPath);
        if (!manifestRevision || *manifestRevision != stamp) {
            auto envelope = json::parse(readText(manifestPath));
            auto loaded = envelope.at("payload");
            if (loaded.at("version") != FormatVersion || hashText(loaded.dump()) != envelope.at("hash") ||
                !loaded.at("sources").is_object() || !loaded.at("partitions").is_object() ||
                !loaded.at("orders").is_object()) throw Unavailable("Unsupported or damaged live history index.");
            for (auto const& item : loaded.at("sources").items())
                if (item.value().at("source") != item.key() || !loaded.at("orders").contains(item.value().at("order").get<std::string>()) ||
                    !item.value().at("district_hashes").is_object())
                    throw Unavailable("Incomplete live history source inventory.");
            manifest = std::move(loaded);
            manifestRevision = std::move(stamp);
            clearPartitions();
        }
    } catch (std::exception const&) {
        build(sources, validate);
        return;
    }
    std::set<std::string> changed;
    for (auto const& [source, stamp] : sources)
        if (source == force || !manifest.at("sources").contains(source) ||
            manifest.at("sources").at(source).at("revision") != stamp) changed.insert(source);
    for (auto const& item : manifest.at("sources").items())
        if (item.key() < cutoff && !sources.contains(item.key())) changed.insert(item.key());
    for (auto const& source : changed)
        update(source, sources.contains(source) ? std::optional<json>(sources.at(source)) : std::nullopt, validate);
}

void Store::prepare(Selection const& selection, std::string const& cutoff) {
    // Validate required partitions before invoking any selection callbacks. A
    // damaged index can then rebuild without exposing partially selected history.
    for (auto const& [district, keys] : selection) {
        auto id = hashText(district);
        if (manifest.at("partitions").contains(id) && manifest.at("partitions").at(id).at("district") != district)
            throw Unavailable("History district hash collision.");
        partition(id, cutoff);
    }
}

void Store::visit(std::string const& cutoff, Selection const& selection, Visitor const& visitor, RequiredCount required) {
    auto const& sources = manifest.at("sources");
    // Iterate original capture positions, not just change positions: eligibility
    // can depend on the newest observation BEFORE an older TCP clock within a
    // run of unchanged values. This costs small in-memory lookups, not file scans.
    for (auto const& [district, keys] : selection) {
        auto history = partition(hashText(district), cutoff);
        for (auto const& key : keys) {
            auto found = history->find(key);
            if (found == history->end()) continue;
            std::map<std::string const*, bool> availability;
            auto available = [&](char const* field, std::string const& source) {
                auto account = found->second.fields.find(field);
                if (account == found->second.fields.end()) return false;
                auto const& value = at(account->second, source);
                if (!value) return false;
                auto [known, inserted] = availability.emplace(&*value, false);
                if (inserted) known->second = !json::parse(*value).at("v").is_null();
                return known->second;
            };
            for (auto source = sources.rbegin(); source != sources.rend(); ++source) {
                if (source.key() >= cutoff) continue;
                // Avoid reconstructing hundreds of repeated unavailable records
                // for booths that had not reported yet. Test availability once per
                // distinct count state; keep zeros and all later/earlier measured
                // values. Eligibility of an available vector remains the caller's.
                if ((required == RequiredCount::FirstPreferences || required == RequiredCount::Both) &&
                    !available("fp", source.key())) continue;
                if ((required == RequiredCount::TwoCandidatePreferred || required == RequiredCount::Both) &&
                    !available("tcp", source.key())) continue;
                auto const& metadata = source.value().at("metadata");
                auto record = reconstruct(found->second, source.key(), metadata.at("source_time"));
                if (record.is_null()) continue;
                auto const& order = manifest.at("orders").at(source.value().at("order").get<std::string>());
                auto position = std::find(order.begin(), order.end(), json(key));
                if (position == order.end()) throw Unavailable("Record missing from history source inventory.");
                auto snapshot = metadata;
                snapshot["records"] = json::array({std::move(record)});
                if (!visitor(source.key(), std::size_t(position - order.begin()), snapshot)) break;
            }
        }
    }
}

Store& storeFor(Path const& folder) {
    // Lexical normalization does not require traversing protected ancestor
    // directories (which can fail in sandboxed Windows runs). Alias paths may
    // cost a cache reload, but the directory's OS lock still guards publication.
    auto canonical = std::filesystem::absolute(folder).lexically_normal();
    if (!currentStore || currentStore->folder != canonical) {
        currentStore = std::make_unique<Store>(canonical);
        counters.retainedBytes = 0;
    }
    return *currentStore;
}
}

void visitEarlier(Path const& folder, std::string const& cutoff, Selection const& selection,
    Validate const& validate, Visitor const& visitor, RequiredCount required) {
    std::lock_guard lock(indexMutex);
    try {
        auto& store = storeFor(folder);
        FileLock fileLock(store.directory);
        try { store.refresh(cutoff, validate); store.prepare(selection, cutoff); }
        catch (std::exception const&) {
            store.build(inventory(store.folder, cutoff), validate);
            store.prepare(selection, cutoff);
        }
        store.visit(cutoff, selection, visitor, required);
    } catch (std::exception const& error) {
        currentStore.reset(); counters.retainedBytes = 0;
        ++counters.fallbacks; counters.lastFailure = error.what();
        throw Unavailable(error.what());
    }
}

void committed(Path const& folder, std::string const& source, Validate const& validate) noexcept {
    std::lock_guard lock(indexMutex);
    try {
        // Don't force a cold traversal during a first successful write. The first
        // history query builds coverage, then each successful write maintains it.
        if (!std::filesystem::exists(folder / IndexDirectory / "manifest.json")) return;
        auto& store = storeFor(folder);
        FileLock fileLock(store.directory);
        // '~' sorts after any filename-clock suffix, including fractional seconds.
        store.refresh(source + "~", validate, source);
    } catch (std::exception const& error) {
        currentStore.reset(); counters.retainedBytes = 0;
        ++counters.updateFailures; counters.lastFailure = error.what();
        // Accepted data are already safely committed. The next query checks file
        // revisions and either repairs the derived index or reads the originals.
    }
}

Statistics statistics() {
    std::lock_guard lock(indexMutex);
    return counters;
}

void clearMemoryCache(std::size_t byteBudget) {
    std::lock_guard lock(indexMutex);
    currentStore.reset(); counters = {};
    memoryBudget = byteBudget;
}
}
