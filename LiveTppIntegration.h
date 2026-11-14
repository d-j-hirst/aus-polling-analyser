#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <optional>
#include <vector>

// Joint conditioning on the analyser's 25 * log-odds scale. Seat totals share
// national/regional prior errors; categories share errors about how their swing
// differs from ordinary voting. Conditioning them together preserves those
// correlations, including the negative correlations learned from a partial count.
namespace LiveTppIntegration {
// These groups partition the statistical contribution of a reporting unit,
// not its physical location. All retains its complete contribution; the other
// two divide its information according to how much of the received FP account
// still awaits preferences. Larger additions may have a very different mix.
enum class EvidenceSourceGroup { All, MeasuredTcp, FpDerivedTpp };
inline constexpr double EvidenceGroupHalfWeightGap = .05;
inline double evidenceGroupFraction(EvidenceSourceGroup group, double inferredFraction) {
    if (!std::isfinite(inferredFraction) || inferredFraction < 0 || inferredFraction > 1)
        throw std::logic_error("Invalid inferred TPP fraction.");
    if (group == EvidenceSourceGroup::All) return 1;
    // Tiny rechecks retain almost all measured-group weight. At a 5% gap the
    // groups split evenly; larger gaps quickly favour the FP-derived treatment.
    // Square scaled odds to make both endpoints smooth. This equivalent form
    // avoids division by zero at fully unresolved counts and subtractive loss
    // for tiny contributions. The actual unresolved vote fraction is unchanged.
    double measured = EvidenceGroupHalfWeightGap*(1-inferredFraction);
    double inferred = (1-EvidenceGroupHalfWeightGap)*inferredFraction;
    measured *= measured;
    inferred *= inferred;
    return (group == EvidenceSourceGroup::FpDerivedTpp ? inferred : measured)/(measured+inferred);
}
using Vector = std::vector<double>;
using Matrix = std::vector<Vector>;

struct ReceivedTpp {
    double votes = 0, transformedShare = 0, inferredFraction = 0;
    double samplingVariance = 0;
    // Candidate-one votes inferred from FP, excluding any measured TCP. The
    // alignment helper can translate these in a detached comparison copy.
    double inferredFirst = 0;
};

// For a swing comparison, inferred preferences supply half the weight of
// a measured Labor–Coalition ballot. Apply this by the inferred fraction
// so that a partial or revised TCP retains full weight on its counted part.
inline constexpr double InferredPreferenceComparisonWeight = .5;

inline double samplingVariance(double total, double first) {
    // Delta-method variance of 25*log odds. At 50:50 this is about 2500/n;
    // near an endpoint the uncertainty is larger. A possible observed zero
    // must not become an extremely precise observation on the bounded scale.
    return 625 * (1 / (first + .25) + 1 / (total - first + .25));
}

inline std::optional<ReceivedTpp> receivedComparison(double fp, double tcp, double first,
    std::optional<double> inferredShare, std::optional<double> pendingFirst, bool optionalPreferential) {
    // The caller supplies classic TCP only. A non-classic pair cannot measure
    // TPP, but its received FP can still supply an explicitly inferred estimate.
    if (tcp <= 0) {
        if (fp <= 0 || !inferredShare) return {};
        if (!std::isfinite(*inferredShare)) throw std::logic_error("Invalid inferred TPP share.");
        return ReceivedTpp{fp, *inferredShare, 1,
            samplingVariance(fp, fp / (1 + std::exp(-*inferredShare / 25))),
            fp / (1 + std::exp(-*inferredShare / 25))};
    }
    double total = optionalPreferential ? tcp : std::max(fp, tcp);
    if (total > tcp) {
        if (!pendingFirst || !std::isfinite(*pendingFirst) || *pendingFirst < 0 || *pendingFirst > total - tcp)
            throw std::logic_error("Missing or invalid pending-preference TPP allocation.");
        first += *pendingFirst;
    }
    if (!std::isfinite(first) || first < 0 || first > total)
        throw std::logic_error("Invalid received TPP count.");
    return ReceivedTpp{total, 25 * std::log((first + .25) / (total - first + .25)),
        std::clamp(1 - tcp / total, 0., 1.), samplingVariance(total, first),
        total > tcp ? *pendingFirst : 0};
}

// Prepare an inferred observation for comparison with the recentered historical
// booth baseline. The seat prior and the FP-to-TPP conversion can have different
// starting levels: for example, generic preferences can overestimate both the
// previous and current TPP in a non-classic seat. Moving only the historical
// comparison would turn that shared estimation error into an apparent new swing.
// Apply the same seat offset to its current inferred counterpart.
//
// result is a detached received-vote comparison, not the stored source record or
// the physical booth's projected vote account. shift is the common seat offset
// in 25*log-odds units: positive raises Labor's comparison TPP. It is calculated
// from historical booth/category comparisons, expected final vote sizes and the
// pre-count seat prior, not learned from current preference-flow discrepancies.
// Those preference corrections have already been applied by the caller.
//
// Actual TCP stays fixed. A mixed account moves only the inferred allocation;
// a wholly FP-derived account moves its entire comparison share. This assumes
// the historical conversion discrepancy also affects current estimates. The
// offset can additionally reflect changed category composition or missing
// matches, so correct historical classification and comparisons are important.
inline ReceivedTpp alignInferredReference(ReceivedTpp result, double shift) {
    double inferredVotes = result.votes * result.inferredFraction;
    if (inferredVotes <= 0 || shift == 0) return result;
    // Retain the established endpoint bound for inferred allocations only.
    // It keeps the log-odds calculation finite at zero/all inferred votes;
    // genuine measured zeros and fully measured accounts never enter this path.
    double fraction = std::clamp(result.inferredFirst / inferredVotes, 1e-8, 1-1e-8);
    double adjustedFirst = inferredVotes / (1 + std::exp(-(
        std::log(fraction/(1-fraction)) + shift/25)));
    if (result.inferredFraction == 1) {
        result.transformedShare += shift;
        result.samplingVariance = samplingVariance(result.votes, adjustedFirst);
    }
    else {
        // Recover the received count before receivedComparison's quarter-vote
        // endpoint smoothing, replace only its inferred part, then apply the
        // same smoothing. The measured candidate totals are thereby preserved.
        double first = (result.votes+.5)/(1+std::exp(-result.transformedShare/25))-.25;
        first += adjustedFirst-result.inferredFirst;
        result.transformedShare = 25*std::log((first+.25)/(result.votes-first+.25));
        result.samplingVariance = samplingVariance(result.votes, first);
    }
    result.inferredFirst = adjustedFirst;
    return result;
}

struct Moments {
    Vector mean;
    Matrix covariance;
};

// Only sums of centred products survive the baseline. Storage is quadratic in
// the number of seats, independent of iterations and replay length. The caller
// uses the simulation's existing recording mutex, once per accepted iteration.
class Accumulator {
    std::size_t count = 0;
    Vector mean;
    Matrix products;
public:
    void add(Vector const& sample) {
        if (count == 0) {
            mean.assign(sample.size(), 0);
            products.assign(sample.size(), Vector(sample.size(), 0));
        }
        if (sample.size() != mean.size()) throw std::logic_error("TPP baseline dimension changed.");
        Vector delta(sample.size());
        ++count;
        for (std::size_t i = 0; i < sample.size(); ++i) {
            if (!std::isfinite(sample[i])) throw std::logic_error("Invalid TPP baseline draw.");
            delta[i] = sample[i] - mean[i];
            mean[i] += delta[i] / count;
        }
        double scale = double(count - 1) / count;
        for (std::size_t i = 0; i < sample.size(); ++i)
            for (std::size_t j = 0; j <= i; ++j)
                products[i][j] += scale * delta[i] * delta[j];
    }
    Moments finish() const {
        Moments result{mean, products};
        for (std::size_t i = 0; i < mean.size(); ++i)
            for (std::size_t j = 0; j <= i; ++j)
                result.covariance[i][j] = result.covariance[j][i] = products[i][j] / std::max(std::size_t(1), count);
        return result;
    }
};

struct Observation {
    // h maps seat totals and category contrasts to this received category's
    // comparison. Variance is residual reporting-location uncertainty, before
    // any confidence phase-in. Category contrasts belong in h; conversion
    // uncertainty has local covariance and explicit shared party-transfer states.
    Vector h;
    double value = 0, variance = 0;
    int seat = -1, category = 0; // Diagnostic identities, not matrix positions.
    // Private comparisons divide pending FP by their received comparison weight.
    // Unified diagnostics divide pending FP by the expected category population;
    // actual component error scales have already been combined at that stage.
    double inferredFraction = 0, conversionVariance = 0;
    double localConversionSd = 0, sharedConversionSd = 0;
    // Local party components are separate so a Greens-heavy category does not
    // acquire the uncertainty of an unrelated independent-heavy category.
    double greensLocalConversionSd = 0, otherLocalConversionSd = 0;
    // Separate population-transfer errors survive abundant unrelated classic
    // evidence. Fractions are unresolved Greens/independent FP per received vote.
    double greensPreferenceFraction = 0, independentPreferenceFraction = 0;
    double greensTransferSd = 0, independentTransferSd = 0;
    double localMatchedUnits = 0, sharedMatchedUnits = 0;
    // The group's statistical role, not an assertion that every contributing
    // ballot is inferred. Partial booths allocate information to both groups.
    bool fpDerivedGroup = false;
    // Published observations contain one physical vote account per category.
    // Source groups remain private comparisons for its unreported portion.
    bool unifiedCategory = false;
    double receivedVotes = 0, receivedFirstVotes = 0, pendingPreferenceVotes = 0;
    double remainingVotes = 0, categoryEstimate = 0, remainingEstimate = 0;
    double measuredFutureWeight = 0, inferredFutureWeight = 0;
    double syntheticBaselineSd = 0; // Same-category comparison error, retained at completion.
    // Private comparisons use the quality-weighted unresolved preference pool.
    // Unified rows report received unresolved preferences per expected category
    // vote; their scales also retain future-prediction conversion uncertainty.
    double unresolvedPreferenceFraction = 0, conversionJacobian = 1;
    double comparisonWeight = 0; // Diagnostic averaging weight, not new votes.
    double sparseSampleVariance = 0, sampleVotes = 0, sampleCoverage = 0;
    // sampleVotes/sampleCoverage describe this source group's contribution.
    // The sparse allowance instead uses the combined usable sample below,
    // before dividing its information between measured and FP-derived groups.
    // Votes are received continuing votes, capped at each location's target;
    // coverage is a fraction of the category's expected continuing votes.
    double categorySampleVotes = 0, categorySampleCoverage = 0;
    // Unlike physical sample coverage, this fraction includes historical
    // comparison-quality discounts. It controls the completion multiplier.
    double categoryComparisonCoverage = 0;
    // Physical FP coverage is diagnostic. It does not promote TCP comparison
    // quality. The former completion-boost fields remain zero in unified rows
    // so older archive readers can continue reading their existing schema.
    double categoryFpCoverage = 0, comparisonQualityFloor = 0;
    double unfadedLocationVariance = 0, retainedSmallSampleVariance = 0;
    // A completed small category still gives a weak comparison for movement
    // elsewhere. These are variances in (25*log odds)^2: the unscaled location
    // comparison, its vote-volume floor, and the extra variance actually kept.
    double comparisonVariance = 0, comparisonVarianceFloor = 0, retainedComparisonVariance = 0;
    // Fraction of comparison weight restored by completion, measured against
    // this snapshot before its quality floor. It is not additional counted votes.
    double restoredComparisonFraction = 0, compensationReportingOrderSd = 0;
};
struct Step {
    Observation observation;
    Vector gain;
};
struct Update {
    Moments prior, posterior;
    std::vector<Step> steps;
};

inline double dot(Vector const& a, Vector const& b) {
    if (a.size() != b.size()) throw std::logic_error("TPP vector dimension mismatch.");
    double result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) result += a[i] * b[i];
    return result;
}

