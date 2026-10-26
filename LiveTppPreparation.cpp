#include "LiveV2.h"
#include "Simulation.h"
#include "PollingProject.h"
#include "LiveFpIntegration.h"
#include "SimulationRun.h"
#include "LiveSimulationMath.h"

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

struct PreferencePool {
    // Same confidence-weighted aggregation as the live preference hierarchy,
    // restricted to scalar preference evidence. Unreported seats stay in the
    // coverage denominator. No results or project objects are copied.
    double expected = 0, weight = 0, sum = 0;
    void add(double target, double relevance, double confidence, std::optional<float> deviation) {
        expected += target;
        if (!deviation) return;
        double w = target*relevance*confidence;
        weight += w; sum += w * *deviation;
    }
    double confidence() const { return expected > 0 ? weight/expected : 0; }
    std::optional<float> deviation() const {
        return weight > 0 ? std::optional<float>(float(sum/weight)) : std::nullopt;
    }
};

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
    prepareMatchedConversion();
    auto prior = tppJointPrior();
    tppPriorExtension = std::make_shared<LiveTppComposition::Extension const>(
        LiveTppComposition::prepareExtension(prior, sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()));
    auto observations = tppJointObservations();
    tppIntegration = std::make_shared<LiveTppIntegration::Update const>(
        LiveTppIntegration::prepareWithExternalFactors(std::move(prior), observations,
            sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()
                + tppCategoryCodes.size() + LiveTppComposition::ReportingOrderFactors));
}

double LiveV2::Election::heldOutPreferenceOffset(int excludedSeat) const {
    // A matched district must not train the broad correction used to assess
    // its own prediction. Recreate only the election/region flow hierarchy
    // with that district omitted. The tested seat and booth corrections are
    // also excluded; unobserved destination seats have neither available.
    std::vector<PreferencePool> regions(largeRegions.size());
    for (std::size_t s = 0; s < seats.size(); ++s) if (int(s) != excludedSeat) {
        auto const& seat = seats[s];
        auto const& n = seat.node;
        // Preparation normally already has turnout projections. Retain the
        // aggregation fallback for a historical or newly reported district.
        double target = n.totalFpVotesProjected();
        if (target <= 0) target = n.totalVotesPrevious() > 0
            ? n.totalVotesPrevious() : n.totalFpVotesCurrent();
        regions.at(seat.parentRegionIndex).add(target,
            n.relevanceModifier,n.preferenceFlowConfidence,n.preferenceFlowDeviation);
    }
    PreferencePool election;
    for (std::size_t r = 0; r < regions.size(); ++r)
        election.add(regions[r].expected,largeRegions[r].node.relevanceModifier,
            regions[r].confidence(),regions[r].deviation());
    constexpr float FlowConfidenceStrength = 200; // existing preference hierarchy
    double overall = election.deviation().value_or(0)*LiveSimulationMath::observationWeight(
        float(election.confidence()),FlowConfidenceStrength);
    auto const& region = regions.at(seats.at(excludedSeat).parentRegionIndex);
    double regional = region.deviation() ? (*region.deviation()-overall)*
        LiveSimulationMath::observationWeight(float(region.confidence()),FlowConfidenceStrength) : 0;
    return (overall+regional)/25;
}

LivePartyConversion::MatchedAccount LiveV2::Election::matchedConversionAccount(int seatIndex) const {
    LivePartyConversion::MatchedAccount account;
    double broadOffset = heldOutPreferenceOffset(seatIndex);
    for (int index : seats.at(seatIndex).booths) {
        auto const& booth = readBooths()[index];
        // Require the existing valid current FP/TPP match, not an earlier
        // partial cohort or a non-classic TCP. OPV is excluded by the caller.
        if (tppBoothMatchedPreferenceUnit(index) <= 0) continue;
        auto previous = historicalPreferenceTransfer(booth);
        if (!previous) continue;
        double quality = booth.node.relevanceModifier*
            LivePartialCount::historicalFlowReliability(previous->share());
        int coalition = coalitionFinalist(booth);
        double offset = preferenceRateOffset(booth,false).value_or(0)/25+broadOffset;
        account.add(booth.hasTcpRevision() ? booth.tcpBasis->fp : booth.node.fpVotesCurrent,booth.node.tcpVotesCurrent,
            classicPreferenceFlows(booth,coalition),offset,coalition,
            run.grnPartyIndex,IndependentPartyIdOffset,quality);
    }
    return account;
}

