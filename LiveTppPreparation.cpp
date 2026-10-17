#include "LiveV2.h"
#include "Simulation.h"
#include "PollingProject.h"
#include "LiveFpIntegration.h"
#include "SimulationRun.h"

#include <algorithm>
#include <numeric>

namespace {
// Ordinary locations are the reference. A category contrast is its departure
// from the ordinary swing, not its historic partisan lean (already removed by
// the booth's previous-election comparison). Declaration types remain distinct.
int categoryCode(LiveV2::Booth const& booth) {
    if (booth.voteType != Results2::VoteType::Ordinary) return 100 + int(booth.voteType);
    return int(booth.boothType);
}

constexpr std::uint32_t CategoryDrawTag = 70, ObservationDrawTag = 71;

double transformedCounts(std::map<int, float> const& votes) {
    double total = 0;
    for (auto const& [id, count] : votes) total += count;
    if (total <= 0) return 0;
    // A quarter-vote smoothing allowance handles possible zero endpoints only.
    return 25 * std::log((votes.at(0) + .25) / (total - votes.at(0) + .25));
}
}

void LiveV2::Election::prepareTppIntegration() {
    // Each snapshot starts from the same pre-election joint distribution and
    // replaces the cumulative observations. No posterior is fed back as a new
    // prior on the next snapshot, so unchanged ballots are never counted twice.
    identifyTppIntegrationSeats();
    prepareTppCategoryAccounts();
    prepareTppReportingOrder();
    auto prior = tppJointPrior();
    tppPriorExtension = std::make_shared<LiveTppComposition::Extension const>(
        LiveTppComposition::prepareExtension(prior, sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()));
    auto observations = tppJointObservations();
    tppIntegration = std::make_shared<LiveTppIntegration::Update const>(
        LiveTppIntegration::prepare(std::move(prior), observations));
}

void LiveV2::Election::identifyTppIntegrationSeats() {
    auto const& baseline = sim.getLiveBaselineReport().value();
    if (baseline.seatName.size() != std::size_t(project.seats().count()))
        throw std::runtime_error("TPP baseline district configuration changed.");
    for (int p = 0; p < project.seats().count(); ++p)
        if (baseline.seatName[p] != project.seats().viewByIndex(p).name)
            throw std::runtime_error("TPP baseline district order changed.");
    tppProjectSeatIndices.clear();
    std::set<int> categories;
    for (auto const& seat : seats) {
        auto found = std::find(baseline.seatName.begin(), baseline.seatName.end(), seat.name);
        if (found == baseline.seatName.end()) throw std::runtime_error("Missing TPP baseline district: " + seat.name);
        tppProjectSeatIndices.push_back(int(found - baseline.seatName.begin()));
        for (int index : seat.booths)
            if (turnoutFpTarget(index) > 0 && categoryCode(booths[index]) != 0)
                categories.insert(categoryCode(booths[index]));
    }
    tppCategoryCodes.assign(categories.begin(), categories.end());
}

void LiveV2::Election::prepareTppCategoryAccounts() {
    // Historical booth/category leans are recentred using current target
    // sizes, on the vote scale. The weighted total remains the existing seat
    // prior: growth in early voting is not assigned a directional swing.
    std::vector<TppCategoryAccount> accounts;
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    for (std::size_t s = 0; s < seats.size(); ++s) {
        TppCategoryAccount prepared;
        double priorShare = LiveTppComposition::share(baseline.mean.at(tppProjectSeatIndices[s]));
        prepared.referenceShift = tppSeatReferenceShift(int(s), priorShare);
        prepared.evidence = tppCategoryEvidence(int(s), prepared.referenceShift);
        // Keep wholly measured locations separate from those still needing
        // conversion. New FP-only booths must not dilute TCP already received.
        for (int group = 0; group < 2; ++group)
            prepared.observationEvidence[group] = tppCategoryEvidence(int(s), prepared.referenceShift, group != 0);
        LiveTppIntegration::Vector shares, amounts;
        for (auto const& [code, evidence] : prepared.evidence) {
            prepared.codes.push_back(code);
            shares.push_back(evidence.baselineFirst/evidence.expected);
            amounts.push_back(evidence.expected);
        }
        if (!shares.empty()) prepared.account = LiveTppComposition::centre(shares, amounts,
            priorShare);
        accounts.push_back(std::move(prepared));
    }
    tppCategoryAccounts = std::make_shared<std::vector<TppCategoryAccount> const>(std::move(accounts));
}

