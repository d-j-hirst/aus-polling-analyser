#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// This portable count model consumes immutable pre-election count outcomes. It has no
// party model or GUI dependency: the same immutable input is updated afresh
// for each snapshot, including when counted votes have been revised downwards.
namespace TurnoutModel {
using Matrix = std::vector<std::vector<double>>;
inline constexpr char CountVersion[] = "live-counts-6";
inline constexpr char ProgressVersion[] = "live-late-counts-4";
inline constexpr char ScheduleVersion[] = "live-declaration-schedule-1";
inline constexpr char AllocationVersion[] = "live-allocation-1";
// Maintained conservative response scales, shared with the offline calibration.
// Progress influence is 1-exp(-(strength/scale)^2), reaching about .632 at the
// scale. Direction is logistic(-log-odds difference/scale): larger reductions
// receive more of that influence than increases. Neither is a completion cutoff.
inline constexpr double AllocationProgressScale = .5;
inline constexpr double AllocationDirectionScale = .5;

struct Unit {
    std::size_t seat = 0, group = 0;
    std::string name, category, kind;
    std::string closureReason, exclusionReason;
    double weight = 0, counted = 0; // Starting fraction of its group; measured formal votes.
    bool matched = false, eav = false, closed = false, excluded = false;
};
struct Prior {
    std::string election;
    std::vector<std::string> seats, groups, subdivisions;
    std::vector<double> enrolment;
    // One row per joint prior outcome. District/group columns are flattened
    // as seat * groups.size() + group, using the name vectors' ordering.
    Matrix totals; // prior outcome x district: final formal vote counts.
    Matrix counts; // prior outcome x flattened district/group: formal counts.
    std::vector<Unit> units;
};
struct Observation {
    double hour = 0; // source clock, in hours since the epoch; never capture time.
    std::map<std::pair<std::string, std::string>, double> counts;
};
struct Options {
    std::size_t countDraws = 8; // Cheap diagnostic count draws per prior outcome.
    unsigned long long seed = 20261002;
    double ppvcFactor = 1; // Delay multiplier applied on odds of an unreported PPVC/enrolment share.
    std::optional<double> postalDeadline;
    // Last receipt time for this election's declaration ballots. This is an
    // operational input, not a claim that all seats have counted them by then.
    std::optional<double> receiptDeadline;
};
// Count estimates after unit/group balancing, before the late-addition mixture.
// Whole-group progress can subsequently revise the allocation in this account.
struct Broad {
    // Rows retain the prior-outcome pairing through the count update.
    Matrix totals;        // prior outcome x district: projected formal total.
    Matrix counts;        // prior outcome x flattened district/group: projected counts.
    Matrix unitCounts;    // prior outcome x unit: counted votes plus additions.
    Matrix remaining;     // prior outcome x unit: additions after group allocation/release.
    Matrix ownRemaining;  // prior outcome x unit: additions before group allocation.
    std::vector<double> counted;      // district: sum of measured unit counts.
    std::vector<double> compensation; // unit: PPVC adjustment in log odds of count/enrolment.
    std::vector<bool> complete;       // unit: reported booth, known closure or district final flag.
};
struct Evidence {
    // Strength is a response score, not a completion probability. Activity is
    // recent change-event support; coverage measures observed elapsed time.
    // Started = current category count / (count + 10). Volume is the decayed
    // sum of absolute vote-count changes, including downward rechecks.
    double strength = 0, activity = 0, coverage = 0, started = 0, volume = 0;
    // Trimmed district means for the same category, with regional blending.
    // Pooled volume is relative to each district's current count + half a vote.
    double pooledActivity = 0, pooledStarted = 0, pooledVolume = 0;
    // Regional share of the pooled measurement; postal receipt-window discount.
    double stateWeight = 0, postalReceiptSupport = 1;
    // Support supplied by small positive batches after the receipt deadline.
    double smallBatchSupport = 0;
};
// Keep the conditional distributions for every preparation outcome. A rare
// batch must be available for deliberate party-share evaluation even when no
// mixed sample happened to select it. These are additions, not total counts.
struct Component {
    std::size_t unit = 0; // Index in the snapshot's unit vector.
    // Weight is the progress response. Small/batch means are conditional
    // additions with other category counts held fixed, not probability weights.
    double weight = 0, smallMean = 0, batchMean = 0;
    // Unconditional weights: sum = 1 - noAdditionProbability[unit's district].
    // Divide by that sum only when sampling conditional on further votes.
    double previousProbability = 0, smallProbability = 0, batchProbability = 0;
    double usualLogShift = 0; // Timetable reduction of routine addition log odds.
    // Each vector has one entry per prior outcome. Capacity is unused enrolment
    // (enrolment - broad formal total); reference is the allocated addition.
    // Odds vectors are logs of addition/capacity, with the routine shift applied
    // to referenceOdds. Small/batch spreads are applied during sampling.
    std::vector<double> capacity, reference, referenceOdds, smallOdds, batchOdds;
};
struct Result {
    Broad broad;
    // Samples describe the branch with additional votes. Mix with the shared
    // exact counted outcome using noAdditionProbability when reading results.
    Matrix totals, counts; // (prior outcomes * countDraws) x district or flattened district/group.
    std::vector<double> noAdditionProbability; // district: exact counted-only outcome.
    std::vector<double> unitMeans;             // unit: unconditional projected count mean.
    std::vector<Evidence> evidence;            // unit: counting-progress measurements.
    std::vector<Component> components;         // One per unfinished declaration unit.
    // Inspect the allowance before release and the evidence used to relax it.
    // All three vectors are indexed by unit. The progress weight precedes the
    // outcome-specific directional response in the allocation calculation.
    std::vector<double> balancedRemainingMeans, allocationStrength, allocationWeight;
    // Schedule summaries: blend fraction, exceptional-batch probability
    // multiplier, and Sunday-adjusted hours since the legal receipt deadline.
    double processingPhase = 0, batchSurvival = 1, countingHoursAfterDeadline = 0;
};

// Freeze the indexing needed by main simulation draws once per snapshot. The
// large prior/conditional arrays remain shared in Result; a draw only chooses
// one joint prior outcome and evaluates the few unfinished categories.
struct DrawPlan {
    std::vector<double> counted, enrolment; // Unit counts; district enrolment.
    std::vector<std::size_t> seats;         // Unit -> district index.
    std::vector<std::vector<std::size_t>> componentsBySeat; // District -> Result component indices.
    std::vector<bool> active; // District: a component differs from its broad reference.
};
DrawPlan makeDrawPlan(Result const& result, std::vector<Unit> const& units,
    std::vector<double> const& enrolment);
std::vector<double> drawUnitCounts(Result const& result, DrawPlan const& plan,
    unsigned long long seed);

void validate(Prior const& prior);
Broad update(Prior const& prior, std::vector<Unit> const& units,
    std::vector<bool> const& finalised, double ppvcFactor = 1);
std::vector<Evidence> progress(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> postalDeadline = {},
    bool scheduleAware = false, double eventScale = .001);
Result prepare(Prior const& prior, std::vector<Unit> const& units,
    std::vector<bool> const& finalised, std::vector<Observation> const& history,
    Options const& options = {}, Matrix const& suppliedUniforms = {});
std::vector<double> meanTotals(Result const& result);
double reportingFactor(double hoursAfterPollClose);
double sourceHour(std::string const& timestamp);
// Elapsed processing time on the source's local clock. Sunday contributes half
// a day; receipt deadlines themselves always remain on the calendar clock.
double countingHours(double before, double after);
}
