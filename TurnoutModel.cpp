#include "TurnoutModel.h"
#include "Date.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>

namespace TurnoutModel {
namespace {
double logistic(double x) { return x >= 0 ? 1 / (1 + std::exp(-x)) : std::exp(x) / (1 + std::exp(x)); }
double softplus(double x) { return std::max(x,0.)+std::log1p(std::exp(-std::abs(x))); }
double odds(double x) {
    if (!(x > 0 && x < 1)) throw std::runtime_error("Turnout share must be inside its parent.");
    return std::log(x) - std::log1p(-x);
}
double cdf(double x) { return .5 * std::erfc(-x / std::sqrt(2.)); }
// Count conditioning needs probabilities in a normal distribution's far tail.
// Usually the standard formula suffices; use log probabilities and a tail
// approximation when observed counts greatly exceed the starting expectation,
// so tiny probabilities do not become artificial numerical zeros.
double logCdf(double x) {
    if (x >= 0) return std::log1p(-cdf(-x));
    if (x > -10) return std::log(cdf(x));
    double inv = 1 / (x*x), term = 1, sum = 1;
    for (int k = 1; k < 50; ++k) {
        double next = term * -(2*k-1) * inv;
        if (std::abs(next) >= std::abs(term)) break;
        sum += next; term = next;
        if (std::abs(term) < 1e-16 * std::abs(sum)) break;
    }
    return -.5*x*x - std::log(-x) - .5*std::log(2*std::acos(-1.)) + std::log(sum);
}
double inverseLogCdf(double value) {
    double low = -std::max(12., std::sqrt(-2*value)+2), high = 12;
    for (int k = 0; k < 64; ++k) {
        double mid = (low+high)/2;
        if (logCdf(mid) < value) low = mid; else high = mid;
    }
    return (low+high)/2;
}
double mean(std::vector<double> const& v) { return std::accumulate(v.begin(), v.end(), 0.) / v.size(); }
Matrix zeros(std::size_t rows, std::size_t columns) { return Matrix(rows, std::vector<double>(columns)); }

// Update the original outcomes for one share after some votes have been counted.
// Shares and counted lower bounds use the same parent count. Fit their original
// spread on log odds, then retain only possibilities above the counted bound.
// Each original row keeps its relative position within that remaining spread,
// preserving its relationships to other counts in the same prior outcome.
std::vector<double> condition(std::vector<double> const& shares, std::vector<double> const& lower) {
    std::vector<double> transformed;
    for (double x : shares) transformed.push_back(odds(x));
    double centre = mean(transformed), variance = 0;
    for (double x : transformed) variance += (x-centre)*(x-centre);
    double spread = std::sqrt(variance/shares.size());
    auto result = shares;
    for (std::size_t r = 0; r < shares.size(); ++r) {
        if (!(lower[r] >= 0 && lower[r] < 1)) throw std::runtime_error("Counted votes reach their turnout parent.");
        if (lower[r] == 0) continue;
        if (spread < 1e-12) {
            if (shares[r] <= lower[r]) throw std::runtime_error("Deterministic turnout prior conflicts with counted votes.");
            continue;
        }
        double threshold = (odds(lower[r])-centre)/spread;
        double position = (transformed[r]-centre)/spread;
        result[r] = logistic(centre-spread*inverseLogCdf(logCdf(-threshold)+logCdf(-position)));
    }
    return result;
}
// Divide a finite remaining vote total between booth and declaration additions.
// Base is the original booth estimate, requested is its adjusted amount, and
// declaration is the competing aggregate estimate. Shift the original booth
// proportion smoothly in log odds: a larger request increases its allocation
// while preserving a positive declaration remainder when both were possible.
std::pair<double, double> reserve(double base, double requested, double declaration, double budget) {
    double reference = base+declaration > 0 ? base/(base+declaration) : 0;
    if (reference == 0) return {0, budget};
    if (reference == 1) return {budget, 0};
    double ratio = budget > 0 ? requested/budget/reference : 0;
    double shifted = odds(reference)+std::log(ratio)/(1-reference);
    return {budget*logistic(shifted), budget*logistic(-shifted)};
}
bool eav(Unit const& u) { return u.eav || u.name.rfind("EAV ", 0) == 0; }

// Stable per-unit permutations provide a balanced quantile grid without tying
// the C++ implementation to NumPy's random generator or booth traversal order.
unsigned long long step(unsigned long long& state) {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value = (value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
std::vector<double> uniforms(std::string const& key, std::size_t count, unsigned long long seed) {
    unsigned long long state = 14695981039346656037ULL;
    for (unsigned char c : key) state = (state^c)*1099511628211ULL;
    state ^= seed;
    std::vector<std::size_t> positions(count);
    std::iota(positions.begin(), positions.end(), 0);
    for (std::size_t n = count; n > 1; --n) std::swap(positions[n-1], positions[step(state)%n]);
    std::vector<double> result;
    for (auto p : positions) result.push_back((p+.5)/count);
    return result;
}
// Find the addition size at a supplied probability within the positive outcome.
// Possibilities mix the unchanged previous estimate with small additions and
// larger batches. Their probabilities sum to one here, after the separate
// no-addition outcome has been removed. Small/batch spreads are 1/1.5 in the
// log odds of addition versus unused capacity. Numerical search brackets and
// 40 bisections limit calculation time without capping possible vote counts.
double mixture(double ref, double small, double batch, double previous, double smallProbability, double batchProbability, double uniform) {
    auto continuous = [&](double x) { return smallProbability*cdf(x-small)+batchProbability*cdf((x-batch)/1.5); };
    double below = continuous(ref), above = below+previous;
    if (uniform >= below && uniform <= above) return ref;
    double target = uniform < below ? uniform : uniform-previous;
    double low = std::min(small-12, batch-18), high = std::max(small+12, batch+18);
    for (int k = 0; k < 40; ++k) {
        double middle = (low+high)/2;
        if (continuous(middle) < target) low = middle; else high = middle;
    }
    return (low+high)/2;
}
}

void validate(Prior const& p) {
    auto fail = [] { throw std::runtime_error("Invalid frozen turnout prior dimensions or counts."); };
    auto n = p.seats.size(), g = p.groups.size();
    if (!n || !g || p.totals.size() < 2 || p.counts.size() != p.totals.size() || p.enrolment.size() != n || p.subdivisions.size() != n) fail();
    if (std::set<std::string>(p.seats.begin(),p.seats.end()).size() != n || std::set<std::string>(p.groups.begin(),p.groups.end()).size() != g) fail();
    for (std::size_t r = 0; r < p.totals.size(); ++r) {
        if (p.totals[r].size() != n || p.counts[r].size() != n*g) fail();
        for (std::size_t s = 0; s < n; ++s) {
            if (!std::isfinite(p.enrolment[s]) || !std::isfinite(p.totals[r][s]) || !(p.totals[r][s] > 0 && p.totals[r][s] < p.enrolment[s])) fail();
            double sum = 0;
            for (std::size_t k = 0; k < g; ++k) {
                double x = p.counts[r][s*g+k];
                if (!std::isfinite(x) || x < 0) fail();
                sum += x;
            }
            if (std::abs(sum-p.totals[r][s]) > 1e-6) fail();
        }
    }
    std::set<std::pair<std::size_t,std::string>> identities;
    for (auto const& u : p.units) {
        if (u.seat >= n || u.group >= g || !std::isfinite(u.weight) || u.weight < 0 || u.weight > 1 || u.name.empty()
            || (u.kind != "ordinary" && u.kind != "ppvc" && u.kind != "declaration") || !identities.emplace(u.seat,u.name).second) fail();
    }
}

namespace {
// Scratch arrays for one immutable-prior update. Unit columns and flattened
// district/group columns use the same ordering as Broad and Prior respectively.
// This workspace is discarded after preparation; it is not snapshot history.
struct CountUpdateWorkspace {
    Broad result;
    Matrix adjustedExpected, originalExpected, ownAdditions, groupAdditions;
    std::vector<std::vector<std::size_t>> unitsByGroup;
    std::vector<double> groupCounted;
    std::vector<bool> boothOnlyGroups, openGroups, completeSeats;
};

// Validate measured units, classify their completion and assemble group lookup
// tables before any estimates change. Reported ordinary/PPVC counts are final
// for this count update; declarations remain lower bounds unless closed/final.
CountUpdateWorkspace classifyCountUnits(Prior const& prior, std::vector<Unit> const& units,
    std::vector<bool> const& finalised) {
    auto samples = prior.totals.size(), seats = prior.seats.size(), groups = prior.groups.size();
    CountUpdateWorkspace work;
    auto& result = work.result;
    result.totals = zeros(samples,seats);
    result.counts = zeros(samples,seats*groups);
    result.unitCounts = zeros(samples,units.size());
    result.remaining = zeros(samples,units.size());
    result.counted.assign(seats,0);
    result.complete.resize(units.size());
    result.compensation.assign(units.size(),0);
    work.adjustedExpected = zeros(samples,units.size());
    work.originalExpected = zeros(samples,units.size());
    work.ownAdditions = zeros(samples,units.size());
    work.groupAdditions = zeros(samples,seats*groups);
    work.unitsByGroup.resize(seats*groups);
    work.groupCounted.resize(seats*groups);
    work.boothOnlyGroups.assign(seats*groups,true);
    work.openGroups.resize(seats*groups);
    work.completeSeats = finalised;
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& unit = units[j];
        if (unit.seat >= seats || unit.group >= groups || !std::isfinite(unit.counted) || unit.counted < 0
            || !std::isfinite(unit.weight) || unit.weight < 0 || unit.weight > 1)
            throw std::runtime_error("Invalid current turnout unit.");
        auto group = unit.seat*groups+unit.group;
        work.unitsByGroup[group].push_back(j);
        work.groupCounted[group] += unit.counted;
        result.counted[unit.seat] += unit.counted;
        result.complete[j] = unit.closed || unit.acceptedComplete.value_or(
            (unit.kind != "declaration" && unit.counted > 0) || finalised[unit.seat]);
        work.boothOnlyGroups[group] = work.boothOnlyGroups[group] && unit.kind != "declaration";
        work.openGroups[group] = work.openGroups[group] || !result.complete[j];
        for (std::size_t r = 0; r < samples; ++r)
            work.adjustedExpected[r][j] = work.originalExpected[r][j] = prior.counts[r][group]*unit.weight;
    }
    return work;
}

void requireCountedVotesBelowEnrolment(std::string const& district, double counted, double enrolment) {
    // Normally the formal count is below the registered-elector total, even
    // when counting has finished. An impossible feed total cannot be repaired
    // by discarding counted votes or increasing the model's enrolment. Stop
    // with the district and both figures so the input can be reviewed directly.
    // Live source recovery runs before this model; this remains the final
    // defence against an inconsistent prepared account or configuration.
    if (counted < enrolment) return;
    std::ostringstream message;
    message << "Counted formal votes in " << district << " (" << counted
        << ") reach or exceed the prepared enrolment (" << enrolment << "). "
        << "Check the result feed for duplicate or inconsistent counts, or correct the prepared enrolment if it is wrong.";
    throw std::runtime_error(message.str());
}

// Resolve the formal-total parent first. A completed count is exactly its
// measured total; otherwise condition the original total/enrolment distribution
// above that lower bound. An empty configuration does not establish completion.
void conditionDistrictTotals(Prior const& prior, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), groups = prior.groups.size();
    auto& result = work.result;
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        bool hasUnits = false, hasOpenUnits = false;
        for (std::size_t k = s*groups; k < (s+1)*groups; ++k) {
            hasUnits = hasUnits || !work.unitsByGroup[k].empty();
            hasOpenUnits = hasOpenUnits || work.openGroups[k];
        }
        work.completeSeats[s] = work.completeSeats[s] || (hasUnits && !hasOpenUnits);
        requireCountedVotesBelowEnrolment(prior.seats[s],result.counted[s],prior.enrolment[s]);
        if (work.completeSeats[s]) {
            for (std::size_t r = 0; r < samples; ++r) result.totals[r][s] = result.counted[s];
            continue;
        }
        std::vector<double> shares(samples), lower(samples,result.counted[s]/prior.enrolment[s]);
        for (std::size_t r = 0; r < samples; ++r) shares[r] = prior.totals[r][s]/prior.enrolment[s];
        auto conditioned = condition(shares,lower);
        for (std::size_t r = 0; r < samples; ++r) result.totals[r][s] = conditioned[r]*prior.enrolment[s];
    }
}

// Most PPVC starting sizes follow matched historical public centres. EAV
// services are small assistance services, not ordinary public PPVCs. Replace
// unmatched starting sizes with the median of available matched EAV services
// in the same prior outcome, or the maintained 25-vote assumption if none exist.
void replaceUnmatchedEavSizes(std::vector<Unit> const& units, Matrix& expected) {
    for (auto& row : expected) {
        std::vector<double> references;
        for (std::size_t j = 0; j < units.size(); ++j)
            if (eav(units[j]) && units[j].matched && !units[j].closed) references.push_back(row[j]);
        std::sort(references.begin(),references.end());
        auto count = references.size();
        double replacement = count ? (references[(count-1)/2]+references[count/2])/2 : 25;
        for (std::size_t j = 0; j < units.size(); ++j)
            if (eav(units[j]) && !units[j].matched) row[j] = replacement;
    }
}

// Federal 2025 reliable non-EAV matches, excluding Brand's Rockingham reporting
// consolidation, gave 95% district-bootstrap slope endpoints nearest zero of
// -0.1443, -1.1731 and -0.6358. Magnitudes are rounded towards zero below; smaller
// centres' intervals included zero. These are exploratory log-odds slopes, not
// correlations or fractions of missing votes, and are not independently tested
// on another election. Selection uses discrete starting-size bands.
double ppvcCompensationCoefficient(double expectedCount) {
    return expectedCount >= 8000 ? .63 : expectedCount >= 4000 ? 1.17 : expectedCount >= 2000 ? .14 : 0;
}

// Only completed, reliably matched public centres supply compensation evidence.
// The reported/other expected-count ratio attenuates the slope when only a small
// part of the district's other PPVC vote is observed. A negative observed change
// therefore raises this centre's expectation, while a positive one lowers it.
void calculatePpvcCompensation(Prior const& prior, std::vector<Unit> const& units, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size();
    std::vector<double> expectedMeans(units.size());
    for (std::size_t j = 0; j < units.size(); ++j)
        for (std::size_t r = 0; r < samples; ++r) expectedMeans[j] += work.adjustedExpected[r][j]/samples;
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& unit = units[j];
        if (work.result.complete[j] || unit.kind != "ppvc" || !unit.matched || eav(unit) || unit.excluded) continue;
        double coefficient = ppvcCompensationCoefficient(expectedMeans[j]);
        double otherExpected = 0, reportedExpected = 0, reportedCount = 0;
        for (std::size_t k = 0; k < units.size(); ++k) {
            auto const& other = units[k];
            if (other.kind != "ppvc" || other.seat != unit.seat || other.group != unit.group) continue;
            if (k != j) otherExpected += expectedMeans[k];
            if (work.result.complete[k] && other.matched && !eav(other) && other.counted > 0 && !other.closed && !other.excluded) {
                reportedExpected += expectedMeans[k];
                reportedCount += other.counted;
            }
        }
        if (coefficient && reportedExpected > 0 && otherExpected > 0)
            work.result.compensation[j] = -coefficient*reportedExpected/otherExpected
                *(odds(reportedCount/prior.enrolment[unit.seat])-odds(reportedExpected/prior.enrolment[unit.seat]));
    }
}

