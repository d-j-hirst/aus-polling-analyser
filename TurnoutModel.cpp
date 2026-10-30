#include "TurnoutModel.h"
#include "DeclarationAllocation.h"
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
// no-addition outcome has been removed. Small SD is 1; the prepared batch SD
// is 1.5 on addition/unused-capacity log odds; extra postal uncertainty is
// applied only to final allocated quantities, outside this shared mixture.
// Numerical search brackets and
// 40 bisections limit calculation time without capping possible vote counts.
double mixture(double ref, double small, double batch, double previous, double smallProbability, double batchProbability, double uniform, double batchSd) {
    auto continuous = [&](double x) { return smallProbability*cdf(x-small)+batchProbability*cdf((x-batch)/batchSd); };
    double below = continuous(ref), above = below+previous;
    if (uniform >= below && uniform <= above) return ref;
    double target = uniform < below ? uniform : uniform-previous;
    double low = std::min(small-12, batch-12*batchSd), high = std::max(small+12, batch+12*batchSd);
    for (int k = 0; k < 40; ++k) {
        double middle = (low+high)/2;
        if (continuous(middle) < target) low = middle; else high = middle;
    }
    return (low+high)/2;
}
}

double postalRemainderLogOdds(std::optional<double> daysBeforeDeadline) {
    // This describes the small remainder only when main counting is largely
    // finished. Comparisons of mature, slowing counts in Federal 2022/2025
    // and Queensland 2024 motivate original medians of 6%, 4%, 2%. The broader
    // spread lowers the median to preserve expected additions. These are
    // starting assumptions, not a jurisdiction fit or a deadline completion
    // rule (see docs/live-turnout.md). Interpolate on a bounded share scale;
    // continue the early slope before day four and halve remainder odds every
    // two calendar days afterwards. Processing backlogs keep separate paths.
    if (!daysBeforeDeadline) return odds(.06)+PostalRemainderMedianShift;
    double days = *daysBeforeDeadline;
    if (days < 0) return odds(.02)+days*std::log(2.)/2+PostalRemainderMedianShift;
    if (days < 2) return odds(.02)+days*(odds(.04)-odds(.02))/2+PostalRemainderMedianShift;
    return odds(.04)+(days-2)*(odds(.06)-odds(.04))/2+PostalRemainderMedianShift;
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
    Matrix adjustedExpected, ownAdditions, groupAdditions;
    std::vector<std::vector<std::size_t>> unitsByGroup;
    std::vector<double> groupCounted;
    std::vector<bool> hasBooths, openGroups, completeSeats;
};

// Validate measured units, classify their completion and assemble group lookup
// tables before any estimates change. Reported ordinary/PPVC counts normally
// finish the physical account; explicitly incremental places and declarations
// remain lower bounds unless closed/final. This protects mixed early/day places
// whose first reported primary batch does not include all voters at that venue.
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
    work.ownAdditions = zeros(samples,units.size());
    work.groupAdditions = zeros(samples,seats*groups);
    work.unitsByGroup.resize(seats*groups);
    work.groupCounted.resize(seats*groups);
    work.hasBooths.resize(seats*groups);
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
            (unit.kind != "declaration" && !unit.incremental && unit.counted > 0) || finalised[unit.seat]);
        work.hasBooths[group] = work.hasBooths[group] || unit.kind != "declaration";
        work.openGroups[group] = work.openGroups[group] || !result.complete[j];
        for (std::size_t r = 0; r < samples; ++r)
            work.adjustedExpected[r][j] = prior.counts[r][group]*unit.weight;
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
// above that lower bound. This is a conditioning parent, not a required final
// budget: the returned total is rebuilt from service accounts. An empty
// configuration does not establish completion.
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
// unit's original count/formal-total share above its measured count. This
// retains the established conditioning and correlations in the prior. The
// resulting service size is not a weight for filling a district shortfall.
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

// Declaration-only groups retain their aggregate distribution and internal
// split. Groups containing physical booths instead use each service's own size:
// historical grouping does not mean a missing booth voter is a provisional or
// telephone voter. Keep the original joint draws, including synthetic SA sizes,
// rather than treating these finer estimates as known or independent counts.
void conditionGroupAdditions(Prior const& prior, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), groups = prior.groups.size();
    for (std::size_t k = 0; k < work.unitsByGroup.size(); ++k) {
        if (!work.openGroups[k] || work.hasBooths[k]) continue;
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

// Allocate within supported declaration-only groups; otherwise retain each
// unfinished service's count-conditioned size. The district total is an output
// of these accounts, not a budget that forces a shortfall in completed booths
// onto unrelated unfinished services. Each prior row keeps its joint uncertainty.
void assembleServiceAdditions(Prior const& prior, std::vector<Unit> const& units, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), groups = prior.groups.size();
    for (std::size_t k = 0; k < work.unitsByGroup.size(); ++k) {
        if (work.completeSeats[k/groups] || !work.openGroups[k]) continue;
        for (std::size_t r = 0; r < samples; ++r) {
            double own = 0;
            for (auto j : work.unitsByGroup[k]) own += work.ownAdditions[r][j];
            for (auto j : work.unitsByGroup[k])
                work.result.remaining[r][j] = work.hasBooths[k] ? work.ownAdditions[r][j]
                    : (own > 0 ? work.ownAdditions[r][j]*work.groupAdditions[r][k]/own : 0);
        }
    }
    // Reject an unsupported configuration, while allowing genuine completed
    // accounts. Counted votes are never removed to make a projection fit.
    for (std::size_t s = 0; s < prior.seats.size(); ++s) if (!work.completeSeats[s]) {
        bool supported = false;
        for (std::size_t k = s*groups; k < (s+1)*groups; ++k)
            for (auto j : work.unitsByGroup[k]) supported = supported || (!work.result.complete[j] && units[j].weight > 0);
        if (!supported) throw std::runtime_error("Unfinished turnout district has no remaining supported units.");
    }
}

// Separate lower-bound fits can oversubscribe enrolment. This numerical guard
// reserves 1% of the still-uncounted electorate as unused capacity for the late
// count model; normally it does nothing. Unlike the old formal-total ceiling,
// it permits both higher and lower turnout. Never reduce a measured vote.
void constrainAdditionsToEnrolment(Prior const& prior, CountUpdateWorkspace& work) {
    for (std::size_t s = 0; s < prior.seats.size(); ++s) for (std::size_t r = 0; r < prior.totals.size(); ++r) {
        double requested = 0;
        for (std::size_t k = s*prior.groups.size(); k < (s+1)*prior.groups.size(); ++k)
            for (auto j : work.unitsByGroup[k]) requested += work.result.remaining[r][j];
        double capacity = (prior.enrolment[s]-work.result.counted[s])*(1-UncountedEnrolmentHeadroom);
        if (requested <= capacity) continue;
        for (std::size_t k = s*prior.groups.size(); k < (s+1)*prior.groups.size(); ++k)
            for (auto j : work.unitsByGroup[k]) work.result.remaining[r][j] *= capacity/requested;
    }
}

// Use actual voting categories, not historical combined groups: ordinary
// booths may share an input group with provisional/telephone services without
// those services becoming interchangeable. Splitting one physical category
// into more booths must not give it more transfer capacity or more weight.
std::string transferCategory(Unit const& unit) {
    return unit.kind == "declaration" ? "service/"+unit.category
        : unit.kind+"/"+std::to_string(unit.group);
}
struct TransferCategory {
    std::vector<std::size_t> units;
    double spread = TransferCategorySpreadFloor;
};
double spreadAroundMean(std::vector<double> const& values) {
    double centre = mean(values), variance = 0;
    for (double value : values) variance += (value-centre)*(value-centre)/values.size();
    return std::sqrt(variance);
}
std::vector<TransferCategory> transferCategories(Prior const& prior,
    std::vector<Unit> const& units, CountUpdateWorkspace const& work, std::size_t seat) {
    std::map<std::string,TransferCategory> grouped;
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& unit = units[j];
        if (unit.seat == seat && !work.result.complete[j] && unit.transferExclusionReason.empty())
            grouped[transferCategory(unit)].units.push_back(j);
    }
    std::vector<TransferCategory> result;
    for (auto& entry : grouped) {
        auto& category = entry.second;
        std::vector<double> shares;
        for (std::size_t r = 0; r < prior.totals.size(); ++r) {
            double original = 0;
            for (auto j : category.units)
                original += prior.counts[r][seat*prior.groups.size()+units[j].group]*units[j].weight;
            if (original > 0 && original < prior.enrolment[seat]) shares.push_back(odds(original/prior.enrolment[seat]));
        }
        // A genuinely absent size baseline cannot supply a transfer estimate.
        // Ordinary unmatched/replaced booths with positive starting sizes can.
        if (shares.size() != prior.totals.size()) continue;
        // Broad own-size uncertainty is not unlimited evidence for transfer.
        // Combine its precision with a common proportional-transfer restraint;
        // otherwise very uncertain small services absorb implausibly large
        // residuals while better-known, much larger categories hardly move.
        // This regularises only the response, never the original size draws.
        double rawSpread = std::max(TransferCategorySpreadFloor,spreadAroundMean(shares));
        category.spread = std::max(TransferCategorySpreadFloor,1/std::sqrt(
            1/(rawSpread*rawSpread)+1/(TransferPlausibilitySpread*TransferPlausibilitySpread)));
        result.push_back(std::move(category));
    }
    return result;
}

