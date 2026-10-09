#pragma once

#include "json.h"
#include "TurnoutModel.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Recovery works on candidate counts before parties are combined or turnout
// is estimated. The original feed remains untouched; these are the effective
// measured accounts used for one forecast, not estimates of future votes.
namespace LiveInputRecovery {
using Counts = std::map<int, int>;
enum class Status { Current, Restored, Unavailable };
// These are count-comparability allowances, not likelihood thresholds. Small
// asynchronous rechecking differences can be genuine. Ordinary booths normally
// report complete pairs; declaration batches and OPV exhaustion behave differently.
inline constexpr double AbsoluteTcpAllowance = 10;
inline constexpr double RelativeTcpAllowance = .05;
inline constexpr double OrdinaryTcpComparableFraction = .95;

struct CountMetadata {
    Status status = Status::Unavailable;
    std::string observedAt;
    bool available = false, complete = false;
    bool fresh() const { return available && status == Status::Current; }
};
using RecordStatuses = std::map<std::string,std::pair<CountMetadata,CountMetadata>>;

struct Count {
    std::optional<Counts> votes;
    Status status = Status::Current;
    std::string observedAt;
    std::string error;
    bool complete = false; // Validated ordinary/PPVC completion, or explicit closure.
    bool fresh() const { return status == Status::Current && votes.has_value(); }
    CountMetadata metadata() const { return {status,observedAt,votes.has_value(),complete}; }
};
struct Record {
    std::string key, district, name, role, identity;
    std::vector<int> candidates;
    Count fp, tcp;
    double expected = 0;
    bool closed = false;
};
struct District {
    double enrolment = 0;
    bool finalised = false;
    bool wholeAccount = true; // Runtime cache-selection flag; source snapshots are whole accounts.
};
struct DistrictAccount {
    District district;
    std::vector<Record> records;
};
struct Snapshot {
    std::string election, sourceTime, sourceHash;
    bool optionalPreferential = false;
    std::map<std::string, District> districts;
    std::vector<Record> records;
    // Only needed when nearest per-record fallbacks span different snapshots.
    // Whole FP rollback then uses an earlier consistent district account. This
    // transient lookup is never serialized as part of a received source.
    std::map<std::string,DistrictAccount> wholeDistricts;
};

struct Issue {
    std::string type, key, district, name, explanation, action, observedAt;
    double impact = 0;
    bool routine = false;
    std::optional<Counts> rejected, effective;
};
// One case per type/record, irrespective of how often later checks revisit it.
// Summaries retain the largest vote discrepancy, with a stable identity tie.
class Registry {
public:
    void add(Issue issue);
    nlohmann::json summary() const;
    nlohmann::json details() const;
    bool empty() const { return cases.empty(); }
private:
    std::map<std::pair<std::string, std::string>, Issue> cases;
};
struct Result {
    Snapshot snapshot;
    Registry issues;
};

std::int64_t total(std::optional<Counts> const& votes);
std::string canonicalTime(std::string value);
// Most runs need only the nearest compatible record. The optional disk lookup
// searches older compatible FP before local recovery, and older TCP after FP
// recovery has established the effective parent. Its final argument selects
// TCP rather than FP; each search reads the affected records in one disk pass.
using CountHistoryLookup = std::function<std::optional<Snapshot>(Snapshot const&, Snapshot const*, bool)>;
Result recover(Snapshot current, std::optional<Snapshot> const& previous,
    CountHistoryLookup const& history = {});
bool fpFits(Record const& record, Count const& count, double enrolment);
bool tcpFits(Record const& record, Count const& count, bool optionalPreferential);
// Only checked current measurements can refresh the counting clock. A group
// containing any restored/unavailable constituent is not a fresh aggregate.
TurnoutModel::Observation freshObservation(TurnoutModel::Prior const& prior,
    std::vector<TurnoutModel::Unit> const& units, RecordStatuses const& statuses,
    std::string const& sourceTime);
// Disk state is private and integer-only. A search parses one source file at a
// time, retaining only the selected fallbacks and any coherent district account.
std::filesystem::path directory(std::filesystem::path const& root, std::string const& election);
std::optional<Snapshot> loadPrevious(std::filesystem::path const& directory,
    std::string const& election, std::string const& sourceTime);
std::optional<Snapshot> loadCompatiblePrevious(std::filesystem::path const& directory,
    Snapshot const& current);
std::optional<Snapshot> loadEarlierCounts(std::filesystem::path const& directory,
    Snapshot const& effective, Snapshot const* previous, bool tcp);
void commit(std::filesystem::path const& directory, Snapshot const& snapshot);
std::string fingerprint(std::filesystem::path const& source);
}
