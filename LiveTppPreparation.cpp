#include "LiveV2.h"
#include "Simulation.h"
#include "PollingProject.h"
#include "LiveFpIntegration.h"
#include "LiveFpTpp.h"
#include "SimulationRun.h"
#include "LiveSimulationMath.h"

#include <algorithm>
#include <cmath>
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
constexpr std::uint32_t CoalitionTcpDrawTag = 73;

using LivePartialCount::Counts;
using LivePartialCount::TcpBasis;
using LivePartialCount::value;

LiveCoalitionTcp::SourceGroup coalitionSource(int party, int national) {
    using enum LiveCoalitionTcp::SourceGroup;
    if (party == 0) return Labor;
    if (party == 1) return Liberal;
    if (party == national) return National;
    return Other;
}

// The party priors are defined on the live ballot, including aliases and
// historical local adjustments. Use their probabilities for minor donors
// and the same prior leakage for either excluded Coalition partner.
std::vector<LiveCoalitionTcp::Source> coalitionSources(auto const& votes,
    LivePartialCount::Flows const& flows, int national, double generic) {
    using enum LiveCoalitionTcp::SourceGroup;
    std::vector<LiveCoalitionTcp::Source> result;
    for (auto const& [party,count] : votes) {
        if (count <= 0) continue;
        auto const group = coalitionSource(party,national);
        double rate = .5; // Labor primaries never enter a transfer calculation.
        if (group == Other) {
            auto found = flows.find(party);
            rate = found == flows.end() ? generic : found->second;
        } else if (group != Labor) rate = LiveCoalitionTcp::LeakageMean;
        result.push_back({group,double(count),LivePartialCount::logOdds(rate),party});
    }
    return result;
}

bool countedCoalitionPair(Counts const& tcp, int opponent) {
    return tcp.size() == 2 && tcp.contains(0) && tcp.contains(opponent) && LivePartialCount::total(tcp) > 0;
}

double displacedPrimaries(Counts const& original, Counts const& current) {
    double displaced = 0;
    for (auto const& [party,votes] : original)
        displaced += std::max(0.,votes-value(current,party));
    return displaced;
}

// Prepare the directly counted allocation and the original primary voters
// still awaiting preferences, separately for each hypothetical matchup.
LiveCoalitionTcp::PairAccount coalitionPairAccount(Counts const& fp, Counts const& tcp,
    TcpBasis const* original, LivePartialCount::Flows flows, int opponent, int national,
    double generic, bool active) {
    LiveCoalitionTcp::PairAccount result;
    Counts const* basis = original ? &original->fp : nullptr;
    double const displaced = basis ? displacedPrimaries(*basis,fp) : 0;
    if (!countedCoalitionPair(tcp,opponent) || (!active && displaced > 2)) {
        result.pending = coalitionSources(fp,flows,national,generic);
        return result;
    }
    Counts revisedFp;
    result.counted = LivePartialCount::total(tcp);
    result.labor = tcp.at(0);
    if (displaced > 0) {
        // An active count may have a known original batch underneath a
        // recheck. For a retired pair only modest removals can preserve its
        // direct-ballot status. Larger revisions can still train preferences.
        revisedFp = *basis;
        for (auto& [party,votes] : revisedFp) {
            votes = int(std::min(double(votes),value(fp,party)));
            flows.try_emplace(party,generic);
        }
        result.revision = LivePartialCount::reviseTcp(revisedFp,tcp,*original,flows,0,0,opponent);
        if (result.revision) {
            result.counted = result.revision->votes;
            result.labor = result.revision->first;
            basis = &revisedFp;
        } else basis = nullptr;
    }
    result.pending = coalitionSources(
        LiveCoalitionTcp::remainingPrimaries(fp,tcp,basis,opponent),flows,national,generic);
    return result;
}

struct PreferencePool {
    // Same confidence-weighted aggregation as the live preference hierarchy,
    // restricted to scalar preference evidence. Unreported seats stay in the
    // coverage denominator. No results or project objects are copied.
    double expected = 0, weight = 0, sum = 0;
    void add(double target, double relevance, double confidence, std::optional<double> deviation) {
        expected += target;
        if (!deviation) return;
        double w = target*relevance*confidence;
        weight += w; sum += w * *deviation;
    }
    double confidence() const { return expected > 0 ? weight/expected : 0; }
    std::optional<double> deviation() const {
        return weight > 0 ? std::optional<double>(double(sum/weight)) : std::nullopt;
    }
};

double transformedCounts(std::map<int, double> const& votes) {
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
    prepareSourceSeatTcp();
    prepareClassicPreferenceAccounts();
    auto prior = tppJointPrior();
    tppPriorExtension = std::make_shared<LiveTppComposition::Extension const>(
        LiveTppComposition::prepareExtension(prior, sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()));
    auto observations = tppJointObservations();
    tppIntegration = std::make_shared<LiveTppIntegration::Update const>(
        LiveTppIntegration::prepareWithExternalFactors(std::move(prior), observations,
            sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()
                + tppCategoryCodes.size() + LiveTppComposition::ReportingOrderFactors));
}