// Compare two coherent district accounts. Transfer restores the conditioned
// expected total, moving ONLY unfinished, eligible category additions. Turnout
// change retains their own sizes. A shared log-odds displacement allocates the
// gap once, with more movement allowed in categories whose sizes are uncertain.
// As remaining capacity dwindles, the required displacement grows: a completed
// category cannot absorb a shortfall, even if its original total was very large.
void chooseTurnoutExplanation(Prior const& prior, std::vector<Unit> const& units, CountUpdateWorkspace& work) {
    auto samples = prior.totals.size(), seats = prior.seats.size();
    auto& result = work.result;
    result.transferProbability = zeros(samples,seats);
    result.transferSelected = zeros(samples,seats);
    for (std::size_t s = 0; s < seats; ++s) {
        if (work.completeSeats[s] || result.counted[s] == 0) continue;
        auto categories = transferCategories(prior,units,work,s);
        if (categories.empty()) continue;
        std::vector<double> logTotals;
        for (auto const& row : prior.totals) logTotals.push_back(std::log(row[s]));
        double turnoutSpread = std::max(TurnoutChangeSpreadFloor,spreadAroundMean(logTotals));
        // Fixed, independently permuted midpoint quantiles avoid resampling
        // noise on repeated snapshots. This is a mixture across preparation
        // outcomes, not an average of two vote accounts within every outcome.
        auto choices = uniforms(prior.election+"/"+prior.seats[s]+"/turnout-transfer",samples,20261025);
        for (std::size_t r = 0; r < samples; ++r) {
            std::vector<double> additions(categories.size()), transformed(categories.size());
            double ownTotal = result.counted[s], eligible = 0;
            for (std::size_t j = 0; j < units.size(); ++j)
                if (units[j].seat == s) ownTotal += result.remaining[r][j];
            for (std::size_t c = 0; c < categories.size(); ++c) {
                for (auto j : categories[c].units) additions[c] += result.remaining[r][j];
                eligible += additions[c];
            }
            double fixed = ownTotal-eligible;
            double parent = prior.enrolment[s]-fixed;
            double target = result.totals[r][s]-fixed;
            if (!(eligible > 0 && parent > eligible && target > 0 && target < parent)) continue;
            if (std::abs(target-eligible) < 1e-8) continue;
            for (std::size_t c = 0; c < categories.size(); ++c)
                if (additions[c] > 0) transformed[c] = odds(additions[c]/parent);
            auto proposed = [&](std::size_t c, double shift) {
                return additions[c] > 0 ? parent*logistic(transformed[c]+shift*categories[c].spread*categories[c].spread) : 0.;
            };
            double low = -1024, high = 1024;
            for (int iteration = 0; iteration < 56; ++iteration) {
                double middle = (low+high)/2, total = 0;
                for (std::size_t c = 0; c < categories.size(); ++c) total += proposed(c,middle);
                if (total < target) low = middle; else high = middle;
            }
            double shift = (low+high)/2, transferCost = 0;
            for (std::size_t c = 0; c < categories.size(); ++c)
                transferCost += additions[c]/eligible*std::pow(shift*categories[c].spread,2);
            double turnoutError = std::log(ownTotal/result.totals[r][s])/turnoutSpread;
            // Initial regularisation, not fitted coefficients: 3:1 transfer
            // odds for a negligible discrepancy; Gaussian transfer cost and
            // a Student-t(4) turnout cost. The heavier turnout tail ensures
            // extreme required category changes favour an overall size change.
            double logOdds = std::log(TransferPriorOdds)-.5*transferCost
                +.5*(TurnoutChangeDegreesFreedom+1)*std::log1p(turnoutError*turnoutError/TurnoutChangeDegreesFreedom);
            double probability = logistic(logOdds);
            result.transferProbability[r][s] = probability;
            if (choices[r] >= probability) continue;
            result.transferSelected[r][s] = 1;
            for (std::size_t c = 0; c < categories.size(); ++c) if (additions[c] > 0)
                for (auto j : categories[c].units) result.remaining[r][j] *= proposed(c,shift)/additions[c];
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
    for (std::size_t r = 0; r < prior.totals.size(); ++r)
        for (std::size_t s = 0; s < prior.seats.size(); ++s)
            work.result.totals[r][s] = std::accumulate(work.result.counts[r].begin()+s*groups,
                work.result.counts[r].begin()+(s+1)*groups,0.);
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
    assembleServiceAdditions(prior,units,work);
    constrainAdditionsToEnrolment(prior,work);
    chooseTurnoutExplanation(prior,units,work);
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
    double value, std::optional<ResolvedTimetable> timetable, bool scheduleAware, double eventScale) {
    // Usually declaration counts grow in batches, then become quiet. Measure
    // both changes and the observation coverage needed to interpret that quiet.
    // Rechecks still count as activity; they are not additional voters.
    ProgressRow row;
    row.current = value;
    double now = ordered.rbegin()->first;
    double activity = 0, coverage = 0, volume = 0, sharedActivity = 0, sharedVolume = 0;
    double gentleActivity = 0, gentleTolerance = std::hypot(10.,DeclarationLocalEventScale*value);
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
        // Measure the gentler LOCAL response in the same history traversal.
        // It never enters sharedActivity: activity elsewhere keeps its existing
        // meaning, even when this seat's small revisions carry less weight.
        double gentleRatio = delta/gentleTolerance;
        gentleActivity += gentleRatio*gentleRatio/(1+gentleRatio*gentleRatio)*decay;
        volume += delta*decay;
        // Late activity remains local evidence, but gradually loses influence
        // elsewhere after this CATEGORY's usual processing period. Postal
        // receipt says nothing about absent/provisional processing. Without a
        // supported timetable, retain the full shared activity measurement.
        double shared = timetable ? logistic(-(next->first-timetable->referenceHour)
            /timetable->sharedTransitionHours) : 1;
        sharedActivity += ratio*ratio/(1+ratio*ratio)*decay*shared;
        sharedVolume += delta*decay*shared;
        // Observation coverage has a longer 48-hour memory. Integrate the
        // observed interval, then discount long gaps (half at 48 hours):
        // two distant snapshots do not establish repeated quiet counting.
        coverage += std::exp(-age/48)*(-std::expm1(-hours/48))/(1+(hours/48)*(hours/48));
        if (timetable) {
            // Small positive batches late in the category's own processing
            // period support diminishing additions. Large batches and downward
            // rechecks provide less support, without declaring a batch final.
            // Batch memory decays over 72 counting hours. The 50-vote
            // scale discounts tiny changes; 5% of current count is the
            // half-support scale for judging a positive batch as small.
            double post = logistic((it->first-timetable->referenceHour)/timetable->smallBatchTransitionHours);
            double event = post*std::exp(-age/72)*delta*delta/(delta*delta+50*50);
            changedSupport += event;
            if (current->second > old->second)
                smallSupport += event/(1+std::pow(delta/(.05*(value+.5)),2));
        }
    }
    // Map accumulated events to a bounded score. Started is a smooth
    // count/(count+10) measure, not a binary reported/unreported flag.
    row.e.activity = -std::expm1(-activity);
    row.e.gentleLocalActivity = -std::expm1(-gentleActivity);
    row.e.volume = volume;
    row.e.coverage = coverage;
    row.e.started = value/(value+10);
    row.e.smallBatchSupport = smallSupport/(1+changedSupport);
    row.sharedActivity = -std::expm1(-sharedActivity);
    row.sharedVolume = sharedVolume;
    return row;
}

ProgressRows measureAvailableProgress(Prior const& prior, std::vector<Unit> const& units,
    ProgressHistory const& ordered, bool scheduleAware, double eventScale,
    std::map<std::string, ResolvedTimetable> const& timetables) {
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
        auto found = timetables.find(key.second);
        auto timetable = found == timetables.end() ? std::optional<ResolvedTimetable>{} : found->second;
        auto row = measureLocalProgress(ordered, key, value, timetable, scheduleAware, eventScale);
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
            e.postalDaysBeforeDeadline = (*deadline-now)/24;
            e.strength *= e.postalReceiptSupport;
        }
        result[j] = e;
    }
    return result;
}
}