// Apply size changes on count/enrolment log odds, then condition an unfinished
// unit's original count/formal-total share above its measured count. Retain this
// own-unit estimate before allocating any aggregate group allowance.
void conditionUnitAdditions(Prior const& prior, std::vector<Unit> const& units,
    double ppvcFactor, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size();
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& unit = units[j];
        double shift = work.result.compensation[j];
        if (unit.kind == "ppvc" && !work.result.complete[j] && !eav(unit)) shift += std::log(ppvcFactor);
        for (std::size_t r = 0; r < samples; ++r) {
            if (shift != 0) work.adjustedExpected[r][j] = prior.enrolment[unit.seat]
                *logistic(odds(work.adjustedExpected[r][j]/prior.enrolment[unit.seat])+shift);
            work.ownAdditions[r][j] = work.result.complete[j] ? 0 : work.adjustedExpected[r][j];
        }
        if (!work.result.complete[j] && unit.counted > 0) {
            std::vector<double> shares(samples), lower(samples);
            for (std::size_t r = 0; r < samples; ++r) {
                shares[r] = work.adjustedExpected[r][j]/prior.totals[r][unit.seat];
                lower[r] = unit.counted/work.result.totals[r][unit.seat];
            }
            auto conditioned = condition(shares,lower);
            for (std::size_t r = 0; r < samples; ++r)
                work.ownAdditions[r][j] = work.result.totals[r][unit.seat]*conditioned[r]-unit.counted;
        }
    }
}

