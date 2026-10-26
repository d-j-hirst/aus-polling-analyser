#pragma once

#include "LiveTppIntegration.h"
#include "LiveTppCompositionParameters.h"

#include <array>
#include <map>
#include <string_view>

// Category composition reallocates an existing seat forecast between voting
// channels. Shared departures describe an election; local departures describe
// how one district differs from it. Neither is extra independent seat-TPP noise.
namespace LiveTppComposition {
using namespace LiveTppIntegration;

// Allow modestly larger departures than the historical covariance estimate.
// Scale both local and shared category SDs, preserving their correlations.
// The joint prior still retains the original seat covariance budget; this
// does not add independent national noise or alter category mean estimates.
constexpr double CategoryDepartureSdMultiplier = 1.1;

// Only actual historical category accounts establish an observed swing.
// Synthetic accounts are useful comparisons, but their allocation error remains
// after all current ballots arrive. Five transformed points (about five share
// points near 50%) is a conservative working allowance, not a fitted accuracy.
enum class BaselineSource { Unavailable, Historical, Synthetic };
constexpr double SyntheticBaselineSd = 4;
constexpr double SyntheticBaselineSharedSd = 3; // 3 shared and 4 local combine to 5.
inline Moments withSyntheticBaselines(Moments prior, std::size_t count) {
    auto start = prior.mean.size();
    prior.mean.resize(start+count,0);
    for (auto& row : prior.covariance) row.resize(start+count,0);
    prior.covariance.resize(start+count,Vector(start+count));
    for (std::size_t i = start; i < start+count; ++i) prior.covariance[i][i] = 1;
    return prior;
}
inline char const* baselineSourceName(BaselineSource source) {
    switch (source) {
    case BaselineSource::Historical: return "historical";
    case BaselineSource::Synthetic: return "synthetic";
    default: return "unavailable";
    }
}
struct CategoryHistory {
    double first = 0, total = 0;
    bool synthetic = false;
    BaselineSource source() const {
        return total <= 0 ? BaselineSource::Unavailable :
            synthetic ? BaselineSource::Synthetic : BaselineSource::Historical;
    }
};

// Unsupported categories cannot teach a swing. Their received votes instead
// replace exactly their own portion of a seat draw, leaving the other ballots'
// share unchanged. TCP integers stay fixed; FP-only preferences remain uncertain.
struct DirectVotes {
    double votes = 0, first = 0, inferredVotes = 0, inferredFirst = 0;
    double conversionVariance = 0; // Share-point variance of inferred ballots.
    double add(ReceivedTpp const& received, double measuredVotes, double measuredFirst) {
        // Ordinarily a partial account retains some pending preferences. Take
        // that count by subtraction, never by reversing its floating-point
        // fraction: 144*(1-143/144) is slightly below one. An allocation of the
        // whole pending vote to one side must still be exactly a 100% share.
        // Under OPV, received.votes already excludes exhausted ballots.
        double pending = received.votes-measuredVotes;
        if (!std::isfinite(pending) || pending < 0 || !std::isfinite(measuredFirst)
            || measuredFirst < 0 || measuredFirst > measuredVotes
            || !std::isfinite(received.inferredFirst) || received.inferredFirst < 0
            || received.inferredFirst > pending)
            throw std::logic_error("Invalid direct TPP count account.");
        votes += received.votes; first += measuredFirst;
        inferredVotes += pending; inferredFirst += received.inferredFirst;
        return pending;
    }
    double shareAt(double seatShare, double expected, double inferredShare) const {
        if (votes <= 0 || expected <= 0) return seatShare;
        double total = std::max(expected, votes);
        return ((total-votes)*seatShare + 100*first + inferredVotes*inferredShare)/total;
    }
    double inferredShare() const { return inferredVotes > 0 ? 100*inferredFirst/inferredVotes : 50; }
    double centre(double seatShare, double expected) const {
        return shareAt(seatShare, expected, inferredShare());
    }
};

// Variance in percentage-points-squared per unit unresolved preference share.
// 2022 federal non-classic conversion errors suggest K about 29--30; 35 is a
// conservative working scale, not a multi-election coverage guarantee.
constexpr double ConversionPoolVariance = 35;
// Greens conversion is materially more stable than the mixed non-major pool.
// Historical strength/history predictions had about .5--.6 seat-TPP points RMS
// error in two later federal elections. K=4 gives 1.10 points local SD for a
// 30% Greens pool with no local matches, before the separate .60-point shared
// allowance. This is a conservative working scale, not a fitted optimum.
// Other parties retain K=35 until comparable evidence supports a distinction.
constexpr double GreensConversionPoolVariance = 4;
constexpr double LocalConversionVarianceFloor = .2;
constexpr double LocalConversionEvidenceRate = 2;
// Working shared FLOW errors, in preference percentage points. Greens' fitted
// historical means still left roughly 1 point of weighted flow bias in two
// later-election tests; use 2 points SD. Substantial independent populations
// differed from classic independent references by several points in all three
// studied elections; use 5. These protect population transfer, not sampling
// error, and are conservative settings rather than fitted coverage guarantees.
constexpr double GreensTransferFlowSd = 2;
constexpr double IndependentTransferFlowSd = 5;
constexpr std::size_t PartyTransferFactors = 2;

inline Moments withPartyTransfer(Moments prior) {
    // Two independent unit-normal errors, shared by all exposed observations.
    // Appending observation errors preserves the original seat/category prior.
    // Unlike a per-seat precision bound, this retains the true local scale.
    auto start = prior.mean.size();
    prior.mean.resize(start + PartyTransferFactors, 0);
    for (auto& row : prior.covariance) row.resize(prior.mean.size(), 0);
    prior.covariance.resize(prior.mean.size(), Vector(prior.mean.size()));
    for (std::size_t i = start; i < prior.mean.size(); ++i) prior.covariance[i][i] = 1;
    return prior;
}

template<class Counts>
inline double preferencePoolFraction(Counts const& fp, int nationals) {
    double total = 0, pool = 0;
    for (auto const& [party, votes] : fp) {
        if (!std::isfinite(double(votes)) || votes < 0)
            throw std::logic_error("Invalid preference-pool count.");
        total += votes;
        // Both Coalition partners are on the same side of a hypothetical TPP.
        if (party != 0 && party != 1 && party != nationals) pool += votes;
    }
    return total > 0 ? pool/total : 0;
}

inline double localConversionRetention(double matchedUnits) {
    if (!std::isfinite(matchedUnits) || matchedUnits < 0)
        throw std::logic_error("Invalid matched preference evidence.");
    // Good same-district matches across categories reduce extrapolation uncertainty;
    // remaining systematic differences do not disappear with booth count.
    return LocalConversionVarianceFloor + (1-LocalConversionVarianceFloor)
        / (1+LocalConversionEvidenceRate*matchedUnits);
}

inline double conversionShareJacobian(double percent) {
    if (!std::isfinite(percent) || percent < 0 || percent > 100)
        throw std::logic_error("Invalid TPP conversion share.");
    // Same endpoint convention as transform(). d(25 log odds)/d(share points).
    double p = std::clamp(percent, 1e-6, 100-1e-6);
    return 2500/(p*(100-p));
}

inline double matchedPreferenceUnit(double preferenceVotes, double reliability) {
    // One large booth supplies at most one location's information. A tiny
    // preference pool supplies much less: 500 transferred votes give half a
    // location. Use the same count scale as the existing preference-flow fit.
    return std::clamp(reliability, 0., 1.) * std::max(0., preferenceVotes)
        / (std::max(0., preferenceVotes) + 500.);
}

inline double conversionSd(double matchedUnits) {
    if (!std::isfinite(matchedUnits) || matchedUnits < 0)
        throw std::logic_error("Invalid matched preference evidence.");
    // Unit prior precision plus fractional independent reporting locations.
    // This measures robustness only; no residual sign is fitted here.
    return std::sqrt(ConversionPoolVariance / (1 + matchedUnits));
}

inline double pooledPreferenceUnits(std::map<int, CategoryEvidence> const& categories) {
    // Count each physical category once, from the combined evidence account,
    // never again from its measured/inferred observation subsets. The existing
    // preference-flow mean already pools through seat, region and election.
    double units = 0;
    for (auto const& [code, evidence] : categories) units += evidence.matchedPreferenceUnits;
    return units;
}

// Reporting order is an error in the observed ordinary sample, distinct from
// the true whole-category departure and from preference conversion. These are
// conservative first-pass scales, not a fit to one election's signed error.
// Near 50% TPP the transformed scale is approximately percentage points.
constexpr std::size_t ReportingOrderFactors = 2;
constexpr double ReportingOrderInitialSd = 1.5;
constexpr double ReportingOrderCoverageScale = .3;
constexpr double ReportingOrderCorrelation = .5;
constexpr double ReportingOrderCompensationStrength = 5;

inline double reportingOrderSd(ReportingProgress const& progress, bool measured) {
    if (progress.expectedVotes <= 0) return 0;
    double coverage = progress.coverage(measured);
    // At 10% coverage this retains 67.5% of the initial SD; complete coverage
    // removes it. Using the lesser of vote and location coverage avoids either
    // one huge booth or many tiny booths prematurely exhausting the allowance.
    return ReportingOrderInitialSd*(1-coverage)/(1+coverage/ReportingOrderCoverageScale);
}

inline Moments withReportingOrder(Moments prior) {
    // Two correlated unit-variance nuisance states: FP-reporting order and
    // TCP-readiness order. They share geography, but the smaller TCP subset can
    // differ. Their observation loadings shrink with current-snapshot coverage.
    // Seat/category priors are untouched; no noise is added after conditioning.
    auto start = prior.mean.size();
    prior.mean.resize(start+ReportingOrderFactors, 0);
    for (auto& row : prior.covariance) row.resize(prior.mean.size(), 0);
    prior.covariance.resize(prior.mean.size(), Vector(prior.mean.size()));
    for (std::size_t i = start; i < prior.mean.size(); ++i)
        for (std::size_t j = start; j < prior.mean.size(); ++j)
            prior.covariance[i][j] = i == j ? 1 : ReportingOrderCorrelation;
    return prior;
}

inline void applyReportingOrder(Observation& row, ReportingProgress const& progress,
    std::size_t factorStart) {
    if (row.category != 0) return; // First pass: ordinary-day booth reporting order.
    auto factor = factorStart + (row.fpDerivedGroup ? 0 : 1);
    // A completed district can justify restoring its own comparison weight
    // before it represents the whole election. Increase its exposure to the
    // existing shared error while ordinary FP coverage elsewhere remains low.
    // With no restored weight the normal reporting-order allowance applies.
    row.compensationReportingOrderSd = ReportingOrderCompensationStrength
        * row.restoredComparisonFraction * (1-progress.coverage(false));
    row.h.at(factor) = std::hypot(reportingOrderSd(progress, !row.fpDerivedGroup),
        row.compensationReportingOrderSd);
    // Both components load the same factor: this does not add an independent
    // noise draw or change the local booth means. Full FP coverage removes the
    // compensation component, including for TCP subsets still being counted.
}

struct Exposure {
    std::array<double, 4> mean{}, split{};
    bool unsupported = false;
};

inline Exposure exposure(int code, std::string_view region, int year = 2025) {
    Exposure result;
    if (code == 0) return result; // Ordinary is the contrast reference.
    if (code == 1) result.mean[0] = 1; // Ordinary PPVC.
    else if (code == 104 && region == "wa") {
        // WA's adapter calls its in-person Early total PrePoll. Before 2025
        // that total included outside-district early votes; those are now Absent.
        if (year >= 2025) result.mean[0] = 1;
        else {
            result.mean[0] = result.mean[1] = .5;
            result.split[0] = 1/std::sqrt(12.); result.split[1] = -result.split[0];
        }
    }
    else if (code == 104) result.mean[1] = 1; // Federal/QLD declaration pre-poll.
    else if (code == 105) result.mean[3] = 1;
    else if (code == 106 && region == "vic") {
        // VEC Early includes in-district AND out-of-district early votes.
        // A uniform unknown mixture is marginalised, not entered as fictitious
        // half-sized count records. Its split uncertainty is retained below.
        result.mean[0] = result.mean[1] = .5;
        result.split[0] = 1 / std::sqrt(12.); result.split[1] = -result.split[0];
    }
    else if (code == 106) result.mean[0] = 1;
    else if (code == 102 && (region == "nsw" || (region == "wa" && year >= 2025))) {
        // These jurisdictions' Absent totals combine early-away and day-away.
        result.mean[1] = result.mean[2] = .5;
        result.split[1] = 1 / std::sqrt(12.); result.split[2] = -result.split[1];
    }
    else if (code == 102) result.mean[2] = 1;
    else result.unsupported = true; // Mobile/provisional etc: separate broad prior.
    return result;
}

inline double bilinear(std::array<double, 4> const& a, double const (&cov)[4][4],
    std::array<double, 4> const& b) {
    double value = 0;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) value += a[i] * cov[i][j] * b[j];
    return value;
}