LiveTppIntegration::Moments LiveV2::Election::tppJointPrior() {
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    auto n = baseline.mean.size();
    if (n != std::size_t(project.seats().count()) || baseline.covariance.size() != n)
        throw std::runtime_error("Missing joint TPP baseline; regenerate the live baseline.");

    auto covariance = LiveTppComposition::categoryCovariance(tppCategoryCodes, run.regionCode, false, std::stoi(run.yearCode));
    auto mixtures = tppReferenceMixtures();
    auto result = LiveTppComposition::jointPrior(baseline, covariance, mixtures);
    tppCompositionBudgetScale = result.budgetScale;
    return LiveTppComposition::withReportingOrder(std::move(result.moments));
}

void LiveV2::Election::prepareTppReportingOrder() {
    // Use the whole current ordinary account, including unreported districts.
    // Global coverage avoids interpreting district coverage differences as a
    // geographic swing. Each snapshot rebuilds this; no replay memory is used.
    tppOrdinaryReportingProgress = {};
    for (auto const& prepared : *tppCategoryAccounts) {
        auto ordinary = prepared.evidence.find(0);
        if (ordinary != prepared.evidence.end())
            tppOrdinaryReportingProgress.include(ordinary->second.reporting);
    }
}

LiveTppIntegration::Matrix LiveV2::Election::tppReferenceMixtures() const {
    // Keep a COMMON reference mixture, rather than implicitly regressing
    // district forecast errors on their postal fractions. The difference from
    // that reference shares covariance with the seat prior; the prior's own
    // covariance is unchanged. Ordinary-only differences cannot then identify
    // a postal effect just because postal sizes vary geographically.
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    auto n = baseline.mean.size(), k = tppCategoryCodes.size();
    LiveTppIntegration::Matrix mixtures(n, LiveTppIntegration::Vector(k));
    LiveTppIntegration::Vector seatWeights(n), loadings(n, 1);
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto const& prepared = tppCategoryAccounts->at(s);
        auto p = tppProjectSeatIndices[s];
        loadings[p] = prepared.account.seatLoading;
        for (std::size_t c = 0; c < k; ++c) {
            auto found = std::find(prepared.codes.begin(), prepared.codes.end(), tppCategoryCodes[c]);
            if (found != prepared.codes.end()) mixtures[p][c] =
                prepared.account.contrastWeights[found-prepared.codes.begin()];
        }
        for (auto const& [code, evidence] : prepared.evidence) seatWeights[p] += evidence.expected;
        // The derivative converts an overall transformed-share movement back
        // to votes. This makes the reference's composition offset sum to zero
        // in the local linearisation used by the joint update.
        seatWeights[p] *= LiveTppComposition::derivative(LiveTppComposition::share(baseline.mean[p]))/loadings[p];
    }
    return LiveTppComposition::referenceMixtures(std::move(mixtures), seatWeights, loadings);
}

double LiveV2::Election::tppBoothTarget(int index) const {
    if (!optionalPreferential) return turnoutFpTarget(index);
    // OPV accounts use continuing votes, not exhausted formal ballots.
    double expected = 0;
    for (auto const& [party, votes] : booths[index].node.tppVotesProjected) expected += votes;
    return expected;
}

double LiveV2::Election::tppBoothBaseline(int index) const {
    auto const& booth = booths[index];
    auto const& n = booth.node;
    auto const& seat = seats.at(booth.parentSeatId);
    return n.tppSharePrevious
        ? *n.tppSharePrevious + seat.node.tppSwingBaseline.value_or(0)
        : seat.node.tppShareBaseline.value_or(0);
}

double LiveV2::Election::tppSeatReferenceShift(int seatIndex, double priorShare) const {
    // Align at the reporting-unit level BEFORE comparing evidence. Usually
    // this reconciles modest category-size/redistribution differences. Where
    // previous TPP is synthetic (a non-classic contest), its absolute level can
    // be much further from the configured seat history. Both the comparison
    // baseline and FP-derived estimate must use the same reference; actual TCP
    // retains its recorded level. No shift is a new counted observation.
    LiveTppIntegration::Vector shares, amounts;
    for (int index : seats.at(seatIndex).booths) {
        double expected = tppBoothTarget(index);
        if (expected <= 0) continue;
        shares.push_back(LiveTppComposition::share(tppBoothBaseline(index)));
        amounts.push_back(expected);
    }
    if (shares.empty()) return 0;
    auto aligned = LiveTppComposition::centre(shares, amounts, priorShare);
    return LiveTppComposition::transform(aligned.shares.front())-LiveTppComposition::transform(shares.front());
}

