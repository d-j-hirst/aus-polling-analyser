#include "LivePreferenceCorrections.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

namespace LivePreferenceCorrections {
namespace {
using Vector = std::array<double, 4>;
using Matrix = std::array<Vector, 4>;

// Shared coefficients fitted offline to Federal 2025 revisions after 20 May,
// following comparisons that omitted each test seat from parameter training.
// The stronger directed response is retained; separate background rechecks
// restore uncertainty near the regression without imposing a movement towards
// it. These values are fixed in live operation, and the regression itself uses
// only current counts. See docs/live-turnout.md for the four components.
constexpr double RoutineChance = .5327639742322045;
constexpr double LogRoutineScale = .08737413053901756;
constexpr double RoutineSizePower = .339131984114037;
constexpr double BackgroundChanceAtReference = .10534213368051161;
constexpr double BackgroundChanceSizeEffect = .39602184615771785;
constexpr double LogAdditionalBackgroundScale = 2.329939422870341;
constexpr double BackgroundSizePower = .5629780364803446;
constexpr double BroadIntercept = -7.809508178103534;
constexpr double BroadDiscrepancy = 3.20705363776961;
constexpr double BroadSize = .520532187574425;
constexpr double MinimumFraction = .8093269894532144;
constexpr double LogBroadScatter = -.22719181582595513;

double logistic(double value) {
    return value >= 0 ? 1 / (1 + std::exp(-value))
        : std::exp(value) / (1 + std::exp(value));
}

double transformed(double count, double parent) {
    return std::log((count + .25) / (parent - count + .25));
}

double dot(Vector const& a, Vector const& b) {
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.);
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    auto n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

double weightedMedian(std::vector<double> const& values, std::vector<double> const& weights) {
    std::vector<std::size_t> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return values[a] < values[b]; });
    double const half = std::accumulate(weights.begin(), weights.end(), 0.) / 2;
    double sum = 0;
    for (auto i : order) {
        sum += weights[i];
        if (sum >= half) return values[i];
    }
    return values[order.back()];
}

Matrix inverse(Matrix const& matrix) {
    // Four predictors keep the regression small. Partial pivoting and a ridge
    // on the three slopes protect seats whose booths have similar composition.
    std::array<std::array<double, 8>, 4> work{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) work[i][j] = matrix[i][j];
        work[i][i + 4] = 1;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row)
            if (std::abs(work[row][column]) > std::abs(work[pivot][column])) pivot = row;
        std::swap(work[column], work[pivot]);
        double divisor = work[column][column];
        if (!std::isfinite(divisor) || divisor == 0)
            throw std::runtime_error("Singular preference correction regression.");
        for (double& value : work[column]) value /= divisor;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            double const factor = work[row][column];
            for (std::size_t j = 0; j < 8; ++j) work[row][j] -= factor * work[column][j];
        }
    }
    Matrix result{};
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 4; ++j) result[i][j] = work[i][j + 4];
    return result;
}

struct Fit { Vector coefficients{}; Matrix covariance{}; double scatter = .05; };

Fit fitWithout(std::vector<Vector> const& x, std::vector<double> const& y,
    std::vector<double> const& baseWeights, std::size_t omitted) {
    // Each target is absent from every fit and scatter calculation. Smooth
    // Cauchy weights prevent an erroneous peer from dragging the seat trend.
    auto weights = baseWeights;
    weights[omitted] = 0;
    Fit fit;
    for (int iteration = 0; iteration < 12; ++iteration) {
        Matrix matrix{};
        Vector right{};
        for (std::size_t j = 1; j < 4; ++j) matrix[j][j] = .1;
        for (std::size_t i = 0; i < x.size(); ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                right[j] += weights[i] * x[i][j] * y[i];
                for (std::size_t k = 0; k < 4; ++k) matrix[j][k] += weights[i] * x[i][j] * x[i][k];
            }
        }
        fit.covariance = inverse(matrix);
        for (std::size_t j = 0; j < 4; ++j) fit.coefficients[j] = dot(fit.covariance[j], right);
        std::vector<double> residuals;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (i != omitted) residuals.push_back(y[i] - dot(x[i], fit.coefficients));
        double const centre = median(residuals);
        auto deviations = residuals;
        for (double& value : deviations) value = std::abs(value - centre);
        fit.scatter = std::max(.05, 1.4826 * median(deviations));
        for (std::size_t i = 0; i < x.size(); ++i) {
            double const relative = (y[i] - dot(x[i], fit.coefficients)) / (2 * fit.scatter);
            weights[i] = i == omitted ? 0 : baseWeights[i] / (1 + relative * relative);
        }
    }
    return fit;
}