inline Matrix categoryCovariance(std::vector<int> const& codes, std::string_view region, bool local, int year = 2025) {
    auto const& covariance = local ? LocalCovariance : SharedCovariance;
    Matrix result(codes.size(), Vector(codes.size()));
    for (std::size_t i = 0; i < codes.size(); ++i) {
        auto a = exposure(codes[i], region, year);
        for (std::size_t j = 0; j < codes.size(); ++j) {
            auto b = exposure(codes[j], region, year);
            result[i][j] = bilinear(a.mean, covariance, b.mean);
            if (i == j) {
                result[i][j] += bilinear(a.split, covariance, a.split);
            }
            result[i][j] *= CategoryDepartureSdMultiplier*CategoryDepartureSdMultiplier;
            // Unstudied channels already have a separate broad SD of 5;
            // this tuning applies to the calibrated category relationships.
            if (i == j && a.unsupported) result[i][i] += 25;
        }
    }
    return result;
}

inline bool factor(Matrix const& covariance, Matrix& root) {
    // A semidefinite Cholesky factor also accepts deterministic baseline seats.
    // A non-positive budget is reported to the caller, never repaired by adding
    // national variance. The tolerance only absorbs floating-point round-off.
    auto n = covariance.size();
    root.assign(n, Vector(n));
    double tolerance = 1e-9;
    for (std::size_t i = 0; i < n; ++i) {
        if (covariance[i].size() != n) throw std::logic_error("Invalid composition covariance shape.");
        tolerance = std::max(tolerance, std::abs(covariance[i][i])*1e-10);
    }
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
        double value = covariance[i][j];
        for (std::size_t k = 0; k < j; ++k) value -= root[i][k]*root[j][k];
        if (!std::isfinite(value)) return false;
        if (i == j) {
            if (value < -tolerance) return false;
            root[i][j] = std::sqrt(std::max(0., value));
        }
        else if (root[j][j] > 0) root[i][j] = value/root[j][j];
        else if (std::abs(value) > tolerance) return false;
    }
    return true;
}