void LiveV2::Election::prepareMatchedConversion() {
    // Fit only errors remaining after the existing strength/history and broad
    // observed preference corrections. Actual TPP provides the target; the
    // pre-election seat TPP prior never enters this calibration.
    std::vector<LivePartyConversion::MatchedAccount> accounts(seats.size());
    if (!optionalPreferential)
        for (std::size_t s = 0; s < seats.size(); ++s) accounts[s] = matchedConversionAccount(int(s));
    tppTransferCalibration = std::make_shared<LivePartyConversion::Calibration const>(
        LivePartyConversion::calibrate(std::move(accounts),
            LiveTppComposition::GreensTransferFlowSd,LiveTppComposition::IndependentTransferFlowSd));
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
            if (turnoutFpTarget(index) > 0 && categoryCode(readBooths()[index]) != 0)
                categories.insert(categoryCode(readBooths()[index]));
    }
    tppCategoryCodes.assign(categories.begin(), categories.end());
}

void LiveV2::Election::prepareCategoryHistory(int seatIndex, Results2::Seat const& previousSeat,
    Results2::Election const& previousElection, Results2::Election const& currentElection) {
    // Retain the whole old category, including old locations with no current ID
    // match. Completion can then compare aggregate PPVC results even after moves.
    // A zero historical account supplies neither a comparison nor provenance.
    auto add = [&](int code, auto const& fp, auto const& tcp, bool synthetic) {
        std::map<int, float> primaries, preferences;
        for (auto const& [id, votes] : fp)
            primaries[mapPartyId(id,true,previousElection,currentElection)] += float(votes);
        for (auto const& [id, votes] : tcp)
            preferences[mapPartyId(id,true,previousElection,currentElection)] += float(votes);
        double first = 0, total = 0;
        bool classic = preferences.contains(0) && (preferences.contains(1) || preferences.contains(natPartyIndex));
        for (auto const& [party, votes] : preferences) if (classic) {
            total += votes; if (party == 0) first += votes;
        }
        if (total <= 0) {
            // Non-classic historical FP remains actual category composition;
            // ordinary conversion uncertainty is separate from invented category
            // allocations such as the historical SA declaration split.
            first = total = 0;
            for (auto const& [party, votes] : primaries) {
                double flow = party == 0 ? 100 : party == 1 || party == natPartyIndex ? 0 :
                    prevPreferenceOverrides.contains(party) ? prevPreferenceOverrides.at(party) :
                    preferenceFlowMap.contains(party) ? preferenceFlowMap.at(party) : preferenceFlowMap.at(-1);
                total += votes; first += votes*flow*.01;
            }
        }
        if (total <= 0) return;
        auto& history = seats.at(seatIndex).categoryHistory[code];
        history.first += first; history.total += total; history.synthetic |= synthetic;
    };
    for (auto const& [id, booth] : previousElection.booths)
        if (booth.parentSeat == previousSeat.id)
            add(int(booth.type),booth.fpVotes,booth.tcpVotesCandidate,booth.syntheticBaseline);
    std::map<Results2::VoteType,std::map<int,int>> fp, tcp;
    for (auto const& [candidate, types] : previousSeat.fpVotes)
        for (auto const& [type, votes] : types) if (type != Results2::VoteType::Ordinary) fp[type][candidate] += votes;
    for (auto const& [candidate, types] : previousSeat.tcpVotesCandidate)
        for (auto const& [type, votes] : types) if (type != Results2::VoteType::Ordinary) tcp[type][candidate] += votes;
    for (auto const& [type, counts] : fp)
        add(100+int(type),counts,tcp[type],previousSeat.syntheticBaselineTypes.contains(type));
}