// Historically combined groups retain their supported aggregate distribution.
// Booth-only groups instead use the unfinished booths' own sizes, so a small
// service cannot inherit all votes missing from a larger centre's expectation.
void conditionGroupAdditions(Prior const& prior, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), groups = prior.groups.size();
    for (std::size_t k = 0; k < work.unitsByGroup.size(); ++k) {
        if (!work.openGroups[k] || work.boothOnlyGroups[k]) continue;
        auto seat = k/groups;
        std::vector<double> shares(samples), lower(samples);
        for (std::size_t r = 0; r < samples; ++r) {
            shares[r] = prior.counts[r][k]/prior.totals[r][seat];
            lower[r] = work.groupCounted[k]/work.result.totals[r][seat];
        }
        auto conditioned = condition(shares,lower);
        for (std::size_t r = 0; r < samples; ++r)
            work.groupAdditions[r][k] = work.result.totals[r][seat]*conditioned[r]-work.groupCounted[k];
    }
}

// Share each district's remaining formal total between booth-only estimates
// and supported aggregate groups. Within a mixed group, reserve booth sizes
// first and divide the rest among its unfinished declarations. Counted votes
// are lower bounds throughout; completed units receive no estimated additions.
void allocateDistrictRemainders(Prior const& prior, std::vector<Unit> const& units, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), groups = prior.groups.size();
    for (std::size_t s = 0; s < prior.seats.size(); ++s) for (std::size_t r = 0; r < samples; ++r) {
        if (work.completeSeats[s]) continue;
        double originalBooths = 0, requestedBooths = 0, combinedGroups = 0;
        for (std::size_t k = s*groups; k < (s+1)*groups; ++k) {
            combinedGroups += work.groupAdditions[r][k];
            if (work.boothOnlyGroups[k]) for (auto j : work.unitsByGroup[k]) if (!work.result.complete[j]) {
                originalBooths += work.originalExpected[r][j];
                requestedBooths += work.ownAdditions[r][j];
            }
        }
        if (originalBooths+combinedGroups <= 0) throw std::runtime_error("Unfinished turnout district has no remaining supported units.");
        auto [boothBudget, aggregateBudget] = reserve(originalBooths,requestedBooths,combinedGroups,
            work.result.totals[r][s]-work.result.counted[s]);
        for (std::size_t k = s*groups; k < (s+1)*groups; ++k) {
            if (work.boothOnlyGroups[k]) {
                for (auto j : work.unitsByGroup[k]) if (!work.result.complete[j])
                    work.result.remaining[r][j] = requestedBooths > 0 ? work.ownAdditions[r][j]*boothBudget/requestedBooths : 0;
                continue;
            }
            if (!work.openGroups[k]) continue;
            double budget = combinedGroups > 0 ? aggregateBudget*work.groupAdditions[r][k]/combinedGroups : 0;
            double boothAdditions = 0, declarationAdditions = 0, originalGroupBooths = 0;
            for (auto j : work.unitsByGroup[k]) if (!work.result.complete[j]) {
                if (units[j].kind == "declaration") declarationAdditions += work.ownAdditions[r][j];
                else {
                    boothAdditions += work.ownAdditions[r][j];
                    originalGroupBooths += work.originalExpected[r][j];
                }
            }
            auto [groupBoothBudget,declarationBudget] = reserve(originalGroupBooths,boothAdditions,declarationAdditions,budget);
            for (auto j : work.unitsByGroup[k]) if (!work.result.complete[j])
                work.result.remaining[r][j] = units[j].kind == "declaration"
                    ? (declarationAdditions > 0 ? work.ownAdditions[r][j]*declarationBudget/declarationAdditions : 0)
                    : (boothAdditions > 0 ? work.ownAdditions[r][j]*groupBoothBudget/boothAdditions : 0);
        }
    }
}

// Assemble counts from the exact measured account plus allocated additions.
// Keep the pre-allocation own estimates for the later whole-group progress
// adjustment, avoiding a second set of marginal fits.
Broad assembleUpdatedCounts(Prior const& prior, std::vector<Unit> const& units, CountUpdateWorkspace& work) {
    auto groups = prior.groups.size();
    for (std::size_t j = 0; j < units.size(); ++j) for (std::size_t r = 0; r < prior.totals.size(); ++r) {
        work.result.unitCounts[r][j] = units[j].counted+work.result.remaining[r][j];
        work.result.counts[r][units[j].seat*groups+units[j].group] += work.result.unitCounts[r][j];
    }
    work.result.ownRemaining = std::move(work.ownAdditions);
    return std::move(work.result);
}
}

Broad update(Prior const& prior, std::vector<Unit> const& units, std::vector<bool> const& finalised, double ppvcFactor) {
    // Establish the counted account, update expectations from its lower bounds,
    // then allocate only the remaining voters. Reported ordinary/PPVC booths
    // normally need no additions; declarations can remain open for later batches.
    validate(prior);
    if (finalised.size() != prior.seats.size() || !(ppvcFactor > 0 && ppvcFactor <= 1))
        throw std::runtime_error("Invalid turnout snapshot options.");
    auto work = classifyCountUnits(prior,units,finalised);
    conditionDistrictTotals(prior,work);
    replaceUnmatchedEavSizes(units,work.adjustedExpected);
    calculatePpvcCompensation(prior,units,work);
    conditionUnitAdditions(prior,units,ppvcFactor,work);
    conditionGroupAdditions(prior,work);
    allocateDistrictRemainders(prior,units,work);
    return assembleUpdatedCounts(prior,units,work);
}