inline Vector forwardSolve(Matrix const& root, Vector values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) values[i] -= root[i][j]*values[j];
        if (root[i][i] > 0) values[i] /= root[i][i];
        else if (std::abs(values[i]) < 1e-7) values[i] = 0;
        else throw std::logic_error("Composition exceeds deterministic TPP budget.");
    }
    return values;
}

inline Vector covarianceSolve(Matrix const& root, Vector values) {
    values = forwardSolve(root, std::move(values));
    for (std::size_t end = values.size(); end > 0; --end) {
        auto i = end - 1;
        for (std::size_t j = i + 1; j < values.size(); ++j) values[i] -= root[j][i]*values[j];
        values[i] = root[i][i] > 0 ? values[i]/root[i][i] : 0;
    }
    return values;
}

inline Matrix mappedCovariance(Matrix const& exposures, Matrix const& covariance) {
    Matrix products(exposures.size(), Vector(covariance.size()));
    for (std::size_t i = 0; i < exposures.size(); ++i)
        for (std::size_t c = 0; c < covariance.size(); ++c)
            products[i][c] = dot(exposures[i], covariance[c]);
    Matrix result(exposures.size(), Vector(exposures.size()));
    for (std::size_t i = 0; i < exposures.size(); ++i)
        for (std::size_t j = 0; j < exposures.size(); ++j) result[i][j] = dot(products[i], exposures[j]);
    return result;
}