std::optional<ResolvedTimetable> processingTimetable(Options const& options, std::string const& category) {
    // Normally use the previous comparable election's processing progression.
    // Calendar offsets remain portable between elections without confusing the
    // date a ballot may arrive with the date officials are likely to count it.
    // Preserve unconfigured postal parameters; explicit Postal settings refer
    // to counting progression and never move the ballot receipt safeguard.
    auto found = options.processingTimetables.find(category);
    if (category == "Postal" && found == options.processingTimetables.end()) {
        if (!options.receiptDeadline) return {};
        return ResolvedTimetable{*options.receiptDeadline,48,12,24,12,true};
    }
    if (found == options.processingTimetables.end() || !found->second) return {};
    auto const& timing = *found->second;
    if (!std::isfinite(timing.daysAfterPollClose) || timing.daysAfterPollClose < 0
        || !std::isfinite(timing.transitionDays) || timing.transitionDays <= 0)
        throw std::runtime_error("Invalid category processing timetable.");
    if (!options.pollClose) return {};
    double width = 24*timing.transitionDays;
    return ResolvedTimetable{*options.pollClose+24*timing.daysAfterPollClose,0,width,width,width,false,category == "Postal"};
}

std::vector<Evidence> progress(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> deadline,
    bool scheduleAware, double eventScale, std::map<std::string, ResolvedTimetable> const& timetables) {
    // Use received history to measure local counting, compare it with other
    // districts and attach completion evidence. With no history, retain prior
    // expectations. Response scales are shared with the Python calibration.
    if (history.empty()) return std::vector<Evidence>(units.size());
    auto ordered = orderProgressHistory(history);
    auto rows = measureAvailableProgress(prior, units, ordered, scheduleAware, eventScale,timetables);
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
            // For a mixed place, a first batch is only partial progress toward
            // the starting quantity. Ordinary physical booths retain their
            // normal whole-booth progress once reported.
            if (u.counted > 0) reported[u.seat] += (u.incremental
                ? std::min(expected,u.counted) : expected)/p.counts.size();
        }
    }
    for (std::size_t s = 0; s < total.size(); ++s) total[s] = total[s] > 0 ? reported[s]/total[s] : 0;
    return total;
}