void LiveV2::Election::prepareMatchedCategoryHistory(int seatIndex) {
    // New districts can inherit usable booth history even without an old seat
    // account. Prefer the full old category when it exists; otherwise retain
    // the real matched predecessor booths rather than calling them unsupported.
    std::map<int,LiveTppComposition::CategoryHistory> matchedHistory;
    for (int index : seats[seatIndex].booths) {
        auto const& booth = readBooths()[index];
        if (!booth.node.tppSharePrevious || booth.node.totalVotesPrevious() <= 0) continue;
        auto& history = matchedHistory[categoryCode(booth)];
        double votes = booth.node.totalVotesPrevious();
        history.total += votes;
        history.first += votes*LiveTppComposition::share(*booth.node.tppSharePrevious)*.01;
        history.synthetic |= booth.syntheticPreviousCategory;
    }
    for (auto const& [code,history] : matchedHistory)
        if (!seats[seatIndex].categoryHistory.contains(code)) seats[seatIndex].categoryHistory.emplace(code,history);
}

void LiveV2::Election::prepareTppCategoryAccounts() {
    std::vector<TppCategoryAccount> accounts;
    for (std::size_t s = 0; s < seats.size(); ++s)
        accounts.push_back(prepareTppCategoryAccount(int(s)));
    identifySyntheticTppCategories(accounts);
    tppCategoryAccounts = std::make_shared<std::vector<TppCategoryAccount> const>(std::move(accounts));
}

LiveV2::Election::TppCategoryAccount LiveV2::Election::prepareTppCategoryAccount(int seatIndex) {
    // Build comparisons and received-vote accounts separately. Their category
    // sizes share one centred prior, but only supported comparisons teach swing.
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    TppCategoryAccount prepared;
    prepareMatchedCategoryHistory(seatIndex);
    double priorShare = LiveTppComposition::share(baseline.mean.at(tppProjectSeatIndices[seatIndex]));
    prepared.referenceShift = tppSeatReferenceShift(seatIndex, priorShare);
    prepared.evidence = tppCategoryEvidence(seatIndex, prepared.referenceShift);
    // Divide each location's information between measured-TCP and FP-derived
    // groups. An FP-only booth must not dilute received TCP; a one-vote
    // recheck must not move an otherwise measured booth wholesale either.
    for (int group = 0; group < 2; ++group)
        prepared.observationEvidence[group] = tppCategoryEvidence(seatIndex, prepared.referenceShift, group == 0 ? LiveTppIntegration::EvidenceSourceGroup::MeasuredTcp : LiveTppIntegration::EvidenceSourceGroup::FpDerivedTpp);
    for (auto const& [code, evidence] : prepared.evidence) {
        auto found = seats[seatIndex].categoryHistory.find(code);
        prepared.sources[code] = found == seats[seatIndex].categoryHistory.end()
            ? LiveTppComposition::BaselineSource::Unavailable : found->second.source();
    }
    applyTppCategoryCompletion(seatIndex, prepared);
    prepareDirectTppVotes(seatIndex, prepared);
    LiveTppIntegration::Vector shares, amounts;
    for (auto const& [code, evidence] : prepared.evidence) {
        prepared.codes.push_back(code);
        shares.push_back(evidence.baselineFirst/evidence.expected);
        amounts.push_back(evidence.expected);
    }
    if (!shares.empty()) prepared.account = LiveTppComposition::centre(shares, amounts,
        priorShare);
    return prepared;
}