inline double covarianceBudget(Matrix const& baseline, Matrix const& contribution) {
    // Reserve ten percent of the baseline covariance for other forecast errors.
    // The maximum admissible scale is determined by the entire covariance, not
    // by each diagonal separately (which could produce an impossible joint prior).
    auto fits = [&](double scale) {
        Matrix remainder = baseline, root;
        for (std::size_t i = 0; i < baseline.size(); ++i)
            for (std::size_t j = 0; j < baseline.size(); ++j)
                remainder[i][j] = .9*baseline[i][j] - scale*contribution[i][j];
        return factor(remainder, root);
    };
    if (fits(1)) return 1;
    double low = 0, high = 1;
    for (int iteration = 0; iteration < 40; ++iteration) {
        double middle = (low + high)/2;
        if (fits(middle)) low = middle; else high = middle;
    }
    return low < 1e-8 ? 0 : low;
}

struct Prior {
    Moments moments;
    double budgetScale = 1;
};

inline Matrix referenceMixtures(Matrix mixtures, Vector const& weights, Vector const& loadings) {
    // Centre geography around the same election-wide mixture. The caller's
    // weights include the share derivative and divide by the seat loading, so
    // the induced geographical offsets conserve the national vote total to
    // first order, including seats with different baseline partisan leans.
    if (mixtures.empty()) return mixtures;
    Vector reference(mixtures.front().size());
    double total = 0;
    for (std::size_t s = 0; s < mixtures.size(); ++s) {
        total += weights.at(s);
        for (std::size_t c = 0; c < reference.size(); ++c) reference[c] += weights[s]*mixtures[s][c];
    }
    if (total > 0) for (double& value : reference) value /= total;
    for (std::size_t s = 0; s < mixtures.size(); ++s) for (std::size_t c = 0; c < reference.size(); ++c)
        mixtures[s][c] = weights[s] > 0 ? (mixtures[s][c]-reference[c])/loadings.at(s) : 0;
    return mixtures;
}