namespace {
using ProgressIdentity = std::pair<std::string, std::string>;
using ProgressHistory = std::map<double, Observation const*>;
struct ProgressRow {
    Evidence e;
    double current = 0, sharedActivity = 0, sharedVolume = 0;
    std::string state;
};
using ProgressRows = std::map<ProgressIdentity, ProgressRow>;

ProgressHistory orderProgressHistory(std::vector<Observation> const& history) {
    // A revised source time replaces its earlier record. All changes are then
    // measured in source order, independently of the order feeds were replayed.
    ProgressHistory ordered;
    for (auto const& observation : history) ordered[observation.hour] = &observation;
    return ordered;
}

ProgressRow measureLocalProgress(ProgressHistory const& ordered, ProgressIdentity const& key,
    double value, std::optional<double> deadline, bool scheduleAware, double eventScale) {
    // Usually declaration counts grow in batches, then become quiet. Measure
    // both changes and the observation coverage needed to interpret that quiet.
    // Rechecks still count as activity; they are not additional voters.
    ProgressRow row;
    row.current = value;
    double now = ordered.rbegin()->first;
    double activity = 0, coverage = 0, volume = 0, sharedActivity = 0, sharedVolume = 0;
    // A change equal to this tolerance supplies half an activity event.
    // Combine a 10-vote absolute scale with the supplied relative scale
    // (.1% ordinarily, 2% in the cautious timetable measurement) in quadrature.
    double smallSupport = 0, changedSupport = 0, tolerance = std::hypot(10.,eventScale*value);
    for (auto it = ordered.begin(); it != ordered.end(); ++it) {
        auto next = std::next(it);
        if (next == ordered.end()) break;
        auto old = it->second->counts.find(key), current = next->second->counts.find(key);
        if (old == it->second->counts.end() || current == next->second->counts.end()) continue;
        if (old->second < 0 || current->second < 0 || !std::isfinite(old->second) || !std::isfinite(current->second)) throw std::runtime_error("Invalid turnout history count.");
        double hours = scheduleAware ? countingHours(it->first,next->first) : next->first-it->first;
        // Activity and absolute vote-volume memory have a 24-hour decay
        // time. A large revision tends to one event rather than dominating
        // the event score in proportion to its vote count.
        double age = scheduleAware ? countingHours(next->first,now) : now-next->first;
        double decay = std::exp(-age/24);
        double delta = std::abs(current->second-old->second), ratio = delta/tolerance;
        activity += ratio*ratio/(1+ratio*ratio)*decay;
        volume += delta*decay;
        // A late backlog is strong evidence about this seat, but weaker
        // evidence that another seat still has routine counting to do.
        // Its shared influence is half at the receipt deadline, with a
        // 24-calendar-hour logistic transition around that date.
        double shared = scheduleAware && deadline ? logistic(-((next->first-*deadline)/24)) : 1;
        sharedActivity += ratio*ratio/(1+ratio*ratio)*decay*shared;
        sharedVolume += delta*decay*shared;
        // Observation coverage has a longer 48-hour memory. Integrate the
        // observed interval, then discount long gaps (half at 48 hours):
        // two distant snapshots do not establish repeated quiet counting.
        coverage += std::exp(-age/48)*(-std::expm1(-hours/48))/(1+(hours/48)*(hours/48));
        if (scheduleAware && deadline) {
            // Small positive batches after receipt closes support an end
            // to routine processing. Large batches and downward rechecks
            // provide less support, without declaring any batch final.
            // Deadline influence transitions over 12 calendar hours;
            // batch memory decays over 72 counting hours. The 50-vote
            // scale discounts tiny changes; 5% of current count is the
            // half-support scale for judging a positive batch as small.
            double post = logistic((it->first-*deadline)/12);
            double event = post*std::exp(-age/72)*delta*delta/(delta*delta+50*50);
            changedSupport += event;
            if (current->second > old->second)
                smallSupport += event/(1+std::pow(delta/(.05*(value+.5)),2));
        }
    }
    // Map accumulated events to a bounded score. Started is a smooth
    // count/(count+10) measure, not a binary reported/unreported flag.
    row.e.activity = -std::expm1(-activity);
    row.e.volume = volume;
    row.e.coverage = coverage;
    row.e.started = value/(value+10);
    row.e.smallBatchSupport = smallSupport/(1+changedSupport);
    row.sharedActivity = -std::expm1(-sharedActivity);
    row.sharedVolume = sharedVolume;
    return row;
}

ProgressRows measureAvailableProgress(Prior const& prior, std::vector<Unit> const& units,
    ProgressHistory const& ordered, std::optional<double> deadline, bool scheduleAware, double eventScale) {
    // Compare the same available declaration category across districts. An
    // unstarted open category supplies useful zero evidence; a known closure
    // supplies none, because that service was never expected to receive votes.
    std::set<ProgressIdentity> available;
    for (auto const& unit : units) if (unit.kind == "declaration" && !unit.closed)
        available.emplace(prior.seats[unit.seat], unit.category);
    ProgressRows rows;
    for (auto const& [key, value] : ordered.rbegin()->second->counts) {
        if (!available.count(key)) continue;
        if (value < 0 || !std::isfinite(value)) throw std::runtime_error("Invalid turnout progress count.");
        auto row = measureLocalProgress(ordered, key, value, deadline, scheduleAware, eventScale);
        auto seat = std::find(prior.seats.begin(), prior.seats.end(), key.first);
        if (seat != prior.seats.end()) row.state = prior.subdivisions[seat-prior.seats.begin()];
        rows[key] = row;
    }
    return rows;
}

// Pool typical activity across districts, then blend regional evidence where
// enough observations exist. A few exceptional districts must not determine
// the expected counting progress of every other district.
Evidence poolProgressRows(std::vector<ProgressRow const*> const& pool) {
    Evidence e;
    auto central = [&](auto measure) {
        // Districts have equal influence. Trim one extreme at each end
        // per ten observations, up to two, so a few anomalous categories
        // cannot define typical progress. Local evidence is never trimmed.
        std::vector<double> values;
        for (auto r : pool) values.push_back(measure(*r));
        std::sort(values.begin(),values.end());
        std::size_t trim = std::min<std::size_t>(2,values.size()/10);
        return std::accumulate(values.begin()+trim,values.end()-trim,0.)/(values.size()-2*trim);
    };
    e.pooledActivity = central([](ProgressRow const& r) { return r.sharedActivity; });
    e.pooledStarted = central([](ProgressRow const& r) { return r.e.started; });
    // Normalize volume within each district before averaging; the half-vote
    // equivalent keeps genuine open-service zeros numerically defined.
    e.pooledVolume = central([](ProgressRow const& r) { return r.sharedVolume/(r.current+.5); });
    return e;
}

std::vector<Evidence> attachProgressEvidence(Prior const& p, std::vector<Unit> const& units,
    ProgressRows const& rows, double now, std::optional<double> deadline) {
    // Each service receives its category's local evidence and the typical
    // progress elsewhere. The resulting strength is a response score: a larger
    // value means more reason to reduce usual remaining-vote expectations.
    std::vector<Evidence> result(units.size());
    std::map<std::pair<std::string,std::string>,double> currentUnits;
    for (auto const& u : units) if (u.kind == "declaration") currentUnits[{p.seats[u.seat],u.category}] += u.counted;
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& u = units[j]; auto key = std::make_pair(p.seats[u.seat],u.category);
        auto local = rows.find(key);
        if (u.kind != "declaration" || u.closed || local == rows.end()) continue;
        auto const& row = local->second;
        if (currentUnits.at(key) != row.current) throw std::runtime_error("Turnout history and current category counts differ.");
        std::vector<ProgressRow const*> national, regional;
        for (auto const& [k,v] : rows) if (k.second == key.second) { national.push_back(&v); if (v.state == row.state) regional.push_back(&v); }
        Evidence e = row.e, broad = poolProgressRows(national);
        // Twenty regional observations give the regional mean half the blend,
        // with national evidence supplying the rest. No subdivision means a
        // national-only measurement, not an inferred regional relationship.
        e.stateWeight = row.state.empty() ? 0 : double(regional.size())/(regional.size()+20);
        if (e.stateWeight) {
            auto state = poolProgressRows(regional);
            broad.pooledActivity += e.stateWeight*(state.pooledActivity-broad.pooledActivity);
            broad.pooledStarted += e.stateWeight*(state.pooledStarted-broad.pooledStarted);
            broad.pooledVolume += e.stateWeight*(state.pooledVolume-broad.pooledVolume);
        }
        e.pooledActivity = broad.pooledActivity;
        e.pooledStarted = broad.pooledStarted;
        e.pooledVolume = broad.pooledVolume;
        auto quiet = [](double value,double scale) { return 1/(1+(value/scale)*(value/scale)); };
        // Each quietness response is half at its scale. The .1 scores govern
        // activity and lack of shared starting support; .005 is recent volume
        // equal to .5% of current category counts. Shared quietness gives 75%
        // weight to event activity and 25% to relative volume. Their product
        // with local coverage/starting support is a response score, not a
        // calibrated probability that the category is finished.
        e.strength = e.coverage*e.started*quiet(e.activity,.1)*quiet(1-e.pooledStarted,.1)
            *(.75*quiet(e.pooledActivity,.1)+.25*quiet(e.pooledVolume,.005));
        if (u.category == "Postal" && deadline) {
            // Postal quietness supplies half its ordinary support at receipt
            // closure and changes smoothly on a 48-calendar-hour scale.
            e.postalReceiptSupport = logistic((now-*deadline)/48);
            e.strength *= e.postalReceiptSupport;
        }
        result[j] = e;
    }
    return result;
}
}