inline Update prepare(Moments prior, std::vector<Observation> const& observations) {
    auto n = prior.mean.size();
    if (prior.covariance.size() != n) throw std::logic_error("Missing TPP prior covariance.");
    for (auto const& row : prior.covariance) {
        if (row.size() != n) throw std::logic_error("Invalid TPP prior covariance shape.");
        for (double value : row)
            if (!std::isfinite(value)) throw std::logic_error("Invalid TPP prior covariance.");
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isfinite(prior.mean[i]) || prior.covariance[i][i] < 0)
            throw std::logic_error("Invalid TPP prior moment.");
    Update result{prior, prior, {}};
    // Scalar conditional updates avoid a large observation-matrix inverse.
    // The residuals are independent; the shared factors remain IN the state.
    // This gives the same Gaussian posterior as conditioning on all rows at once.
    for (auto const& observation : observations) {
        if (observation.h.size() != n || !std::isfinite(observation.value)
            || !std::isfinite(observation.variance) || observation.variance <= 0)
            throw std::logic_error("Invalid TPP observation.");
        for (double value : observation.h)
            if (!std::isfinite(value)) throw std::logic_error("Invalid TPP observation exposure.");
        Vector cross(n);
        for (std::size_t i = 0; i < n; ++i) cross[i] = dot(result.posterior.covariance[i], observation.h);
        double variance = dot(observation.h, cross) + observation.variance;
        if (!std::isfinite(variance) || variance <= 0) throw std::logic_error("Invalid TPP innovation variance.");
        Vector gain(n);
        double residual = observation.value - dot(observation.h, result.posterior.mean);
        for (std::size_t i = 0; i < n; ++i) {
            gain[i] = cross[i] / variance;
            result.posterior.mean[i] += gain[i] * residual;
            for (std::size_t j = 0; j <= i; ++j) {
                double value = result.posterior.covariance[i][j] - cross[i] * cross[j] / variance;
                result.posterior.covariance[i][j] = result.posterior.covariance[j][i] = value;
            }
        }
        result.steps.push_back({observation, std::move(gain)});
    }
    return result;
}

