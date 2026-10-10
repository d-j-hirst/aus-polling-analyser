#include "LiveInputRecovery.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace LiveInputRecovery {
namespace {
using json = nlohmann::json;
int integerValue(json const& value) {
    if (!value.is_number_integer() || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        throw std::runtime_error("Non-integer or out-of-range accepted count identity/value.");
    return value.get<int>();
}
json encodeCount(Count const& count) {
    // Store measured integer pairs and their validation metadata. Null remains
    // unavailable, so neither reload nor compression can invent a genuine zero.
    auto values = json::array();
    if (count.votes) for (auto const& [candidate,votes] : *count.votes) values.push_back({candidate,votes});
    return {{"v",count.votes ? values : json(nullptr)},{"t",count.observedAt},{"s",int(count.status)},{"complete",count.complete}};
}
Count decodeCount(json const& value) {
    // Cache entries are integer measurements, not model outputs. Refuse
    // fractional values instead of allowing JSON conversion to truncate them.
    Count count;
    count.observedAt = value.at("t");
    if (!count.observedAt.empty()) count.observedAt = canonicalTime(count.observedAt);
    auto status = value.at("s").get<int>();
    if (status < 0 || status > 2) throw std::runtime_error("Invalid accepted-count status.");
    count.status = Status(status);
    count.complete = value.value("complete",false);
    if (!value.at("v").is_null()) {
        count.votes.emplace();
        for (auto const& row : value.at("v")) {
            int candidate = integerValue(row.at(0)), votes = integerValue(row.at(1));
            if (votes < 0 || !count.votes->emplace(candidate,votes).second)
                throw std::runtime_error("Invalid accepted candidate count.");
        }
    }
    return count;
}
json encode(Snapshot const& snapshot) {
    json value{{"version",1},{"election",snapshot.election},{"source_time",snapshot.sourceTime},
        {"source_fingerprint",snapshot.sourceHash},{"opv",snapshot.optionalPreferential},
        {"districts",json::object()},{"records",json::array()}};
    for (auto const& [name,district] : snapshot.districts)
        value["districts"][name] = {district.enrolment,district.finalised};
    // Each record identity is written once; candidate values are integer pairs.
    // Restored observations keep their original times when this state is saved.
    for (auto const& record : snapshot.records)
        value["records"].push_back({{"k",record.key},{"d",record.district},{"n",record.name},
            {"r",record.role},{"i",record.identity},{"c",record.candidates},{"e",record.expected},
            {"closed",record.closed},{"fp",encodeCount(record.fp)},{"tcp",encodeCount(record.tcp)}});
    return value;
}
Snapshot decode(json const& value) {
    if (value.at("version") != 1) throw std::runtime_error("Unsupported accepted-count cache.");
    Snapshot snapshot;
    snapshot.election = value.at("election"); snapshot.sourceTime = canonicalTime(value.at("source_time"));
    snapshot.sourceHash = value.at("source_fingerprint"); snapshot.optionalPreferential = value.at("opv");
    for (auto const& [name,district] : value.at("districts").items())
        snapshot.districts[name] = {district.at(0).get<double>(),district.at(1).get<bool>()};
    std::set<std::string> keys;
    for (auto const& row : value.at("records")) {
        Record record;
        record.key = row.at("k"); record.district = row.at("d"); record.name = row.at("n");
        record.role = row.at("r"); record.identity = row.at("i"); record.candidates = row.at("c").get<std::vector<int>>();
        record.expected = row.at("e"); record.closed = row.at("closed");
        record.fp = decodeCount(row.at("fp")); record.tcp = decodeCount(row.at("tcp"));
        for (auto const* count : {&record.fp,&record.tcp})
            if (count->votes && (count->observedAt.empty() || count->observedAt > snapshot.sourceTime))
                throw std::runtime_error("Accepted count has an observation outside its source time.");
        if (!keys.insert(record.key).second || !snapshot.districts.count(record.district))
            throw std::runtime_error("Invalid accepted-count identity.");
        snapshot.records.push_back(std::move(record));
    }
    return snapshot;
}
std::string fileCode(std::string sourceTime) {
    sourceTime = canonicalTime(std::move(sourceTime));
    for (auto& c : sourceTime) if (c == ':') c = '-';
    if (sourceTime.find_first_not_of("0123456789T-.") != std::string::npos)
        throw std::runtime_error("Unsupported accepted-count source clock.");
    return sourceTime;
}

std::vector<std::filesystem::path> earlierFiles(std::filesystem::path const& folder, std::string const& sourceTime) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(folder)) return result;
    auto cutoff = fileCode(sourceTime);
    for (auto const& entry : std::filesystem::directory_iterator(folder))
        if (entry.is_regular_file() && entry.path().extension() == ".json" && entry.path().stem().string() < cutoff)
            result.push_back(entry.path());
    // Compare clocks without the .json suffix: otherwise a whole-second file
    // sorts after its fractional-second successors because 'j' follows digits.
    std::sort(result.begin(),result.end(),[](auto const& a, auto const& b) {
        return a.stem().string() > b.stem().string();
    });
    return result;
}