std::vector<Evidence> progress(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> deadline,
    bool scheduleAware, double eventScale) {
    // Use received history to measure local counting, compare it with other
    // districts and attach completion evidence. With no history, retain prior
    // expectations. Response scales are shared with the Python calibration.
    if (history.empty()) return std::vector<Evidence>(units.size());
    auto ordered = orderProgressHistory(history);
    auto rows = measureAvailableProgress(prior, units, ordered, deadline, scheduleAware, eventScale);
    return attachProgressEvidence(prior, units, rows, ordered.rbegin()->first, deadline);
}

// Expected booth sizes, rather than the number of reporting booths, determine
// how much independent evidence the completed ordinary/PPVC account supplies.
// Known closures contribute no voter-behaviour evidence. Match the EAV origin
// treatment before weighting, so an unmatched tiny service cannot dominate.
std::vector<double> boothCompletion(Prior const& p, std::vector<Unit> const& units) {
    std::vector<double> total(p.seats.size()), reported(p.seats.size());
    for (std::size_t r = 0; r < p.counts.size(); ++r) {
        std::vector<double> references;
        for (auto const& u : units) if (eav(u) && u.matched && !u.closed)
            references.push_back(p.counts[r][u.seat*p.groups.size()+u.group]*u.weight);
        std::sort(references.begin(),references.end()); auto z = references.size();
        double replacement = z ? (references[(z-1)/2]+references[z/2])/2 : 25;
        for (auto const& u : units) if (u.kind != "declaration" && !u.closed) {
            double expected = eav(u) && !u.matched ? replacement : p.counts[r][u.seat*p.groups.size()+u.group]*u.weight;
            total[u.seat] += expected/p.counts.size();
            if (u.counted > 0) reported[u.seat] += expected/p.counts.size();
        }
    }
    for (std::size_t s = 0; s < total.size(); ++s) total[s] = total[s] > 0 ? reported[s]/total[s] : 0;
    return total;
}

// Blend the earlier sensitive activity measurement with a more conservative
// two-percent measurement in probability space. This retains caution during
// pauses while recognising small late batches after most booths have reported.
std::vector<Evidence> scheduledEvidence(Prior const& p, std::vector<Unit> const& units,
    std::vector<Observation> const& history, double deadline, std::vector<double> const& completed) {
    auto original = progress(p,units,history,deadline,true,.001);
    auto result = progress(p,units,history,deadline,true,.02);
    auto quiet = [](double value,double scale) { return 1/(1+std::pow(value/scale,2)); };
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto& e = result[j];
        // A .3 activity score halves this more cautious quietness response.
        // Convert scores to influence with 1-exp(-(score/.3)^2), then average
        // the original and cautious influence weights.
        double shared = quiet(e.pooledActivity,.3);
        double strength = e.coverage*e.started*quiet(1-e.pooledStarted,.1)*quiet(e.activity,.3)
            *(.5+.5*shared)*e.postalReceiptSupport;
        double oldWeight = -std::expm1(-std::pow(original[j].strength/.3,2));
        double proposedWeight = -std::expm1(-std::pow(strength/.3,2))*shared;
        double weight = .5*(oldWeight+proposedWeight);
        // Small late batches can close at most 80% of the remaining influence
        // gap, scaled by expected-size-weighted booth completion squared and
        // local starting support. This adds evidence, not an extra vote draw.
        double extra = .8*e.smallBatchSupport*std::pow(completed[units[j].seat],2)*e.started;
        weight += (1-weight)*extra;
        e.strength = .3*std::sqrt(-std::log1p(-weight));
    }
    return result;
}

namespace {
std::vector<Unit> makeAllocationGroups(Prior const& p, std::vector<Unit> const& units) {
    // Most groups have one supported declaration category or a known combined
    // historical category. Measure their full observed count, including booths,
    // while retaining whether any service in the group was actually available.
    auto n = p.seats.size(), g = p.groups.size();
    std::vector<Unit> groups(n*g);
    for (std::size_t s = 0; s < n; ++s) for (std::size_t k = 0; k < g; ++k) {
        auto& group = groups[s*g+k];
        group.seat = s; group.group = k; group.kind = "declaration";
        group.category = p.groups[k] == "postal" ? "Postal" : "allocation:"+p.groups[k];
        group.name = group.category;
        group.closed = true;
    }
    // Aggregating services must retain their availability. An entirely closed
    // or absent group supplies no progress evidence; a group with an open
    // sibling still does. Preserve all measured counts in either case.
    for (auto const& unit : units) {
        auto& group = groups[unit.seat*g+unit.group];
        group.counted += unit.counted;
        if (!unit.closed) group.closed = false;
    }
    return groups;
}

bool measureAllocationWeights(Prior const& p, std::vector<Unit> const& units,
    std::vector<Evidence> const& evidence, std::vector<double> const& completed, Result& result) {
    // Ordinary progress retains the aggregate allowance. Slowing groups gradually
    // allow their unfinished declarations to approach their own size estimates.
    // Completed booths add support only for compatible single-category groups.
    auto const& broad = result.broad;
    auto m = p.totals.size(), g = p.groups.size();
    result.balancedRemainingMeans.assign(units.size(),0);
    result.allocationStrength.assign(units.size(),0);
    result.allocationWeight.assign(units.size(),0);
    bool active = false;
    for (std::size_t j = 0; j < units.size(); ++j) {
        for (auto const& row : broad.remaining) result.balancedRemainingMeans[j] += row[j]/m;
        if (units[j].kind != "declaration" || broad.complete[j]) continue;
        double strength = evidence[units[j].seat*g+units[j].group].strength;
        double weight = -std::expm1(-std::pow(strength/AllocationProgressScale,2));
        if (!completed.empty()) {
            // Completed booths can help release an old total allowance only
            // where the historical group supports one actual declaration
            // category. Mixed SA groups retain their existing evidence rule.
            auto const& unit = units[j]; bool compatible = true; double counted = 0, own = 0;
            for (std::size_t k = 0; k < units.size(); ++k) if (units[k].seat == unit.seat && units[k].group == unit.group) {
                compatible = compatible && units[k].kind == "declaration" && units[k].category == unit.category;
                counted += units[k].counted;
                for (auto const& row : broad.ownRemaining) own += row[k]/m;
            }
            double maturity = counted+own > 0 ? counted/(counted+own) : 0;
            double extra = compatible ? .5*std::pow(completed[unit.seat]*maturity,2) : 0;
            weight += (1-weight)*extra;
            strength = AllocationProgressScale*std::sqrt(-std::log1p(-weight));
        }
        result.allocationStrength[j] = strength;
        result.allocationWeight[j] = weight;
        active = active || result.allocationWeight[j] != 0;
    }
    return active;
}

void releaseDistrictAllowances(Prior const& p, std::vector<Unit> const& units, Result& result) {
    // Replace only the evidence-supported part of an imposed allowance. Keep
    // counted votes and booth estimates fixed, and let removed expected voters
    // become unused capacity instead of pushing them into another category.
    auto& broad = result.broad;
    auto m = p.totals.size(), n = p.seats.size();
    for (std::size_t s = 0; s < n; ++s) {
        std::vector<std::size_t> columns;
        bool localActive = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == s && units[j].kind == "declaration" && !broad.complete[j]) {
            columns.push_back(j); localActive = localActive || result.allocationWeight[j] != 0;
        }
        if (!localActive) continue;
        for (std::size_t r = 0; r < m; ++r) {
            double slack = p.enrolment[s]-broad.totals[r][s], capacity = slack, maximum = 0;
            if (!(slack > 0)) throw std::runtime_error("Allocation release needs positive unused enrolment.");
            std::vector<double> shifted;
            for (auto j : columns) {
                double base = broad.remaining[r][j], own = broad.ownRemaining[r][j];
                if (base < 0 || own < 0) throw std::runtime_error("Allocation release has a negative category estimate.");
                capacity += base;
                // Machine epsilon only represents subtraction roundoff. It
                // does not replace unknown counts or impose a vote-count floor.
                double oldOdds = std::log((base+std::numeric_limits<double>::epsilon())/slack);
                double ownOdds = std::log((own+std::numeric_limits<double>::epsilon())/slack);
                double difference = ownOdds-oldOdds;
                // A larger individual prior is weak evidence for reinstating
                // votes restrained by the aggregate account. Reduce its weight
                // smoothly while allowing substantial excess allowances to fall.
                double direction = logistic(-difference/AllocationDirectionScale);
                double value = oldOdds+difference*result.allocationWeight[j]*direction;
                shifted.push_back(value); maximum = std::max(maximum,value);
            }
            // Include unused enrolment as a competing share. Removed expected
            // votes can become unused capacity instead of being forced onto a
            // different category. Counted votes and booth estimates stay fixed.
            double denominator = std::exp(-maximum);
            for (double value : shifted) denominator += std::exp(value-maximum);
            for (std::size_t k = 0; k < columns.size(); ++k) {
                auto j = columns[k];
                broad.remaining[r][j] = capacity*std::exp(shifted[k]-maximum)/denominator;
                broad.unitCounts[r][j] = units[j].counted+broad.remaining[r][j];
            }
        }
    }
}