inline Prior jointPrior(Moments const& baseline, Matrix const& category, Matrix const& mix) {
    // The reference mix is common to all districts. Cov(T,D)=A*C, where A is
    // each district's departure from that reference. Consequently ordinary
    // results cannot explain a regional forecast miss by regressing it on the
    // district's postal fraction. Paired category counts can still learn D.
    // T's complete covariance is copied unchanged: this ALLOCATES its budget.
    auto n = baseline.mean.size(), k = category.size(), size = n + k;
    if (mix.size() != n) throw std::logic_error("Missing TPP category mixtures.");
    Matrix root;
    if (!factor(baseline.covariance, root)) throw std::logic_error("Invalid TPP baseline covariance.");
    double scale = covarianceBudget(baseline.covariance, mappedCovariance(mix, category));
    Prior result{baseline, scale};
    result.moments.mean.resize(size);
    auto& covariance = result.moments.covariance;
    for (auto& row : covariance) row.resize(size);
    covariance.resize(size, Vector(size));
    for (std::size_t c = 0; c < k; ++c) {
        for (std::size_t d = 0; d < k; ++d) covariance[n+c][n+d] = scale*category[c][d];
        for (std::size_t s = 0; s < n; ++s)
            covariance[s][n+c] = covariance[n+c][s] = scale*dot(mix[s], category[c]);
    }
    return result;
}

struct Extension {
    std::size_t seats = 0;
    Vector seatMeans;
    Matrix regression, root;
    Vector extraMeans;
};

inline Extension prepareExtension(Moments const& prior, std::size_t seats) {
    // Append category draws CONDITIONAL on the actual baseline seat draw. An
    // independent normal here would undo the very covariance prepared above.
    Extension result{seats, Vector(prior.mean.begin(), prior.mean.begin()+seats), {}, {},
        Vector(prior.mean.begin()+seats, prior.mean.end())};
    Matrix baseline(seats, Vector(seats)), root;
    for (std::size_t i = 0; i < seats; ++i)
        std::copy_n(prior.covariance[i].begin(), seats, baseline[i].begin());
    if (!factor(baseline, root)) throw std::logic_error("Invalid TPP baseline factor.");
    auto k = prior.mean.size() - seats;
    Matrix remaining(k, Vector(k));
    for (std::size_t c = 0; c < k; ++c) {
        Vector cross(prior.covariance[seats+c].begin(), prior.covariance[seats+c].begin()+seats);
        result.regression.push_back(covarianceSolve(root, cross));
    }
    for (std::size_t c = 0; c < k; ++c) for (std::size_t d = 0; d < k; ++d) {
        Vector cross(prior.covariance[seats+d].begin(), prior.covariance[seats+d].begin()+seats);
        remaining[c][d] = prior.covariance[seats+c][seats+d] - dot(result.regression[c], cross);
    }
    if (!factor(remaining, result.root)) throw std::logic_error("Invalid conditional category covariance.");
    return result;
}

template<class Normal>
inline Vector extend(Extension const& extension, Vector state, Normal normal) {
    if (state.size() != extension.seats) throw std::logic_error("Invalid TPP baseline draw size.");
    auto delta = state;
    for (std::size_t s = 0; s < delta.size(); ++s) delta[s] -= extension.seatMeans[s];
    Vector noise(extension.root.size());
    for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = normal(i);
    for (std::size_t c = 0; c < noise.size(); ++c)
        state.push_back(extension.extraMeans[c] + dot(extension.regression[c], delta) + dot(extension.root[c], noise));
    return state;
}

