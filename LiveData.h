#pragma once

#include "LiveTppIntegration.h"
#include "PreferenceModel.h"

#include <map>
#include <cmath>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <vector>

// Portable boundary between the core simulation and a live-results analysis
// implementation. These types deliberately have no dependency on electoral-
// commission parsers, wxWidgets, or third-party libraries.
// A "provider" here is a C++ object supplying that analysis, not an electoral
// commission or an external data service. LiveV2::Election implements it for
// automatic feeds. The simulator asks it for prepared count evidence and for
// a separate possible live outcome to combine with each pre-election draw;
// result tables and exports also read its summaries and diagnostics.
// Working shares, deviations and estimated counts use double precision across
// this boundary. Narrowing an estimate here and widening it again later creates
// apparent movement even when the count supplies no adjustment. Raw published
// integer votes and the existing saved-report representation remain separate.
namespace LiveData {

// Election-wide reporting coverage, separate from the model's confidence in
// the count. Both numerators are accepted, published candidate counts; TCP
// includes classic TPP but excludes FP-derived estimates and retained counts
// for a withdrawn candidate pairing. The denominator is the prepared mean
// final formal turnout, held fixed across this snapshot's simulation draws.
// Sum votes before dividing, so a small district cannot outweigh a large one.
// In optional-preferential elections, exhausted ballots are not included in
// the two surviving candidates' TCP totals; this is coverage of reported votes,
// not a claim that every preference-counting operation is unfinished.
struct CountProgress {
	double fpVotes = 0;
	double tcpVotes = 0;
	double expectedFormalVotes = 0;
	double fpPercent() const { return 100.0 * fpVotes / expectedFormalVotes; }
	double tcpPercent() const { return 100.0 * tcpVotes / expectedFormalVotes; }
};

// Numeric values preserve the existing Results2::VoteType representation used
// in saved project files.
enum class VoteType : int {
	Invalid = 0,
	Ordinary = 1,
	Absent = 2,
	Provisional = 3,
	PrePoll = 4,
	Postal = 5,
	Early = 6,
	IVote = 7,
	Telephone = 8,
	SIR = 9,
	EarlyProvisional = 10,
	EVM = 11,
	TIO = 12,
	MarkedAsVoted = 13,
};

// Numeric values preserve the existing Results2::Booth::Type representation.
enum class BoothType : int {
	Normal = 0,
	Ppvc = 1,
	Remote = 2,
	Prison = 3,
	Hospital = 4,
	Other = 5,
	Invalid = 6,
};

constexpr std::string_view voteTypeName(VoteType voteType)
{
	switch (voteType) {
	case VoteType::Ordinary: return "Ordinary";
	case VoteType::Absent: return "Absent";
	case VoteType::Provisional: return "Provisional";
	case VoteType::PrePoll: return "PrePoll";
	case VoteType::Postal: return "Postal";
	case VoteType::Early: return "Early";
	case VoteType::IVote: return "iVote";
	case VoteType::Telephone: return "Telephone";
	case VoteType::SIR: return "SIR";
	case VoteType::EarlyProvisional: return "Early Provisional";
	case VoteType::EVM: return "EVM";
	case VoteType::TIO: return "TIO";
	case VoteType::MarkedAsVoted: return "Marked as voted";
	case VoteType::Invalid: return "Invalid";
	}
	return "Invalid";
}

constexpr std::string_view boothTypeName(BoothType boothType)
{
	switch (boothType) {
	case BoothType::Normal: return "Normal";
	case BoothType::Ppvc: return "PPVC";
	case BoothType::Remote: return "Remote";
	case BoothType::Prison: return "Prison";
	case BoothType::Hospital: return "Hospital";
	case BoothType::Other: return "Other";
	case BoothType::Invalid: return "Invalid";
	}
	return "Invalid";
}

struct ValueInformation {
	double value = 0.0;
	double completion = 0.0;
	double confidence = 0.0;
};

struct BaselineInformation {
	double baseline = 0.0;
	double completion = 0.0;
	double confidence = 0.0;
	double deviation = 0.0;
};

struct TcpShareInformation {
	std::map<int, double> shares;
	double completion = 0.0;
	double confidence = 0.0;
};

// A scenario's finite vote account. References remain valid while its provider
// is alive. The main simulator may blend the estimated remainder with its
// prior, but cannot change this scenario's counted candidate allocation. It
// equals published counts except for a provider's separate simulated preference
// rechecking path; FP counts and pair totals stay fixed there. Providers without
// integrated turnout counts leave this optional boundary unavailable.
struct VoteCountAccount {
	std::map<int, double> const* counted = nullptr;
	std::map<int, double> const* projected = nullptr;
};
enum class CountKind { Fp, Tpp, Tcp };

// Authoritative final district counts, with the same party identities as the
// provider's finite account. FP/TCP are final together; TPP can be absent if no
// Labor/Coalition scrutiny was published. These votes cannot be resampled,
// shrunk towards a prior, or replaced by a different modelled finalist pair.
struct FinalSeatResult {
	std::map<int, double> fp, tcp, tpp;
	std::string basis;
	std::optional<int> winner;
};

// Final seat sizes used ONLY when aggregating already-calculated live shares.
// FP is formal ballots; TPP is continuing ballots after any exhaustion. A
// scenario returns its own drawn totals and a prepared provider returns means.
// An explicitly inactive contest returns both zeros. These are estimated final
// totals, not the currently received count or a confidence measure.
struct AggregateVoteTotals {
	double fp = 0, tpp = 0;
};

// Moments of actual FP draws, measured in natural log odds and vote fractions.
// Share covariances allow the simulator to combine ballot candidates into its
// anonymous Others group without treating their uncertainties as independent.
// These small runtime objects are rebuilt with the live baseline, not saved in
// a project or retained separately for every snapshot.
struct FpDistributionMoments {
	std::map<int, double> logOddsMean, logOddsVariance, shareMean;
	std::map<std::pair<int, int>, double> shareCovariance;
};
struct FpIntegrationInformation {
	FpDistributionMoments baseline, count;
	// Accepted election + region movement, in the analyser's log odds times 25.
	// Freeze the prepared mean: local random count errors must not also enter
	// the conditional prior indirectly through recalculated regional summaries.
	std::map<int,double> sharedDeviation;
	double evidenceGate = 0;
};

// Diagnostic values used by the live summary export. They remain DTOs rather
// than exposing the live analyser's internal node hierarchy.
struct Internals {
	std::map<BoothType, double> boothTypeBiases;
	std::map<VoteType, double> voteTypeBiases;
	std::map<BoothType, double> boothTypeBiasStdDev;
	std::map<VoteType, double> voteTypeBiasStdDev;
	std::map<BoothType, double> boothTypeBiasesRaw;
	std::map<VoteType, double> voteTypeBiasesRaw;
	std::map<BoothType, double> boothTypeSourceCount;
	std::map<VoteType, double> voteTypeSourceCount;
	std::map<BoothType, double> boothTypeVoteCount;
	std::map<VoteType, double> voteTypeVoteCount;
	double projected2pp = 0.0;
	double raw2ppDeviation = 0.0;
};

// A display statistic from received booth/category evidence. Counted classic
// TCP supplies the share and weight where available. Before preferences arrive,
// use the existing FP-derived booth share, weighted by its received FP count.
// With partial TCP, retain its votes and add the forecast's FP-derived allocation
// of the received votes still awaiting preferences. Never count those votes twice.
// Historical FP can likewise supply a comparison when classic TCP is absent.
// These are comparisons of the received samples: outstanding votes, vote-type
// rebalancing and redistribution discounts belong to the separate live estimate.
struct RawTppSwing {
	double weightedSwing = 0, countedVotes = 0;
	void add(std::map<int,int> const& current, std::map<int,int> const& previous, int nationals,
		double fpVotes = 0, std::optional<double> fpBasedCurrentShare = {},
		std::optional<double> fpBasedPreviousShare = {},
		std::optional<double> pendingFirstVotes = {}) {
		auto classic = [nationals](auto const& counts) {
			return counts.size() == 2 && counts.contains(0) &&
				(counts.contains(1) || (nationals > 0 && counts.contains(nationals)));
		};
		auto total = [](auto const& counts) {
			double sum = 0;
			for (auto const& [party,votes] : counts) { if (votes < 0) return -1.0; sum += votes; }
			return sum;
		};
		double const now = total(current), before = total(previous);
		auto currentShare = fpBasedCurrentShare, previousShare = fpBasedPreviousShare;
		double weight = fpVotes;
		if (classic(current) && now > 0) {
			currentShare = 100.0*current.at(0)/now;
			weight = now;
			// A normal completed booth needs no inferred addition. A partial
			// category instead keeps the same pending-preference allocation as
			// the forecast; a first small TCP batch cannot remove the other FP.
			double const pending = fpVotes-now;
			if (pending > 0 && pendingFirstVotes && std::isfinite(*pendingFirstVotes)
				&& *pendingFirstVotes >= 0 && *pendingFirstVotes <= pending) {
				weight = fpVotes;
				currentShare = 100.0*(current.at(0)+*pendingFirstVotes)/weight;
			}
		}
		if (classic(previous) && before > 0) previousShare = 100.0*previous.at(0)/before;
		auto validShare = [](auto const& share) {
			return share && std::isfinite(*share) && *share >= 0 && *share <= 100;
		};
		if (!std::isfinite(weight) || weight <= 0 || !validShare(currentShare) || !validShare(previousShare)) return;
		weightedSwing += weight * (*currentShare-*previousShare);
		countedVotes += weight;
	}
	std::optional<double> value() const {
		return countedVotes > 0 ? std::optional<double>(double(weightedSwing/countedVotes)) : std::nullopt;
	}
};

struct BoothSnapshot {
	std::string seatName;
	std::string boothName;
	BoothType boothType = BoothType::Invalid;
	VoteType voteType = VoteType::Invalid;
	bool sameSeat = false;
};

// Common set of requests the simulator and result views can make of the live
// analysis object described above. Its prepared form owns the count evidence;
// generateScenario creates a separate possible outcome for one iteration,
// including uncertainty about votes still outstanding. Optional responses let
// simpler implementations (including test substitutes) omit features for which
// they have no data. Missing responses must not be interpreted as zero votes.
class Provider {
public:
	virtual ~Provider() = default;
	// Unavailable for providers without an automatic accepted count account.
	virtual std::optional<CountProgress> getCountProgress() const { return {}; }