void rebuildReleasedTotals(Prior const& p, std::vector<Unit> const& units, Broad& broad) {
    // District and group accounts must reflect the same released unit counts.
    // Summing them again preserves the parent's accounting for the late model.
    auto m = p.totals.size(), n = p.seats.size(), g = p.groups.size();
    broad.counts = zeros(m,n*g);
    broad.totals = zeros(m,n);
    for (std::size_t r = 0; r < m; ++r) {
        for (std::size_t j = 0; j < units.size(); ++j) broad.counts[r][units[j].seat*g+units[j].group] += broad.unitCounts[r][j];
        for (std::size_t s = 0; s < n; ++s)
            broad.totals[r][s] = std::accumulate(broad.counts[r].begin()+s*g,broad.counts[r].begin()+(s+1)*g,0.);
    }
}
}

void releaseAllocation(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> deadline, Result& result,
    std::vector<double> const& completed = {}) {
    // Usually historical group allowances remain useful while counting is
    // active. As whole-group counting settles, release excessive allocations
    // before preparing the separate category-level finishing and batch outcomes.
    auto groups = makeAllocationGroups(prior, units);
    auto evidence = progress(prior, groups, history, deadline);
    if (!measureAllocationWeights(prior, units, evidence, completed, result)) return;
    releaseDistrictAllowances(prior, units, result);
    rebuildReleasedTotals(prior, units, result.broad);
}

namespace {
// Resolve the counting clock from the latest received source, never wall time.
// Routine processing has a 48-counting-hour allowance and 12-hour transition;
// exceptional-batch probability decays over 14 counting days after that phase.
bool applyReceiptTimetable(std::vector<Observation> const& history, Options const& options, Result& result) {
    bool scheduled = options.receiptDeadline && !history.empty()
        && std::accumulate(result.broad.counted.begin(),result.broad.counted.end(),0.) > 0;
    if (scheduled) {
        double now = std::max_element(history.begin(),history.end(),[](auto const& a,auto const& b) { return a.hour < b.hour; })->hour;
        result.countingHoursAfterDeadline = countingHours(*options.receiptDeadline,now);
        double age = 12*softplus((result.countingHoursAfterDeadline-48)/12);
        result.processingPhase = logistic((result.countingHoursAfterDeadline-48)/12);
        result.batchSurvival = std::exp(-age/(14*24));
    }
    return scheduled;
}

// Retain the previous estimate and the two continuous addition distributions
// explicitly for every unfinished declaration. This lets party preparation
// evaluate rare batches deliberately, even if no diagnostic draw selects one.
void buildLateCountComponents(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, bool scheduled, Result& result) {
    auto samples = prior.totals.size(), count = samples*options.countDraws;
    auto const& broad = result.broad;
    // All units reuse this small grid of normal quantiles. Its midpoint
    // probabilities stay inside (0,1) and bound conditional evaluation cost.
    std::vector<double> normalQuantiles;
    for (std::size_t k = 0; k < options.countDraws; ++k)
        normalQuantiles.push_back(inverseLogCdf(std::log((k+.5)/options.countDraws)));
    for (std::size_t j = 0; j < units.size(); ++j) {
        if (units[j].kind != "declaration" || broad.complete[j]) continue;
        Component component;
        component.unit = j;
        component.weight = -std::expm1(-std::pow(result.evidence[j].strength/.3,2));
        if (scheduled) {
            double currentCategory = 0;
            for (auto const& unit : units)
                if (unit.seat == units[j].seat && unit.kind == "declaration" && unit.category == units[j].category)
                    currentCategory += unit.counted;
            // Recent volume/current count supplies up to 24 extra processing
            // hours (half at 2%) and retains half the routine amount at 5%.
            // The half-vote denominator keeps a zero current count well defined.
            double relative = result.evidence[j].volume/(currentCategory+.5);
            double delay = 24*relative*relative/(relative*relative+.02*.02);
            double routine = logistic(-(result.countingHoursAfterDeadline-48-delay)/12);
            double active = relative*relative/(relative*relative+.05*.05);
            component.usualLogShift = std::log(routine+(1-routine)*active);
        }
        for (std::size_t r = 0; r < samples; ++r) {
            double base = broad.remaining[r][j];
            double slack = prior.enrolment[units[j].seat]-broad.totals[r][units[j].seat];
            if (base < 0 || slack <= 0) throw std::runtime_error("Invalid remaining turnout capacity.");
            component.capacity.push_back(slack);
            component.reference.push_back(base);
            // Epsilon represents subtraction roundoff, not a substantive floor.
            // Small additions have a .25-vote median on the addition/slack odds
            // scale and log-odds SD 1. Batches use an added 5% of counted votes
            // and SD 1.5. After the deadline, their centre moves from the group
            // allowance towards the unit's own pre-allocation expectation.
            component.referenceOdds.push_back(std::log((base+std::numeric_limits<double>::epsilon())/slack)+component.usualLogShift);
            component.smallOdds.push_back(std::log(.25/slack));
            double oldBatch = std::log((base+.25+.05*units[j].counted)/slack);
            double lateBatch = std::log((broad.ownRemaining[r][j]+.25+.05*units[j].counted)/slack);
            component.batchOdds.push_back((1-result.processingPhase)*oldBatch+result.processingPhase*lateBatch);
            for (std::size_t k = 0; k < options.countDraws; ++k) {
                double quantile = normalQuantiles[k];
                component.smallMean += (slack+base)*logistic(component.smallOdds.back()+quantile)/count;
                component.batchMean += (slack+base)*logistic(component.batchOdds.back()+1.5*quantile)/count;
            }
        }
        result.components.push_back(std::move(component));
    }
}

struct LateCountLayout {
    std::vector<std::vector<std::size_t>> componentsBySeat;
    std::vector<bool> activeSeats;
};

// Index the few open declaration components once, keeping neutral districts on
// their broad reference and grouping the others for finite-parent allocation.
LateCountLayout indexLateComponents(Result const& result, std::vector<Unit> const& units) {
    LateCountLayout layout;
    layout.componentsBySeat.resize(result.broad.counted.size());
    layout.activeSeats.resize(result.broad.counted.size());
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto seat = units[result.components[k].unit].seat;
        layout.componentsBySeat[seat].push_back(k);
        layout.activeSeats[seat] = layout.activeSeats[seat] || result.components[k].weight != 0
            || result.components[k].usualLogShift != 0 || result.processingPhase != 0;
    }
    return layout;
}