double sample(Mixture const& mixture, std::mt19937_64& engine,
    std::student_t_distribution<double>& student) {
    // Student-t(5) scale here is the experiment's raw t scale, not an SD.
    // The exact unchanged branch avoids turning possible observed zeros into
    // forced positive counts. Other outcomes stay inside the preference pool.
    double const choice = std::generate_canonical<double, 53>(engine);
    double location = mixture.original, scale = mixture.routineScale;
    if (choice < mixture.broadProbability) {
        location = mixture.broadCentre;
        scale = mixture.broadScale;
    }
    else if (choice < mixture.broadProbability + mixture.backgroundProbability) {
        // A larger recheck is possible even on an ordinary-looking booth.
        // Keep its centre at the reported flow, rather than making that
        // possibility itself an expectation of movement to the regression.
        scale = mixture.backgroundScale;
    }
    else if (choice >= mixture.broadProbability + mixture.backgroundProbability + mixture.routineProbability) return 0;
    return mixture.pool * logistic(location + scale * student(engine)) - mixture.gain;
}

// 32-point Gauss-Legendre quadrature in Student-t(5) cumulative probability.
// Deterministic moment calculations select the large contributors and provide
// diagnostics; actual scenario draws are not limited to these quadrature nodes.
constexpr std::array<double, 32> StudentNodes = {
    -5.4903271556713262, -3.6736504600378113, -2.8640811288609482, -2.355745747987056,
    -1.9867564200286068, -1.696099201822127, -1.4546646307599875, -1.246428678639929,
    -1.0616596561819844, -.89399668524462272, -.7390296194798035, -.59354220971094018,
    -.45507780172821954, -.32167363648972647, -.19168801652802586, -.063680171785204881,
    .063680171785204881, .19168801652802586, .3216736364897263, .45507780172821977,
    .59354220971094018, .73902961947980339, .89399668524462272, 1.0616596561819844,
    1.246428678639929, 1.4546646307599871, 1.6960992018221275, 1.9867564200286068,
    2.355745747987056, 2.8640811288609482, 3.6736504600378113, 5.4903271556713262
};
constexpr std::array<double, 32> QuadratureWeights = {
    .003509305004735, .008137197365453, .012696032654631, .017136931456511,
    .021417949011113, .025499029631188, .029342046739268, .032911111388181,
    .036172897054424, .039096947893535, .041655962113474, .043826046502202,
    .045586939347882, .046922199540402, .047819360039637, .048270044257364,
    .048270044257364, .047819360039637, .046922199540402, .045586939347882,
    .043826046502202, .041655962113474, .039096947893535, .036172897054424,
    .032911111388181, .029342046739268, .025499029631188, .021417949011113,
    .017136931456511, .012696032654631, .008137197365453, .003509305004735
};

void moments(Mixture& mixture) {
    for (std::size_t i = 0; i < StudentNodes.size(); ++i) {
        double const routine = mixture.pool * logistic(mixture.original + mixture.routineScale * StudentNodes[i]) - mixture.gain;
        double const background = mixture.pool * logistic(mixture.original + mixture.backgroundScale * StudentNodes[i]) - mixture.gain;
        double const broad = mixture.pool * logistic(mixture.broadCentre + mixture.broadScale * StudentNodes[i]) - mixture.gain;
        mixture.expectedRevision += QuadratureWeights[i] * (mixture.routineProbability * routine
            + mixture.backgroundProbability * background + mixture.broadProbability * broad);
        mixture.revisionSecondMoment += QuadratureWeights[i] * (mixture.routineProbability * routine * routine
            + mixture.backgroundProbability * background * background + mixture.broadProbability * broad * broad);
    }
}
} // namespace