inline Update prepareWithExternalFactors(Moments prior,
    std::vector<Observation> const& observations, std::size_t start) {
    // Conversion factors are learned from matched FP/TPP accounts separately.
    // Condition seats/categories for each possible factor value, then retain
    // uncertainty across those values. Seat-prior disagreement cannot change
    // the factor distribution. Ordinary joint factors still condition normally.
    auto n = prior.mean.size();
    if (start > n) throw std::logic_error("Invalid external TPP factor boundary.");
    if (prior.covariance.size() != n) throw std::logic_error("Missing external TPP covariance.");
    for (std::size_t i = 0; i < n; ++i) {
        if (prior.covariance[i].size() != n) throw std::logic_error("Invalid external TPP covariance shape.");
        for (double value : prior.covariance[i])
            if (!std::isfinite(value)) throw std::logic_error("Invalid external TPP covariance.");
        if (prior.covariance[i][i] < 0) throw std::logic_error("Negative external TPP variance.");
    }
    auto conditional = prior;
    for (std::size_t i = 0; i < n; ++i) {
        if (prior.covariance.at(i).size() != n)
            throw std::logic_error("Invalid external TPP covariance shape.");
        for (std::size_t j = start; j < n; ++j) {
            if (i < start && (prior.covariance[i][j] != 0 || prior.covariance[j][i] != 0))
                throw std::logic_error("External TPP factors must start independently.");
            conditional.covariance[i][j] = conditional.covariance[j][i] = 0;
        }
    }
    auto result = prepare(std::move(conditional), observations);
    // The scenario path applies these same gains while leaving the external
    // factors fixed. Propagate their sensitivity to obtain matching diagnostics;
    // adding independent noise afterwards would lose cross-seat correlations.
    Matrix sensitivity(n, Vector(n-start));
    for (std::size_t j = start; j < n; ++j) sensitivity[j][j-start] = 1;
    for (auto const& step : result.steps) {
        Vector exposure(n-start);
        for (std::size_t j = 0; j < exposure.size(); ++j)
            for (std::size_t i = 0; i < n; ++i)
                exposure[j] += step.observation.h[i]*sensitivity[i][j];
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < exposure.size(); ++j)
                sensitivity[i][j] -= step.gain[i]*exposure[j];
    }
    for (std::size_t i = 0; i < n; ++i) for (std::size_t j = 0; j <= i; ++j) {
        double extra = 0;
        for (std::size_t a = start; a < n; ++a) for (std::size_t b = start; b < n; ++b)
            extra += sensitivity[i][a-start]*prior.covariance[a][b]*sensitivity[j][b-start];
        result.posterior.covariance[i][j] += extra;
        result.posterior.covariance[j][i] = result.posterior.covariance[i][j];
    }
    result.prior = std::move(prior);
    return result;
}