// Give a district an explicit counted-only outcome. The counted-weighted
// progress response is weakened by outstanding estimates, the postal receipt
// window and retained batch probability. The .95 factor preserves residual
// uncertainty unless its configured units are actually complete.
void calculateNoAdditionProbabilities(std::vector<Unit> const& units, LateCountLayout const& layout, Result& result) {
    auto const& broad = result.broad;
    auto samples = broad.totals.size();
    result.noAdditionProbability.assign(broad.counted.size(),0);
    for (std::size_t s = 0; s < broad.counted.size(); ++s) {
        bool allComplete = true, hasUnits = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == s) {
            hasUnits = true;
            allComplete = allComplete && broad.complete[j];
        }
        if (hasUnits && allComplete) {
            result.noAdditionProbability[s] = 1;
            continue;
        }
        double counted = 0, support = 0, nonbatch = 1, receipt = 1, remaining = 0;
        for (auto k : layout.componentsBySeat[s]) {
            auto const& component = result.components[k];
            auto const& unit = units[component.unit];
            counted += unit.counted;
            support += unit.counted*component.weight;
            nonbatch *= 1-.02*component.weight;
            if (unit.category == "Postal") receipt = std::min(receipt,result.evidence[component.unit].postalReceiptSupport);
        }
        if (!counted) continue;
        for (auto const& row : broad.totals) remaining += (row[s]-broad.counted[s])/samples;
        double coverage = counted/(counted+remaining);
        result.noAdditionProbability[s] = .95*support/counted*coverage*coverage*nonbatch*receipt;
    }
}

// These are unconditional weights for one category: previous + small + batch
// = 1 - its district's no-addition probability. Preserve a separately fading
// exceptional chance (starting at .02), while compressing it smoothly when the
// available positive probability is small. Never transfer it into routine mean
// votes simply because the counted-only outcome has become likely.
void assignComponentProbabilities(std::vector<Unit> const& units, Result& result) {
    for (auto& component : result.components) {
        double zero = result.noAdditionProbability[units[component.unit].seat];
        double positive = 1-zero, requested = .02*result.batchSurvival;
        double possible = positive > 0 ? positive*(-std::expm1(-requested/positive)) : 0;
        component.batchProbability = (1-result.processingPhase)*.02*component.weight+result.processingPhase*possible;
        double smallShare = .98*component.weight/(1-.02*component.weight);
        double remaining = positive-component.batchProbability;
        component.previousProbability = (1-smallShare)*remaining;
        component.smallProbability = smallShare*remaining;
    }
}

// Produce conditional positive-branch log weights for the diagnostic sample.
// Divide unconditional probabilities by the positive-branch probability here,
// not when storing them. Explicit quantiles are only a cross-language comparison
// input; normal preparation uses the unchanged deterministic per-unit streams.
Matrix sampleLateComponentOdds(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, Result const& result, Matrix const& suppliedUniforms) {
    auto count = prior.totals.size()*options.countDraws, components = result.components.size();
    if (!suppliedUniforms.empty()) {
        if (suppliedUniforms.size() != count) throw std::runtime_error("Invalid supplied turnout quantile count.");
        for (auto const& row : suppliedUniforms) {
            if (row.size() != components) throw std::runtime_error("Invalid supplied turnout quantile columns.");
            for (double value : row) if (!(value > 0 && value < 1)) throw std::runtime_error("Turnout quantiles must be interior.");
        }
    }
    Matrix varied = zeros(count,components);
    for (std::size_t k = 0; k < components; ++k) {
        auto const& component = result.components[k];
        auto const& unit = units[component.unit];
        double positive = 1-result.noAdditionProbability[unit.seat];
        auto quantiles = uniforms(prior.election+"/"+prior.seats[unit.seat]+"/"+unit.name,count,options.seed);
        for (std::size_t r = 0; r < count; ++r) {
            auto outer = r/options.countDraws;
            varied[r][k] = mixture(component.referenceOdds[outer],component.smallOdds[outer],component.batchOdds[outer],
                component.previousProbability/positive,component.smallProbability/positive,component.batchProbability/positive,
                suppliedUniforms.empty() ? quantiles[r] : suppliedUniforms[r][k]);
        }
    }
    return varied;
}

// Apply sampled category additions jointly inside unused enrolment plus their
// existing allowance, retaining a slack component for non-voters/informal votes.
// Store the conditional positive-branch totals; unit means then mix back the
// district's exact counted-only outcome to provide unconditional count targets.
void assembleCountSamples(Prior const& prior, std::vector<Unit> const& units, Options const& options,
    LateCountLayout const& layout, Matrix const& varied, Result& result) {
    auto samples = prior.totals.size(), seats = prior.seats.size(), groups = prior.groups.size();
    auto count = samples*options.countDraws;
    auto const& broad = result.broad;
    result.unitMeans.assign(units.size(),0);
    for (std::size_t j = 0; j < units.size(); ++j)
        for (std::size_t r = 0; r < samples; ++r) result.unitMeans[j] += broad.unitCounts[r][j]/samples;
    result.totals = zeros(count,seats);
    result.counts = zeros(count,seats*groups);
    for (std::size_t r = 0; r < count; ++r) {
        auto outer = r/options.countDraws;
        result.counts[r] = broad.counts[outer];
        result.totals[r] = broad.totals[outer];
        for (std::size_t s = 0; s < seats; ++s) {
            if (!layout.activeSeats[s]) continue;
            auto const& local = layout.componentsBySeat[s];
            double parent = prior.enrolment[s]-broad.totals[outer][s], maximum = 0;
            for (auto k : local) {
                parent += result.components[k].reference[outer];
                maximum = std::max(maximum,varied[r][k]);
            }
            double denominator = std::exp(-maximum);
            for (auto k : local) denominator += std::exp(varied[r][k]-maximum);
            for (auto k : local) {
                auto j = result.components[k].unit;
                double addition = parent*std::exp(varied[r][k]-maximum)/denominator;
                result.counts[r][s*groups+units[j].group] += addition-result.components[k].reference[outer];
                result.unitMeans[j] += (addition-result.components[k].reference[outer])/count;
            }
            result.totals[r][s] = std::accumulate(result.counts[r].begin()+s*groups,result.counts[r].begin()+(s+1)*groups,0.);
        }
    }
    for (std::size_t j = 0; j < units.size(); ++j)
        result.unitMeans[j] = units[j].counted+(1-result.noAdditionProbability[units[j].seat])*(result.unitMeans[j]-units[j].counted);
}
}

Result prepare(Prior const& prior, std::vector<Unit> const& units, std::vector<bool> const& finalised,
    std::vector<Observation> const& history, Options const& options, Matrix const& suppliedUniforms) {
    if (!options.countDraws) throw std::runtime_error("Turnout preparation needs at least one cheap count draw.");
    Result result;
    result.broad = update(prior,units,finalised,options.ppvcFactor);
    auto completed = options.receiptDeadline ? boothCompletion(prior,units) : std::vector<double>{};
    result.evidence = options.receiptDeadline ? scheduledEvidence(prior,units,history,*options.receiptDeadline,completed)
        : progress(prior,units,history,options.postalDeadline);
    releaseAllocation(prior,units,history,options.receiptDeadline ? options.receiptDeadline : options.postalDeadline,result,completed);

    bool scheduled = applyReceiptTimetable(history,options,result);
    buildLateCountComponents(prior,units,options,scheduled,result);
    auto layout = indexLateComponents(result,units);
    calculateNoAdditionProbabilities(units,layout,result);
    assignComponentProbabilities(units,result);
    auto sampledOdds = sampleLateComponentOdds(prior,units,options,result,suppliedUniforms);
    assembleCountSamples(prior,units,options,layout,sampledOdds,result);
    return result;
}

DrawPlan makeDrawPlan(Result const& result, std::vector<Unit> const& units,
    std::vector<double> const& enrolment) {
    DrawPlan plan; plan.enrolment = enrolment;
    plan.componentsBySeat.resize(enrolment.size()); plan.active.resize(enrolment.size());
    for (auto const& u : units) { plan.counted.push_back(u.counted); plan.seats.push_back(u.seat); }
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto s = units.at(result.components[k].unit).seat;
        plan.componentsBySeat.at(s).push_back(k);
        plan.active[s] = plan.active[s] || result.components[k].weight != 0
            || result.components[k].usualLogShift != 0 || result.processingPhase != 0;
    }
    if (result.broad.unitCounts.empty() || result.broad.unitCounts.front().size() != units.size()
        || result.noAdditionProbability.size() != enrolment.size())
        throw std::runtime_error("Invalid turnout draw plan.");
    return plan;
}