void LiveV2::Election::identifySyntheticTppCategories(std::vector<TppCategoryAccount> const& accounts) {
    std::set<int> synthetic;
    for (auto const& account : accounts) for (auto const& [code, source] : account.sources)
        if (source == LiveTppComposition::BaselineSource::Synthetic) synthetic.insert(code);
    tppSyntheticCategoryCodes.assign(synthetic.begin(),synthetic.end());
}

void LiveV2::Election::applyTppCategoryCompletion(int seatIndex, TppCategoryAccount& account) const {
    // Measure physical FP coverage before historical relevance or conversion
    // discounts. A few outstanding small booths then have only their expected
    // vote weight, rather than holding a near-complete category at low quality.
    for (int index : seats.at(seatIndex).booths) {
        // Use formal FP targets even under optional preferential voting;
        // exhausted ballots are still received FP, not unreported ballots.
        double target = turnoutFpTarget(index);
        if (target <= 0) continue; // Explicit closures remain outside the account.
        auto const& booth = readBooths()[index];
        account.fpProgress[categoryCode(booth)].add(target,booth.node.totalFpVotesCurrent(),0);
    }
    std::map<int,double> floors;
    for (auto const& [code,progress] : account.fpProgress) {
        double coverage = progress.fpVotes/progress.expectedVotes;
        double floor = LiveTppIntegration::comparisonQualityFloor(coverage);
        if (floor > 0 && account.sources.at(code) != LiveTppComposition::BaselineSource::Unavailable)
            floors.emplace(code,floor);
    }
    if (floors.empty()) return; // Keep the established sparse early-count path.

    // The same whole-category floor applies to both measured and FP-derived
    // subsets. Rebuild all weighted sums together: lifting only precision would
    // give extra authority to a mean still dominated by the old booth matches.
    account.unfadedEvidence = std::move(account.evidence);
    account.unfadedObservationEvidence = std::move(account.observationEvidence);
    account.evidence = tppCategoryEvidence(seatIndex,account.referenceShift,LiveTppIntegration::EvidenceSourceGroup::All,floors);
    for (int group = 0; group < 2; ++group)
        account.observationEvidence[group] = tppCategoryEvidence(
            seatIndex,account.referenceShift,group == 0 ? LiveTppIntegration::EvidenceSourceGroup::MeasuredTcp : LiveTppIntegration::EvidenceSourceGroup::FpDerivedTpp,floors);
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
    auto prior = LiveTppComposition::withPartyTransfer(
        LiveTppComposition::withReportingOrder(std::move(result.moments)));
    prior = LivePartyConversion::applyCalibration(std::move(prior), *tppTransferCalibration);
    return LiveTppComposition::withSyntheticBaselines(std::move(prior),tppSyntheticCategoryCodes.size());
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
    // Expected final continuing ballot count, in votes, for this preparation.
    // Under compulsory preferences this is the turnout model's mean FP target.
    // Under OPV it is the live projected TPP count after allowing for exhaustion.
    if (!optionalPreferential) return turnoutFpTarget(index);
    // OPV accounts use continuing votes, not exhausted formal ballots.
    double expected = 0;
    for (auto const& [party, votes] : readBooths()[index].node.tppVotesProjected) expected += votes;
    return expected;
}

double LiveV2::Election::tppBoothBaseline(int index) const {
    // Comparison expectation on the 25*log-odds scale (not a vote percentage).
    // A matched booth uses its previous TPP plus the seat's pre-count baseline
    // swing. Without previous booth TPP, use the seat's baseline absolute TPP.
    auto const& booth = readBooths()[index];
    auto const& n = booth.node;
    auto const& seat = seats.at(booth.parentSeatId);
    if (n.tppSharePrevious) return *n.tppSharePrevious + seat.node.tppSwingBaseline.value_or(0);
    auto found = seat.categoryHistory.find(categoryCode(booth));
    if (found != seat.categoryHistory.end() && found->second.total > 0)
        return LiveTppComposition::transform(100*found->second.first/found->second.total)
            + seat.node.tppSwingBaseline.value_or(0);
    return seat.node.tppShareBaseline.value_or(0);
}

