#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// This portable count model consumes frozen Python prior outcomes. It has no
// party model or GUI dependency: the same immutable input is updated afresh
// for each snapshot, including when counted votes have been revised downwards.
namespace TurnoutModel {
using Matrix = std::vector<std::vector<double>>;
inline constexpr char CountVersion[] = "live-counts-6";
inline constexpr char ProgressVersion[] = "live-late-counts-3";
inline constexpr char AllocationVersion[] = "live-allocation-1";
// These conservative response scales are experiment assumptions, shared with
// Python. They govern continuous influence, not completion thresholds.
inline constexpr double AllocationProgressScale = .5;
inline constexpr double AllocationDirectionScale = .5;

struct Unit {
    std::size_t seat = 0, group = 0;
    std::string name, category, kind;
    std::string closureReason, exclusionReason;
    double weight = 0, counted = 0;
    bool matched = false, eav = false, closed = false, excluded = false;
};
struct Prior {
    std::string election;
    std::vector<std::string> seats, groups, subdivisions;
    std::vector<double> enrolment;
    Matrix totals, counts; // counts: sample x (seat * group count + group).
    std::vector<Unit> units;
};
struct Observation {
    double hour = 0; // source clock, in hours since the epoch; never capture time.
    std::map<std::pair<std::string, std::string>, double> counts;
};
struct Options {
    std::size_t countDraws = 8;
    unsigned long long seed = 20261002;
    double ppvcFactor = 1;
    std::optional<double> postalDeadline;
};
struct Broad {
    Matrix totals, counts, unitCounts, remaining, ownRemaining;
    std::vector<double> counted, compensation;
    std::vector<bool> complete;
};
struct Evidence {
    double strength = 0, activity = 0, coverage = 0, started = 0, volume = 0;
    double pooledActivity = 0, pooledStarted = 0, pooledVolume = 0;
    double stateWeight = 0, postalReceiptSupport = 1;
};
// Keep the conditional distributions for every preparation outcome. A rare
// batch must be available for deliberate party-share evaluation even when no
// mixed sample happened to select it. These are additions, not total counts.
struct Component {
    std::size_t unit = 0;
    double weight = 0, smallMean = 0, batchMean = 0;
    double previousProbability = 0, smallProbability = 0, batchProbability = 0;
    std::vector<double> capacity, reference, referenceOdds, smallOdds, batchOdds;
};
struct Result {
    Broad broad;
    // Samples describe the branch with additional votes. Mix with the shared
    // exact counted outcome using noAdditionProbability when reading results.
    Matrix totals, counts;
    std::vector<double> noAdditionProbability;
    std::vector<double> unitMeans;
    std::vector<Evidence> evidence;
    std::vector<Component> components;
    // Inspect the allowance before release and the evidence used to relax it.
    // The progress weight precedes the outcome-specific directional response.
    std::vector<double> balancedRemainingMeans, allocationStrength, allocationWeight;
};

void validate(Prior const& prior);
Broad update(Prior const& prior, std::vector<Unit> const& units,
    std::vector<bool> const& finalised, double ppvcFactor = 1);
std::vector<Evidence> progress(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> postalDeadline = {});
Result prepare(Prior const& prior, std::vector<Unit> const& units,
    std::vector<bool> const& finalised, std::vector<Observation> const& history,
    Options const& options = {}, Matrix const& suppliedUniforms = {});
std::vector<double> meanTotals(Result const& result);
double reportingFactor(double hoursAfterPollClose);
double sourceHour(std::string const& timestamp);
}