template<class Normal>
Vector condition(Update const& update, Vector state, Normal normal) {
    if (state.size() != update.prior.mean.size()) throw std::logic_error("Invalid TPP scenario dimension.");
    // A conditional draw, not a new independent Gaussian seat forecast: retain
    // the actual correlated pre-election draw and correct its innovations.
    // Perturbed observations supply exactly the residual posterior variance.
    for (std::size_t o = 0; o < update.steps.size(); ++o) {
        auto const& step = update.steps[o];
        double residual = step.observation.value - dot(step.observation.h, state)
            - std::sqrt(step.observation.variance) * normal(o);
        for (std::size_t i = 0; i < state.size(); ++i) state[i] += step.gain[i] * residual;
    }
    return state;
}

// Count coverage and location coverage separately. A large booth can supply
// many votes without sampling many locations; a collection of tiny booths can
// cover many locations without representing much of the electorate. Partial
// counts contribute fractions of a location, not a completed-booth flag.
struct ReportingProgress {
    double expectedVotes = 0, fpVotes = 0, tcpVotes = 0;
    double expectedLocations = 0, fpLocations = 0, tcpLocations = 0;
    void add(double target, double received, double inferredFraction, double contribution = 1) {
        if (target <= 0) return;
        double votes = std::min(target, received)*contribution;
        expectedVotes += target;
        fpVotes += votes; tcpVotes += votes*(1-inferredFraction);
        expectedLocations += 1;
        fpLocations += votes/target; tcpLocations += votes/target*(1-inferredFraction);
    }
    void include(ReportingProgress const& other) {
        expectedVotes += other.expectedVotes; fpVotes += other.fpVotes; tcpVotes += other.tcpVotes;
        expectedLocations += other.expectedLocations;
        fpLocations += other.fpLocations; tcpLocations += other.tcpLocations;
    }
    double coverage(bool measured) const {
        if (expectedVotes <= 0 || expectedLocations <= 0) return 0;
        return std::clamp(std::min((measured ? tcpVotes : fpVotes)/expectedVotes,
            (measured ? tcpLocations : fpLocations)/expectedLocations), 0., 1.);
    }
};