double LiveV2::Election::tppSeatReferenceShift(int seatIndex, double priorShare) const {
    // Align at the reporting-unit level BEFORE comparing evidence. Usually
    // this reconciles modest category-size/redistribution differences. Where
    // previous TPP is synthetic (a non-classic contest), its absolute level can
    // be much further from the configured seat history. Align only that
    // comparison baseline. Received FP plus estimated preference transfers is
    // an absolute vote estimate, just as received TCP is an absolute count;
    // neither is translated to agree with the pre-election expectation.
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
    LiveTppIntegration::EvidenceSourceGroup sourceGroup, std::map<int,double> const& qualityFloors) const {
    // Each physical reporting unit contributes once to its published category.
    // The per-booth helper owns partial-preference and comparison-quality rules.
    std::map<int, LiveTppIntegration::CategoryEvidence> categories;
    for (int index : seats.at(seatIndex).booths)
        appendTppBoothEvidence(index, referenceShift, categories, sourceGroup, qualityFloors);
    return categories;
}

double LiveV2::Election::tppBoothMatchedPreferenceUnit(int index) const {
    auto const& booth = readBooths()[index];
    auto const& n = booth.node;
    // Reuse the eligibility of the existing correction: comparable current
    // FP/TCP, usable historical flows and nonzero reliability. An old stored
    // partial-count cohort alone cannot establish a current FP/TCP match.
    if (!n.preferenceFlowDeviation || n.preferenceFlowConfidence <= 0) return 0;
    int coalition = n.tcpVotesCurrent.contains(natPartyIndex) ? natPartyIndex : 1;
    auto const& matchedFp = booth.hasTcpRevision() ? booth.tcpBasis->fp : n.fpVotesCurrent;
    auto current = LivePartialCount::measureTransfer(matchedFp, n.tcpVotesCurrent, 0, coalition);
    auto historical = historicalPreferenceTransfer(booth);
    if (!current || !historical) return 0;
    double reliability = n.relevanceModifier * LivePartialCount::historicalFlowReliability(historical->share());
    return LiveTppComposition::matchedPreferenceUnit(current->votes, reliability);
}

