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

// A disposable index of accepted measurements, not another source of election
// results. The original snapshots remain authoritative. The index stores changes
// separately for each record field, and reconstructs their original source clocks
// before the existing recovery and preference-matching rules see them.
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

// An unavailable/damaged derived index may fall back to the original file reader.
// Callers must restart their search, discarding any partially selected results.
class Unavailable : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Visit strictly earlier observations newest first FOR EACH selected record.
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

// Called only AFTER the authoritative accepted-count file has been committed.
// Refresh an existing index (including same-time replacements); an index failure
// cannot turn a successful accepted-count write into a failed forecast.
void committed(std::filesystem::path const& folder, std::string const& fileStem,
    Validate const& validate) noexcept;

// Diagnostics for portable tests and profiling. The cache holds no project or GUI
// references, is mutex-protected, and retains at most one history directory.
struct Statistics {
    std::size_t sourceReads = 0, partitionReads = 0, builds = 0;
    std::size_t appendBatches = 0, partitionWrites = 0, publications = 0;
    std::size_t retainedBytes = 0, peakRetainedBytes = 0;
    std::size_t fallbacks = 0, updateFailures = 0;
    std::size_t fpRunCallbacks = 0, fpSourceClocks = 0;
    std::string lastFailure;
};
Statistics statistics();
void clearMemoryCache(std::size_t byteBudget = 16 * 1024 * 1024);
}