	// Input/output are transformed TPP draws in project seat order. The automatic
	// live analysis combines these with count evidence jointly across seats. An
	// implementation without this joint calculation returns no result, allowing
	// the simulator to use its separate seat-by-seat integration instead.
	virtual std::optional<std::vector<double>> integrateTpp(
		std::vector<double> const&) const { return {}; }

	virtual std::unique_ptr<Provider> generateScenario(
		int iterationIndex) const = 0;

	virtual BaselineInformation getFinalSpecificTppInformation() const = 0;
	virtual std::map<int, double> getFinalSpecificFpDeviations() const = 0;
	virtual double getRegionFinalSpecificTppDeviation(int regionIndex) const = 0;
	virtual std::map<int, double> getRegionFinalSpecificFpDeviations(
		int regionIndex) const = 0;
	virtual BaselineInformation getSeatTppInformation(
		std::string const& seatName) const = 0;
	virtual std::map<int, BaselineInformation> getSeatFpInformation(
		std::string const& seatName) const = 0;
	virtual ValueInformation getSeatOthersInformation(
		std::string const& seatName,
		std::map<int, double> const& representedParties) const = 0;
	virtual TcpShareInformation getSeatTcpInformation(
		std::string const& seatName) const = 0;
	// A conditional share of votes for the first supplied finalist. Providers
	// without live preference learning use the ordinary TCP integration.
	virtual std::optional<double> getLearnedTcpShare(std::string const&, std::array<int,2>,
		std::span<PreferenceModel::FinalSource const>) const { return {}; }
	// Providers without turnout integration return no account. Leave these
	// intentionally unused parameters unnamed in the default implementation.
	virtual std::optional<VoteCountAccount> getSeatVoteCountAccount(
		std::string const&, CountKind) const { return {}; }
	virtual FinalSeatResult const* getFinalSeatResult(std::string const&) const { return nullptr; }
	// LiveAggregation uses these final-size estimates to weight seat vote shares
	// in election/region summaries. Automatic live analysis supplies every seat,
	// including zeros for explicitly inactive contests. Implementations without
	// a count-progress account use historical weights and do not need these totals;
	// this is about available information, not the age of a feed or saved project.
	virtual std::optional<AggregateVoteTotals> getSeatAggregateVoteTotals(
		std::string const&) const { return {}; }
	// Borrowed immutable moments for the FP precision handoff. Its lifetime is
	// the provider's; scenario copies share preparation rather than copying draws.
	virtual FpIntegrationInformation const* getSeatFpIntegrationInformation(
		std::string const&) const { return nullptr; }
	virtual std::optional<ValueInformation> getSeatNationalsProportion(
		std::string const& seatName) const = 0;

	// Prepared seat-wide TPP estimate (counted plus outstanding votes), relative
	// to the configured previous margin. Returns NaN before live evidence exists.
	virtual double getSeatEstimatedTppSwing(std::string const& seatName) const = 0;
	// Raw, count-weighted booth swing (TCP where available, otherwise FP-derived),
	// without forecast blending or outstanding-vote adjustments.
	virtual double getSeatRawTppSwing(std::string const& seatName) const = 0;
	virtual double getSeatFpCompletion(std::string const& seatName) const = 0;
	virtual double getSeatTppCompletion(std::string const& seatName) const = 0;
	virtual double getSeatTcpCompletion(std::string const& seatName) const = 0;

	virtual Internals getInternals() const = 0;
	virtual std::vector<BoothSnapshot> getBoothSnapshots() const = 0;
};

} // namespace LiveData