void LiveV2::Election::appendTppBoothEvidence(int index, double referenceShift,
    std::map<int, LiveTppIntegration::CategoryEvidence>& categories,
    LiveTppIntegration::EvidenceSourceGroup sourceGroup, std::map<int,double> const& qualityFloors) const {
    // index identifies a prepared reporting unit (physical booth or declaration
    // service). readBooths() gives the immutable prepared records: recovered
    // current candidate counts, historical comparisons and live TPP estimates.
    // It does not read a feed file or use map coordinates here.
    //
    // referenceShift is a seat-wide offset in 25*log-odds units aligning booth
    // comparisons with the pre-count seat baseline. categories accumulates
    // evidence by voting category. sourceGroup selects all information or its
    // measured/FP-derived fraction. qualityFloors maps category codes to the
    // minimum comparison quality [0,1] justified by category FP completion.
    auto const& booth = readBooths()[index];
    auto const& n = booth.node;
    double expected = tppBoothTarget(index); // Expected final continuing votes, not a TPP share.
    if (expected <= 0) return; // Explicit closures/inactive services carry no weight.
    auto& evidence = categories[categoryCode(booth)];
    double baseline = tppBoothBaseline(index) + referenceShift; // Transformed comparison expectation.
    double baselineShare = LiveTppComposition::share(baseline); // Percentage, 0..100.
    evidence.baselineFirst += expected*baselineShare;
    if (sourceGroup == LiveTppIntegration::EvidenceSourceGroup::All) evidence.matchedPreferenceUnits += tppBoothMatchedPreferenceUnit(index);
    double fp = n.totalFpVotesCurrent();
    // The received account uses actual TCP and estimates missing preferences.
    // When FP was corrected underneath that TCP, it instead anchors to the
    // original matched batch and estimates only revised transfers. Source count
    // fields remain untouched, and n.tppShare supplies the FP-only fallback.
    auto received = receivedBoothTpp(booth);
    if (!received) {
        // Both groups compare with the SAME whole-category historical mix.
        // Unreported locations add targets, not invented observations.
        evidence.add(expected, 0, 0, 0, 0, 0);
        return;
    }
    // Split the information smoothly: seven unresolved votes out of 2,004
    // retain about 99.56% of measured-group weight; a 5% gap retains only 50%.
    // A substantial newly added batch need not resemble the earlier TCP. The
    // CategoryEvidence accumulator partitions its precision as well as weight.
    double contribution = LiveTppIntegration::evidenceGroupFraction(sourceGroup, received->inferredFraction);
    // Reference alignment belongs only to baseline above. Both FP-derived TPP
    // and pending-preference allocations already describe received ballots.
    // Retain their measured preference-flow corrections and uncertainty, but
    // never move their central estimates by the seat's prior-reference gap.
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
    if (auto floor = qualityFloors.find(categoryCode(booth)); floor != qualityFloors.end())
        quality = std::max(quality,floor->second);
    // Completion lifts historical comparison quality, not preference certainty.
    // Unresolved preferences retain their explicit conversion covariance below.
    // Existing unreported-booth TPP scale, now used as reporting-location
    // uncertainty before any progress gate. Heterogeneous observed swings
    // can increase this allowance; votes in one booth do not create breadth.
    double locationSd = 4.5 + 7 * std::exp(-expected * .0001);
    evidence.add(expected, received->votes, quality, received->transformedShare - baseline,
        received->inferredFraction, locationSd, received->samplingVariance, contribution);
    double comparisonWeight = std::min(expected, received->votes)*quality*contribution;
    // A measured TPP supplies no unresolved preferences. Partial classic TCP
    // removes its covered fraction smoothly; FP-only non-classic seats retain
    // their full non-major pool. Use the same weights as the comparison itself.
    evidence.unresolvedPreferenceWeight += comparisonWeight*received->inferredFraction
        * LiveTppComposition::preferencePoolFraction(n.fpVotesCurrent, natPartyIndex);
    // Raw independent identities remain separate from registered minor parties.
    // The shared factors cover possible common campaign-selection error, without
    // asserting that a rural independent has the same mean as an urban one.
    if (fp > 0) for (auto const& [party, votes] : n.fpVotesCurrent) {
        double weight = comparisonWeight*received->inferredFraction*votes/fp;
        if (party == run.grnPartyIndex) evidence.greensPreferenceWeight += weight;
        else if (party >= IndependentPartyIdOffset) evidence.independentPreferenceWeight += weight;
    }
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
            if (account.sources.at(code) == LiveTppComposition::BaselineSource::Unavailable) continue;
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
    auto transferStart = sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()
        + tppCategoryCodes.size() + LiveTppComposition::ReportingOrderFactors;
    LiveTppComposition::applyPartyTransfer(block, transferStart, tppTransferCalibration->factors.covariance);
    return block;
}