namespace {
double interiorDrawUniform(unsigned long long& key) {
    // A random position strictly inside 0..1 keeps logarithms finite even in
    // the far tails. Midpoints of a 52-bit grid cannot reach either endpoint.
    return (double(step(key) >> 12) + .5) / 4503599627370496.;
}

std::vector<bool> drawNoAdditionSeats(Result const& result, DrawPlan const& plan,
    unsigned long long seed) {
    // Finishing is a seat-wide outcome: every unfinished service gets exactly
    // its current count together. Seat-specific random keys keep other seats'
    // draws unchanged when this seat finishes or gains another reporting unit.
    std::vector<bool> zero(plan.enrolment.size());
    for (std::size_t s = 0; s < zero.size(); ++s) {
        auto seatKey = seed ^ ((s+1)*0x9e3779b97f4a7c15ULL);
        zero[s] = interiorDrawUniform(seatKey) < result.noAdditionProbability[s];
    }
    return zero;
}

void drawSeatDeclarationAdditions(Result const& result, DrawPlan const& plan,
    std::size_t s, std::size_t outer, unsigned long long seed, std::vector<double>& counts) {
    // In the positive-count outcome, draw each category from its previous,
    // small-addition or exceptional-batch distribution. Their finite shared
    // parent prevents new votes from exceeding the number of enrolled electors.
    auto const& local = plan.componentsBySeat[s];
    std::vector<double> odds; odds.reserve(local.size());
    double parent = plan.enrolment[s]-result.broad.totals[outer][s], maximum = 0;
    for (auto k : local) {
        auto const& c = result.components[k];
        auto unitKey = seed ^ ((c.unit+1)*0xd1b54a32d192ed03ULL);
        double selector = interiorDrawUniform(unitKey)*(1-result.noAdditionProbability[s]);
        double value = c.referenceOdds[outer];
        if (selector >= c.previousProbability) {
            // Direct mixture sampling avoids solving an inverse mixture
            // CDF in every iteration, while preserving the same component
            // distributions used by the offline count calibration.
            double z = std::sqrt(-2*std::log(interiorDrawUniform(unitKey)))
                * std::cos(6.2831853071795864769*interiorDrawUniform(unitKey));
            bool small = selector < c.previousProbability+c.smallProbability;
            value = small ? c.smallOdds[outer]+z : c.batchOdds[outer]+1.5*z;
        }
        odds.push_back(value); maximum = std::max(maximum,value);
        parent += c.reference[outer];
    }
    // All unfinished categories share a finite parent and leave a slack
    // component for non-voters/informal votes. Joint log weights prevent
    // endpoint caps and preserve the enrolment account in a large batch.
    double denominator = std::exp(-maximum);
    for (double value : odds) denominator += std::exp(value-maximum);
    for (std::size_t k = 0; k < local.size(); ++k) {
        auto j = result.components[local[k]].unit;
        counts[j] = plan.counted[j]+parent*std::exp(odds[k]-maximum)/denominator;
    }
}

void drawOpenDeclarationSeats(Result const& result, DrawPlan const& plan, std::size_t outer,
    unsigned long long seed, std::vector<bool> const& zero, std::vector<double>& counts) {
    // Most seats retain their fixed prior row until progress changes their
    // remaining-vote distribution. Only active positive outcomes need sampling.
    for (std::size_t s = 0; s < zero.size(); ++s)
        if (!zero[s] && plan.active[s]) drawSeatDeclarationAdditions(result, plan, s, outer, seed, counts);
}

void retainCountedOnlySeats(DrawPlan const& plan, std::vector<bool> const& zero, std::vector<double>& counts) {
    // A no-addition outcome removes estimates of new voters, while preserving
    // every already counted vote, including those in completed booths.
    for (std::size_t j = 0; j < counts.size(); ++j)
        if (zero[plan.seats[j]]) counts[j] = plan.counted[j];
}
}

std::vector<double> drawUnitCounts(Result const& result, DrawPlan const& plan,
    unsigned long long seed) {
    // One shared prior row retains the learnt relationships between districts
    // and vote categories. Separate local events then represent finishing or
    // further counting; expensive distribution preparation has already run.
    auto state = seed;
    auto outer = step(state) % result.broad.unitCounts.size();
    auto counts = result.broad.unitCounts[outer];
    auto zero = drawNoAdditionSeats(result, plan, seed);
    drawOpenDeclarationSeats(result, plan, outer, seed, zero, counts);
    retainCountedOnlySeats(plan, zero, counts);
    return counts;
}

std::vector<double> meanTotals(Result const& result) {
    // Consumers must include the exact counted branch when reporting means;
    // the stored sample matrix intentionally retains positive outcomes only.
    std::vector<double> means(result.broad.counted.size());
    for (auto const& row : result.totals) for (std::size_t s = 0; s < row.size(); ++s) means[s] += row[s]/result.totals.size();
    for (std::size_t s = 0; s < means.size(); ++s)
        means[s] = result.broad.counted[s]+(1-result.noAdditionProbability[s])*(means[s]-result.broad.counted[s]);
    return means;
}

double reportingFactor(double hours) {
    // Public PPVC counts normally arrive by the day after polling. For an
    // unreported centre, interpolate a declining odds multiplier after 48 hours,
    // retaining a small possible size rather than declaring the service closed.
    // These delay points/factors are maintained assumptions shared with Python.
    constexpr double times[] = {48,54,78,102}, factors[] = {1,.25,.1,.02};
    if (hours <= times[0]) return factors[0];
    for (int k = 1; k < 4; ++k) if (hours <= times[k]) {
        double weight = (hours-times[k-1])/(times[k]-times[k-1]);
        return std::exp((1-weight)*std::log(factors[k-1])+weight*std::log(factors[k]));
    }
    return factors[3];
}
double countingHours(double before, double after) {
    if (!std::isfinite(before) || !std::isfinite(after)) throw std::runtime_error("Invalid turnout counting clock.");
    if (after < before) return -countingHours(after,before);
    double total = 0;
    while (before < after) {
        double day = std::floor(before/24), boundary = std::min(after,(day+1)*24);
        // Unix day zero was Thursday. Sunday contributes twelve equivalent
        // hours, with no jump in accumulated time when midnight is crossed.
        int weekday = (static_cast<int>(day)%7+11)%7;
        total += (boundary-before)*(weekday == 0 ? .5 : 1);
        before = boundary;
    }
    return total;
}
double sourceHour(std::string const& stamp) {
    // Use the feed's local source clock. Zoned or incomplete strings
    // require an explicit convention rather than silently mixing clock bases.
    if (stamp.size() < 19 || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != 'T' || stamp[13] != ':' || stamp[16] != ':')
        throw std::runtime_error("Unsupported turnout source timestamp: "+stamp);
    auto number = [&](std::size_t start,std::size_t length) {
        int value = 0;
        auto first = stamp.data()+start, last = first+length;
        auto parsed = std::from_chars(first,last,value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) throw std::runtime_error("Invalid turnout source timestamp.");
        return value;
    };
    int y = number(0,4), mo = number(5,2), d = number(8,2), h = number(11,2), mi = number(14,2), s = number(17,2);
    auto date = Date::fromYmd(y,mo,d);
    if (!date || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) throw std::runtime_error("Invalid turnout source timestamp.");
    // Some sources publish fractions of a second. Keep that precision while
    // rejecting offsets: model deadlines use the same explicit local clock.
    double fraction = 0, place = .1;
    if (stamp.size() > 19) {
        if (stamp[19] != '.' || stamp.size() == 20) throw std::runtime_error("Invalid turnout source timestamp.");
        for (std::size_t i = 20; i < stamp.size(); ++i) {
            if (stamp[i] < '0' || stamp[i] > '9') throw std::runtime_error("Invalid turnout source timestamp.");
            fraction += (stamp[i]-'0') * place; place *= .1;
        }
    }
    // Reuse the repository's calendar arithmetic. Avoid local timezone APIs:
    // the Python replay measures differences on the feed's own local clock.
    return double(date->modifiedJulianDay()-40587)*24+h+mi/60.+(s+fraction)/3600.;
}
}
