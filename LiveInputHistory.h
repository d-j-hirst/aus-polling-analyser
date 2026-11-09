#pragma once

#include "json.h"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>

// Accepted measurements are stored as timestamped field changes, preserving
// original clocks, missing/zero distinctions and candidate identities. Normal
// live work uses the authoritative change-history-v1 store. The older disposable
// change-index-v1 reader remains available for legacy imports/equivalence tests.
namespace LiveInputHistory {
using Selection = std::map<std::string, std::set<std::string>>; // District -> record keys.
using Validate = std::function<void(nlohmann::json const&)>;
// Null/unavailable accounts cannot satisfy these readers. Genuine measured zero
// remains eligible; this filter never discards an older available observation.
enum class RequiredCount { None, FirstPreferences, TwoCandidatePreferred, Both };
// Each callback receives one selected record inside its original source metadata.
// False finishes that record's search. Source stem and original record position
// let callers preserve ordering when combining independently selected fallbacks.
using Visitor = std::function<bool(std::string const&, std::size_t, nlohmann::json const&)>;

// Legacy disposable-index readers may recover this error by restarting from the
// full files. Authoritative-history errors must propagate: falling back could
// silently replace accepted evidence with an older or incomplete legacy version.
class Unavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Legacy derived-index reader: visit strictly earlier observations newest first
// FOR EACH selected record.
// Districts are processed separately to bound memory without repeatedly reloading
// evicted partitions. Validation uses complete original sources at ingest.
void visitEarlier(std::filesystem::path const& folder, std::string const& cutoffFileStem,
    Selection const& selection, Validate const& validate, Visitor const& visitor,
    RequiredCount required = RequiredCount::None);

// A numeric FP history needs its count states and observation clocks, without
// reconstructing TCP, preferences or the original record's array position.
// One run contains consecutive captures with the same encoded FP state. Sources
// retain EVERY capture clock, newest first: restored observations may refer to
// an older time between otherwise identical fresh counts. Dropping those clocks
// could change the ordering after the caller sorts by original observation time.
struct CountSourceClock {
    std::string election, time; // Embedded source identity and clock, not filesystem time.
};
// The account uses the first source's time when its clock follows each capture;
// followsSource tells the caller to substitute each source's clock thereafter.
// Otherwise its fixed original observation time must be retained. The borrowed
// clock span is valid only during the callback. Unavailable FP stays JSON null
// within the account; it is not converted to a measured zero.
using FirstPreferenceRunVisitor = std::function<void(std::string const&,
    nlohmann::json const&, bool followsSource, std::span<CountSourceClock const>)>;

// Visit FP runs strictly BEFORE cutoffFileStem, for selected district/record
// keys only. Like visitEarlier this validates/refreshes the disposable index,
// bounds retained history and throws Unavailable for source-file fallback.
void visitFirstPreferenceRuns(std::filesystem::path const& folder,
    std::string const& cutoffFileStem, Selection const& selection,
    Validate const& validate, FirstPreferenceRunVisitor const& visitor);

// Legacy maintenance only, AFTER the complete accepted-count file is committed.
// Refresh an existing index (including same-time replacements); an index failure
// cannot turn a successful accepted-count write into a failed forecast.
void committed(std::filesystem::path const& folder, std::string const& fileStem,
    Validate const& validate) noexcept;

// New production storage. Adopt an existing validated legacy index on first use,
// importing any missing legacy sources once. Subsequent reads never inventory
// the old full files. Failure of an authoritative history is an error, not a
// reason to silently use an older/incomplete legacy cache.
bool hasAcceptedHistory(std::filesystem::path const& folder);
void accept(std::filesystem::path const& folder, std::string const& source,
    nlohmann::json const& snapshot, Validate const& validate);
void eraseAccepted(std::filesystem::path const& folder, std::string const& source,
    Validate const& validate);
using DistrictVisitor = std::function<bool(std::string const&, nlohmann::json const&)>;
// Visit one district at a time, newest strictly-earlier sources first. False
// finishes that district. This preserves coherent whole-district recovery
// without reconstructing every other district at each candidate timestamp.
void visitAcceptedDistricts(std::filesystem::path const& folder, std::string const& cutoff,
    Selection const& selection, Validate const& validate, DistrictVisitor const& visitor);
void visitAcceptedEarlier(std::filesystem::path const& folder, std::string const& cutoff,
    Selection const& selection, Validate const& validate, Visitor const& visitor,
    RequiredCount required = RequiredCount::None);
void visitAcceptedFirstPreferenceRuns(std::filesystem::path const& folder,
    std::string const& cutoff, Selection const& selection, Validate const& validate,
    FirstPreferenceRunVisitor const& visitor);

// Diagnostics for portable tests and profiling. The cache holds no project or GUI
// references, is mutex-protected, and retains at most one history directory.
struct Statistics {
    std::size_t sourceReads = 0, partitionReads = 0, builds = 0;
    std::size_t appendBatches = 0, partitionWrites = 0, publications = 0;
    std::size_t retainedBytes = 0, peakRetainedBytes = 0;
    std::size_t fallbacks = 0, updateFailures = 0;
    std::size_t fpRunCallbacks = 0, fpSourceClocks = 0;
    std::size_t adoptions = 0, unchangedCommits = 0, acceptedCommits = 0;
    std::string lastFailure;
};
Statistics statistics();
void clearMemoryCache(std::size_t byteBudget = 16 * 1024 * 1024);
}