// Prepare the evidence each simulated district uses to turn primaries into
// TPP. Ordinary, early and declaration counts retain their own FP mix when
// assigning measured transfers. For voters still awaiting that count, save
// what matched counts elsewhere and in this district say about preferences.
// The simulator supplies the primary shares, starting rates and turnout for each draw.
void LiveV2::Election::prepareClassicPreferenceAccounts() {
    if (optionalPreferential) return; // Exhausted ballots need a separate denominator.
    auto accounts = std::make_shared<std::map<int,LiveFpTpp::District>>();
    auto const& error = tppTransferCalibration->factors.mean;
    for (size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s];
        // Two Coalition candidates need distinct leakage accounts. A final
        // official TPP already supersedes every estimate of missing transfers.
        if (seat.splitCoalitionCandidates || inactiveContests.contains(seat.name) ||
            (seat.finalResult && !seat.finalResult->tpp.empty())) continue;
        int const projectIndex = tppProjectSeatIndices.at(s);
        auto observation = getPreferenceCountObservation(projectIndex);
        // A published three-way tally can identify transfers among all three
        // recipients. That joint ballot model already estimates both the TPP
        // and the exclusion from the same sample; keep its established input.
        if (observation && !observation->candidateTotals.empty() && run.preferenceModel &&
            run.preferenceModel->isActive() && run.preferenceModel->coversDistrict(projectIndex)) continue;
        auto& account = (*accounts)[projectIndex];
        for (int opponent : {1,natPartyIndex})
            if (auto curve = sourceSeatTcp(projectIndex,opponent)) account.sourceArea = *curve;
        // The booth hierarchy compares matched FP and Labor-Coalition counts
        // with their historical preference patterns. Its election-wide movement
        // is weighted by the evidence received. The regional term measures the
        // additional movement beyond it. Both are in 25 times natural log odds.
        // Convert their sum once for all draws. Positive movement favours Labor.
        // A district with no classic count can learn from other districts; a
        // district with its own count applies this only to outstanding transfers.
        auto const& region = largeRegions.at(seat.parentRegionIndex);
        double const broadMovement = node.specificPreferenceFlowDeviation.value_or(0.)
            + region.node.specificPreferenceFlowDeviation.value_or(0.);
        account.transferOddsMultiplier = std::exp(broadMovement / 25.);
        // These are errors learned from matched FP/TPP, not disagreement
        // with the seat prior. Positive error means the prediction favoured
        // Labor too strongly. The draw already varies its initial source rates.
        if (run.grnPartyIndex >= 0)
            account.rateChange[run.grnPartyIndex] = -error.at(0)*LiveTppComposition::GreensTransferFlowSd*.01;
        double indChange = -error.at(1)*LiveTppComposition::IndependentTransferFlowSd*.01;
        account.rateChange[run.indPartyIndex] = indChange;
        account.rateChange[EmergingIndIndex] = indChange;
        auto partyGroup = [&](int party) {
            if (party == seat.liveIndependentPartyIndex) return run.indPartyIndex;
            if (natPartyIndex >= 0 && party == natPartyIndex) return 1;
            return party >= 0 && party < project.parties().count() ? party : OthersIndex;
        };
        auto grouped = [&](auto const& values) {
            PreferenceModel::Shares result;
            for (auto const& [party,n] : values) result[partyGroup(party)] += n;
            return result;
        };
        for (int b : seat.booths) {
            auto const& booth = readBooths().at(b);
            auto const bounds = booth.tppLowerBounds(natPartyIndex);
            if (LiveFpTpp::total(bounds) <= 0) continue;
            auto const& original = booth.tppCount(natPartyIndex);
            auto const* basis = booth.retainedClassic ? &booth.retainedClassic->basis :
                (booth.tcpBasis ? &*booth.tcpBasis : nullptr);
            bool const exact = basis && basis->tcp == original && !booth.hasTppRevision()
                && LivePartialCount::validBasis(*basis);
            // Use the original matched primaries even after another pairing
            // replaces this one. A recheck only guarantees the exported lower
            // bounds, and its remaining ballots stay available for estimation.
            auto const& fp = exact ? basis->fp : booth.tppPreferencePrimaries();
            LivePartialCount::Transfers transfers;
            if (exact) for (auto const& [source,received] : basis->transfers)
                for (auto const& [side,n] : received) transfers[partyGroup(source)][partyGroup(side)] += n;
            LiveFpTpp::addBooth(account,grouped(fp),grouped(booth.node.fpVotesProjected),
                grouped(bounds),transfers);
        }
    }
    classicPreferenceAccounts = std::move(accounts);
}

// Most booths stayed in their old district. Only ordinary venues with a
// measured result for the identical pair in a known former district can
// teach us about other venues transferred from that same district. Other
// locations still enter the denominator and keep their own forecasts.
std::optional<LiveSourceSeatTcp::Curve> LiveV2::Election::sourceAreaCurve(Seat const& seat,int opponent) const {
    bool const classic = opponent==1 || opponent==natPartyIndex;
    if ((classic && seat.splitCoalitionCandidates) || !seat.node.fpVotesProjected.contains(opponent)) return {};
    auto hasPair = [&](auto const& counts) {
        return counts.size()==2 && counts.contains(0) && counts.contains(opponent);
    };
    if (!classic && !hasPair(seat.node.tcpVotesCurrent)) return {};
    std::vector<LiveSourceSeatTcp::Venue> venues;
    for (int index : seat.booths) {
        auto const& booth = readBooths().at(index);
        auto const& n = booth.node;
        auto const& projected = classic ? n.tppVotesProjected : n.tcpVotesProjected;
        int const projectedOpponent = classic ? 1 : opponent;
        if (projected.size()!=2 || !projected.contains(0) || !projected.contains(projectedOpponent)) {
            if (n.totalFpVotesProjected()>0) return {};
            continue;
        }
        double const size = projected.at(0)+projected.at(projectedOpponent);
        if (size<=0) continue;
        auto const& measured = classic ? booth.tppCount(natPartyIndex) : n.tcpVotesCurrent;
        auto bounds = classic ? booth.tppLowerBounds(natPartyIndex) : booth.tcpLowerBounds();
        double first = getAt(bounds,0,0.), second = getAt(bounds,opponent,0.);
        if (first+second<size-.01) {
            first = std::max(first,double(getAt(n.fpVotesCurrent,0,0)));
            second = std::max(second,double(getAt(n.fpVotesCurrent,opponent,0)));
        }
        // Exact direct counts own their ballots. An inconsistent denominator
        // supplies no usable extra comparison for this seat and pair.
        if (first+second>size+.01) return {};
        LiveSourceSeatTcp::Venue v{size,projected.at(0),std::min(size,first),
            std::max(std::min(size,first),size-second),{}};
        if (!booth.sameSeat && !booth.syntheticPreviousCategory && !booth.previousPreferenceSeat.empty()
            && booth.boothType==Results2::Booth::Type::Normal && !booth.receivesFpBatches()
            && hasPair(n.tcpVotesPrevious) && n.totalTcpVotesPrevious()>0) {
            v.source = booth.previousPreferenceSeat;
            v.previousShare = double(n.tcpVotesPrevious.at(0))/n.totalTcpVotesPrevious();
            if (hasPair(measured) && !(classic ? booth.hasTppRevision() : booth.hasTcpRevision())) {
                v.counted = LivePartialCount::total(measured);
                v.countedFirst = measured.at(0);
            }
            v.pending = LivePartialCount::total(measured)==0 && n.totalTcpVotesCurrent()==0;
        }
        venues.push_back(std::move(v));
    }
    return LiveSourceSeatTcp::prepare(venues);
}

