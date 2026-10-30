#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "WaEarlyCounting.h"

// This portable count model consumes immutable pre-election count outcomes. It has no
// party model or GUI dependency: the same immutable input is updated afresh
// for each snapshot, including when counted votes have been revised downwards.
namespace TurnoutModel {
using Matrix = std::vector<std::vector<double>>;
inline constexpr char CountVersion[] = "live-counts-15";
inline constexpr char ProgressVersion[] = "live-late-counts-5";
inline constexpr char ScheduleVersion[] = "live-declaration-schedule-3";
inline constexpr char AllocationVersion[] = "live-allocation-1";
// Maintained conservative response scales, shared with the offline calibration.
// Progress influence is 1-exp(-(strength/scale)^2), reaching about .632 at the
// scale. Direction is logistic(-log-odds difference/scale): larger reductions
// receive more of that influence than increases. Neither is a completion cutoff.
inline constexpr double AllocationProgressScale = .5;
inline constexpr double AllocationDirectionScale = .5;
// Starting regularisation for competing size explanations, not fitted turnout
// errors. Historical prior spread can increase either minimum response scale.
inline constexpr double TransferCategorySpreadFloor = .20; // Natural log odds.
inline constexpr double TransferPlausibilitySpread = .25; // Restraint on transfer, not on own size.
inline constexpr double TurnoutChangeSpreadFloor = .15;    // Natural log total.
inline constexpr double TransferPriorOdds = 3;
inline constexpr double TurnoutChangeDegreesFreedom = 4;
inline constexpr double UncountedEnrolmentHeadroom = .01;
// At two original-size SDs above expectation, completion and an undersized
// prior receive equal support. This is a conservative response scale, not a
// fitted probability. Counts use log odds within enrolment with a half vote
// at each endpoint so an observed zero has a finite transformed position.
inline constexpr double CompletionSurpriseScale = 2;
inline constexpr double CompletionCountEquivalent = .5;
// Uncertainty around the postal near-finished remainder, in natural log odds
// of uncounted/final formal postals. Broaden the total SD from sqrt(.32) to
// 1.25, retaining shared SD .4 and putting the rest into local uncertainty.
// Lowering the log median by half the variance increase preserves the expected
// addition before allocation limits. These conservative Queensland 2024 trial
// settings do not establish calibration across elections.
inline constexpr double PostalRemainderSharedSd = .4;
inline constexpr double PostalRemainderLocalSd = 1.1842719282327012; // sqrt(1.25^2-.4^2).
inline constexpr double PostalRemainderMedianShift = (.32-1.25*1.25)/2;
inline constexpr double PostalFinishingOdds = 3;
// Nominal additional uncertainty: routine transformed variance uses the extra
// variance in a 1.5-fold spread; batches use the extra variance in SD 1.5 -> 2.
// A bounded mean-preserving pair, rather than a normal shape, applies these
// local magnitude scales AFTER allocation, leaving sibling quantities fixed.
inline constexpr double PostalRoutineSpread = 1.5;
inline constexpr double PostalBatchTargetSd = 2;

// The ORIGINAL medians were 6%, 4%, 2% at four/two/zero days before receipt
// closes. Recentring lowers these medians while preserving expected additions.
// Continuous log-odds interpolation and a two-day odds half-life remain; an
// unknown timetable uses the recentered day-four value, not an invented date.
double postalRemainderLogOdds(std::optional<double> daysBeforeDeadline);

// For absent/early-absent declarations, additional quietness evidence can
// replace part of an oversized routine allowance with a positive remainder.
// Two days of recent processing sets its median quantity, not a finish date.
// A local change supplies half an activity event when its size is the square
// root of (10 votes squared + 5% of the current count squared); the shared
// progress measurement retains its existing 2% relative response scale.
// Natural-log-odds SD 1.75 includes a shared SD .5 and independent local rest.
// These conservative settings were trialled on Queensland 2024, not fitted
// across elections; a processing pause can still conceal a large late batch.
inline constexpr double DeclarationLocalEventScale = .05;
inline constexpr double DeclarationRemainderDays = 2;
inline constexpr double DeclarationRemainderSd = 1.75;
inline constexpr double DeclarationRemainderSharedSd = .5;

// A second, three-part treatment of routine absent/early-absent allowances:
// use four days of processing as an expected positive amount, retain 20% of
// original routine scenarios as unresolved backlogs, and transfer some tiny
// additions towards modest ongoing counts. These Queensland 2024 trial
// assumptions change quantities after allocation; postal receipt keeps its
// existing treatment. The total natural-log spread includes shared SD .5.
inline constexpr double RoutineRemainderDays = 4;
inline constexpr double RoutineBacklogFraction = .2;
inline constexpr double RoutineRemainderSd = 1.25;
inline constexpr double RoutineRemainderSharedSd = .5;

struct Unit {
    std::size_t seat = 0, group = 0;
    std::string name, category, kind;
    std::string closureReason, exclusionReason;
    double weight = 0, counted = 0; // Starting fraction of its group; measured formal votes.
    bool matched = false, eav = false, closed = false, excluded = false;
    // Most physical places report a complete FP account once. Explicitly
    // mixed early/day places can report several batches while retaining their
    // physical identity. Source completion or closure can finish them; WA's
    // separate procedure can also finish their modelled routine additions.
    bool incremental = false;
    // Empty normally. An explicit category-transfer exclusion (for example a
    // newly introduced service with no comparable size baseline) leaves its
    // own estimate and counted votes intact. Unmatched booth names alone do
    // not establish that a voting category is new.
    std::string transferExclusionReason;
    // Runtime-only checked FP completion. The input boundary supplies this for
    // live runs so a restored count retains its accepted completion, while an
    // unavailable count cannot inherit a rejected source's finalisation flag.
    // Offline fixtures without recovery retain the ordinary counting rules.
    std::optional<bool> acceptedComplete;
    // Runtime observations from the compact accepted-count index, oldest first.
    // Only the WA mixed-place rule reads this; no project/prior format change.
    std::vector<double> receivedFpTotals;
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
// Expected processing timing, not a legal deadline or a completion flag. Days
// are calendar days after polling closes. At the centre, a quiet category keeps
// half the routine remaining-addition odds relative to unused enrolment, not
// half the counted votes. A two-day logistic scale makes that decline gradual.
// Local activity can retain more, and rare batches remain.
// Defaults are working assumptions; election overrides should cite comparable
// counting history (or explicitly identify retrospective calibration).
struct ProcessingTimetable {
    double daysAfterPollClose = 14, transitionDays = 2;
};
// Resolved source-clock settings. Unconfigured postal processing retains its
// receipt-based allowance and Sunday-adjusted clock. Explicit category entries
// use calendar dates; absence of one means activity-only evidence.
struct ResolvedTimetable {
    double referenceHour = 0, allowanceHours = 0, transitionHours = 48;
    double sharedTransitionHours = 48, smallBatchTransitionHours = 48;
    bool countingClock = false;
    bool separatePostalProcessing = false; // Also moderates quietness-derived completion support.
};
struct Options {
    std::size_t countDraws = 8; // Cheap diagnostic count draws per prior outcome.
    unsigned long long seed = 20261002;
    double ppvcFactor = 1; // Delay multiplier applied on odds of an unreported PPVC/enrolment share.
    std::optional<double> postalDeadline;
    // Legacy input name: postal processing reference only. It must never be
    // inherited by another category merely because that category is declared.
    std::optional<double> receiptDeadline;
    // Explicit polling close on the same source clock as Observation::hour.
    // Unknown dates disable the early, far-below-expectation completion guard.
    std::optional<double> pollClose;
    // PrePoll means declaration pre-poll/early-absent, not physical early booths.
    // Null explicitly disables a category's timetable. An explicit Postal entry
    // separates processing from receipt; otherwise legacy postal timing remains.
    // Other unlisted categories have no schedule. Poll-close-based entries are
    // inactive if that date is unknown; receipt safeguards remain independent.
    std::map<std::string, std::optional<ProcessingTimetable>> processingTimetables = {
        {"Absent",ProcessingTimetable{14,2}}, {"PrePoll",ProcessingTimetable{14,2}},
        {"Provisional",ProcessingTimetable{18,2}}};
    bool postalWidening = true; // Runtime comparison control; no project-format change.
    bool declarationRemainders = true; // Runtime comparison only; frozen priors are unchanged.
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
    // One competing-explanation probability per prior outcome and district.
    // The branch is sampled coherently for the whole district, once per joint
    // preparation row; main simulation draws retain that row's chosen account.
    Matrix transferProbability;
    Matrix transferSelected;
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
    std::optional<double> postalDaysBeforeDeadline; // Calendar days, from the received source clock.
    // Support from small positive batches late in this category's own timetable.
    double smallBatchSupport = 0;
    // Supplementary local activity and quietness support for positive
    // remainders only. Neither replaces strength nor changes pooled evidence,
    // allocation, exact-zero probability or exceptional-batch probability.
    double gentleLocalActivity = 0, positiveRemainderSupport = 0;
    double postalFinishingSupport = 0; // Gentle support without a receipt-closure assumption.
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
    // These probabilities include the early-shortfall guard but precede the
    // local quantity limit. A fraction completionWeight of routine draws uses
    // a finishing amount. Postal limits enter the shared allocation before
    // other categories receive their shares. Non-postal limits are applied
    // afterwards, releasing their reductions only to unused turnout.
    double completionWeight = 0;
    // Additional fraction of the prepared routine branch selected after its
    // size-based completion fraction (both rescaled by the early guard).
    // The positive amount is a cap applied
    // after allocation, leaving siblings' counts and all observed votes fixed.
    double positiveRemainderWeight = 0, positiveRemainderMedian = 0;
    // Present only for Postal. This is the median fraction of final formal
    // postals still uncounted, expressed as log odds, before allocation limits.
    // A positive remainder distinguishes main counting being largely finished
    // from exactly no further votes. The latter retains its receipt safeguard.
    std::optional<double> postalRemainderLogOdds;
    double postalCounted = 0;
    std::optional<double> originalSizeZ; // SDs on original count/enrolment log odds; absent if unsupported.
    double usualLogShift = 0; // Timetable reduction of routine addition log odds.
    double processingPhase = 0, batchSurvival = 1; // This category only.
    double earlyCompletionRetention = 1; // Fraction of local finishing support retained.
    double batchSd = 1.5; // Underlying natural-log-odds SD BEFORE postal output widening.
    // Extra local variation measured after final allocation/finishing limits.
    // Each smaller/larger possibility pair has the original draw as its exact
    // mean, so main iterations need no fitted offset or numerical solver.
    bool postalWidening = false;
    double postalRoutineExtraSd = 0, postalBatchExtraSd = 0;
    // Three-part routine treatment. Support combines observed history coverage,
    // counting having started, the early/below-prior guard, and category-wide
    // maturity relative to its frozen original size. Mean is expected additional
    // formal votes from the processing proxy, not known uncounted envelopes.
    // Bridge moves a fraction of tiny outcomes to modest positive amounts when
    // that estimate is distinct from the old routine allowance. ReferenceMean
    // holds sibling odds fixed; MeanAdjustment integrates the new local choice.
    double routineSupport = 0, routineMean = 0, routineMaturity = 0;
    double routineBridge = 0, routineReferenceMean = 0, routineMeanAdjustment = 0;
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
    // Exact conditional means before the analytically neutral postal spread.
    // Percentiles use the broader samples above; means avoid added sampling
    // noise. Empty in hand-constructed legacy test accounts.
    std::vector<double> positiveMeanTotals, positiveMeanCounts;
    std::vector<double> noAdditionProbability; // district: exact counted-only outcome.
    std::vector<double> unitMeans;             // unit: unconditional projected count mean.
    std::vector<Evidence> evidence;            // unit: counting-progress measurements.
    std::vector<Component> components;         // One per unfinished declaration unit.
    // Inspect the allowance before release and the evidence used to relax it.
    // All three vectors are indexed by unit. The progress weight precedes the
    // outcome-specific directional response in the allocation calculation.
    std::vector<double> balancedRemainingMeans, allocationStrength, allocationWeight;
    // Only categories actually present and with a usable clock are listed.
    std::map<std::string, ResolvedTimetable> processingTimetables;
    std::optional<double> daysAfterPollClose; // Calendar days; absent if no explicit date/source.
    struct WaEarlyPlace {
        std::size_t unit = 0;
        WaEarlyCounting::State state;
        double counted = 0;
    };
    std::vector<WaEarlyPlace> waEarlyPlaces;
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
    bool scheduleAware = false, double eventScale = .001,
    std::map<std::string, ResolvedTimetable> const& timetables = {});
std::optional<ResolvedTimetable> processingTimetable(Options const& options, std::string const& category);
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
