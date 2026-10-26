#pragma once

#include "json.h"
#include "TurnoutModel.h"
#include "LivePartialCount.h"
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
// A count's source stage survives recovery independently of its observation
// clock. Most feeds do not identify stages; only an explicit later count round
// can establish that a recheck has priority over a preliminary account.
enum class CountStage { Unspecified, Preliminary, Rechecked };
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
    CountStage stage = CountStage::Unspecified;
    bool fresh() const { return available && status == Status::Current; }
};
using RecordStatuses = std::map<std::string,std::pair<CountMetadata,CountMetadata>>;

// Use the effective records' stages, including restored records' original
// stages. A newer source's official label cannot promote preliminary fallback
// counts. Count reconciliation remains the consuming model's responsibility.
inline bool explicitlyRecheckedPair(CountMetadata const& fp, CountMetadata const& tcp) {
    return fp.available && tcp.available && fp.stage == CountStage::Rechecked
        && tcp.stage == CountStage::Rechecked;
}

struct Count {
    std::optional<Counts> votes;
    Status status = Status::Current;
    std::string observedAt;
    std::string error;
    bool complete = false; // Validated ordinary/PPVC completion, or explicit closure.
    CountStage stage = CountStage::Unspecified;
    bool fresh() const { return status == Status::Current && votes.has_value(); }
    CountMetadata metadata() const { return {status,observedAt,votes.has_value(),complete,stage}; }
};
// A matched preference batch retains measured integer accounts and their
// original observation times. It contains no fitted flow or forecast values.
struct PreferenceCohort {
    Counts fp, tcp;
    std::string fpTime, tcpTime;
    LivePartialCount::Transfers transfers;
    // Preserve explicit recheck status with this pair. An unrelated later FP
    // stage cannot certify old TCP, or erase a previously reconciled recheck.
    bool recheckedReconciledPair = false;
};
struct Record {
    std::string key, district, name, role, identity;
    std::vector<int> candidates;
    Count fp, tcp;
    double expected = 0;
    bool closed = false;
    std::optional<PreferenceCohort> matchedPreferences;
    // Original FP/transfer account belonging to the current TCP. Unlike the
    // most recent useful flow measurement, this must follow an unchanged TCP
    // through later FP revisions and source withdrawal/recovery.
    std::optional<PreferenceCohort> tcpBasis;
    // Source-only publication status, never inherited from cached counts.
    // ECQ withdraws indicative TCP while official rechecking is in progress.
    bool expectedTcpWithdrawal = false;
};
inline bool hasCurrentTcpBasis(Record const& record) {
    if (!record.tcpBasis || !record.fp.votes || !record.tcp.votes || record.closed ||
        LivePartialCount::total(*record.fp.votes) <= 0) return false;
    auto const& basis = *record.tcpBasis;
    if (basis.tcp != *record.tcp.votes || basis.fp.size() != record.fp.votes->size() ||
        !LivePartialCount::validBasis({basis.fp,basis.tcp,basis.transfers})) return false;
    for (auto const& [candidate,votes] : basis.fp)
        if (!record.fp.votes->contains(candidate)) return false;
    return true;
}
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

// A completed preference batch may describe an older FP batch while new first
// preferences have already arrived. Retain only that cohort and the latest
// comparable earlier account, not the sequence of parsed source documents.
struct PreferenceHistory {
    std::optional<PreferenceCohort> matchingCurrentTcp, previousAligned;
    std::optional<PreferenceCohort> tcpBasis;
};
using PreferenceHistories = std::map<std::string,PreferenceHistory>;
PreferenceHistories loadPreferenceHistory(std::filesystem::path const& folder,
    Snapshot const& effective);
// Stage the selected measured pair with the accepted count state. It is saved
// only by the normal successful-run commit, so later TCP does not erase it.
void retainPreferenceHistory(Snapshot& effective, PreferenceHistories const& history);

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
// Most runs need only the nearest compatible record. A reconciliation hold
// needs both counts from one accepted record, rather than independently selected
// FP and TCP dates. Other recovery searches FP first, then TCP against effective
// FP. Each disk search reads only affected records, one source file at a time.
enum class HistoryAccount { FirstPreferences, TwoCandidatePreferred, Reconciled };
using CountHistoryLookup = std::function<std::optional<Snapshot>(Snapshot const&, Snapshot const*, HistoryAccount)>;
Result recover(Snapshot current, std::optional<Snapshot> const& previous,
    CountHistoryLookup const& history = {});
bool fpFits(Record const& record, Count const& count, double enrolment);
bool tcpFits(Record const& record, Count const& count, bool optionalPreferential);
// A temporary FP reset needs a larger earlier accepted account. A contradiction
// within finalist primaries can also hold a coherent older pair when neither
// current count has a clear source-stage advantage. Reconciled corrections
// still pass through; this is not a monotonic-count rule.
bool reconciliationNeeded(Record const& record, bool optionalPreferential);
// A candidate-level contradiction can require a hold even when the aggregate
// FP count did not fall. This also controls whether older joint history is useful.
bool tcpContradictsPrimaries(Record const& record, bool optionalPreferential);
bool recheckHasPriority(Record const& record, Record const& previous);
bool reconciliationFits(Record const& record, Record const& previous,
    double enrolment, bool optionalPreferential);
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
    Snapshot const& effective, Snapshot const* previous, HistoryAccount account);
void commit(std::filesystem::path const& directory, Snapshot const& snapshot);
std::string fingerprint(std::filesystem::path const& source);
}