// Collapse comparable reporting locations into a cumulative observation.
// The caller partitions each location's information between measured-TCP and
// FP-derived groups within each seat/category. More votes in one location do
// not create more independent locations; absent records supply no observation.
// A smooth scale for extrapolating small received samples, not a count gate.
// Chosen against early checkpoint response; it is a working calibration, not
// an estimate of how many votes make a sample geographically representative.
constexpr double SparseSampleVoteScale = 5000;
// Good historical matches retain their usual weight. Once most category FP
// votes are known, unmatched locations smoothly regain that same weight too.
// Flattening both ends avoids jumps when the last small locations report.
constexpr double ComparisonCompletionStart = .75;

inline double comparisonQualityFloor(double coverage) {
    if (!std::isfinite(coverage)) throw std::logic_error("Invalid category FP coverage.");
    double t = std::clamp((coverage-ComparisonCompletionStart)/(1-ComparisonCompletionStart),0.,1.);
    return t*t*(3-2*t);
}

inline double retainSmallCategoryVariance(double revised, double original, double receivedVotes) {
    if (!std::isfinite(revised) || !std::isfinite(original) || !std::isfinite(receivedVotes) ||
        revised < 0 || original < 0 || receivedVotes < 0)
        throw std::logic_error("Invalid small-category TPP variance.");
    // A hospital category may finish with only a few ballots. Its votes can
    // receive normal aggregation weight without becoming a precise measure
    // of the district's or election's general swing. Reuse the established
    // absolute sample scale; broad samples rapidly release this allowance.
    double retention = SparseSampleVoteScale/(SparseSampleVoteScale+receivedVotes);
    return std::max(revised,original*retention*retention);
}