std::pair<std::vector<LiveTppIntegration::Observation>, LiveTppIntegration::Matrix>
LiveV2::Election::tppSeatObservationRows(int s, double matchedUnits) const {
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    auto n = baseline.mean.size();
    auto orderStart = n + tppCategoryCodes.size();
    auto syntheticStart = orderStart + LiveTppComposition::ReportingOrderFactors + LiveTppComposition::PartyTransferFactors;
    auto dimension = syntheticStart+tppSyntheticCategoryCodes.size();
    auto const& prepared = tppCategoryAccounts->at(s);
    if (prepared.codes.empty()) return {};
    auto const& account = prepared.account;
    auto p = tppProjectSeatIndices[s];
    std::vector<LiveTppIntegration::Observation> rows;
    LiveTppIntegration::Matrix localExposures;
    for (int group = 0; group < 2; ++group) for (std::size_t c = 0; c < prepared.codes.size(); ++c) {
        auto code = prepared.codes[c];
        auto source = prepared.sources.at(code);
        if (source == LiveTppComposition::BaselineSource::Unavailable) continue;
        auto const& evidence = prepared.observationEvidence[group].at(code);
        if (evidence.weight <= 0) continue;
        LiveTppIntegration::Observation observation;
        observation.seat = p;
        observation.category = code;
        if (source == LiveTppComposition::BaselineSource::Synthetic)
            observation.syntheticBaselineSd = LiveTppComposition::SyntheticBaselineSd;
        observation.fpDerivedGroup = group != 0;
        observation.localMatchedUnits = LiveTppComposition::pooledPreferenceUnits(prepared.evidence);
        observation.sharedMatchedUnits = matchedUnits;
        observation.h.assign(dimension, 0);
        observation.h[p] = account.seatLoading;
        if (source == LiveTppComposition::BaselineSource::Synthetic) {
            auto found = std::find(tppSyntheticCategoryCodes.begin(),tppSyntheticCategoryCodes.end(),code);
            observation.h[syntheticStart+(found-tppSyntheticCategoryCodes.begin())] = LiveTppComposition::SyntheticBaselineSharedSd;
        }
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
        observation.greensPreferenceFraction = evidence.greensPreferenceWeight/evidence.weight;
        observation.independentPreferenceFraction = evidence.independentPreferenceWeight/evidence.weight;
        observation.comparisonWeight = evidence.weight;
        observation.conversionJacobian = LiveTppComposition::conversionShareJacobian(observed);
        observation.value = account.seatLoading*baseline.mean[p] + LiveTppComposition::transform(observed)
            - LiveTppComposition::transform(account.shares[c]);
        applyTppObservationCoverage(prepared,group,code,observation);
        LiveTppComposition::applyReportingOrder(observation, tppOrdinaryReportingProgress, orderStart);
        rows.push_back(std::move(observation));
        localExposures.push_back(std::move(local));
    }
    return {std::move(rows), std::move(localExposures)};
}

void LiveV2::Election::applyTppObservationCoverage(TppCategoryAccount const& prepared,
    int group, int code, LiveTppIntegration::Observation& observation) const {
    auto const& evidence = prepared.observationEvidence[group].at(code);
    // Small samples must not bypass district uncertainty just because their
    // own counts are complete. Keep cross-subset scatter and the established
    // sparse-sample allowance in the covariance used for both means and draws.
    observation.variance = evidence.variance(prepared.evidence.at(code).heterogeneity());
    observation.sampleVotes = evidence.reporting.fpVotes;
    observation.sampleCoverage = evidence.sampleCoverage();
    observation.sparseSampleVariance = evidence.sparseSampleVariance(observation.variance);
    observation.variance += observation.sparseSampleVariance;

    auto const& progress = prepared.fpProgress.at(code);
    observation.categoryFpCoverage = progress.fpVotes/progress.expectedVotes;
    observation.comparisonQualityFloor = LiveTppIntegration::comparisonQualityFloor(observation.categoryFpCoverage);
    observation.unfadedLocationVariance = observation.variance;
    if (prepared.unfadedEvidence.empty()) return;
    auto const& original = prepared.unfadedObservationEvidence[group].at(code);
    observation.restoredComparisonFraction = std::clamp(1-original.weight/evidence.weight,0.,1.);
    observation.unfadedLocationVariance = original.locationVariance(
        prepared.unfadedEvidence.at(code).heterogeneity());
    double retained = LiveTppIntegration::retainSmallCategoryVariance(
        observation.variance,observation.unfadedLocationVariance,progress.fpVotes);
    observation.retainedSmallSampleVariance = retained-observation.variance;
    observation.variance = retained;
}