std::map<int, LiveTppIntegration::CategoryEvidence>
LiveV2::Election::tppCategoryEvidence(int seatIndex, double referenceShift,
    std::optional<bool> inferredLocations) const {
    // Each physical reporting unit contributes once to its published category.
    // The per-booth helper owns partial-preference and comparison-quality rules.
    std::map<int, LiveTppIntegration::CategoryEvidence> categories;
    for (int index : seats.at(seatIndex).booths)
        appendTppBoothEvidence(index, referenceShift, categories, inferredLocations);
    return categories;
}

double LiveV2::Election::tppBoothMatchedPreferenceUnit(int index) const {
    auto const& booth = booths[index];
    auto const& n = booth.node;
    // Reuse the eligibility of the existing correction: comparable current
    // FP/TCP, usable historical flows and nonzero reliability. An old stored
    // partial-count cohort alone cannot establish a current FP/TCP match.
    if (!n.preferenceFlowDeviation || n.preferenceFlowConfidence <= 0) return 0;
    int coalition = n.tcpVotesCurrent.contains(natPartyIndex) ? natPartyIndex : 1;
    auto current = LivePartialCount::measureTransfer(n.fpVotesCurrent, n.tcpVotesCurrent, 0, coalition);
    auto historical = historicalPreferenceTransfer(booth);
    if (!current || !historical) return 0;
    double reliability = n.relevanceModifier * LivePartialCount::historicalFlowReliability(historical->share());
    return LiveTppComposition::matchedPreferenceUnit(current->votes, reliability);
}

void LiveV2::Election::appendTppBoothEvidence(int index, double referenceShift,
    std::map<int, LiveTppIntegration::CategoryEvidence>& categories,
    std::optional<bool> inferredLocations) const {
    auto const& booth = booths[index];
    auto const& n = booth.node;
    double expected = tppBoothTarget(index);
    if (expected <= 0) return; // Explicit closures/inactive services carry no weight.
    auto& evidence = categories[categoryCode(booth)];
    double baseline = tppBoothBaseline(index) + referenceShift;
    double baselineShare = LiveTppComposition::share(baseline);
    evidence.baselineFirst += expected*baselineShare;
    if (!inferredLocations) evidence.matchedPreferenceUnits += tppBoothMatchedPreferenceUnit(index);
    double fp = n.totalFpVotesCurrent();
    double tcp = LiveEstimatedTpp::observedTppVotes(n.tcpVotesCurrent, natPartyIndex);
    auto received = LiveTppIntegration::receivedComparison(fp, tcp,
        tcp > 0 ? n.tcpVotesCurrent.at(0) : 0,
        n.tppShare, rawPendingFirstVotes(booth), optionalPreferential);
    if (!received || (inferredLocations && *inferredLocations != (received->inferredFraction > 0))) {
        // Both subsets compare with the SAME whole-category historical mix.
        // Locations outside the subset add targets, not invented observations.
        evidence.add(expected, 0, 0, 0, 0, 0);
        return;
    }
    *received = LiveTppIntegration::alignInferredReference(*received, referenceShift);
    // Only received votes enter this estimate. A first partial TCP retains
    // the pending-FP allocation used by the forecast, not an extrapolation
    // of its possibly unrepresentative small TCP batch. Under OPV the FP/TCP
    // gap can be exhaustion, so use the observed continuing-vote share.
    // Matched booths compare with their historical lean; genuinely new
    // locations use the seat baseline with reduced comparison quality.
    double quality = n.relevanceModifier * (n.tppSharePrevious ? 1 : .1);
    // Retain the existing comparison-weight convention, but no additional
    // blanket variance multiplier. Explicit conversion uncertainty now owns
    // the precision difference; reporting order also covers measured TCP.
    quality *= 1 - .5 * received->inferredFraction;
    // Existing unreported-booth TPP scale, now used as reporting-location
    // uncertainty before any progress gate. Heterogeneous observed swings
    // can increase this allowance; votes in one booth do not create breadth.
    double locationSd = 4.5 + 7 * std::exp(-expected * .0001);
    evidence.add(expected, received->votes, quality, received->transformedShare - baseline,
        received->inferredFraction, locationSd, received->samplingVariance);
    double comparisonWeight = std::min(expected, received->votes)*quality;
    // A measured TPP supplies no unresolved preferences. Partial classic TCP
    // removes its covered fraction smoothly; FP-only non-classic seats retain
    // their full non-major pool. Use the same weights as the comparison itself.
    evidence.unresolvedPreferenceWeight += comparisonWeight*received->inferredFraction
        * LiveTppComposition::preferencePoolFraction(n.fpVotesCurrent, natPartyIndex);
    evidence.weightedObservedShare += comparisonWeight*LiveTppComposition::share(received->transformedShare);
    evidence.weightedBaselineShare += comparisonWeight*baselineShare;
}

