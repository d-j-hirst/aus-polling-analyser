#pragma once

#include "LiveTppIntegration.h"

#include <map>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Portable boundary between the core simulation and a live-results analysis
// implementation. These types deliberately have no dependency on electoral-
// commission parsers, wxWidgets, or third-party libraries.
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
	float value = 0.0f;
	float completion = 0.0f;
	float confidence = 0.0f;
};

struct BaselineInformation {
	float baseline = 0.0f;
	float completion = 0.0f;
	float confidence = 0.0f;
	float deviation = 0.0f;
};

struct TcpShareInformation {
	std::map<int, float> shares;
	float completion = 0.0f;
	float confidence = 0.0f;
};

// A scenario's finite vote account. References remain valid while its provider
// is alive. The main simulator may blend the estimated remainder with its
// prior, but cannot change this scenario's counted candidate allocation. It
// equals published counts except for a provider's separate simulated preference
// rechecking path; FP counts and pair totals stay fixed there. Providers without
// integrated turnout counts leave this optional boundary unavailable.
struct VoteCountAccount {
	std::map<int, double> const* counted = nullptr;
	std::map<int, float> const* projected = nullptr;
};
enum class CountKind { Fp, Tpp, Tcp };

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
	std::map<int,float> sharedDeviation;
	double evidenceGate = 0;
};

// Diagnostic values used by the live summary export. They remain DTOs rather
// than exposing the live analyser's internal node hierarchy.
struct Internals {
	std::map<BoothType, float> boothTypeBiases;
	std::map<VoteType, float> voteTypeBiases;
	std::map<BoothType, float> boothTypeBiasStdDev;
	std::map<VoteType, float> voteTypeBiasStdDev;
	std::map<BoothType, float> boothTypeBiasesRaw;
	std::map<VoteType, float> voteTypeBiasesRaw;
	std::map<BoothType, float> boothTypeSourceCount;
	std::map<VoteType, float> voteTypeSourceCount;
	std::map<BoothType, float> boothTypeVoteCount;
	std::map<VoteType, float> voteTypeVoteCount;
	float projected2pp = 0.0f;
	float raw2ppDeviation = 0.0f;
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
	std::optional<float> value() const {
		return countedVotes > 0 ? std::optional<float>(float(weightedSwing/countedVotes)) : std::nullopt;
	}
};

struct BoothSnapshot {
	std::string seatName;
	std::string boothName;
	BoothType boothType = BoothType::Invalid;
	VoteType voteType = VoteType::Invalid;
	bool sameSeat = false;
};

// Read-only contract consumed by simulation iterations, completion reporting,
// and live-result GUI views. Implementations own their internal data and return
// an independently owned provider for each stochastic scenario.
class Provider {
public:
	virtual ~Provider() = default;
	// Unavailable for providers without an automatic accepted count account.
	virtual std::optional<CountProgress> getCountProgress() const { return {}; }

	// Input/output are transformed TPP draws in project seat order. Automatic
	// live providers condition the whole vector together; legacy/manual providers
	// can retain their existing handoff by leaving this boundary unavailable.
	virtual std::optional<std::vector<double>> integrateTpp(
		std::vector<double> const&) const { return {}; }

	virtual std::unique_ptr<Provider> generateScenario(
		int iterationIndex) const = 0;

	virtual BaselineInformation getFinalSpecificTppInformation() const = 0;
	virtual std::map<int, float> getFinalSpecificFpDeviations() const = 0;
	virtual float getRegionFinalSpecificTppDeviation(int regionIndex) const = 0;
	virtual std::map<int, float> getRegionFinalSpecificFpDeviations(
		int regionIndex) const = 0;
	virtual BaselineInformation getSeatTppInformation(
		std::string const& seatName) const = 0;
	virtual std::map<int, BaselineInformation> getSeatFpInformation(
		std::string const& seatName) const = 0;
	virtual ValueInformation getSeatOthersInformation(
		std::string const& seatName,
		std::map<int, float> const& representedParties) const = 0;
	virtual TcpShareInformation getSeatTcpInformation(
		std::string const& seatName) const = 0;
	// Providers without turnout integration return no account. Leave these
	// intentionally unused parameters unnamed in the default implementation.
	virtual std::optional<VoteCountAccount> getSeatVoteCountAccount(
		std::string const&, CountKind) const { return {}; }
	// Borrowed immutable moments for the FP precision handoff. Its lifetime is
	// the provider's; scenario copies share preparation rather than copying draws.
	virtual FpIntegrationInformation const* getSeatFpIntegrationInformation(
		std::string const&) const { return nullptr; }
	virtual std::optional<ValueInformation> getSeatNationalsProportion(
		std::string const& seatName) const = 0;

	// Prepared seat-wide TPP estimate (counted plus outstanding votes), relative
	// to the configured previous margin. Returns NaN before live evidence exists.
	virtual float getSeatEstimatedTppSwing(std::string const& seatName) const = 0;
	// Raw, count-weighted booth swing (TCP where available, otherwise FP-derived),
	// without forecast blending or outstanding-vote adjustments.
	virtual float getSeatRawTppSwing(std::string const& seatName) const = 0;
	virtual float getSeatFpCompletion(std::string const& seatName) const = 0;
	virtual float getSeatTppCompletion(std::string const& seatName) const = 0;
	virtual float getSeatTcpCompletion(std::string const& seatName) const = 0;

	virtual Internals getInternals() const = 0;
	virtual std::vector<BoothSnapshot> getBoothSnapshots() const = 0;
};

} // namespace LiveData