void LiveV2::Election::prepareSourceSeatTcp() {
    // The tested matchups have identifiable continuing parties. OPV can
    // exhaust primaries, and two Coalition candidates require separate TPP
    // comparison geographies. Keep their existing accounts.
    if (optionalPreferential) return;
    auto curves = std::make_shared<std::map<std::pair<int,int>,LiveSourceSeatTcp::Curve>>();
    for (size_t s=0; s<seats.size(); ++s) {
        auto const& seat = seats[s];
        if (seat.finalResult) continue;
        std::set<int> opponents;
        for (int index : seat.booths) {
            auto const& b = readBooths().at(index);
            if (b.sameSeat || b.previousPreferenceSeat.empty() || b.receivesFpBatches() ||
                b.boothType!=Results2::Booth::Type::Normal || b.syntheticPreviousCategory) continue;
            for (int opponent : {1,natPartyIndex,run.grnPartyIndex})
                if (opponent>0 && b.node.tcpVotesPrevious.contains(0) && b.node.tcpVotesPrevious.contains(opponent))
                    opponents.insert(opponent);
        }
        for (int opponent : opponents)
            if (auto curve = sourceAreaCurve(seat,opponent))
                curves->emplace(std::make_pair(tppProjectSeatIndices.at(s),opponent),*curve);
    }
    if (!curves->empty()) sourceSeatCurves = std::move(curves);
}

LiveSourceSeatTcp::Curve const* LiveV2::Election::sourceSeatTcp(int district,int opponent) const {
    if (!sourceSeatCurves) return nullptr;
    auto found = sourceSeatCurves->find({district,opponent});
    return found==sourceSeatCurves->end() ? nullptr : &found->second;
}

LiveFpTpp::District const* LiveV2::Election::classicPreferenceAccount(int district) const {
    if (!classicPreferenceAccounts) return nullptr;
    auto found = classicPreferenceAccounts->find(district);
    return found == classicPreferenceAccounts->end() ? nullptr : &found->second;
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
    constexpr double FlowConfidenceStrength = 200; // existing preference hierarchy
    double overall = election.deviation().value_or(0)*LiveSimulationMath::observationWeight(
        double(election.confidence()),FlowConfidenceStrength);
    auto const& region = regions.at(seats.at(excludedSeat).parentRegionIndex);
    double regional = region.deviation() ? (*region.deviation()-overall)*
        LiveSimulationMath::observationWeight(double(region.confidence()),FlowConfidenceStrength) : 0;
    return (overall+regional)/25;
}