std::vector<Mixture> prepare(std::vector<Observation> const& observations) {
    // Preference share is within the non-finalists' FP pool. Primary share has
    // all formal votes as its parent, and minor-party mix has the preference
    // pool as its parent. They are regression predictors, not interchangeable
    // percentages. A quarter vote permits genuine behavioural zeros.
    std::vector<Vector> x;
    std::vector<double> y, weights;
    for (auto const& row : observations) {
        double const pool = row.formal - row.fpA - row.fpB;
        if (!std::isfinite(row.formal) || !std::isfinite(row.fpA) || !std::isfinite(row.fpB)
            || !std::isfinite(row.gainA) || !std::isfinite(row.otherParty)
            || !(pool > 0) || row.fpA < 0 || row.fpB < 0 || row.gainA < 0 || row.gainA > pool
            || row.otherParty < 0 || row.otherParty > pool)
            throw std::runtime_error("Invalid preference correction parent counts.");
        x.push_back({1, transformed(row.fpA, row.formal), transformed(row.otherParty, pool), row.ppvc ? 1. : 0.});
        y.push_back(transformed(row.gainA, pool));
        weights.push_back(pool / (pool + 100));
    }
    std::vector<Mixture> result;
    for (std::size_t i = 0; i < observations.size(); ++i) {
        auto const& row = observations[i];
        Mixture mixture;
        mixture.booth = row.booth;
        mixture.pool = row.formal - row.fpA - row.fpB;
        mixture.gain = row.gainA;
        mixture.original = mixture.broadCentre = mixture.prediction = y[i];
        double const logSize = std::log(row.formal / 1000);
        double const share = logistic(y[i]);
        // Fit recheck magnitudes in votes, then map them into the bounded
        // preference share. Background scale adds to the routine allowance;
        // neither is a standard deviation or a hard count limit. Its chance
        // depends smoothly on size and remains available at zero discrepancy.
        double const routineVotes = std::exp(LogRoutineScale + RoutineSizePower * logSize);
        double const backgroundVotes = routineVotes
            + std::exp(LogAdditionalBackgroundScale + BackgroundSizePower * logSize);
        double const countToLogOdds = mixture.pool * share * (1 - share);
        mixture.routineScale = std::log1p(routineVotes / countToLogOdds);
        mixture.backgroundScale = std::log1p(backgroundVotes / countToLogOdds);
        double const backgroundChance = logistic(std::log(BackgroundChanceAtReference / (1 - BackgroundChanceAtReference))
            + BackgroundChanceSizeEffect * logSize);
        mixture.peers = observations.size() - 1;
        if (observations.size() > 1) {
            auto const fit = fitWithout(x, y, weights, i);
            mixture.prediction = dot(x[i], fit.coefficients);
            // Peer scatter responds smoothly to comparable booth sizes. Sparse
            // local comparisons shrink back towards the whole-seat scatter;
            // sampling and regression uncertainty are retained separately.
            std::vector<double> residuals, localWeights;
            for (std::size_t j = 0; j < observations.size(); ++j) {
                residuals.push_back(y[j] - dot(x[j], fit.coefficients));
                double const sizeDistance = std::log(observations[j].formal / row.formal) / 1.5;
                localWeights.push_back(j == i ? 0 : weights[j] * std::exp(-.5 * sizeDistance * sizeDistance));
            }
            double const centre = weightedMedian(residuals, localWeights);
            auto deviations = residuals;
            for (double& value : deviations) value = std::abs(value - centre);
            double const nearby = std::max(.05, 1.4826 * weightedMedian(deviations, localWeights));
            double const support = std::accumulate(localWeights.begin(), localWeights.end(), 0.);
            double const influence = support / (support + 8);
            mixture.scatter = std::sqrt(influence * nearby * nearby + (1 - influence) * fit.scatter * fit.scatter);
            Vector covarianceProduct{};
            for (std::size_t j = 0; j < 4; ++j) covarianceProduct[j] = dot(fit.covariance[j], x[i]);
            double const predictedShare = logistic(mixture.prediction);
            double const samplingVariance = 1 / (mixture.pool * predictedShare * logistic(-mixture.prediction));
            double const fittingVariance = fit.scatter * fit.scatter * dot(x[i], covarianceProduct);
            double const gap = mixture.prediction - y[i];
            mixture.discrepancy = std::abs(gap) / std::sqrt(mixture.scatter * mixture.scatter + samplingVariance + std::max(0., fittingVariance));
            double const squared = mixture.discrepancy * mixture.discrepancy;
            // Small booths supply weaker evidence, but size must not impose a
            // ceiling on the correction probability for a compelling error.
            // Reducing the odds allows increasingly strong discrepancies to
            // overcome that penalty, with no booth-size or score cutoff.
            double const sizeOddsPenalty = std::log(-std::expm1(-row.formal / SizeResponseScale));
            mixture.broadProbability = logistic(BroadIntercept + BroadSize * logSize
                + BroadDiscrepancy * std::log1p(squared) + sizeOddsPenalty);
            mixture.fraction = MinimumFraction + (1 - MinimumFraction) * squared / (squared + 2.25);
            mixture.broadCentre = y[i] + mixture.fraction * gap;
            mixture.broadScale = std::exp(LogBroadScatter) * mixture.scatter;
        }
        // With no other measured booth, a seat trend cannot be estimated. It
        // still retains both reported-flow recheck components. For comparable
        // booths, select the directed component first, then background or
        // routine rechecking; the residual probability leaves counts unchanged.
        // Sparse regressions need no minimum booth-count cutoff: ridge and
        // fitting uncertainty already limit their influence.
        mixture.backgroundProbability = (1 - mixture.broadProbability) * backgroundChance;
        mixture.routineProbability = (1 - mixture.broadProbability) * (1 - backgroundChance) * RoutineChance;
        moments(mixture);
        result.push_back(mixture);
    }
    return result;
}