double LiveV2::Election::TppCategoryAccount::expectedVotes() const {
    // Unreported categories retain their expected size in the precision budget.
    double total = 0;
    for (auto const& [code, category] : evidence) total += category.expected;
    return total;
}

double LiveV2::Election::tppExpectedVotes() const {
    double total = 0;
    for (auto const& account : *tppCategoryAccounts) total += account.expectedVotes();
    return total;
}

std::map<int, double> LiveV2::Election::tppMatchedPreferenceUnits() const {
    std::map<int, double> result;
    for (auto const& account : *tppCategoryAccounts)
        for (auto const& [code, evidence] : account.evidence)
            result[code] += evidence.matchedPreferenceUnits;
    return result;
}

double LiveV2::Election::tppPooledPreferenceUnits() const {
    double units = 0;
    for (auto const& account : *tppCategoryAccounts)
        units += LiveTppComposition::pooledPreferenceUnits(account.evidence);
    return units;
}

std::optional<double> LiveV2::Election::tppInferredEvidencePercent() const {
    // Use the observation's actual comparison weights: this is the inferred
    // share of received evidence, not a percentage of all expected ballots.
    double received = 0, inferred = 0;
    for (auto const& account : *tppCategoryAccounts)
        for (auto const& [code, evidence] : account.evidence) {
            received += evidence.weight;
            inferred += evidence.inferredWeight;
        }
    if (received <= 0) return {};
    return 100 * inferred / received;
}

std::vector<LiveTppIntegration::Observation> LiveV2::Election::tppJointObservations() {
    // Every district supplies a small correlated block. Retain readable raw
    // rows for diagnostics, and condition on decorrelated numerical rows.
    std::vector<LiveTppIntegration::Observation> observations, received;
    // Allocate conversion precision by the entire expected election account,
    // not by which seats happen to have reported. Newly reporting districts
    // must not take precision away from previously received observations.
    double expected = tppExpectedVotes();
    double matchedUnits = tppPooledPreferenceUnits();
    for (std::size_t s = 0; s < seats.size(); ++s) {
        auto block = tppSeatObservations(int(s), expected, matchedUnits);
        for (std::size_t c = 0; c < block.rows.size(); ++c) {
            auto raw = block.rows[c]; raw.variance = block.covariance[c][c];
            received.push_back(std::move(raw));
        }
        auto independent = LiveTppComposition::decorrelate(std::move(block.rows), block.covariance);
        observations.insert(observations.end(), independent.begin(), independent.end());
    }
    tppReceivedObservations = std::make_shared<std::vector<LiveTppIntegration::Observation> const>(std::move(received));
    return observations;
}

LiveTppComposition::ObservationBlock LiveV2::Election::tppSeatObservations(int s, double electionExpected,
    double matchedUnits) const {
    auto const& prepared = tppCategoryAccounts->at(s);
    if (prepared.codes.empty()) return {};
    auto [rows, localExposures] = tppSeatObservationRows(s, matchedUnits);
    auto block = LiveTppComposition::observationBlock(std::move(rows), localExposures,
        LiveTppComposition::categoryCovariance(prepared.codes, run.regionCode, true, std::stoi(run.yearCode)));
    LiveTppComposition::allowForConversionUncertainty(block, prepared.expectedVotes()/electionExpected);
    return block;
}