LivePartyConversion::MatchedAccount LiveV2::Election::matchedConversionAccount(int seatIndex) const {
    LivePartyConversion::MatchedAccount account;
    double broadOffset = heldOutPreferenceOffset(seatIndex);
    for (int index : seats.at(seatIndex).booths) {
        auto const& booth = readBooths()[index];
        // Use one matched Laborâ€“Coalition account per location. It can be
        // the active pair or a retained earlier count with its own FP batch.
        // A newly reported non-classic TCP describes a separate comparison.
        if (tppBoothMatchedPreferenceUnit(index) <= 0) continue;
        auto previous = historicalPreferenceTransfer(booth);
        if (!previous) continue;
        double quality = booth.node.relevanceModifier*
            LivePartialCount::historicalFlowReliability(previous->share());
        int coalition = coalitionFinalist(booth);
        double offset = preferenceRateOffset(booth,false).value_or(0)/25+broadOffset;
        account.add(booth.tppPreferencePrimaries(),booth.tppCount(natPartyIndex),
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
    Results2::Election const& previousElection, Results2::Election const& currentElection,
    LivePartyConversion::History const& partyFlowHistory,
    LiveHistoricalPreferences::History const& notionalHistory) {
    // Retain the whole old category, including old locations with no current ID
    // match. Completion can then compare aggregate PPVC results even after moves.
    // A zero historical account supplies neither a comparison nor provenance.
    auto add = [&](int code, auto const& fp, auto const& tcp, bool synthetic,
        std::string const& kind, int identifier) {
        LivePartialCount::Counts primaries, preferences;
        for (auto const& [id, votes] : fp)
            primaries[mapPartyId(id,true,previousElection,currentElection)] += votes;
        for (auto const& [id, votes] : tcp)
            preferences[mapPartyId(id,true,previousElection,currentElection)] += votes;
        // Actual classic TCP wins. Non-classic FP uses the same historical
        // Greens/candidate priors as matched booths, retaining generic estimates
        // when those priors are absent. Category-specific flows may differ; this
        // shared assumption is not a measurement of preferences in the category.
        // Generic-only category estimates would otherwise create a comparison
        // mismatch and a compensating seat-wide reference shift.
        auto historicalFlows = historicalPartyPreferenceFlows(partyFlowHistory,
            previousSeat.name,primaries,previousElection);
        LivePartialCount::Counts notional;
        if (!synthetic) {
            LivePartialCount::Counts raw(fp.begin(),fp.end());
            auto record = notionalHistory.find(previousSeat.name,kind,identifier,raw);
            if (record) notional = LiveHistoricalPreferences::mappedTpp(*record,primaries,natPartyIndex);
        }
        auto [first,total] = LivePartyConversion::historicalCategoryTpp(primaries,preferences,
            historicalFlows,preferenceFlowMap,prevPreferenceOverrides,natPartyIndex,notional);
        if (total <= 0) return;
        auto& history = seats.at(seatIndex).categoryHistory[code];
        history.first += first; history.total += total; history.synthetic |= synthetic;
    };
    for (auto const& [id, booth] : previousElection.booths)
        if (booth.parentSeat == previousSeat.id)
            add(int(booth.type),booth.fpVotes,booth.tcpVotesCandidate,booth.syntheticBaseline,"booth",id);
    std::map<Results2::VoteType,std::map<int,int>> fp, tcp;
    for (auto const& [candidate, types] : previousSeat.fpVotes)
        for (auto const& [type, votes] : types) if (type != Results2::VoteType::Ordinary) fp[type][candidate] += votes;
    for (auto const& [candidate, types] : previousSeat.tcpVotesCandidate)
        for (auto const& [type, votes] : types) if (type != Results2::VoteType::Ordinary) tcp[type][candidate] += votes;
    for (auto const& [type, counts] : fp)
        add(100+int(type),counts,tcp[type],previousSeat.syntheticBaselineTypes.contains(type),"declaration",int(type));
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
    prepareTppCategoryProgress(seatIndex, prepared);
    prepareTppCategoryVotes(seatIndex, prepared);
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

void LiveV2::Election::prepareTppCategoryProgress(int seatIndex, TppCategoryAccount& account) const {
    // Physical coverage describes how much of the category is already in its
    // vote account. It does not establish how representative the TCP subset is:
    // FP completion cannot upgrade an unmatched booth's TCP comparison. Actual
    // received votes instead get their full weight in prepareTppCategoryVotes.
    for (int index : seats.at(seatIndex).booths) {
        // Use formal FP targets even under optional preferential voting;
        // exhausted ballots are still received FP, not unreported ballots.
        double target = turnoutFpTarget(index);
        if (target <= 0) continue; // Explicit closures remain outside the account.
        auto const& booth = readBooths()[index];
        account.fpProgress[categoryCode(booth)].add(target,booth.node.totalFpVotesCurrent(),0);
    }
}

void LiveV2::Election::prepareTppCategoryVotes(int seatIndex, TppCategoryAccount& account) const {
    // Assemble received continuing ballots independently of the comparisons
    // used to predict missing places. The historical alignment remains on those
    // comparison copies; it cannot transfer known FP or TCP votes between sides.
    for (int index : seats.at(seatIndex).booths) {
        auto const& booth = readBooths()[index];
        auto const& n = booth.node;
        double target = tppBoothTarget(index);
        if (target <= 0) continue;
        int code = categoryCode(booth);
        if (account.sources.at(code) == LiveTppComposition::BaselineSource::Unavailable)
            continue; // Its existing direct received-vote replacement is prepared separately.
        double baseline = LiveTppComposition::share(tppBoothBaseline(index)+account.referenceShift);
        auto received = receivedBoothTpp(booth);
        auto& votes = account.votes[code];
        if (!received) {
            votes.add(target,0,0,0,0,baseline,0,0,0);
            continue;
        }
        double pending = received->votes;
        double pendingFirst = received->inferredFirst;
        double first = pendingFirst;
        auto const& measured = booth.tppCount(natPartyIndex);
        double tcp = LivePartialCount::total(measured);
        double continuing = received->votes;
        if (optionalPreferential && tcp <= 0) {
            // FP includes ballots that will exhaust, whereas this account's
            // target is continuing TPP. Retain the existing projected exhaustion
            // allowance for the received fraction rather than counting exhausted
            // ballots as additional outstanding TPP. Actual TCP below stays exact.
            double fpTarget = turnoutFpTarget(index);
            continuing = fpTarget > 0 ? target*std::min(1.,n.totalFpVotesCurrent()/fpTarget) : 0;
            double fraction = received->votes > 0 ? continuing/received->votes : 0;
            pendingFirst *= fraction;
            first = pendingFirst;
            pending = continuing;
        }
        if (tcp > 0) {
            if (auto revised = revisedBoothTpp(booth)) {
                pending = revised->uncertainVotes();
                first = revised->first;
                votes.revisionVariance += std::pow(pending/2,2);
            }
            else {
                // Subtract counts directly. Reversing a rounded fraction can
                // turn exactly one pending ballot into slightly less than one.
                pending = received->votes-tcp;
                first += measured.at(0);
            }
        }
        else if (!optionalPreferential) {
            // Allocate exact major FP plus the existing corrected preference
            // flows. A smoothed absolute TPP estimate must not move known major
            // primaries. Zero future share excludes not-yet-received ballots.
            first = projectUnmatchedBoothTpp(index,0,0);
            pendingFirst = first;
        }
        double fp = n.totalFpVotesCurrent(), greens = 0, independent = 0;
        if (fp > 0) for (auto const& [party,count] : n.fpVotesCurrent) {
            if (party == run.grnPartyIndex) greens += count/fp;
            else if (party >= IndependentPartyIdOffset) independent += count/fp;
        }
        votes.add(target,continuing,first,pending,pendingFirst,baseline,
            LiveTppComposition::preferencePoolFraction(n.fpVotesCurrent,natPartyIndex),greens,independent);
    }
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
    // be much further from the configured seat history. Use the same common
    // seat offset for the historical comparisons and their current FP-derived
    // counterparts; otherwise a shared conversion error becomes apparent swing.
    // appendTppBoothEvidence adjusts detached inferred comparisons, retaining
    // measured TCP and the original received/projected vote accounts. This
    // offset uses expected final vote sizes, not current TCP reporting confidence.
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
    LiveTppIntegration::EvidenceSourceGroup sourceGroup) const {
    // Each physical reporting unit contributes once to its published category.
    // The per-booth helper owns partial-preference and comparison-quality rules.
    std::map<int, LiveTppIntegration::CategoryEvidence> categories;
    for (int index : seats.at(seatIndex).booths)
        appendTppBoothEvidence(index, referenceShift, categories, sourceGroup);
    return categories;
}

double LiveV2::Election::tppBoothMatchedPreferenceUnit(int index) const {
    auto const& booth = readBooths()[index];
    auto const& n = booth.node;
    // Apply the same comparison checks to active and retained TPP. Always
    // test the measured TCP against its original primary mix. Estimated
    // transfers after an FP recheck never add to this evidence weight.
    if (!n.preferenceFlowDeviation || n.preferenceFlowConfidence <= 0) return 0;
    auto const& tcp = booth.tppCount(natPartyIndex);
    int coalition = tcp.contains(natPartyIndex) ? natPartyIndex : 1;
    auto current = LivePartialCount::measureTransfer(booth.tppPreferencePrimaries(), tcp, 0, coalition);
    auto historical = historicalPreferenceTransfer(booth);
    if (!current || !historical) return 0;
    double reliability = n.relevanceModifier * LivePartialCount::historicalFlowReliability(historical->share());
    return LiveTppComposition::matchedPreferenceUnit(current->votes, reliability);
}

void LiveV2::Election::appendTppBoothEvidence(int index, double referenceShift,
    std::map<int, LiveTppIntegration::CategoryEvidence>& categories,
    LiveTppIntegration::EvidenceSourceGroup sourceGroup) const {
    // index identifies a prepared reporting unit (physical booth or declaration
    // service). readBooths() gives the immutable prepared records: recovered
    // current candidate counts, historical comparisons and live TPP estimates.
    // It does not read a feed file or use map coordinates here.
    //
    // referenceShift is a seat-wide offset in 25*log-odds units aligning booth
    // comparisons with the pre-count seat baseline. categories accumulates
    // evidence by voting category. sourceGroup selects all information or its
    // measured/FP-derived fraction for private prediction of unreported places.
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
    // Put inferred evidence on the same reference as baseline above. This
    // changes only this comparison copy, after its usual preference correction;
    // it does not overwrite the booth's raw estimate, counted votes or direct
    // received-vote replacement. Fully measured TCP is unchanged. See
    // alignInferredReference for the shared-error assumption and partial counts.
    *received = LiveTppIntegration::alignInferredReference(*received, referenceShift);
    // Only received votes enter this estimate. A first partial TCP retains
    // the pending-FP allocation used by the forecast, not an extrapolation
    // of its possibly unrepresentative small TCP batch. Under OPV the FP/TCP
    // gap can be exhaustion, so use the observed continuing-vote share.
    // Matched booths compare with their historical lean; genuinely new
    // locations use category history (or the seat fallback) with reduced
    // comparison quality. Full physical FP coverage does not lift that quality.
    double quality = n.relevanceModifier * (n.tppSharePrevious ? 1 : .1);
    // Retain the existing comparison-weight convention, but no additional
    // blanket variance multiplier. Explicit conversion uncertainty now owns
    // the precision difference; reporting order also covers measured TCP.
    quality *= 1 - (1-LiveTppIntegration::InferredPreferenceComparisonWeight)*received->inferredFraction;
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

// Each location contributes one current FP account and, where available, a
// measured TCP cohort for each Coalition matchup. Fit flows once on those
// observed cohorts. Each simulation updates both possible counts against
// its own starting preferences before the exclusions select its final pair.
void LiveV2::Election::prepareCoalitionTcpForecasts(Results2::Election const& previousElection,
    Results2::Election const& currentElection, LiveInputRecovery::PairPreferenceHistory const& history,
    LiveInputRecovery::PairPreferenceHistory const& revisedFlows) {
    std::map<int,CoalitionTcpDistrict> prepared;
    std::array<int,2> const opponents{1,natPartyIndex};
    double const generic = preferenceFlowMap.at(PartyCollection::InvalidIndex)*.01;
    auto mapCounts = [&](Counts const& counts) {
        Counts mapped;
        for (auto const& [party,votes] : counts)
            mapped[mapPartyId(party,false,previousElection,currentElection)] += votes;
        return mapped;
    };
    struct ArchivedCohort {
        TcpBasis basis;
        std::pair<std::string_view,std::string_view> clock;
    };
    auto readHistory = [&](LiveInputRecovery::PairPreferenceHistory const& source, std::string const& key) {
        std::array<std::optional<ArchivedCohort>,2> result;
        auto found = source.find(key);
        if (found == source.end()) return result;
        for (auto const& [candidates,cohort] : found->second) {
            auto tcp = mapCounts(cohort.tcp);
            for (std::size_t p = 0; p < opponents.size(); ++p) {
                if (!countedCoalitionPair(tcp,opponents[p])) continue;
                TcpBasis basis{mapCounts(cohort.fp),std::move(tcp),{},cohort.recheckedReconciledPair};
                for (auto const& [candidate,counts] : cohort.transfers) {
                    auto& target = basis.transfers[mapPartyId(candidate,false,previousElection,currentElection)];
                    for (auto const& [party,votes] : mapCounts(counts)) target[party] += votes;
                }
                auto const clock = std::pair<std::string_view,std::string_view>{cohort.tcpTime,cohort.fpTime};
                if (!result[p] || result[p]->clock < clock)
                    result[p] = ArchivedCohort{std::move(basis),clock};
                break;
            }
        }
        return result;
    };
    for (int s = 0; s < int(seats.size()); ++s) {
        auto const& seat = seats[s];
        if (!seat.splitCoalitionCandidates || seat.finalResult) continue;
        std::vector<LiveCoalitionTcp::Booth> booths;
        std::vector<LiveCoalitionTcp::Observation> observations;
        std::array<double,4> countedPrimaries{};
        CoalitionPreference::Shares countedByParty;
        auto const& region = largeRegions.at(seat.parentRegionIndex);
        double const broadLogOdds = (node.specificPreferenceFlowDeviation.value_or(0.)
            +region.node.specificPreferenceFlowDeviation.value_or(0.))/25.;
        auto canonicalParty = [&](int id) {
            if (id >= 0 && id == seat.liveIndependentPartyIndex) return run.indPartyIndex;
            return id >= 0 && id < project.parties().count() ? id : OthersIndex;
        };
        // Centre the one-time fit on the same comparison as the ordinary
        // forecast. The baseline report provides mean FP and reference TPP.
        // Each simulation later uses its own FP, TPP and sampled rates. An
        // older baseline missing the means can still fit around the party priors.
        int const projectIndex = tppProjectSeatIndices.at(s);
        CoalitionPreference::Shares centralRates;
        auto const& baseline = sim.getLiveBaselineReport();
        if (run.projectBothCoalitionMatchups && baseline && projectIndex >= 0 &&
            size_t(projectIndex) < baseline->seatPartyMeanFpShare.size() &&
            size_t(projectIndex) < baseline->seatPartyOneMarginAverage.size()) {
            auto const& means = baseline->seatPartyMeanFpShare[projectIndex];
            CoalitionPreference::Shares donors(means.begin(),means.end());
            CoalitionPreference::Leakage central;
            central.nationalReference = seat.nationalTppReference;
            double leak = central.reference(LiveFpTpp::votesFor(donors,1),LiveFpTpp::votesFor(donors,natPartyIndex));
            donors.erase(natPartyIndex);
            for (auto [id,n] : donors) centralRates[id] = preferenceFlowMap.contains(id) ?
                preferenceFlowMap.at(id)*.01 : generic;
            for (auto [id,q] : seat.partyPreferenceFlows) centralRates[canonicalParty(id)] = q;
            if (!LiveFpTpp::anchorRates(donors,50.+baseline->seatPartyOneMarginAverage[projectIndex]-leak,centralRates))
                centralRates.clear();
        }
        auto identifySources = [&](auto& sources) {
            for (auto& source : sources) {
                source.party = canonicalParty(source.party);
                if (source.group == LiveCoalitionTcp::SourceGroup::Other) {
                    if (centralRates.contains(source.party)) source.logOdds = LivePartialCount::logOdds(centralRates.at(source.party));
                    source.logOdds += broadLogOdds;
                }
            }
        };
        for (int index : seat.booths) {
            auto const& booth = readBooths()[index];
            auto const& n = booth.node;
            if (n.totalFpVotesProjected() <= 0 && n.totalTcpVotesCurrent() <= 0) continue;
            for (auto const& [party,votes] : n.fpVotesCurrent) {
                countedByParty[canonicalParty(party)] += votes;
                countedPrimaries.at(std::size_t(coalitionSource(party,natPartyIndex))) += votes;
            }
            LiveCoalitionTcp::Booth account{categoryCode(booth),{},{}};
            account.sourceId = booth.sourceId;
            auto const flows = classicPreferenceFlows(booth,1);
            std::map<int,double> future;
            for (auto const& [party,votes] : n.fpVotesProjected)
                future.emplace(party,std::max(0.,votes-value(n.fpVotesCurrent,party)));
            double futureVotes = 0;
            for (auto const& [party,votes] : future) futureVotes += votes;
            // Projection IDs can group an independent's received primaries.
            // Keep the total outstanding FP fixed when that changes the mix.
            if (futureVotes > 0) {
                double scale = std::max(0.,n.totalFpVotesProjected()-n.totalFpVotesCurrent())/futureVotes;
                for (auto& [party,votes] : future) votes *= scale;
            }
            account.future = coalitionSources(future,flows,natPartyIndex,generic);
            auto role = booth.voteType != Results2::VoteType::Ordinary ? "declaration" :
                booth.boothType == Results2::Booth::Type::Ppvc ? "ppvc" : "ordinary";
            auto const key = seat.name+"/"+role+"/"+booth.name;
            auto const retained = readHistory(history,key);
            auto const revised = readHistory(revisedFlows,key);
            for (std::size_t p = 0; p < opponents.size(); ++p) {
                int const opponent = opponents[p];
                auto const pairFlows = p == 0 ? flows : classicPreferenceFlows(booth,opponent);
                bool const active = countedCoalitionPair(n.tcpVotesCurrent,opponent);
                Counts const empty;
                auto const& tcp = active ? n.tcpVotesCurrent : (retained[p] ? retained[p]->basis.tcp : empty);
                std::optional<TcpBasis> current;
                if (active) {
                    if (booth.tcpBasis && booth.tcpBasis->tcp == tcp) current = *booth.tcpBasis;
                    else if (retained[p] && retained[p]->basis.tcp == tcp) current = retained[p]->basis;
                    else current = TcpBasis{n.fpVotesCurrent,tcp,{},false};
                    if (!LivePartialCount::validBasis(*current)) current.reset();
                }
                TcpBasis const* original = current ? &*current : nullptr;
                if (!active && retained[p] && LivePartialCount::validBasis(retained[p]->basis))
                    original = &retained[p]->basis;
                account.pairs[p] = coalitionPairAccount(n.fpVotesCurrent,tcp,original,pairFlows,
                    opponent,natPartyIndex,generic,active);

                // A small newer TCP without identified primaries can still
                // occupy the count account. Learn only from the latest batch
                // whose original primaries are known to describe those votes.
                TcpBasis const* evidence = current ? &*current : nullptr;
                if (!evidence && revised[p] && (!retained[p] || revised[p]->clock > retained[p]->clock))
                    evidence = &revised[p]->basis;
                if (!evidence && retained[p]) evidence = &retained[p]->basis;
                if (!evidence || !LivePartialCount::validBasis(*evidence)) {
                    // For nearly simultaneous FP and TCP returns one or two
                    // ballots can differ. Learn from the observed mix with the
                    // ordinary reliability taper, leaving both vote totals intact.
                    if (active) {
                        auto transfer = LivePartialCount::measureTransfer(n.fpVotesCurrent,tcp,0,opponent);
                        if (transfer) {
                            auto donors = n.fpVotesCurrent;
                            donors.erase(0);
                            donors.erase(opponent);
                            double pool = LivePartialCount::total(donors);
                            double weight = LivePartialCount::tcpPreferenceMatchWeight(
                                n.totalFpVotesCurrent(),LivePartialCount::total(tcp),pool);
                            if (weight > 0) observations.push_back({booths.size(),p,
                                coalitionSources(donors,pairFlows,natPartyIndex,generic),pool,
                                transfer->received,weight});
                        }
                    }
                    continue;
                }
                if (active && !current && std::any_of(evidence->tcp.begin(),evidence->tcp.end(),
                    [&](auto const& entry) { return entry.second > value(tcp,entry.first); })) continue;
                auto donors = evidence->fp;
                donors.erase(0);
                donors.erase(opponent);
                double const pool = LivePartialCount::total(donors);
                if (pool <= 0) continue;
                double const reachedLabor = value(evidence->tcp,0)-value(evidence->fp,0);
                double const overlap = 1-displacedPrimaries(evidence->fp,n.fpVotesCurrent)/
                    LivePartialCount::total(evidence->fp);
                observations.push_back({booths.size(),p,coalitionSources(donors,pairFlows,natPartyIndex,generic),
                    pool,reachedLabor,overlap*overlap});
            }
            identifySources(account.future);
            // Count the district's minor voters once, including those whose
            // TCP has already arrived. They set the reference mix for drawing
            // geographical differences in where each party's preferences go.
            for (auto [party,votes] : n.fpVotesCurrent) {
                if (votes > 0 && coalitionSource(party,natPartyIndex) == LiveCoalitionTcp::SourceGroup::Other)
                    account.minorPrimaries[canonicalParty(party)] += votes;
            }
            for (auto const& source : account.future) {
                if (source.group == LiveCoalitionTcp::SourceGroup::Other)
                    account.minorPrimaries[source.party] += source.votes;
            }
            for (auto& pair : account.pairs) identifySources(pair.pending);
            booths.push_back(std::move(account));
        }
        if (observations.empty() && std::none_of(booths.begin(),booths.end(),[](auto const& b) {
            return b.pairs[0].counted > 0 || b.pairs[1].counted > 0;
        })) {
            // FP already updates both accounts in the iteration. A district
            // with shared evidence alone needs only that one change in odds.
            if (broadLogOdds != 0) {
                LiveCoalitionTcp::Prepared shared;
                shared.broadLogOdds = broadLogOdds;
                prepared.emplace(projectIndex,CoalitionTcpDistrict{s,std::move(shared)});
            }
            continue;
        }
        for (auto& observation : observations) identifySources(observation.donors);
        auto const reference = run.projectBothCoalitionMatchups ? std::optional(seat.nationalTppReference) : std::nullopt;
        auto fit = LiveCoalitionTcp::prepare(std::move(booths),observations,reference);
        fit.countedPrimaries = countedPrimaries;
        fit.countedFp = std::move(countedByParty);
        fit.broadLogOdds = broadLogOdds;
        fit.genericOtherLogOdds = LivePartialCount::logOdds(generic);
        prepared.emplace(tppProjectSeatIndices.at(s),CoalitionTcpDistrict{s,std::move(fit)});
    }
    coalitionTcpDistricts = std::make_shared<decltype(prepared) const>(std::move(prepared));
}

void LiveV2::Election::updateCoalitionMatchups(int district, CoalitionPreference::Scenario& scenario,
    CoalitionPreference::Shares const& current) const {
    if (!coalitionTcpDistricts) return;
    auto found = coalitionTcpDistricts->find(district);
    if (found == coalitionTcpDistricts->end()) return;
    int s = found->second.liveIndex;
    auto update = [&](CoalitionPreference::Shares const& primaries) {
        found->second.accounts.update(scenario,primaries,natPartyIndex,OthersIndex,seats[s].node.totalFpVotesProjected(),
            [&](std::uint64_t term) { return variabilityNormal(0,1,s,term,CoalitionTcpDrawTag); });
    };
    // The live principal independent can occupy the emerging slot in this
    // scenario. Match that voter's party to the received FP and TCP cohorts.
    if (seats[s].liveIndependentPartyIndex >= 0 && LiveFpTpp::votesFor(current,run.indPartyIndex) == 0 &&
        LiveFpTpp::votesFor(current,EmergingIndIndex) > 0) {
        auto primaries = current;
        primaries[run.indPartyIndex] = primaries.at(EmergingIndIndex);
        primaries.erase(EmergingIndIndex);
        if (scenario.rates.contains(EmergingIndIndex)) scenario.rates[run.indPartyIndex] = scenario.rates.at(EmergingIndIndex);
        update(primaries);
    } else update(current);
}

std::optional<LiveData::CoalitionTcpShare> LiveV2::Election::getSeatCoalitionTcpShare(
    int district, int opponent, std::array<double,3> primaryShares) const {
    if (!coalitionTcpDistricts || (opponent != 1 && opponent != natPartyIndex)) return {};
    auto found = coalitionTcpDistricts->find(district);
    if (found == coalitionTcpDistricts->end() || found->second.accounts.booths.empty()) return {};
    int const s = found->second.liveIndex;
    // Use this scenario's turnout and primary shares for the future pool.
    // Both preference counts use the same keyed flow draws for this district.
    auto estimate = found->second.accounts.draw(opponent == natPartyIndex ? 1 : 0,
        seats[s].node.totalFpVotesProjected(),primaryShares,
        [&](std::size_t term) { return variabilityNormal(0,1,s,int(term),CoalitionTcpDrawTag); });
    if (estimate.total <= 0) return {};
    double const labor = std::clamp(100*estimate.labor/estimate.total,0.,100.);
    return LiveData::CoalitionTcpShare{labor,estimate.counted > 0};
}

LiveTppComposition::ObservationBlock LiveV2::Election::tppSeatObservations(int s, double electionExpected,
    double matchedUnits) const {
    auto const& prepared = tppCategoryAccounts->at(s);
    if (prepared.codes.empty()) return {};
    auto [comparisons, comparisonExposures] = tppSeatObservationRows(s, matchedUnits);
    for (auto& comparison : comparisons) LiveTppComposition::prepareConversionScales(comparison);
    // Private source comparisons retain their own precision for unreported
    // places. The joint seat model receives one finite vote account per category,
    // rather than two extrapolations over the same already received population.
    std::vector<LiveTppIntegration::Observation> rows;
    LiveTppIntegration::Matrix localExposures;
    double budget = prepared.expectedVotes()/electionExpected;
    for (int code : prepared.codes) {
        std::vector<LiveTppIntegration::Observation> sources;
        LiveTppIntegration::Vector exposure;
        for (std::size_t i = 0; i < comparisons.size(); ++i) if (comparisons[i].category == code) {
            sources.push_back(comparisons[i]);
            exposure = comparisonExposures[i];
        }
        if (sources.empty()) continue; // Unsupported categories retain their direct count path.
        rows.push_back(unifyTppCategoryObservation(prepared,code,sources,budget));
        localExposures.push_back(std::move(exposure));
    }
    auto block = LiveTppComposition::observationBlock(std::move(rows), localExposures,
        LiveTppComposition::categoryCovariance(prepared.codes, run.regionCode, true, std::stoi(run.yearCode)));
    // Scales have already been attached to their received/future components.
    // Recomputing them from a pooled inferred fraction would recreate the loss
    // of measured information which originally motivated source separation.
    LiveTppComposition::addLocalConversionCovariance(block,budget);
    auto transferStart = sim.getLiveBaselineReport()->liveBaselineTppMoments.mean.size()
        + tppCategoryCodes.size() + LiveTppComposition::ReportingOrderFactors;
    LiveTppComposition::applyPartyTransfer(block, transferStart, tppTransferCalibration->factors.covariance);
    return block;
}

LiveTppIntegration::Observation LiveV2::Election::unifyTppCategoryObservation(
    TppCategoryAccount const& account, int category,
    std::vector<LiveTppIntegration::Observation> const& comparisons, double budgetFraction) const {
    auto const& votes = account.votes.at(category);
    auto const& baseline = sim.getLiveBaselineReport()->liveBaselineTppMoments;
    auto orderStart = baseline.mean.size()+tppCategoryCodes.size();
    auto const& transfer = tppTransferCalibration->factors.covariance;
    // Determine how to average the two raw swing comparisons for UNCOUNTED
    // locations. Common category departures cancel when comparing their
    // precision; local preference errors and shared order/transfer errors do
    // not. Keeping their covariance avoids treating correlated estimates as
    // independent extra information. There are at most two sources here.
    auto covariance = LiveTppComposition::futureComparisonCovariance(comparisons,
        budgetFraction,transfer,orderStart);
    LiveTppIntegration::Observation pending;
    pending.localMatchedUnits = comparisons.front().localMatchedUnits;
    pending.sharedMatchedUnits = comparisons.front().sharedMatchedUnits;
    if (votes.pending > 0) {
        // The pool proportions refer to received ballots still awaiting TCP,
        // before division by the WHOLE category. This preserves the existing
        // pool-variance rule while scaling its effect by the affected ballots.
        pending.inferredFraction = 1;
        pending.unresolvedPreferenceFraction = votes.preferencePool/votes.pending;
        pending.greensPreferenceFraction = votes.greensPool/votes.pending;
        pending.independentPreferenceFraction = votes.independentPool/votes.pending;
        pending.conversionJacobian = LiveTppComposition::conversionShareJacobian(
            100*votes.pendingFirst/votes.pending);
        LiveTppComposition::prepareConversionScales(pending);
    }
    auto position = std::find(account.codes.begin(),account.codes.end(),category)-account.codes.begin();
    int seat = comparisons.front().seat;
    return LiveTppComposition::unifiedCategoryObservation(votes,comparisons,covariance,pending,
        baseline.mean.at(seat),account.account.shares.at(position),orderStart);
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
    auto const& wholeCategory = prepared.evidence.at(code);
    // These PRIVATE comparison variances determine how the source predicts
    // unreported places. The unified category row later scales that uncertainty
    // by its remaining population. Physical FP completion does not promote the
    // historical quality of this TCP subset. Both sources retain the combined
    // usable sample for sparse-size adjustments, avoiding a second penalty just
    // because one received population is divided between two information types.
    double scatter = wholeCategory.heterogeneity();
    observation.variance = evidence.variance(wholeCategory,scatter);
    observation.sampleVotes = evidence.reporting.fpVotes;
    observation.sampleCoverage = evidence.sampleCoverage();
    observation.categorySampleVotes = wholeCategory.reporting.fpVotes;
    observation.categorySampleCoverage = wholeCategory.sampleCoverage();
    observation.categoryComparisonCoverage = wholeCategory.comparisonCoverage();
    observation.sparseSampleVariance = evidence.sparseSampleVariance(observation.variance,wholeCategory);
    observation.variance += observation.sparseSampleVariance;
    observation.comparisonVariance = evidence.comparisonVariance(scatter);
    observation.comparisonVarianceFloor = LiveTppIntegration::retainSmallCategoryVariance(
        0,observation.comparisonVariance,observation.categorySampleVotes);
    double reportingVariance = observation.variance;
    observation.variance = evidence.locationVariance(wholeCategory,scatter);
    observation.retainedComparisonVariance = observation.variance-reportingVariance;

    auto const& progress = prepared.fpProgress.at(code);
    observation.categoryFpCoverage = progress.fpVotes/progress.expectedVotes;
    observation.unfadedLocationVariance = observation.variance;
}

void LiveV2::Election::prepareDirectTppVotes(int seatIndex, TppCategoryAccount& account) const {
    double uncertaintyVotes = 0;
    for (int index : seats.at(seatIndex).booths) {
        auto const& booth = readBooths()[index];
        if (!account.sources.contains(categoryCode(booth)) ||
            account.sources.at(categoryCode(booth)) != LiveTppComposition::BaselineSource::Unavailable) continue;
        auto const& n = booth.node;
        auto const& measured = booth.tppCount(natPartyIndex);
        double tcp = LivePartialCount::total(measured);
        auto received = receivedBoothTpp(booth);
        if (!received) continue;
        double measuredFirst = tcp > 0 ? measured.at(0) : 0;
        if (auto revised = revisedBoothTpp(booth)) {
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
    if (auto const& final = seats.at(seatIndex).finalResult; final && !final->tpp.empty()) {
        // The ordinary account below and transform() both use percentage
        // shares (0--100), not fractions (0--1). Apply that same scale to a
        // final count before returning its 25*log-odds value. The final counted
        // account also fixes simulated output; diagnostics need the correct
        // scale here rather than relying on that later count constraint.
        double total = final->tpp.at(0)+final->tpp.at(1);
        double percent = 100*final->tpp.at(0)/total;
        return LiveTppComposition::transform(percent);
    }
    auto const& account = tppCategoryAccounts->at(seatIndex);
    if (account.direct.votes <= 0) return transformed;
    double inferred = account.direct.inferredShare();
    if (sample && account.direct.inferredVotes > 0) {
        double sd = std::sqrt(account.direct.conversionVariance)*LiveTppComposition::conversionShareJacobian(inferred);
        inferred = LiveTppComposition::share(LiveTppComposition::transform(inferred)
            + variabilityNormal(0,double(sd),seatIndex,0,72));
    }
    return LiveTppComposition::transform(account.direct.shareAt(
        LiveTppComposition::share(transformed),account.expectedVotes(),inferred));
}

double LiveV2::Election::accountedTppDerivative(int seatIndex, double transformed) const {
    if (auto const& final = seats.at(seatIndex).finalResult; final && !final->tpp.empty()) return 0;
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

double LiveV2::Election::preparedTppShare(int seatIndex) const {
    if (auto const& final = seats.at(seatIndex).finalResult; final && !final->tpp.empty())
        return double(100*final->tpp.at(0)/(final->tpp.at(0)+final->tpp.at(1)));
    // The booth viewer's estimated swing describes the prepared category
    // estimate. The simulation report also has access to each draw's accepted
    // primaries and uses those to calculate TPP where a ballot account exists.
    // During setup the category estimate still comes from projected counts.
    auto const& projected = turnout->mean(LiveData::CountKind::Tpp, seatIndex);
    double transformed = tppIntegration
        ? accountedTpp(seatIndex,tppIntegration->posterior.mean.at(tppProjectSeatIndices.at(seatIndex)))
        : transformedCounts(projected);
    double share = detransformVoteShare(double(transformed));
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