inline std::vector<Observation> decorrelate(std::vector<Observation> rows, Matrix const& covariance) {
    // Local category departures remain after a category finishes counting, but
    // their weighted sum is zero. Whitening this small within-seat block keeps
    // that cancellation; adding independent variance floors would lose it.
    Matrix root;
    if (!factor(covariance, root)) throw std::logic_error("Invalid local composition covariance.");
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (root[i][i] <= 0) throw std::logic_error("Singular TPP observation block.");
        for (std::size_t j = 0; j < i; ++j) {
            rows[i].value -= root[i][j]*rows[j].value;
            for (std::size_t c = 0; c < rows[i].h.size(); ++c) rows[i].h[c] -= root[i][j]*rows[j].h[c];
        }
        rows[i].value /= root[i][i];
        for (double& value : rows[i].h) value /= root[i][i];
        rows[i].variance = 1;
    }
    return rows;
}

struct ObservationBlock {
    std::vector<Observation> rows;
    Matrix covariance;
};

inline ObservationBlock observationBlock(std::vector<Observation> rows,
    Matrix const& localExposures, Matrix const& categoryCovariance) {
    auto covariance = mappedCovariance(localExposures, categoryCovariance);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        covariance[i][i] += rows[i].variance;
        // Both information groups refer to the SAME synthetic category baseline.
        // Counting more locations or splitting FP/TCP cannot average its error away.
        for (std::size_t j = 0; j < rows.size(); ++j)
            if (rows[i].category == rows[j].category)
                covariance[i][j] += rows[i].syntheticBaselineSd*rows[j].syntheticBaselineSd;
    }
    return {std::move(rows), std::move(covariance)};
}

inline void prepareConversionScales(Observation& row) {
    // Normally measured TPP has no conversion exposure. FP-derived TPP keeps
    // uncertainty only for unresolved preference ballots; known major-party
    // FPs and reporting-order uncertainty are accounted for elsewhere.
    double q = row.unresolvedPreferenceFraction;
    if (!std::isfinite(q) || q < 0 || q > row.inferredFraction + 1e-10
        || !std::isfinite(row.conversionJacobian) || row.conversionJacobian <= 0)
        throw std::logic_error("Invalid unresolved preference exposure.");
    double exposure = std::sqrt(q)*row.conversionJacobian;
    // Keep the existing shared precision budget, without a local residual
    // floor: many measured districts can establish the common correction.
    row.sharedConversionSd = exposure*conversionSd(row.sharedMatchedUnits);
    // A common flow error changes seat TPP in proportion to the party's
    // unresolved primary share (q), NOT sqrt(q). Keep Greens and independent
    // errors separate; the existing local pool variance still scales with q.
    // Direct TPP removes this exposure. Unrelated classic booths cannot
    // demonstrate that the same correction transfers to these voter pools.
    double greens = row.greensPreferenceFraction, independent = row.independentPreferenceFraction;
    if (!std::isfinite(greens) || !std::isfinite(independent) || greens < 0 || independent < 0 ||
        greens+independent > q+1e-10)
        throw std::logic_error("Invalid party conversion exposure.");
    if (!std::isfinite(row.inferredFraction) || row.inferredFraction < 0 || row.inferredFraction > 1)
        throw std::logic_error("Invalid inferred preference fraction.");
    double retention = localConversionRetention(row.localMatchedUnits);
    row.greensLocalConversionSd = row.conversionJacobian
        * std::sqrt(GreensConversionPoolVariance*greens*retention);
    row.otherLocalConversionSd = row.conversionJacobian
        * std::sqrt(ConversionPoolVariance*std::max(0., q-greens)*retention);
    row.localConversionSd = std::hypot(row.greensLocalConversionSd, row.otherLocalConversionSd);
    row.greensTransferSd = greens*row.conversionJacobian*GreensTransferFlowSd;
    row.independentTransferSd = independent*row.conversionJacobian*IndependentTransferFlowSd;
}