struct CategoryEvidence {
    ReportingProgress reporting;
    double expected = 0, weight = 0, squaredWeight = 0, weightedDeviation = 0;
    double weightedSquaredDeviation = 0, residualVarianceSum = 0, inferredWeight = 0;
    // Count-scale comparison sums. They retain the full target's historical
    // composition while estimating change from the locations actually received.
    // Summing transformed shares would not reproduce a completed category.
    double baselineFirst = 0, weightedObservedShare = 0, weightedBaselineShare = 0;
    double matchedPreferenceUnits = 0;
    double unresolvedPreferenceWeight = 0;
    double greensPreferenceWeight = 0, independentPreferenceWeight = 0;
    void add(double target, double received, double quality, double deviation,
        double inferredFraction, double locationSd, std::optional<double> sampleVariance = {},
        double contribution = 1) {
        if (!std::isfinite(target) || target < 0 || !std::isfinite(received) || received < 0
            || !std::isfinite(quality) || quality < 0 || quality > 1
            || !std::isfinite(inferredFraction) || inferredFraction < 0 || inferredFraction > 1
            || !std::isfinite(locationSd) || locationSd < 0 || !std::isfinite(deviation)
            || !std::isfinite(contribution) || contribution < 0 || contribution > 1)
            throw std::logic_error("Invalid TPP category evidence.");
        if (sampleVariance && (!std::isfinite(*sampleVariance) || *sampleVariance < 0))
            throw std::logic_error("Invalid TPP sampling variance.");
        expected += target;
        reporting.add(target, received, inferredFraction, contribution);
        double fullWeight = std::min(target, received) * quality;
        double w = fullWeight*contribution;
        if (w == 0) return;
        weight += w;
        // FP observes the reporting sample too. Its uncertain preferences are
        // accounted for separately by the explicit conversion covariance;
        // doubling sampling/scatter variance as well would discount it twice.
        // Partition information, not just the mean weight. Using w*w here
        // would duplicate the same booth's precision in both groups. Instead
        // each group receives its fraction of the original variance numerator;
        // their combined precision cannot exceed the unsplit booth's budget.
        double allocatedSquaredWeight = fullWeight*fullWeight*contribution;
        squaredWeight += allocatedSquaredWeight;
        weightedDeviation += w * deviation;
        weightedSquaredDeviation += w * deviation * deviation;
        inferredWeight += w * inferredFraction;
        // Local comparison uncertainty plus a finite-sample allowance. At
        // 50:50, 2500/n is binomial variance in percentage points; this term
        // prevents a tiny booth from masquerading as a precise full location.
        residualVarianceSum += allocatedSquaredWeight * (locationSd * locationSd
            + sampleVariance.value_or(2500 / std::max(1., received)));
    }
    double heterogeneity() const {
        return weight > 0 ? std::max(0., weightedSquaredDeviation / weight
            - std::pow(weightedDeviation / weight, 2)) : 0;
    }
    double comparisonVariance(double scatterFloor = 0) const {
        if (weight <= 0) throw std::logic_error("Empty TPP category observation.");
        double scatter = std::max(scatterFloor, heterogeneity());
        // Independent locations, their observed disagreement and the existing
        // location/sample scales determine how broadly this comparison can be
        // applied. More votes at one location do not create extra locations.
        // Keep this quantity before the completion multiplier: finishing a
        // hospital/mobile/prison category establishes its own result, but does
        // not establish a precise swing transferable to other divisions.
        return std::max(residualVarianceSum, scatter * squaredWeight) / (weight * weight);
    }
    // Fraction of the expected vote weight supplying a usable comparison,
    // after historical-quality and FP comparison-weight discounts.
    double comparisonCoverage() const {
        return expected > 0 ? std::clamp(weight / expected, 0., 1.) : 0;
    }
    // The two source groups retain their own comparison variance: each may
    // contain different locations and disagree by a different amount. However,
    // counting progress belongs to their combined usable sample in this seat
    // and voting category. wholeCategory is that sample before the information
    // split, including targets for locations not yet reported. Pass this same
    // object for an unsplit observation. Combining the two groups' expected
    // totals would count the same unreported population twice.
    double variance(CategoryEvidence const& wholeCategory, double scatterFloor = 0) const {
        double coverage = wholeCategory.comparisonCoverage();
        // Completion removes uncertainty about unreported locations. FP-derived
        // TPP retains separate conversion error. locationVariance() also retains
        // comparison uncertainty, distinct from whether more votes remain.
        // 0.01 transformed points SD is numerical regularisation, not a gate.
        return std::max(.0001, (1 - coverage) * comparisonVariance(scatterFloor));
    }
    double sampleCoverage() const {
        return expected > 0 ? std::clamp(reporting.fpVotes / expected, 0., 1.) : 0;
    }
    double sparseSampleVariance(double locationVariance, CategoryEvidence const& wholeCategory) const {
        // Normally a broad sample already has enough precision, and this
        // allowance quickly becomes small. For a handful of completed tiny
        // booths, averaging weights otherwise cancel out and allow their
        // movement to inform an entire district and correlated national swing.
        // Inflate only sampling/location variance, not category contrasts or
        // conversion uncertainty. Use the combined usable count before quality
        // discounts or division between source groups: uncertain preferences
        // do not erase the sample's geographic coverage. For example, dividing
        // 3,000 received votes equally leaves a 3,000-vote sample for both
        // groups' sparse multipliers, not two independent 1,500-vote samples.
        // Whole categories without a usable comparison cannot supply swing
        // evidence; their counted votes enter the separate direct-vote account.
        if (weight <= 0 || !std::isfinite(locationVariance) || locationVariance < 0)
            throw std::logic_error("Invalid sparse TPP sample.");
        double ratio = SparseSampleVoteScale * (1 - wholeCategory.sampleCoverage())
            / std::max(1., wholeCategory.reporting.fpVotes);
        return locationVariance * ratio * ratio;
    }
    double locationVariance(CategoryEvidence const& wholeCategory, double scatterFloor = 0) const {
        double residual = variance(wholeCategory, scatterFloor);
        // The original sparse-reporting allowance still controls early
        // reactions. A second, non-vanishing comparison floor prevents category
        // completion from making a tiny sample authoritative elsewhere. Reuse
        // the existing 5,000-vote retention scale rather than introducing a new
        // cutoff or election-specific setting. Broad samples release this floor
        // smoothly; effective location count and scatter still set its size.
        // This floor also uses the combined physical sample, so splitting its
        // information cannot make the retention stronger. The group's own
        // comparison variance still limits what its locations can teach us.
        return retainSmallCategoryVariance(residual+sparseSampleVariance(residual, wholeCategory),
            comparisonVariance(scatterFloor),wholeCategory.reporting.fpVotes);
    }
};
} // namespace LiveTppIntegration