// Blend the earlier sensitive activity measurement with a more conservative
// two-percent measurement in probability space. This retains caution during
// pauses while recognising small late batches after most booths have reported.
std::vector<Evidence> scheduledEvidence(Prior const& p, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> postalDeadline,
    std::vector<double> const& completed, std::map<std::string, ResolvedTimetable> const& timetables) {
    auto original = progress(p,units,history,postalDeadline,true,.001,timetables);
    auto result = progress(p,units,history,postalDeadline,true,.02,timetables);
    std::optional<double> now;
    for (auto const& row : history) if (!now || row.hour > *now) now = row.hour;
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
        // This extra support permits a modest POSITIVE remainder, not a new
        // completion/zero outcome. Only local event sensitivity changes; pooled
        // activity, starting evidence and the late-small-batch support stay fixed.
        double gentleStrength = e.coverage*e.started*quiet(1-e.pooledStarted,.1)
            *quiet(e.gentleLocalActivity,.3)*(.5+.5*shared)*e.postalReceiptSupport;
        double gentleWeight = -std::expm1(-std::pow(gentleStrength/.3,2))*shared;
        e.positiveRemainderSupport = gentleWeight+(1-gentleWeight)*extra;
        // Receipt can remain open when very few votes remain. Only this positive
        // postal remainder support omits the receipt discount; ordinary progress
        // and the district counted-only outcome keep their existing safeguards.
        double postalStrength = e.coverage*e.started*quiet(1-e.pooledStarted,.1)
            *quiet(e.gentleLocalActivity,.3)*(.5+.5*shared);
        double postalWeight = -std::expm1(-std::pow(postalStrength/.3,2))*shared;
        e.postalFinishingSupport = postalWeight+(1-postalWeight)*extra;
        auto timing = timetables.find(units[j].category);
        if (now && timing != timetables.end() && timing->second.separatePostalProcessing) {
            // Early receipt closure can leave an ordinary processing backlog.
            // Use the explicit processing clock to moderate quietness evidence,
            // rather than treating every pause as a tiny finishing tail. Keep
            // the small-tail amount and receipt guard tied to the receipt date;
            // original-size completion evidence is evaluated separately later.
            double maturity = logistic((*now-timing->second.referenceHour)/timing->second.transitionHours);
            e.strength = .3*std::sqrt(-std::log1p(-weight*maturity));
            e.postalFinishingSupport *= maturity;
        }
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

// Mixed groups already retain each service's own estimate. Exclude them from
// aggregate-allowance release, including its shared-capacity normalization, so
// a change in another declaration cannot quietly inflate these services again.
std::set<std::pair<std::size_t,std::size_t>> groupsWithBooths(std::vector<Unit> const& units) {
    std::set<std::pair<std::size_t,std::size_t>> groups;
    for (auto const& unit : units) if (unit.kind != "declaration") groups.emplace(unit.seat,unit.group);
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
    auto directGroups = groupsWithBooths(units);
    bool active = false;
    for (std::size_t j = 0; j < units.size(); ++j) {
        for (auto const& row : broad.remaining) result.balancedRemainingMeans[j] += row[j]/m;
        if (units[j].kind != "declaration" || broad.complete[j] || directGroups.count({units[j].seat,units[j].group})) continue;
        double strength = evidence[units[j].seat*g+units[j].group].strength;
        double weight = -std::expm1(-std::pow(strength/AllocationProgressScale,2));
        if (!completed.empty()) {
            // Completed booths can help release an old total allowance only
            // where the historical group supports one actual declaration
            // category. Mixed groups already use their own sizes.
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
    auto directGroups = groupsWithBooths(units);
    for (std::size_t s = 0; s < n; ++s) {
        std::vector<std::size_t> columns;
        bool localActive = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == s && units[j].kind == "declaration" && !broad.complete[j]
            && !directGroups.count({units[j].seat,units[j].group})) {
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
// Resolve each available declaration's clock once per snapshot. Unknown dates
// or unsupported categories have no time-based suppression; their observed
// counting activity and original-size evidence still apply normally.
void prepareProcessingTimetables(std::vector<Unit> const& units, Options const& options, Result& result) {
    for (auto const& unit : units) if (unit.kind == "declaration") {
        auto timing = processingTimetable(options,unit.category);
        if (timing) result.processingTimetables[unit.category] = *timing;
    }
}

void applyProcessingTimetable(ResolvedTimetable const& timing, double now,
    double relativeActivity, Component& component) {
    // A quiet category retains half its routine allowance at its configured
    // transition centre. Recent changes can delay that reduction and retain
    // more of the allowance. Neither this phase nor a quiet period closes a
    // category: the exceptional-batch tail fades over fourteen further days.
    double elapsed = timing.countingClock ? countingHours(timing.referenceHour,now) : now-timing.referenceHour;
    double offset = elapsed-timing.allowanceHours;
    double age = timing.transitionHours*softplus(offset/timing.transitionHours);
    component.processingPhase = logistic(offset/timing.transitionHours);
    component.batchSurvival = std::exp(-age/(14*24));
    double relativeSquared = relativeActivity*relativeActivity;
    double delay = 24*relativeSquared/(relativeSquared+.02*.02);
    double routine = logistic(-(offset-delay)/timing.transitionHours);
    double active = relativeSquared/(relativeSquared+.05*.05);
    component.usualLogShift = std::log(routine+(1-routine)*active);
}

void measureLocalCompletion(Prior const& prior, Unit const& unit,
    Component& component) {
    // A count near its originally expected size may be the last substantial
    // batch even when it has only just arrived. Far above that expectation,
    // an undersized prior is a competing explanation: do not turn every
    // overtaken prior outcome into evidence that counting has finished.
    // Only this service's frozen size and current count enter the new score.
    if (unit.counted <= 0 || unit.weight <= 0) return;
    std::vector<double> transformed;
    double population = 0, parent = prior.enrolment[unit.seat]+2*CompletionCountEquivalent;
    for (auto const& row : prior.counts) {
        double count = row[unit.seat*prior.groups.size()+unit.group]*unit.weight;
        population += count;
        transformed.push_back(odds((count+CompletionCountEquivalent)/parent));
    }
    if (population <= 0) return; // Structural absence supplies no completion evidence.
    double centre = mean(transformed), variance = 0;
    for (double value : transformed) variance += (value-centre)*(value-centre)/transformed.size();
    double sd = std::sqrt(variance);
    if (sd <= 1e-8) return; // A fixed size is not a calibrated uncertainty distribution.
    double z = (odds((unit.counted+CompletionCountEquivalent)/parent)-centre)/sd;
    component.originalSizeZ = z;
    component.completionWeight = cdf(z)/(1+std::pow(std::max(0.,z)/CompletionSurpriseScale,4));
    // For postals this means the main count may be largely finished, not that
    // receipt has ended. Its selected outcomes retain a positive postal tail;
    // the separate quietness and exact-zero paths keep their deadline guards.
}

void measurePositiveRemainder(Unit const& unit, Evidence const& evidence, Component& component) {
    // Ordinary early-voting centres are booths, not PrePoll declarations.
    // Restrict this rule to the two declaration services covered by the trial;
    // postals retain their receipt-based remainder and other services are unchanged.
    if ((unit.category != "Absent" && unit.category != "PrePoll") || unit.counted <= 0
        || component.weight >= 1) return;
    // If ordinary quietness supports 50% and gentler local quietness supports
    // 90%, divert 80% of the remaining routine outcomes. Exclude those already
    // near-finished from original-size evidence, retaining their existing draws.
    double extra = std::max(0.,evidence.positiveRemainderSupport-component.weight)/(1-component.weight);
    component.positiveRemainderWeight = (1-component.completionWeight)*extra;
    // Recent volume is absolute processing/rechecking movement with a 24-hour
    // counting-clock decay, not envelope arrivals. Average local volume and
    // robust pooled relative volume scaled to this seat's current count. Two
    // daily quantities and equal local/shared weighting are modelling choices.
    // The quarter-vote minimum keeps negligible-volume outcomes positive.
    component.positiveRemainderMedian = std::max(.25,DeclarationRemainderDays*.5
        *(evidence.volume+evidence.pooledVolume*(unit.counted+.5)));
}

// Widen uncertainty only after every category has received its allocation.
// Routine point draws use the full routine spread. For continuous draws, the
// normal densities distinguish tiny additions from batch-sized possibilities:
// the batch share rises smoothly with the evidence, without a vote threshold.
// This definition works with both inverse-mixture diagnostics and direct draws.
double postalWideningWeight(Component const& c, std::size_t outer, double value) {
    if (value == c.referenceOdds[outer]) return 1;
    if (c.batchProbability <= 0) return 0;
    if (c.smallProbability <= 0) return 1;
    double smallZ = value-c.smallOdds[outer];
    double batchZ = (value-c.batchOdds[outer])/c.batchSd;
    double logRatio = std::log(c.batchProbability)-std::log(c.smallProbability)-std::log(c.batchSd)
        -.5*batchZ*batchZ+.5*smallZ*smallZ;
    return logistic(logRatio);
}

// A postal output draw is widened only within itself plus reserved unused
// enrolment. Move its addition/capacity log odds down/up by a local normal
// magnitude. Choose the larger possibility with probability (original-lower) /
// (upper-lower): the pair's mean is exactly the original allocated count.
// For example 60 and 180 around 100 give the larger amount probability 1/3.
// Averaging over magnitudes retains that mean, including at physical bounds.
// This avoids fitting an offset to a small sample which could still bias the
// much larger main simulation. Finishing paths get no additional spread.
double widenAllocatedPostal(double addition, double capacity, double sd,
    double normal, double choice) {
    if (sd <= 0 || addition <= 0 || addition >= capacity) return addition;
    double location = std::log(addition/(capacity-addition));
    double magnitude = sd*std::abs(normal);
    double lower = std::min(addition,capacity*logistic(location-magnitude));
    double upper = std::max(addition,capacity*logistic(location+magnitude));
    if (upper <= lower) return addition; // Numerical equality, not a vote threshold.
    double probability = std::clamp((addition-lower)/(upper-lower),0.,1.);
    return choice < probability ? upper : lower;
}

// Original-size evidence and gentle quietness concern remaining quantity, not
// legal receipt completion. Three quarters of previously unused gentle support
// enters the finishing fraction, then finishing/substantial odds are multiplied
// by three. Separately, a young count far below its original expectation is more
// likely paused than nearly finished. Retain exp(-shortfall_SD^2/2) of finishing
// support initially, fading that restraint smoothly around seven calendar days.
// At/above expectation, or without an explicit polling date, apply no restraint.
void measureFinishingBalance(Unit const& unit, Evidence const& evidence,
    std::optional<double> days, Component& component) {
    if (unit.category == "Postal" && unit.counted > 0) {
        double extra = component.weight < 1
            ? std::max(0.,evidence.postalFinishingSupport-component.weight)/(1-component.weight) : 0;
        double fraction = component.completionWeight+.75*(1-component.completionWeight)*extra;
        component.completionWeight = PostalFinishingOdds*fraction/(1+(PostalFinishingOdds-1)*fraction);
    }
    if (days && component.originalSizeZ) {
        double shortfall = std::max(0.,-*component.originalSizeZ);
        double early = 1/(1+std::pow(std::max(0.,*days)/7,6));
        component.earlyCompletionRetention = 1-early*(-std::expm1(-shortfall*shortfall/2));
    }
}

// Retain the previous estimate and the two continuous addition distributions
// explicitly for every unfinished declaration. This lets party preparation
// evaluate rare batches deliberately, even if no diagnostic draw selects one.
void buildLateCountComponents(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, std::optional<double> sourceHour, Result& result) {
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
        measureLocalCompletion(prior,units[j],component);
        if (units[j].category == "Postal") {
            component.postalCounted = units[j].counted;
            component.postalRemainderLogOdds = postalRemainderLogOdds(result.evidence[j].postalDaysBeforeDeadline);
        }
        component.weight = -std::expm1(-std::pow(result.evidence[j].strength/.3,2));
        measureFinishingBalance(units[j],result.evidence[j],result.daysAfterPollClose,component);
        measurePositiveRemainder(units[j],result.evidence[j],component);
        auto timing = result.processingTimetables.find(units[j].category);
        if (sourceHour && timing != result.processingTimetables.end()) {
            double currentCategory = 0;
            for (auto const& unit : units)
                if (unit.seat == units[j].seat && unit.kind == "declaration" && unit.category == units[j].category)
                    currentCategory += unit.counted;
            // Recent volume/current count supplies up to 24 extra processing
            // hours (half at 2%) and retains half the routine amount at 5%.
            // The half-vote denominator keeps a zero current count well defined.
            double relative = result.evidence[j].volume/(currentCategory+.5);
            applyProcessingTimetable(timing->second,*sourceHour,relative,component);
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
            // and SD 1.5. Late in this category's timetable, their centre moves from the group
            // allowance towards the unit's own pre-allocation expectation.
            component.referenceOdds.push_back(std::log((base+std::numeric_limits<double>::epsilon())/slack)+component.usualLogShift);
            component.smallOdds.push_back(std::log(.25/slack));
            double oldBatch = std::log((base+.25+.05*units[j].counted)/slack);
            double lateBatch = std::log((broad.ownRemaining[r][j]+.25+.05*units[j].counted)/slack);
            component.batchOdds.push_back((1-component.processingPhase)*oldBatch+component.processingPhase*lateBatch);
        }
        // Preserve original mixture odds for every category. Started postals are
        // widened later, after joint allocation and all finishing limits.
        component.postalWidening = options.postalWidening
            && units[j].category == "Postal" && units[j].counted > 0;
        for (std::size_t r = 0; r < samples; ++r) {
            double parent = component.capacity[r]+component.reference[r];
            for (std::size_t k = 0; k < options.countDraws; ++k) {
                double quantile = normalQuantiles[k];
                component.smallMean += parent*logistic(component.smallOdds[r]+quantile)/count;
                component.batchMean += parent*logistic(component.batchOdds[r]+component.batchSd*quantile)/count;
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
            || result.components[k].usualLogShift != 0 || result.components[k].processingPhase != 0
            || result.components[k].completionWeight != 0 || result.components[k].positiveRemainderWeight != 0;
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
        double positive = 1-zero, requested = .02*component.batchSurvival;
        double possible = positive > 0 ? positive*(-std::expm1(-requested/positive)) : 0;
        component.batchProbability = (1-component.processingPhase)*.02*component.weight+component.processingPhase*possible;
        double smallShare = .98*component.weight/(1-.02*component.weight);
        double remaining = positive-component.batchProbability;
        component.previousProbability = (1-smallShare)*remaining;
        component.smallProbability = smallShare*remaining;
        // Move rejected finishing support back into substantial additions,
        // including the older tiny-addition path. Keep batch probability and
        // the district counted-only outcome unchanged. Completion fractions
        // must be rescaled because their routine parent has just grown.
        double previous = component.previousProbability;
        double retain = component.earlyCompletionRetention;
        component.previousProbability += component.smallProbability*(1-retain);
        component.smallProbability *= retain;
        double scale = component.previousProbability > 0 ? previous*retain/component.previousProbability : 0;
        component.completionWeight *= scale;
        component.positiveRemainderWeight *= scale;
    }
}

// Produce conditional positive-branch log weights for the diagnostic sample.
// Divide unconditional probabilities by the positive-branch probability here,
// not when storing them. Explicit quantiles are only a cross-language comparison
// input; normal preparation uses the unchanged deterministic per-unit streams.
struct LateSamples {
    Matrix odds;
    Matrix completionLimits; // Infinity normally; a near-finished remainder for selected routine outcomes.
    std::vector<std::vector<bool>> sizeFinishing;
    Matrix allocated; // Original post-allocation additions, before postal output uncertainty.
    Matrix unusedBeforePostal; // Fixed shape parent; only the final physical bound uses the postal draw.
};

double localCompletionLimit(Component const& component, std::size_t outer, double normal, double sharedNormal) {
    if (component.postalRemainderLogOdds && component.postalCounted > 0) {
        // If q of final postals remain, additions = counted*q/(1-q). Work on
        // log odds to keep q bounded. One election-wide variation represents
        // shared return/processing delays; the other varies by seat. Existing
        // unfinished and exceptional-batch paths cover more substantial updates.
        double logAddition = std::log(component.postalCounted)+*component.postalRemainderLogOdds
            +PostalRemainderLocalSd*normal+PostalRemainderSharedSd*sharedNormal;
        // Bound the sampled remainder to available capacity for safe numerical
        // evaluation. Joint allocation subsequently respects this postal amount
        // before allowing it to displace other declaration categories.
        return std::exp(std::min(logAddition,std::log(component.capacity[outer]+component.reference[outer])));
    }
    return (component.capacity[outer]+component.reference[outer])*logistic(component.smallOdds[outer]+normal);
}

double positiveRemainderLimit(Component const& component, std::size_t outer, double normal, double sharedNormal) {
    // For current count C and median remainder M, the odds of remaining/final
    // votes are M/C. Varying their natural log gives additions M*exp(variation).
    // A wide local spread retains substantial additions; the shared part prevents
    // that uncertainty averaging away across seats. Bound only for safe numeric
    // evaluation here; the original allocated addition is the actual upper limit.
    double localSd = std::sqrt(DeclarationRemainderSd*DeclarationRemainderSd
        -DeclarationRemainderSharedSd*DeclarationRemainderSharedSd);
    double logAddition = std::log(component.positiveRemainderMedian)
        +localSd*normal+DeclarationRemainderSharedSd*sharedNormal;
    return std::exp(std::min(logAddition,std::log(component.capacity[outer]+component.reference[outer])));
}

LateSamples sampleLateComponentOdds(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, Result const& result, Matrix const& suppliedUniforms) {
    auto count = prior.totals.size()*options.countDraws, components = result.components.size();
    if (!suppliedUniforms.empty()) {
        if (suppliedUniforms.size() != count) throw std::runtime_error("Invalid supplied turnout quantile count.");
        for (auto const& row : suppliedUniforms) {
            if (row.size() != components) throw std::runtime_error("Invalid supplied turnout quantile columns.");
            for (double value : row) if (!(value > 0 && value < 1)) throw std::runtime_error("Turnout quantiles must be interior.");
        }
    }
    LateSamples sampled;
    sampled.odds = zeros(count,components);
    sampled.completionLimits = Matrix(count,std::vector<double>(components,std::numeric_limits<double>::infinity()));
    sampled.sizeFinishing = std::vector<std::vector<bool>>(count,std::vector<bool>(components));
    // One shared draw per joint outcome, independent of unit ordering, current
    // counts and the local branch selector. Other categories keep their streams.
    auto sharedNormals = uniforms("postal-remainder/shared",count,options.seed);
    for (auto& value : sharedNormals) value = inverseLogCdf(std::log(value));
    // Independent of postal uncertainty; retain the trial's keyed stream.
    auto remainderNormals = uniforms("postal-remainder/shared",count,options.seed ^ 0x71651ULL);
    for (auto& value : remainderNormals) value = inverseLogCdf(std::log(value));
    for (std::size_t k = 0; k < components; ++k) {
        auto const& component = result.components[k];
        auto const& unit = units[component.unit];
        double positive = 1-result.noAdditionProbability[unit.seat];
        auto quantiles = uniforms(prior.election+"/"+prior.seats[unit.seat]+"/"+unit.name,count,options.seed);
        for (std::size_t r = 0; r < count; ++r) {
            auto outer = r/options.countDraws;
            double uniform = suppliedUniforms.empty() ? quantiles[r] : suppliedUniforms[r][k];
            sampled.odds[r][k] = mixture(component.referenceOdds[outer],component.smallOdds[outer],component.batchOdds[outer],
                component.previousProbability/positive,component.smallProbability/positive,component.batchProbability/positive,
                uniform,component.batchSd);
            // The inverse-CDF grid assigns a probability interval to the
            // routine point mass. Use only a fraction of that interval for
            // completion; its rescaled position supplies a continuous local
            // remainder draw. Postal limits take part in allocation itself;
            // other categories' local limits release unused turnout afterwards.
            double mass = component.previousProbability/positive*component.completionWeight;
            double extraMass = component.previousProbability/positive*component.positiveRemainderWeight;
            if (mass <= 0 && extraMass <= 0) continue;
            double below = (component.smallProbability*cdf(component.referenceOdds[outer]-component.smallOdds[outer])
                +component.batchProbability*cdf((component.referenceOdds[outer]-component.batchOdds[outer])/component.batchSd))/positive;
            if (uniform > below && uniform < below+mass) {
                sampled.sizeFinishing[r][k] = true;
                sampled.completionLimits[r][k] = localCompletionLimit(component,outer,
                    inverseLogCdf(std::log((uniform-below)/mass)),sharedNormals[r]);
            } else if (extraMass > 0 && uniform > below+mass && uniform < below+mass+extraMass)
                sampled.completionLimits[r][k] = positiveRemainderLimit(component,outer,
                    inverseLogCdf(std::log((uniform-below-mass)/extraMass)),remainderNormals[r]);
        }
    }
    return sampled;
}

// Apply sampled category additions jointly inside unused enrolment plus their
// existing allowance, retaining a slack component for non-voters/informal votes.
// Store the conditional positive-branch totals; unit means then mix back the
// district's exact counted-only outcome to provide unconditional count targets.
void assembleCountSamples(Prior const& prior, std::vector<Unit> const& units, Options const& options,
    LateCountLayout const& layout, LateSamples& sampled, Result& result) {
    auto samples = prior.totals.size(), seats = prior.seats.size(), groups = prior.groups.size();
    auto count = samples*options.countDraws;
    auto const& broad = result.broad;
    auto const& varied = sampled.odds;
    sampled.allocated = zeros(count,result.components.size());
    auto& allocated = sampled.allocated;
    result.unitMeans.assign(units.size(),0);
    for (std::size_t j = 0; j < units.size(); ++j)
        for (std::size_t r = 0; r < samples; ++r) result.unitMeans[j] += broad.unitCounts[r][j]/samples;
    result.totals = zeros(count,seats);
    result.counts = zeros(count,seats*groups);
    for (std::size_t r = 0; r < count; ++r) {
        auto outer = r/options.countDraws;
        for (std::size_t k = 0; k < result.components.size(); ++k)
            allocated[r][k] = result.components[k].reference[outer];
        result.counts[r] = broad.counts[outer];
        result.totals[r] = broad.totals[outer];
        for (std::size_t s = 0; s < seats; ++s) {
            if (!layout.activeSeats[s]) continue;
            auto const& local = layout.componentsBySeat[s];
            double parent = prior.enrolment[s]-broad.totals[outer][s];
            std::vector<double> localOdds, postalLimits;
            localOdds.reserve(local.size()); postalLimits.reserve(local.size());
            for (auto k : local) {
                parent += result.components[k].reference[outer];
                localOdds.push_back(varied[r][k]);
                postalLimits.push_back(result.components[k].postalRemainderLogOdds
                    ? sampled.completionLimits[r][k] : std::numeric_limits<double>::infinity());
            }
            auto additions = allocateDeclarationAdditions(parent,localOdds,postalLimits);
            for (std::size_t position = 0; position < local.size(); ++position) {
                auto k = local[position];
                auto j = result.components[k].unit;
                double addition = additions[position];
                // Postal finishing was already resolved in the joint account.
                // Other categories retain their local-only finishing treatment;
                // their reductions become unused turnout, not sibling additions.
                addition = std::min(addition,sampled.completionLimits[r][k]);
                allocated[r][k] = addition;
                result.counts[r][s*groups+units[j].group] += addition-result.components[k].reference[outer];
                result.unitMeans[j] += (addition-result.components[k].reference[outer])/count;
            }
            result.totals[r][s] = std::accumulate(result.counts[r].begin()+s*groups,result.counts[r].begin()+(s+1)*groups,0.);
        }
    }
    for (std::size_t j = 0; j < units.size(); ++j)
        result.unitMeans[j] = units[j].counted+(1-result.noAdditionProbability[units[j].seat])*(result.unitMeans[j]-units[j].counted);

    // Widening has a known neutral mean for every allocated draw. Record these
    // exact means before drawing its extra local uncertainty; intervals use the
    // broader samples, while displayed expectations avoid extra sampling noise.
    result.positiveMeanTotals.assign(seats,0);
    result.positiveMeanCounts.assign(seats*groups,0);
    for (std::size_t r = 0; r < count; ++r) {
        for (std::size_t s = 0; s < seats; ++s) result.positiveMeanTotals[s] += result.totals[r][s]/count;
        for (std::size_t k = 0; k < seats*groups; ++k) result.positiveMeanCounts[k] += result.counts[r][k]/count;
    }
    sampled.unusedBeforePostal = zeros(count,seats);
    for (std::size_t r = 0; r < count; ++r)
        for (std::size_t s = 0; s < seats; ++s)
            sampled.unusedBeforePostal[r][s] = std::max(0.,prior.enrolment[s]-result.totals[r][s]);
    if (std::none_of(result.components.begin(),result.components.end(),[](auto const& c) { return c.postalWidening; })) return;
    // Every keyed permutation uses this same midpoint-normal grid. Compute it
    // once, rather than repeating an inverse-normal search for every postal unit.
    std::vector<double> normalGrid(count);
    for (std::size_t r = 0; r < count; ++r) normalGrid[r] = inverseLogCdf(std::log((r+.5)/count));
    // Freeze unused enrolment AFTER all original allocations/finishing limits.
    // Multiple postal services reserve disjoint equal shares so their possible
    // increases cannot jointly exceed enrolment or take another service's votes.
    Matrix unused = zeros(count,seats);
    std::vector<double> postalServices(seats);
    for (auto const& c : result.components)
        if (c.postalRemainderLogOdds) ++postalServices[units[c.unit].seat];
    for (std::size_t r = 0; r < count; ++r)
        for (std::size_t s = 0; s < seats; ++s)
            unused[r][s] = std::max(0.,prior.enrolment[s]-result.totals[r][s]);
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto& c = result.components[k];
        if (!layout.activeSeats[units[c.unit].seat]) c.postalWidening = false;
        if (!c.postalWidening) continue;
        auto s = units[c.unit].seat, group = units[c.unit].group;
        std::vector<double> capacity(count), weight(count), location;
        for (std::size_t r = 0; r < count; ++r) {
            auto outer = r/options.countDraws;
            double addition = allocated[r][k];
            capacity[r] = addition+unused[r][s]/postalServices[s];
            if (std::isfinite(sampled.completionLimits[r][k])) continue;
            weight[r] = postalWideningWeight(c,outer,varied[r][k]);
            if (varied[r][k] == c.referenceOdds[outer] && addition > 0 && addition < capacity[r])
                location.push_back(std::log(addition/(capacity[r]-addition)));
        }
        double centre = location.empty() ? 0 : mean(location), variance = 0;
        for (double value : location) variance += (value-centre)*(value-centre)/location.size();
        c.postalRoutineExtraSd = std::sqrt(variance*(PostalRoutineSpread*PostalRoutineSpread-1));
        c.postalBatchExtraSd = std::sqrt(std::max(0.,PostalBatchTargetSd*PostalBatchTargetSd-c.batchSd*c.batchSd));
        auto identity = prior.election+"/"+prior.seats[s]+"/"+units[c.unit].name;
        auto magnitudes = uniforms("postal-widening/magnitude/"+identity,count,options.seed);
        auto choices = uniforms("postal-widening/choice/"+identity,count,options.seed);
        for (std::size_t r = 0; r < count; ++r) {
            bool routine = varied[r][k] == c.referenceOdds[r/options.countDraws];
            double sd = weight[r] <= 0 ? 0 : routine ? c.postalRoutineExtraSd : c.postalBatchExtraSd*std::sqrt(weight[r]);
            double widened = widenAllocatedPostal(allocated[r][k],capacity[r],sd,
                normalGrid[std::size_t(magnitudes[r]*count)],choices[r]);
            double change = widened-allocated[r][k];
            result.counts[r][s*groups+group] += change;
            result.totals[r][s] += change;
        }
    }
}
}

namespace {
bool routineCategory(Unit const& unit) {
    return unit.category == "Absent" || unit.category == "PrePoll";
}

// Replacing this unit's sampled odds by its original routine odds answers how
// much it could allocate in that scenario. Other units' odds remain fixed. The
// stable denominator avoids overflow in very large or tiny batch possibilities.
double routineAllocation(double parent, std::vector<double> const& odds,
    std::vector<std::size_t> const& local, std::size_t component, double reference,
    std::vector<double> const& completionLimits, std::vector<Component> const& components) {
    // A hypothetical routine absent/pre-poll outcome must compete against the
    // same retained postal amount as the real outcome. Keeping its discarded
    // large allowance here would reintroduce the allocation error downstream.
    std::vector<double> localOdds, postalLimits;
    localOdds.reserve(local.size()); postalLimits.reserve(local.size());
    std::size_t position = 0;
    for (auto k : local) {
        if (k == component) position = localOdds.size();
        localOdds.push_back(k == component ? reference : odds[k]);
        postalLimits.push_back(components[k].postalRemainderLogOdds
            ? completionLimits[k] : std::numeric_limits<double>::infinity());
    }
    return allocateDeclarationAdditions(parent,localOdds,postalLimits)[position];
}

struct RoutineAlternatives { double reduced, modest; };

// Additional formal votes are a subset of this draw's allocation plus reserved
// unused enrolment. Blend the routine estimate on log odds towards a broad
// processing-based mean. Subtract half the log variance so that input is not
// inadvertently used as a median with a much larger mean. The modest alternative
// can raise tiny additions, but cannot take votes from another category or exceed
// the counterfactual original routine allowance. Neither changes counted votes.
RoutineAlternatives routineAlternatives(Component const& c, double original,
    double capacity, double reference, double localNormal, double sharedNormal) {
    if (c.routineSupport <= 0 || capacity <= 0) return {original,original};
    double location = odds(std::clamp(original/capacity,1e-15,1-1e-15));
    double localSd = std::sqrt(RoutineRemainderSd*RoutineRemainderSd
        -RoutineRemainderSharedSd*RoutineRemainderSharedSd);
    double target = std::log(c.routineMean/capacity)-RoutineRemainderSd*RoutineRemainderSd/2
        +localSd*localNormal+RoutineRemainderSharedSd*sharedNormal;
    double reduced = std::min(original,capacity*logistic(location+c.routineSupport*std::min(0.,target-location)));
    double modest = std::max(original,std::min(reference,capacity*logistic(target)));
    return {reduced,modest};
}

// Category maturity controls reliance on the processing proxy, not whether
// counting is complete. It compares received counts with the frozen ORIGINAL
// category expectation. At 30/50/75% it phases in about 16/50/90% of the local
// intervention. It does not condition shared totals or transfer voters between
// categories. Missing history, unstarted counts and postal receipt supply no
// such intervention. The existing early/below-prior safeguard remains in force.
void prepareRoutineRemainders(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, LateCountLayout const& layout, LateSamples const& sampled, Result& result) {
    std::map<std::string,std::pair<double,double>> categorySizes;
    for (auto const& unit : units) {
        if (unit.kind != "declaration" || !routineCategory(unit)) continue;
        auto& sizes = categorySizes[unit.category];
        sizes.first += unit.counted;
        for (auto const& row : prior.counts)
            sizes.second += row[unit.seat*prior.groups.size()+unit.group]*unit.weight/prior.counts.size();
    }
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto& c = result.components[k];
        auto const& unit = units[c.unit];
        if (!routineCategory(unit) || unit.counted <= 0) continue;
        auto sizes = categorySizes[unit.category];
        double fraction = sizes.second > 0 ? sizes.first/sizes.second : 0;
        double uncounted = std::max(0.,1-fraction);
        c.routineMaturity = fraction*fraction/(fraction*fraction+uncounted*uncounted);
        auto const& e = result.evidence[c.unit];
        double support = e.coverage*e.started*c.earlyCompletionRetention*c.routineMaturity;
        if (support > 0)
            c.routineMean = std::max(.25,RoutineRemainderDays*.5*(e.volume+e.pooledVolume*(unit.counted+.5)));
        if (!options.declarationRemainders || support <= 0) continue;
        c.routineSupport = support;
        auto const& local = layout.componentsBySeat[unit.seat];
        for (std::size_t r = 0; r < sampled.odds.size(); ++r) {
            auto outer = r/options.countDraws;
            double parent = c.capacity[outer];
            for (auto other : local) parent += result.components[other].reference[outer];
            c.routineReferenceMean += routineAllocation(parent,sampled.odds[r],local,k,c.referenceOdds[outer],
                sampled.completionLimits[r],result.components)/sampled.odds.size();
        }
        c.routineBridge = c.routineSupport*std::pow(c.routineReferenceMean/(c.routineReferenceMean+c.routineMean),2);
    }
}

// The original allocation and postal draws are complete before this map runs.
// Reserve disjoint shares of their remaining unused enrolment for the two
// eligible services. Reducing one allowance cannot inflate another category.
// Independent keyed streams select the new alternative without changing any
// existing selector. Keep analytical local-choice means for displayed totals;
// intervals use samples, as with the separate neutral postal spread.
void reshapeRoutineRemainders(Prior const& prior, std::vector<Unit> const& units,
    Options const& options, LateCountLayout const& layout, LateSamples const& sampled, Result& result) {
    prepareRoutineRemainders(prior,units,options,layout,sampled,result);
    if (std::none_of(result.components.begin(),result.components.end(),[](auto const& c) { return c.routineSupport > 0; })) return;
    auto count = sampled.odds.size(), seats = prior.seats.size(), groups = prior.groups.size();
    // Unit permutations all use the same midpoint grid. Invert its normal CDF
    // once, rather than repeating that search for every service and outcome.
    std::vector<double> normalGrid(count);
    for (std::size_t r = 0; r < count; ++r) normalGrid[r] = inverseLogCdf(std::log((r+.5)/count));
    auto shared = uniforms("declaration-routine/shared",count,options.seed);
    for (auto& value : shared) value = normalGrid[std::size_t(value*count)];
    Matrix unused = zeros(count,seats);
    std::vector<double> services(seats);
    for (auto const& c : result.components)
        if (routineCategory(units[c.unit]) && units[c.unit].counted > 0) ++services[units[c.unit].seat];
    for (std::size_t r = 0; r < count; ++r)
        for (std::size_t s = 0; s < seats; ++s) unused[r][s] = std::max(0.,prior.enrolment[s]-result.totals[r][s]);
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto& c = result.components[k];
        if (c.routineSupport <= 0) continue;
        auto const& unit = units[c.unit];
        auto s = unit.seat, group = s*groups+unit.group;
        auto const& local = layout.componentsBySeat[s];
        auto identity = prior.election+"/"+prior.seats[s]+"/"+unit.name;
        auto normals = uniforms("declaration-routine/local/"+identity,count,options.seed);
        auto choices = uniforms("declaration-routine/choice/"+identity,count,options.seed);
        double meanChange = 0;
        for (std::size_t r = 0; r < count; ++r) {
            auto outer = r/options.countDraws;
            double original = sampled.allocated[r][k], capacity = original+sampled.unusedBeforePostal[r][s]/services[s];
            double parent = c.capacity[outer];
            for (auto other : local) parent += result.components[other].reference[outer];
            double reference = routineAllocation(parent,sampled.odds[r],local,k,c.referenceOdds[outer],
                sampled.completionLimits[r],result.components);
            auto amounts = routineAlternatives(c,original,capacity,reference,normalGrid[std::size_t(normals[r]*count)],shared[r]);
            amounts.modest = std::min(amounts.modest,original+unused[r][s]/services[s]);
            bool point = sampled.odds[r][k] == c.referenceOdds[outer];
            bool finishing = sampled.sizeFinishing[r][k];
            bool processing = std::isfinite(sampled.completionLimits[r][k]) && !finishing;
            double lower = point && !finishing && !processing ? 1-RoutineBacklogFraction : 0;
            double higher = c.routineBridge*(finishing ? 1 : !point ? 1-postalWideningWeight(c,outer,sampled.odds[r][k]) : 0);
            double addition = choices[r] < lower ? amounts.reduced : choices[r] < lower+higher ? amounts.modest : original;
            result.counts[r][group] += addition-original;
            result.totals[r][s] += addition-original;
            meanChange += (lower*(amounts.reduced-original)+higher*(amounts.modest-original))/count;
        }
        c.routineMeanAdjustment = meanChange;
        result.unitMeans[c.unit] += (1-result.noAdditionProbability[s])*meanChange;
        result.positiveMeanCounts[group] += meanChange;
        result.positiveMeanTotals[s] += meanChange;
    }
}
}

namespace {
// Replace only WA mixed early-place allowances, after ordinary count allocation
// and declaration progress have finished. Reductions lower the district total;
// they never enlarge another category. The original broad account is retained
// for diagnostics and as the large-component alternative in scenario draws.
void prepareWaEarlyPlaces(Prior const& prior, std::vector<Unit> const& units,
    std::vector<Observation> const& history, Options const& options, Result& result) {
    if (!prior.election.ends_with("wa")) return;
    std::optional<double> now;
    for (auto const& row : history) if (!now || row.hour > *now) now = row.hour;
    auto samples = result.totals.size();
    // A small common integration grid is sufficient for the bounded smaller
    // remainder. Do not repeat a costly inverse-normal search for every prior
    // outcome in a large election; main scenarios still use continuous draws.
    constexpr std::size_t normalGridSize = 256;
    std::vector<double> normals(normalGridSize);
    for (std::size_t r = 0; r < normalGridSize; ++r)
        normals[r] = inverseLogCdf(std::log((r+.5)/normalGridSize));
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& u = units[j];
        if (u.kind != "ppvc" || !u.incremental || u.counted <= 0) continue;
        auto totals = u.receivedFpTotals;
        totals.push_back(u.counted);
        auto state = WaEarlyCounting::prepare(u.counted,totals,now,options.pollClose,result.broad.complete[j]);
        result.waEarlyPlaces.push_back({j,state,u.counted});
        double changeMean = 0;
        auto identity = prior.election+"/"+prior.seats[u.seat]+"/"+u.name;
        auto choices = uniforms("wa-early/choice/"+identity,samples,options.seed);
        auto magnitudes = uniforms("wa-early/size/"+identity,samples,options.seed);
        for (std::size_t r = 0; r < samples; ++r) {
            double old = result.broad.unitCounts[r/options.countDraws][j]-u.counted;
            double small = WaEarlyCounting::smallerRemainder(u.counted,old,normals[std::size_t(magnitudes[r]*normalGridSize)]);
            double addition = choices[r] < state.smallOnly ? old
                : choices[r] < state.smallOnly+state.largeOnly ? small : 0;
            result.totals[r][u.seat] += addition-old;
            result.counts[r][u.seat*prior.groups.size()+u.group] += addition-old;
            // Integrate the branch probabilities rather than introduce mean
            // noise when very few diagnostic draws select an unfinished place.
            changeMean += (state.smallOnly*old+state.largeOnly*small-old)/samples;
        }
        result.unitMeans[j] += (1-result.noAdditionProbability[u.seat])*changeMean;
        result.positiveMeanTotals[u.seat] += changeMean;
        result.positiveMeanCounts[u.seat*prior.groups.size()+u.group] += changeMean;
        if (state.both == 1) {
            result.unitMeans[j] = u.counted;
            result.broad.complete[j] = true; // Routine FP only, never TCP completion.
        }
    }
}
}

Result prepare(Prior const& prior, std::vector<Unit> const& units, std::vector<bool> const& finalised,
    std::vector<Observation> const& history, Options const& options, Matrix const& suppliedUniforms) {
    if (!options.countDraws) throw std::runtime_error("Turnout preparation needs at least one cheap count draw.");
    Result result;
    std::optional<double> now;
    if (!history.empty()) {
        now = std::max_element(history.begin(),history.end(),[](auto const& a,auto const& b) { return a.hour < b.hour; })->hour;
        if (options.pollClose) result.daysAfterPollClose = (*now-*options.pollClose)/24;
    }
    result.broad = update(prior,units,finalised,options.ppvcFactor);
    prepareProcessingTimetables(units,options,result);
    bool scheduled = options.receiptDeadline || !result.processingTimetables.empty();
    auto completed = scheduled ? boothCompletion(prior,units) : std::vector<double>{};
    auto postalDeadline = options.receiptDeadline ? options.receiptDeadline : options.postalDeadline;
    result.evidence = scheduled ? scheduledEvidence(prior,units,history,postalDeadline,completed,result.processingTimetables)
        : progress(prior,units,history,options.postalDeadline);
    releaseAllocation(prior,units,history,postalDeadline,result,completed);

    if (std::accumulate(result.broad.counted.begin(),result.broad.counted.end(),0.) <= 0) now.reset();
    buildLateCountComponents(prior,units,options,now,result);
    auto layout = indexLateComponents(result,units);
    calculateNoAdditionProbabilities(units,layout,result);
    assignComponentProbabilities(units,result);
    auto sampledOdds = sampleLateComponentOdds(prior,units,options,result,suppliedUniforms);
    assembleCountSamples(prior,units,options,layout,sampledOdds,result);
    reshapeRoutineRemainders(prior,units,options,layout,sampledOdds,result);
    prepareWaEarlyPlaces(prior,units,history,options,result);
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
            || result.components[k].usualLogShift != 0 || result.components[k].processingPhase != 0
            || result.components[k].completionWeight != 0 || result.components[k].positiveRemainderWeight != 0
            || result.components[k].routineSupport > 0;
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
    std::size_t s, std::size_t outer, unsigned long long seed, double sharedNormal,
    double remainderSharedNormal, double routineSharedNormal, std::vector<double>& counts) {
    // In the positive-count outcome, draw each category from its previous,
    // small-addition or exceptional-batch distribution. Their finite shared
    // parent prevents new votes from exceeding the number of enrolled electors.
    auto const& local = plan.componentsBySeat[s];
    std::vector<double> odds; odds.reserve(local.size());
    std::vector<double> completionLimits; completionLimits.reserve(local.size());
    std::vector<double> postalLimits; postalLimits.reserve(local.size());
    enum class Source { Routine, Finishing, Processing, Small, Batch };
    std::vector<Source> sources; sources.reserve(local.size());
    double parent = plan.enrolment[s]-result.broad.totals[outer][s];
    for (auto k : local) {
        auto const& c = result.components[k];
        auto unitKey = seed ^ ((c.unit+1)*0xd1b54a32d192ed03ULL);
        double selector = interiorDrawUniform(unitKey)*(1-result.noAdditionProbability[s]);
        double value = c.referenceOdds[outer];
        double completionLimit = std::numeric_limits<double>::infinity();
        auto source = Source::Routine;
        if (selector >= c.previousProbability) {
            // Direct mixture sampling avoids solving an inverse mixture
            // CDF in every iteration, while preserving the same component
            // distributions used by the offline count calibration.
            double z = std::sqrt(-2*std::log(interiorDrawUniform(unitKey)))
                * std::cos(6.2831853071795864769*interiorDrawUniform(unitKey));
            bool small = selector < c.previousProbability+c.smallProbability;
            source = small ? Source::Small : Source::Batch;
            value = small ? c.smallOdds[outer]+z : c.batchOdds[outer]+c.batchSd*z;
        }
        else if (selector < c.previousProbability*c.completionWeight) {
            // Select completion only from the existing routine branch. A
            // separate unit-local normal draw preserves a continuous finishing
            // remainder without changing any sibling's random stream.
            double z = std::sqrt(-2*std::log(interiorDrawUniform(unitKey)))
                * std::cos(6.2831853071795864769*interiorDrawUniform(unitKey));
            completionLimit = localCompletionLimit(c,outer,z,sharedNormal);
            source = Source::Finishing;
        }
        else if (selector < c.previousProbability*(c.completionWeight+c.positiveRemainderWeight)) {
            // Divert only additional routine outcomes. This unit's extra random
            // draw cannot change another unit's selector or its allocated votes.
            double z = std::sqrt(-2*std::log(interiorDrawUniform(unitKey)))
                * std::cos(6.2831853071795864769*interiorDrawUniform(unitKey));
            completionLimit = positiveRemainderLimit(c,outer,z,remainderSharedNormal);
            source = Source::Processing;
        }
        completionLimits.push_back(completionLimit);
        postalLimits.push_back(c.postalRemainderLogOdds ? completionLimit : std::numeric_limits<double>::infinity());
        sources.push_back(source);
        odds.push_back(value);
        parent += c.reference[outer];
    }
    // Match preparation: reserve any selected postal finishing amount before
    // other declarations compete with it. Unused capacity participates too, so
    // a postal increase need not remove an equal number of other formal votes.
    auto additions = allocateDeclarationAdditions(parent,odds,postalLimits);
    for (std::size_t k = 0; k < local.size(); ++k) {
        auto j = result.components[local[k]].unit;
        double addition = additions[k];
        counts[j] = plan.counted[j]+std::min(addition,completionLimits[k]);
    }
    // Apply exactly the prepared postal output map. The completed shared
    // allocation and every non-postal draw above remain intact. Compute spare
    // enrolment once, before changing any postal service, and reserve disjoint
    // shares if this district has more than one postal unit.
    double total = result.broad.totals[outer][s], postalServices = 0;
    for (auto k : local) {
        auto const& c = result.components[k];
        total += counts[c.unit]-plan.counted[c.unit]-c.reference[outer];
        if (c.postalRemainderLogOdds) ++postalServices;
    }
    double unused = std::max(0.,plan.enrolment[s]-total);
    double prePostalUnused = unused;
    for (std::size_t k = 0; k < local.size(); ++k) {
        auto const& c = result.components[local[k]];
        if (!c.postalWidening || std::isfinite(completionLimits[k])) continue;
        double addition = counts[c.unit]-plan.counted[c.unit];
        double capacity = addition+unused/postalServices;
        bool routine = odds[k] == c.referenceOdds[outer];
        double weight = postalWideningWeight(c,outer,odds[k]);
        double sd = routine ? c.postalRoutineExtraSd : c.postalBatchExtraSd*std::sqrt(weight);
        if (sd <= 0) continue;
        auto wideningKey = seed ^ ((c.unit+1)*0xd1b54a32d192ed03ULL) ^ 0x94c98645b6327a01ULL;
        double normal = std::sqrt(-2*std::log(interiorDrawUniform(wideningKey)))
            *std::cos(6.2831853071795864769*interiorDrawUniform(wideningKey));
        counts[c.unit] = plan.counted[c.unit]+widenAllocatedPostal(addition,capacity,sd,
            normal,interiorDrawUniform(wideningKey));
    }

    // The three-part map follows shared allocation AND postal widening.
    // Share only the actually unused enrolment between eligible services;
    // reductions do not fund another service. Each source is known directly
    // here, so exceptional batches and existing positive processing outcomes
    // stay fixed without the density attribution used by diagnostic samples.
    total = result.broad.totals[outer][s];
    double services = 0;
    for (auto k : local) {
        auto const& c = result.components[k];
        total += counts[c.unit]-plan.counted[c.unit]-c.reference[outer];
        if ((c.routineMaturity > 0 || c.routineSupport > 0) && plan.counted[c.unit] > 0) ++services;
    }
    unused = std::max(0.,plan.enrolment[s]-total);
    for (std::size_t k = 0; k < local.size(); ++k) {
        auto const& c = result.components[local[k]];
        if (c.routineSupport <= 0 || sources[k] == Source::Batch || sources[k] == Source::Processing) continue;
        auto shapeKey = seed ^ ((c.unit+1)*0xd1b54a32d192ed03ULL) ^ 0x32d76e80a1c594b3ULL;
        double selector = interiorDrawUniform(shapeKey);
        bool reduce = sources[k] == Source::Routine && selector >= RoutineBacklogFraction;
        bool increase = sources[k] != Source::Routine && selector < c.routineBridge;
        if (!reduce && !increase) continue;
        double original = counts[c.unit]-plan.counted[c.unit], capacity = original+prePostalUnused/services;
        auto referenceOdds = odds;
        referenceOdds[k] = c.referenceOdds[outer];
        double reference = allocateDeclarationAdditions(parent,referenceOdds,postalLimits)[k];
        double normal = std::sqrt(-2*std::log(interiorDrawUniform(shapeKey)))
            *std::cos(6.2831853071795864769*interiorDrawUniform(shapeKey));
        auto amounts = routineAlternatives(c,original,capacity,reference,normal,routineSharedNormal);
        amounts.modest = std::min(amounts.modest,original+unused/services);
        counts[c.unit] = plan.counted[c.unit]+(reduce ? amounts.reduced : amounts.modest);
    }
}

void drawOpenDeclarationSeats(Result const& result, DrawPlan const& plan, std::size_t outer,
    unsigned long long seed, std::vector<bool> const& zero, std::vector<double>& counts) {
    // Most seats retain their fixed prior row until progress changes their
    // remaining-vote distribution. Only active positive outcomes need sampling.
    // The same independent normal is supplied to every seat in this simulated
    // election. Giving each seat a fresh draw would average away common postal
    // uncertainty as the number of seats increased.
    auto sharedKey = seed ^ 0xc6a4a7935bd1e995ULL;
    double sharedNormal = std::sqrt(-2*std::log(interiorDrawUniform(sharedKey)))
        *std::cos(6.2831853071795864769*interiorDrawUniform(sharedKey));
    auto remainderKey = seed ^ 0xc6a4a7935bd1e995ULL ^ 0x71651ULL;
    double remainderSharedNormal = std::sqrt(-2*std::log(interiorDrawUniform(remainderKey)))
        *std::cos(6.2831853071795864769*interiorDrawUniform(remainderKey));
    auto routineKey = seed ^ 0x75ab8e02dc43619fULL;
    double routineSharedNormal = std::sqrt(-2*std::log(interiorDrawUniform(routineKey)))
        *std::cos(6.2831853071795864769*interiorDrawUniform(routineKey));
    for (std::size_t s = 0; s < zero.size(); ++s)
        if (!zero[s] && plan.active[s]) drawSeatDeclarationAdditions(result, plan, s, outer, seed,
            sharedNormal,remainderSharedNormal,routineSharedNormal,counts);
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
    auto outer = std::size_t(step(state) % result.broad.unitCounts.size());
    auto counts = result.broad.unitCounts[outer];
    auto zero = drawNoAdditionSeats(result, plan, seed);
    drawOpenDeclarationSeats(result, plan, outer, seed, zero, counts);
    retainCountedOnlySeats(plan, zero, counts);
    for (auto const& place : result.waEarlyPlaces) {
        auto key = seed ^ ((place.unit+1)*0xd1b54a32d192ed03ULL) ^ 0x57414561726c7931ULL;
        double choice = interiorDrawUniform(key);
        double addition = counts[place.unit]-place.counted;
        if (choice >= place.state.smallOnly+place.state.largeOnly) addition = 0;
        else if (choice >= place.state.smallOnly) {
            double normal = std::sqrt(-2*std::log(interiorDrawUniform(key)))
                *std::cos(6.2831853071795864769*interiorDrawUniform(key));
            addition = WaEarlyCounting::smallerRemainder(place.counted,addition,normal);
        }
        counts[place.unit] = place.counted+addition;
    }
    return counts;
}

std::vector<double> meanTotals(Result const& result) {
    // Consumers must include the exact counted branch when reporting means;
    // the stored sample matrix intentionally retains positive outcomes only.
    std::vector<double> means(result.broad.counted.size());
    if (result.positiveMeanTotals.size() == means.size()) means = result.positiveMeanTotals;
    else for (auto const& row : result.totals) for (std::size_t s = 0; s < row.size(); ++s) means[s] += row[s]/result.totals.size();
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