bool sameRecord(Record const& current, Record const& previous) {
    return current.identity == previous.identity && current.district == previous.district &&
        current.role == previous.role && current.candidates == previous.candidates && current.closed == previous.closed;
}

using RecordIndex = std::map<std::string,Record const*>;

bool countFitsAccount(Record const& record, Count const& count,
    Snapshot const& effective, HistoryAccount account) {
    return account == HistoryAccount::TwoCandidatePreferred ?
        tcpFits(record,count,effective.optionalPreferential) :
        fpFits(record,count,effective.districts.at(record.district).enrolment);
}

bool earlierRecordFits(Record const& record, Record const& earlier,
    Snapshot const& effective, HistoryAccount account) {
    // Single-count recovery uses the effective parent. A reconciliation hold
    // instead needs both candidate vectors from one accepted historical record;
    // neither count may borrow a same-time or future observation.
    if (account == HistoryAccount::Reconciled)
        return reconciliationFits(record,earlier,effective.districts.at(record.district).enrolment,
            effective.optionalPreferential) && !earlier.fp.observedAt.empty() &&
            !earlier.tcp.observedAt.empty() && earlier.fp.observedAt < effective.sourceTime &&
            earlier.tcp.observedAt < effective.sourceTime;
    auto const& count = account == HistoryAccount::TwoCandidatePreferred ? earlier.tcp : earlier.fp;
    return count.observedAt < effective.sourceTime && countFitsAccount(record,count,effective,account);
}

RecordIndex recordsNeedingEarlierHistory(Snapshot const& effective,
    Snapshot const* previous, HistoryAccount account) {
    RecordIndex needed, nearest;
    if (!previous) return needed;
    for (auto const& record : previous->records) nearest.emplace(record.key,&record);
    // Normally current or nearest accepted counts suffice. Search farther back
    // only for affected records: a corrected FP parent may fit an older TCP, or
    // the nearest source may lack preferences needed for a joint recount hold.
    for (auto const& record : effective.records) {
        if (account == HistoryAccount::Reconciled) {
            if (!reconciliationNeeded(record,effective.optionalPreferential)) continue;
        } else {
            auto const& current = account == HistoryAccount::TwoCandidatePreferred ? record.tcp : record.fp;
            if (countFitsAccount(record,current,effective,account)) continue;
            if (account == HistoryAccount::TwoCandidatePreferred &&
                (!record.fp.votes || total(record.fp.votes) == 0)) continue;
        }
        auto found = nearest.find(record.key);
        if (found == nearest.end() || !sameRecord(record,*found->second)) continue;
        // A reconciled downward revision supersedes older, larger FP totals.
        // Do not resurrect those totals merely because a later TCP is wrong,
        // or scan the full history for a persistent TCP error with stable FP.
        if (account == HistoryAccount::Reconciled &&
            fpFits(record,found->second->fp,effective.districts.at(record.district).enrolment) &&
            total(found->second->fp.votes) <= total(record.fp.votes)) continue;
        if (earlierRecordFits(record,*found->second,effective,account)) continue;
        needed.emplace(record.key,&record);
    }
    return needed;
}

std::optional<Snapshot> readEarlierCountRecords(std::filesystem::path const& folder,
    Snapshot const& effective, HistoryAccount account, RecordIndex needed) {
    // Parse one private source at a time, newest first. Retain only the chosen
    // records, so a long sequence does not accumulate parsed JSON in memory.
    if (needed.empty()) return {};
    Snapshot result;
    result.election = effective.election; result.optionalPreferential = effective.optionalPreferential;
    for (auto const& path : earlierFiles(folder,effective.sourceTime)) {
        std::ifstream stream(path); json value; stream >> value;
        auto old = decode(value);
        if (old.election != effective.election || old.sourceTime >= effective.sourceTime ||
            old.optionalPreferential != effective.optionalPreferential) continue;
        for (auto const& record : old.records) {
            auto requested = needed.find(record.key);
            if (requested == needed.end() || !sameRecord(*requested->second,record)) continue;
            if (!earlierRecordFits(*requested->second,record,effective,account)) continue;
            result.records.push_back(record);
            needed.erase(requested);
        }
        if (needed.empty()) break;
    }
    return result.records.empty() ? std::optional<Snapshot>{} : std::move(result);
}