inline void addLocalConversionCovariance(ObservationBlock& block, double budgetFraction) {
    // Local party errors correlate across categories within the same district.
    // Retain the older general-correction precision bound, which shrinks with
    // matched booths and protects the established sparse early-count response.
    // Persistent party-transfer errors are instead explicit shared states;
    // adding them here too would double count them and destroy local precision.
    for (std::size_t i = 0; i < block.rows.size(); ++i) {
        for (std::size_t j = 0; j < block.rows.size(); ++j) {
            auto const& a = block.rows[i]; auto const& b = block.rows[j];
            // q already includes the fraction pending TCP. Applying f again
            // would turn the intended linear variance taper into a cubic one.
            double extra = a.greensLocalConversionSd*b.greensLocalConversionSd
                + a.otherLocalConversionSd*b.otherLocalConversionSd
                + a.sharedConversionSd*b.sharedConversionSd/budgetFraction;
            block.covariance.at(i).at(j) += extra;
            if (i == j) block.rows[i].conversionVariance = extra
                + a.greensTransferSd*a.greensTransferSd + a.independentTransferSd*a.independentTransferSd;
        }
    }
}

inline void allowForConversionUncertainty(ObservationBlock& block, double budgetFraction) {
    if (!std::isfinite(budgetFraction) || budgetFraction <= 0 || budgetFraction > 1)
        throw std::logic_error("Invalid TPP conversion precision budget.");
    for (auto& row : block.rows) prepareConversionScales(row);
    addLocalConversionCovariance(block, budgetFraction);
}

inline void applyPartyTransfer(ObservationBlock& block, std::size_t factorStart,
    Matrix const& covariance = {{1,0},{0,1}}) {
    // The same two state indices are used for EVERY seat. Their calibration
    // comes from matched accounts and is held fixed by the seat update. Never
    // feed these residual means back into the underlying booth conversion or
    // the next snapshot's prior.
    for (auto& row : block.rows) {
        row.h.at(factorStart) = row.greensTransferSd;
        row.h.at(factorStart+1) = row.independentTransferSd;
        // Residual covariance excludes these explicit factors. The marginal
        // diagnostic does include their calibrated covariance, including the
        // ambiguity between Greens and independent contributions in mixed pools.
        double g = row.greensTransferSd, i = row.independentTransferSd;
        row.conversionVariance += g*g*(covariance[0][0]-1)
            + i*i*(covariance[1][1]-1) + 2*g*i*covariance[0][1];
    }
}

inline double share(double transformed) { return 100/(1+std::exp(-transformed/25)); }
inline double transform(double percent) {
    percent = std::clamp(percent, 1e-6, 100-1e-6);
    return 25*std::log(percent/(100-percent));
}
inline double derivative(double percent) { return percent*(100-percent)/2500; }

inline double observedShare(CategoryEvidence const& evidence) {
    // Compare like locations on the count scale, then carry their bounded
    // swing to the whole category. This avoids an early extreme booth implying
    // a share above 100%, and reproduces the actual share at full coverage.
    double prior = evidence.baselineFirst/evidence.expected;
    double received = evidence.weightedObservedShare/evidence.weight;
    double comparison = evidence.weightedBaselineShare/evidence.weight;
    return share(transform(prior)+transform(received)-transform(comparison));
}

struct Account {
    Vector shares, fractions, contrastWeights;
    double seatLoading = 1;
};

inline Account centre(Vector const& shares, Vector const& amounts, double totalShare) {
    // Preserve the seat prior exactly on the VOTE scale. Neither a weighted
    // average of log odds nor an average of component percentages is a count.
    if (shares.empty() || shares.size() != amounts.size()) throw std::logic_error("Invalid TPP category account.");
    double total = 0;
    for (double value : amounts) total += value;
    if (total <= 0) throw std::logic_error("Empty TPP category account.");
    Account result;
    for (double amount : amounts) result.fractions.push_back(amount/total);
    double low = -1000, high = 1000;
    for (int iteration = 0; iteration < 80; ++iteration) {
        double middle = (low+high)/2, weighted = 0;
        for (std::size_t c = 0; c < shares.size(); ++c)
            weighted += result.fractions[c]*share(transform(shares[c])+middle);
        if (weighted < totalShare) low = middle; else high = middle;
    }
    double slope = 0;
    for (std::size_t c = 0; c < shares.size(); ++c) {
        result.shares.push_back(share(transform(shares[c])+(low+high)/2));
        double weight = result.fractions[c]*derivative(result.shares.back());
        result.contrastWeights.push_back(weight); slope += weight;
    }
    for (double& weight : result.contrastWeights) weight /= slope;
    result.seatLoading = derivative(totalShare)/slope;
    return result;
}
} // namespace LiveTppComposition