SeatDistribution compress(std::vector<Mixture> const& mixtures, std::uint64_t seed) {
    // Preserve the few largest possible contributors explicitly. This is a
    // distribution representation choice, not a cutoff on correction chances.
    // Each other booth is sampled independently before its revision is summed.
    SeatDistribution result;
    if (mixtures.empty()) return result;
    std::vector<std::size_t> order(mixtures.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return mixtures[a].revisionSecondMoment > mixtures[b].revisionSecondMoment;
    });
    auto const retained = std::min(ExplicitBooths, mixtures.size());
    for (std::size_t j = 0; j < retained; ++j) result.explicitBooths.push_back(mixtures[order[j]]);
    result.aggregateRevisions.assign(AggregateSamples, 0);
    std::mt19937_64 engine(seed);
    std::student_t_distribution<double> student(5);
    for (std::size_t j = retained; j < order.size(); ++j) {
        auto const& mixture = mixtures[order[j]];
        for (double& revision : result.aggregateRevisions) revision += sample(mixture, engine, student);
    }
    result.expectedRevision = std::accumulate(result.aggregateRevisions.begin(), result.aggregateRevisions.end(), 0.) / AggregateSamples;
    for (auto const& mixture : result.explicitBooths) result.expectedRevision += mixture.expectedRevision;
    return result;
}

double draw(SeatDistribution const& distribution, std::uint64_t seed) {
    // Main iterations choose one prepared aggregate and draw at most four
    // independent mixtures. Explicit Student-t draws retain continuous tails
    // beyond the short preparation bank and its finite percentile resolution.
    if (distribution.aggregateRevisions.empty()) return 0;
    std::mt19937_64 engine(seed);
    std::student_t_distribution<double> student(5);
    std::uniform_int_distribution<std::size_t> choose(0, distribution.aggregateRevisions.size() - 1);
    double revision = distribution.aggregateRevisions[choose(engine)];
    for (auto const& mixture : distribution.explicitBooths) revision += sample(mixture, engine, student);
    return revision;
}
} // namespace LivePreferenceCorrections