void LiveV2::Election::prepareDirectTppVotes(int seatIndex, TppCategoryAccount& account) const {
    double uncertaintyVotes = 0;
    for (int index : seats.at(seatIndex).booths) {
        auto const& booth = readBooths()[index];
        if (!account.sources.contains(categoryCode(booth)) ||
            account.sources.at(categoryCode(booth)) != LiveTppComposition::BaselineSource::Unavailable) continue;
        auto const& n = booth.node;
        double tcp = LiveEstimatedTpp::observedTppVotes(n.tcpVotesCurrent,natPartyIndex);
        auto received = receivedBoothTpp(booth);
        if (!received) continue;
        double measuredFirst = tcp > 0 ? n.tcpVotesCurrent.at(0) : 0;
        if (auto revised = revisedBoothTcp(booth,0,coalitionFinalist(booth))) {
            tcp = revised->votes-revised->uncertainVotes();
            measuredFirst = revised->lower;
        }
        double inferred = account.direct.add(*received,tcp,measuredFirst);
        // Received FP is useful locally even without a historical swing. Retain
        // the existing conservative preference-pool scale; assume correlated
        // conversion errors within this seat rather than averaging tiny units.
        double pool = LiveTppComposition::preferencePoolFraction(n.fpVotesCurrent,natPartyIndex);
        uncertaintyVotes += inferred*std::sqrt(LiveTppComposition::ConversionPoolVariance*pool);
    }
    if (account.direct.inferredVotes > 0)
        account.direct.conversionVariance = std::pow(uncertaintyVotes/account.direct.inferredVotes,2);
}

double LiveV2::Election::accountedTpp(int seatIndex, double transformed, bool sample) const {
    auto const& account = tppCategoryAccounts->at(seatIndex);
    if (account.direct.votes <= 0) return transformed;
    double inferred = account.direct.inferredShare();
    if (sample && account.direct.inferredVotes > 0) {
        double sd = std::sqrt(account.direct.conversionVariance)*LiveTppComposition::conversionShareJacobian(inferred);
        inferred = LiveTppComposition::share(LiveTppComposition::transform(inferred)
            + variabilityNormal(0,float(sd),seatIndex,0,72));
    }
    return LiveTppComposition::transform(account.direct.shareAt(
        LiveTppComposition::share(transformed),account.expectedVotes(),inferred));
}

double LiveV2::Election::accountedTppDerivative(int seatIndex, double transformed) const {
    auto const& account = tppCategoryAccounts->at(seatIndex);
    double total = std::max(account.expectedVotes(),account.direct.votes);
    if (total <= 0 || account.direct.votes <= 0) return 1;
    return (1-account.direct.votes/total)*LiveTppComposition::derivative(LiveTppComposition::share(transformed))
        / LiveTppComposition::derivative(LiveTppComposition::share(accountedTpp(seatIndex,transformed)));
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
        ? accountedTpp(seatIndex,tppIntegration->posterior.mean.at(tppProjectSeatIndices.at(seatIndex)))
        : transformedCounts(projected);
    double share = detransformVoteShare(float(transformed));
    auto account = LiveTurnoutMath::partitionAccount(
        {&turnout->counted(LiveData::CountKind::Tpp, seatIndex), &projected}, [](int id) { return id; });
    return LiveFpIntegration::constrainToCounted(account, {{0, share}, {1, 100 - share}}).at(0);
}

std::optional<std::vector<double>> LiveV2::Election::integrateTpp(std::vector<double> const& prior) const {
    if (!tppIntegration) throw std::runtime_error("Missing prepared joint TPP integration.");
    // Even with no comparable swing observations, received unsupported counts
    // replace their own share of the prior after the (possibly empty) update.
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
        state[tppProjectSeatIndices[s]] = accountedTpp(int(s),state[tppProjectSeatIndices[s]],true)
            + tppScenarioCountChange(int(s));
    return state;
}