std::pair<std::vector<LiveTppIntegration::Observation>, LiveTppIntegration::Matrix>
LiveV2::Election::tppSeatObservationRows(int s, double matchedUnits) const {
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    auto n = baseline.mean.size();
    auto orderStart = n + tppCategoryCodes.size();
    auto dimension = orderStart + LiveTppComposition::ReportingOrderFactors;
    auto const& prepared = tppCategoryAccounts->at(s);
    if (prepared.codes.empty()) return {};
    auto const& account = prepared.account;
    auto p = tppProjectSeatIndices[s];
    std::vector<LiveTppIntegration::Observation> rows;
    LiveTppIntegration::Matrix localExposures;
    for (int group = 0; group < 2; ++group) for (std::size_t c = 0; c < prepared.codes.size(); ++c) {
        auto code = prepared.codes[c];
        auto const& evidence = prepared.observationEvidence[group].at(code);
        auto const& combined = prepared.evidence.at(code);
        if (evidence.weight <= 0) continue;
        LiveTppIntegration::Observation observation;
        observation.seat = p;
        observation.category = code;
        observation.inferredLocations = group != 0;
        observation.localMatchedUnits = LiveTppComposition::pooledPreferenceUnits(prepared.evidence);
        observation.sharedMatchedUnits = matchedUnits;
        observation.h.assign(dimension, 0);
        observation.h[p] = account.seatLoading;
        LiveTppComposition::applyReportingOrder(observation, tppOrdinaryReportingProgress, orderStart);
        // Derivatives of the count-scale centred account. Weighted category
        // effects cancel; weighting raw log odds would not have this property.
        LiveTppIntegration::Vector local(prepared.codes.size());
        for (std::size_t d = 0; d < prepared.codes.size(); ++d) {
            local[d] = (c == d ? 1. : 0.)-account.contrastWeights[d];
            auto found = std::find(tppCategoryCodes.begin(), tppCategoryCodes.end(), prepared.codes[d]);
            if (found != tppCategoryCodes.end()) observation.h[n+(found-tppCategoryCodes.begin())] = local[d];
        }
        observation.inferredFraction = evidence.inferredWeight / evidence.weight;
        double observed = LiveTppComposition::observedShare(evidence);
        observation.unresolvedPreferenceFraction = evidence.unresolvedPreferenceWeight/evidence.weight;
        observation.comparisonWeight = evidence.weight;
        observation.conversionJacobian = LiveTppComposition::conversionShareJacobian(observed);
        observation.value = account.seatLoading*baseline.mean[p] + LiveTppComposition::transform(observed)
            - LiveTppComposition::transform(account.shares[c]);
        // Preserve scatter across both subsets. Separating their means must
        // not hide heterogeneity when early FP and TCP locations differ.
        observation.variance = evidence.variance(combined.heterogeneity());
        // Small samples must not bypass the district uncertainty just because
        // their own booth counts are complete. This enters the observation
        // covariance before conditioning, so mean and draw precision agree.
        observation.sampleVotes = evidence.reporting.fpVotes;
        observation.sampleCoverage = evidence.sampleCoverage();
        observation.sparseSampleVariance = evidence.sparseSampleVariance(observation.variance);
        observation.variance += observation.sparseSampleVariance;
        rows.push_back(std::move(observation));
        localExposures.push_back(std::move(local));
    }
    return {std::move(rows), std::move(localExposures)};
}

double LiveV2::Election::tppScenarioCountChange(int seatIndex) const {
    // Composition conditioning is prepared at mean turnout. Preserve separate
    // sampled turnout/category-size and preference-rechecking effects. Neither
    // includes the old additive TPP composition noise, which is replaced above.
    return transformedCounts(seats.at(seatIndex).node.tppVotesProjected)
        - transformedCounts(turnout->mean(LiveData::CountKind::Tpp, seatIndex));
}

float LiveV2::Election::preparedTppShare(int seatIndex) const {
    // Results' estimated swing follows the same prepared centre as the new
    // simulator handoff. Keep the older composition centre only during setup,
    // before the joint state exists; the raw-swing column is independent.
    auto const& projected = turnout->mean(LiveData::CountKind::Tpp, seatIndex);
    double transformed = tppIntegration
        ? tppIntegration->posterior.mean.at(tppProjectSeatIndices.at(seatIndex))
        : transformedCounts(projected);
    double share = detransformVoteShare(float(transformed));
    auto account = LiveTurnoutMath::partitionAccount(
        {&turnout->counted(LiveData::CountKind::Tpp, seatIndex), &projected}, [](int id) { return id; });
    return LiveFpIntegration::constrainToCounted(account, {{0, share}, {1, 100 - share}}).at(0);
}

std::optional<std::vector<double>> LiveV2::Election::integrateTpp(std::vector<double> const& prior) const {
    if (!tppIntegration) throw std::runtime_error("Missing prepared joint TPP integration.");
    if (tppIntegration->steps.empty()) return prior;
    auto state = prior;
    auto n = sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size();
    if (prior.size() != n) throw std::runtime_error("Joint TPP handoff seat mismatch.");
    if (!tppPriorExtension) throw std::logic_error("Missing TPP composition draw preparation.");
    state = LiveTppComposition::extend(*tppPriorExtension, std::move(state), [&](std::size_t i) {
        return variabilityNormal(0, 1, int(i), 0, CategoryDrawTag);
    });
    state = LiveTppIntegration::condition(*tppIntegration, std::move(state), [&](std::size_t observation) {
        return variabilityNormal(0, 1, int(observation), 0, ObservationDrawTag);
    });
    state.resize(n);
    for (std::size_t s = 0; s < seats.size(); ++s)
        state[tppProjectSeatIndices[s]] += tppScenarioCountChange(int(s));
    return state;
}