void replaceFile(std::filesystem::path const& temporary, std::filesystem::path const& target) {
    // Replace one fully written source revision atomically. The old file is
    // never removed first, so an interrupted write retains its previous state.
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace the accepted-count cache.");
#else
    std::filesystem::rename(temporary,target);
#endif
}
}
std::filesystem::path directory(std::filesystem::path const& root, std::string const& election) {
    return root/"forecasts"/election/"live-snapshots"/"accepted-counts";
}
std::optional<Snapshot> loadPrevious(std::filesystem::path const& folder,
    std::string const& election, std::string const& sourceTime) {
    // Read only the nearest compatible state. No JSON history is kept alive
    // while a sequence grows. Invalid private cache files are input failures,
    // rather than a reason to trust unchecked candidate counts.
    for (auto const& path : earlierFiles(folder,sourceTime)) {
        std::ifstream stream(path); json value; stream >> value;
        auto snapshot = decode(value);
        if (snapshot.election == election && snapshot.sourceTime < canonicalTime(sourceTime)) return snapshot;
    }
    return {};
}
void commit(std::filesystem::path const& folder, Snapshot const& snapshot) {
    std::filesystem::create_directories(folder);
    auto target = folder/(fileCode(snapshot.sourceTime)+".json");
    auto temporary = target; temporary += ".tmp";
    try {
        { std::ofstream stream(temporary); stream << encode(snapshot).dump(); stream.close();
          if (!stream) throw std::runtime_error("Cannot write the accepted-count cache."); }
        replaceFile(temporary,target);
    } catch (...) { std::error_code error; std::filesystem::remove(temporary,error); throw; }
}
std::optional<Snapshot> loadCompatiblePrevious(std::filesystem::path const& folder, Snapshot const& current) {
    Snapshot result; result.election = current.election;
    result.optionalPreferential = current.optionalPreferential;
    std::map<std::string,std::vector<Record const*>> districtRecords;
    for (auto const& record : current.records) districtRecords[record.district].push_back(&record);
    // Select local fallbacks independently: a changed booth must not prevent
    // another, well-identified booth from recovering. Whole-district rollback
    // also needs a jointly consistent account, rather than a mixture of dates.
    std::map<std::string,std::size_t> selected;
    std::map<std::string,std::string> selectedAt;
    std::set<std::string> wholeAccounts;
    for (auto const& path : earlierFiles(folder,current.sourceTime)) {
        std::ifstream stream(path); json value; stream >> value;
        auto old = decode(value);
        if (old.election != current.election || old.sourceTime >= canonicalTime(current.sourceTime) ||
            old.optionalPreferential != current.optionalPreferential) continue;
        std::map<std::string,Record const*> oldRecords;
        for (auto const& record : old.records) oldRecords.emplace(record.key,&record);
        for (auto const& [district,account] : current.districts) {
            if (!old.districts.count(district) || !districtRecords.count(district)) continue;
            bool compatible = true, oneSource = true;
            std::vector<Record const*> matches;
            for (auto record : districtRecords.at(district)) {
                auto found = oldRecords.find(record->key);
                if (found == oldRecords.end()) { compatible = false; continue; }
                auto previous = found->second;
                if (!sameRecord(*record,*previous)) {
                    compatible = false; continue;
                }
                matches.push_back(previous);
                if (!selected.count(record->key)) {
                    selected[record->key] = result.records.size();
                    selectedAt[record->key] = old.sourceTime;
                    result.records.push_back(*previous);
                    if (!result.districts.count(district)) {
                        result.districts[district] = old.districts.at(district);
                        result.districts[district].wholeAccount = false;
                    }
                    if (result.sourceTime.empty() || result.sourceTime < old.sourceTime) result.sourceTime = old.sourceTime;
                }
                oneSource = oneSource && selectedAt.at(record->key) == old.sourceTime;
            }
            if (!compatible || wholeAccounts.count(district)) continue;
            wholeAccounts.insert(district);
            if (oneSource) result.districts[district] = old.districts.at(district);
            else {
                auto& whole = result.wholeDistricts[district];
                whole.district = old.districts.at(district);
                for (auto record : matches) whole.records.push_back(*record);
            }
        }
        if (selected.size() == current.records.size() && wholeAccounts.size() == current.districts.size()) break;
    }
    if (result.districts.empty()) return {};
    return result;
}

std::optional<Snapshot> loadEarlierCounts(std::filesystem::path const& folder,
    Snapshot const& effective, Snapshot const* previous, HistoryAccount account) {
    auto needed = recordsNeedingEarlierHistory(effective,previous,account);
    return readEarlierCountRecords(folder,effective,account,std::move(needed));
}
std::string fingerprint(std::filesystem::path const& source) {
    // A content fingerprint distinguishes revisions sharing a source clock.
    // This FNV-1a value identifies content; it is not a security signature.
    std::ifstream stream(source,std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot fingerprint the received feed.");
    std::uint64_t hash = 14695981039346656037ULL;
    char buffer[16384];
    while (stream.read(buffer,sizeof buffer) || stream.gcount())
        for (std::streamsize i = 0; i < stream.gcount(); ++i) hash = (hash^static_cast<unsigned char>(buffer[i]))*1099511628211ULL;
    if (!stream.eof()) throw std::runtime_error("Cannot finish reading the received feed.");
    std::ostringstream text; text << std::hex << std::setw(16) << std::setfill('0') << hash;
    return text.str();
}
}
