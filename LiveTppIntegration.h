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
using Vector = std::vector<double>;
using Matrix = std::vector<Vector>;

struct ReceivedTpp {
    double votes = 0, transformedShare = 0, inferredFraction = 0;
    double samplingVariance = 0;
    // Candidate-one votes inferred from FP, excluding any measured TCP.
    double inferredFirst = 0;
};

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

inline ReceivedTpp alignInferredReference(ReceivedTpp result, double shift) {
    // FP-derived shares are matched estimates, so their historical reference
    // moves with the prior. Measured TCP is an absolute count and never moves.
    // In a partial count only the pending-preference allocation is translated.
    double inferred = result.votes * result.inferredFraction;
    if (inferred <= 0 || shift == 0) return result;
    double fraction = std::clamp(result.inferredFirst / inferred, 1e-8, 1-1e-8);
    double adjusted = inferred / (1 + std::exp(-(std::log(fraction/(1-fraction)) + shift/25)));
    if (result.inferredFraction == 1) {
        result.transformedShare += shift;
        result.samplingVariance = samplingVariance(result.votes, adjusted);
    }
    else {
        // Undo receivedComparison's quarter-vote smoothing before replacing
        // the inferred component, then restore the identical endpoint rule.
        double first = (result.votes+.5)/(1+std::exp(-result.transformedShare/25))-.25;
        first += adjusted-result.inferredFirst;
        result.transformedShare = 25*std::log((first+.25)/(result.votes-first+.25));
        result.samplingVariance = samplingVariance(result.votes, first);
    }
    result.inferredFirst = adjusted;
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
    // uncertainty is integrated into the within-seat observation covariance.
    Vector h;
    double value = 0, variance = 0;
    int seat = -1, category = 0; // Diagnostic identities, not matrix positions.
    double inferredFraction = 0, conversionVariance = 0; // Raw-row diagnostics only.
    double localConversionSd = 0, sharedConversionSd = 0;
    double localMatchedUnits = 0, sharedMatchedUnits = 0;
    bool inferredLocations = false;
    // Quality-weighted fraction of comparison votes still requiring an
    // estimated preference transfer, and conversion from share points to z.
    double unresolvedPreferenceFraction = 0, conversionJacobian = 1;
    double comparisonWeight = 0; // Diagnostic averaging weight, not new votes.
    double sparseSampleVariance = 0, sampleVotes = 0, sampleCoverage = 0;
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
    void add(double target, double received, double inferredFraction) {
        if (target <= 0) return;
        double votes = std::min(target, received);
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
// The caller keeps wholly measured and conversion-dependent locations in
// separate groups within each seat/category. More votes in one location do
// not create more independent locations; absent records supply no observation.
// A smooth scale for extrapolating small received samples, not a count gate.
// Chosen against early checkpoint response; it is a working calibration, not
// an estimate of how many votes make a sample geographically representative.
constexpr double SparseSampleVoteScale = 5000;

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
    void add(double target, double received, double quality, double deviation,
        double inferredFraction, double locationSd, std::optional<double> sampleVariance = {}) {
        if (!std::isfinite(target) || target < 0 || !std::isfinite(received) || received < 0
            || !std::isfinite(quality) || quality < 0 || quality > 1
            || !std::isfinite(inferredFraction) || inferredFraction < 0 || inferredFraction > 1
            || !std::isfinite(locationSd) || locationSd < 0 || !std::isfinite(deviation))
            throw std::logic_error("Invalid TPP category evidence.");
        if (sampleVariance && (!std::isfinite(*sampleVariance) || *sampleVariance < 0))
            throw std::logic_error("Invalid TPP sampling variance.");
        expected += target;
        reporting.add(target, received, inferredFraction);
        double w = std::min(target, received) * quality;
        if (w == 0) return;
        weight += w;
        // FP observes the reporting sample too. Its uncertain preferences are
        // accounted for separately by the explicit conversion covariance;
        // doubling sampling/scatter variance as well would discount it twice.
        squaredWeight += w * w;
        weightedDeviation += w * deviation;
        weightedSquaredDeviation += w * deviation * deviation;
        inferredWeight += w * inferredFraction;
        // Local comparison uncertainty plus a finite-sample allowance. At
        // 50:50, 2500/n is binomial variance in percentage points; this term
        // prevents a tiny booth from masquerading as a precise full location.
        residualVarianceSum += w * w * (locationSd * locationSd
            + sampleVariance.value_or(2500 / std::max(1., received)));
    }
    double heterogeneity() const {
        return weight > 0 ? std::max(0., weightedSquaredDeviation / weight
            - std::pow(weightedDeviation / weight, 2)) : 0;
    }
    double variance(double scatterFloor = 0) const {
        if (weight <= 0) throw std::logic_error("Empty TPP category observation.");
        double coverage = expected > 0 ? std::clamp(weight / expected, 0., 1.) : 0;
        double scatter = std::max(scatterFloor, heterogeneity());
        // Completion removes uncertainty about unreported locations. FP-derived
        // TPP still has its separate shared conversion error at full FP count.
        // 0.01 transformed points SD is numerical regularisation, not a gate.
        return std::max(.0001, (1 - coverage) *
            std::max(residualVarianceSum, scatter * squaredWeight) / (weight * weight));
    }
    double sampleCoverage() const {
        return expected > 0 ? std::clamp(reporting.fpVotes / expected, 0., 1.) : 0;
    }
    double sparseSampleVariance(double locationVariance) const {
        // Normally a broad sample already has enough precision, and this
        // allowance quickly becomes small. For a handful of completed tiny
        // booths, averaging weights otherwise cancel out and allow their
        // movement to inform an entire district and correlated national swing.
        // Inflate only sampling/location variance, not category contrasts or
        // conversion uncertainty. Use received votes before the FP discount:
        // uncertain preferences do not erase the sample's geographic coverage.
        if (weight <= 0 || !std::isfinite(locationVariance) || locationVariance < 0)
            throw std::logic_error("Invalid sparse TPP sample.");
        double ratio = SparseSampleVoteScale * (1 - sampleCoverage())
            / std::max(1., reporting.fpVotes);
        return locationVariance * ratio * ratio;
    }
};
} // namespace LiveTppIntegration
