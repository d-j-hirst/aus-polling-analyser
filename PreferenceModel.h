#pragma once

// A compulsory-preferential count for a simulated district. Each source is
// the set of ballots with the same original first preference. One ranking
// profile for that source supplies its transfers at every exclusion.

#include <array>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PreferenceModel {

using Shares = std::map<int, double>;
using Pair = std::array<int, 2>;
using ConfiguredFlows = std::map<int, std::map<std::pair<int, int>, float>>;

// When the Greens or another third candidate is removed from the three-way
// count, these fractions describe how its primary and inherited ballots
// rank Labor over the Coalition. Only the expected flows are supplied here.
struct ThreeCandidateTppPrior {
	double primaryToLabor, inheritedToLabor;
};

// A current-election administrative count. Fractions describe only the
// redistributed ballots. The optional first preferences identify the pool
// which was actually sorted, for example Greens and one independent.
struct CountObservation {
	std::string district, availableAt, sample, source;
	int ballots;
	std::map<std::string, double> flows;
	std::map<std::string, int> firstPreferences;
	double additionalSd;
	std::map<std::string, int> candidateTotals; // Complete 3CP tallies on the sampled ballots.
	std::map<std::string, int> candidateFirstPreferences;
	std::optional<ThreeCandidateTppPrior> tppPrior;
};

// Read all revisions and choose one per district as of a particular feed.
// The cutoff is an ISO timestamp in the source's clock or with an explicit
// offset. Input observations always specify their UTC offset explicitly.
std::map<std::string, CountObservation> loadCountObservations(std::istream& input,
	std::string_view cutoff, std::string_view jurisdiction);

// Preparation resolves the observation's candidate names to the same IDs
// that the count uses. Simulation draws borrow this immutable evidence.
struct PreparedCountObservation {
	Shares flows, firstPreferences;
	double variance;
	Shares candidateTotals{}; // Empty for observations covering only selected donors.
	Shares candidateFirstPreferences{};
	double ballots = 0.0, outsideFlowSd = 0.0;
	std::optional<ThreeCandidateTppPrior> tppPrior{};
};

// The district's simulated formal turnout makes the published sample and
// drawn FP shares comparable. Counted classic TPP takes precedence over an
// estimate based on the sample, including when the count is still partial.
struct ThreeCandidateCountContext {
	double finalFormalVotes;
	bool hasCountedTpp;
};

// Higher consistency makes the generic rankings more responsive to distance
// on the configured political scale. Use the same weights for a forecast
// draw and the central reference used to compare live preference counts.
double proximityForConsistency(int consistency);

struct Ranking {
	std::vector<int> order;
	double fraction = 0.0; // of this source's original ballots
};

// Keep explicit orders for the recipients with evidence. Positive weights
// place the other recipients among those orders. A query returns fractions
// of the original source in the same order as the supplied surviving IDs.
class RankingProfile {
public:
	RankingProfile(std::vector<Ranking> rankings, Shares weights);
	std::vector<double> allocate(std::span<int const> survivors) const;

private:
	struct PreparedOrder {
		Ranking ranking;
		std::vector<double> remainingCoreWeights;
	};
	std::vector<PreparedOrder> orders;
	std::vector<int> core;
	Shares recipientWeights;
};

struct Roles {
	int labor, liberal;
	// Only projects with a separate Coalition partner have a National slot.
	// A minor candidate grouped in Others contributes through that source.
	std::optional<int> national;
	int greens, oneNation, independent, emergingIndependent, others;
};

struct District {
	std::string name;
	std::string independentName;
	bool coalitionHeld = false;
};

struct Source {
	// Original FP as a fraction of the district's formal votes. Others is a
	// source with a vote share, and transfers before the first exclusion.
	int id;
	double primary;
	int ideology;
	double proximityBase;
	double classicRate; // fraction of the original source ranking Labor above Coalition
	// Identifies an additional independent by their live ballot ID. The two
	// regular independent slots already carry this affiliation through Roles.
	bool additionalIndependent = false;
	// Confirms that a regular independent slot represents a distinct person
	// on the current ballot, including a principal using the emerging slot.
	bool confirmedIndependent = false;
};

struct PairResult {
	Pair candidates{}; // ascending by party ID
	double firstShare = 0.0;
	std::optional<int> selectedWinner;
	int winner() const;
};

// Original FP share and fraction of those ballots reaching the first
// finalist. Only automatic live counts request these after exclusions.
struct FinalSource {
	int id;
	double primary, firstRate;
};

struct ClassicCheck {
	// All quantities are fractions. The bounds and the error are shares of
	// district formal votes. The largest rate change is within one source.
	double lower = 0.0, upper = 1.0, unadjusted = 0.0;
	double maximumError = 0.0;
	int pairsChecked = 0;
	double largestRateChange = 0.0;
	int changedSource = 0;
	double previousRate = 0.0, adjustedRate = 0.0;
};

enum class Status { FinalOfficial, Compared, InfeasibleClassicTpp, InvalidAccount };

struct CountResult {
	Status status = Status::InvalidAccount;
	PairResult final;
	ClassicCheck classic;
	double laborTpp = 0.0;
	std::string issue;
	std::vector<FinalSource> finalSources;
	bool conditionedOnLiveSample = false;
	// Mean transfers can be checked against the observed split in the live log.
	// An empty map identifies draws using the ordinary ranking count.
	Shares jointThreeCandidateFlows;
	bool jointThreeCandidateIncompatible = false;
};

// Each sensitivity count changes one assumption for the same district draw.
// The reduction is a fraction of the Labor ballots that rank the Greens
// above the Coalition, as opposed to a share of all district votes.
enum class Sensitivity { None, WithoutClassicTppMatching, ReducedLaborToGreens };

struct SensitivityTests {
	bool withoutClassicTppMatching = false;
	double laborToGreensReduction = 0.0;
};

// Model owns the fixed parameters shared by all iterations. It samples an
// election, then builds district rankings from the simulated FP and classic
// TPP. The mode determines whether these results feed the forecast or serve
// as a comparison alongside it.
class Model {
	enum ProfileIndex { Labor, LiberalLaborFirst, LiberalGreensFirst, Greens, OneNation, LiberalOpen, ProfileCount };

public:
	// The captured file is optional. An enabled file is checked against the
	// resolved project IDs and roster, with errors reported during preparation.
	// configuredFlows are the project's legacy pair means in percentage units.
	static std::optional<Model> load(std::istream& input, Roles roles,
		std::span<District const> districts, ConfiguredFlows const& configuredFlows,
		std::map<std::string, int> const& partyCodes = {});

	struct ElectionDraw {
		std::uint64_t seed = 0;
		std::array<std::array<double, 5>, ProfileCount> commonNodes{};
		bool liberalLaborFirst = false;
		bool central = false;
	};

	ElectionDraw drawElection(std::uint64_t seed) const;
	CountResult count(int district, std::span<Source const> sources,
		double laborTpp, double nationalIncoming, ElectionDraw const& election,
		Sensitivity sensitivity = Sensitivity::None, bool reportFinalSources = false,
		bool namedIndependentMatches = true, PreparedCountObservation const* observation = nullptr,
		std::optional<ThreeCandidateCountContext> countContext = {}) const;
	// Expected original-source rates from the same ranking profiles, using
	// the supplied district primaries and pre-count classic flow assumptions.
	// Each source's fractions are returned in the order of the supplied survivors.
	std::map<int, std::vector<double>> referenceTransferRates(int district, std::span<Source const> sources,
		std::span<int const> survivors, bool namedIndependentMatches = true) const;
	Shares referencePairRates(int district, std::span<Source const> sources, Pair pair,
		bool namedIndependentMatches = true) const;
	bool learnsPair(Pair pair) const;
	SensitivityTests const& sensitivityTests(int district) const;
	bool isActive() const { return active; }
	bool isLiveTrial() const { return liveTrial; }
	bool coversDistrict(int district) const { return !liveTrial || contexts.at(static_cast<std::size_t>(district)).liveTrial; }
	std::string const& version() const { return modelVersion; }
	Roles const& partyRoles() const { return roles; }

private:
	enum class Advice { Unknown, LaborFirst, GreensFirst, Open };

	using Nodes = std::array<double, 5>;
	struct Strength {
		double primary;
		Nodes means;
	};
	struct Prior {
		std::vector<Strength> strengths;
		Nodes referenceMeans{};
		Nodes electionConcentration{};
		Nodes districtConcentration{};
	};
	struct HistoricalTransfer {
		// Counts of ballots transferred at one exclusion. Some ballots may
		// have started with candidates excluded in earlier rounds.
		std::vector<std::pair<int, int>> destinations;
		int originalVotes, inheritedVotes;
		double weight;
		bool usesNamedIndependent;
	};
	struct Context {
		Advice advice = Advice::Unknown;
		double liberalOneNationLastWeight = 0.0;
		bool highIndependentFlow = false;
		bool namedIndependent = false;
		bool rightIndependent = false;
		bool liveTrial = false;
		SensitivityTests sensitivity;
		std::map<int, HistoricalTransfer> historicalTransfers;
	};

	struct SourceProfile {
		int id;
		double primary;
		RankingProfile ranking;
		bool coalitionSlot;
	};
	std::vector<SourceProfile> buildSourceProfiles(int district, std::span<Source const> ballot,
		Shares const& primaries, double leakageVotes, bool dual, bool classicAvailable,
		Shares const& classicRates, ElectionDraw const& election, Sensitivity sensitivity,
		bool namedIndependentMatches, std::optional<std::uint64_t> localSeed = {},
		std::optional<int> onlySource = {}) const;
	std::vector<SourceProfile> conditionOnCountObservation(std::vector<SourceProfile> profiles,
		PreparedCountObservation const& observation, int district, std::span<Source const> ballot,
		Shares const& primaries, double leakageVotes, bool dual, bool classicAvailable,
		Shares const& classicRates, double nationalIncoming, ElectionDraw const& election,
		Sensitivity sensitivity, bool namedIndependentMatches, bool& usedSample) const;
	std::optional<CountResult> countWithThreeCandidateTotals(PreparedCountObservation const& observation,
		ThreeCandidateCountContext context, int district, std::span<Source const> ballot,
		Shares const& primaries, std::vector<int> candidates, double laborTpp,
		bool dual, double leakageVotes, double nationalIncoming, ElectionDraw const& election,
		bool namedIndependentMatches) const;
	void applyHistoricalTransfer(std::vector<Ranking>& rankings, Shares const& weights, int district,
		int source, int independent, Shares const& primaries, ElectionDraw const& election,
		bool namedIndependentMatches, std::uint64_t localSeed) const;
	bool isIndependent(Source const& source) const;

	Nodes drawNodes(ProfileIndex profile, double primary, std::optional<double> classicRate,
		int district, ElectionDraw const& election, std::uint64_t localSeed) const;
	std::vector<Ranking> coreRankings(Source const& source, int coalition,
		std::optional<double> classicRate, int district, ElectionDraw const& election,
		double laborToGreensReduction, std::uint64_t localSeed) const;
	double sampleFallback(double mean, int district, int source, int branch,
		ElectionDraw const& election, std::uint64_t localSeed, std::optional<double> districtConcentration = {}) const;
	double recipientWeight(Source const& source, Source const& recipient,
		int district, ElectionDraw const& election, std::uint64_t localSeed) const;

	Roles roles{};
	std::string modelVersion;
	std::uint64_t versionKey = 0;
	bool active = false;
	bool liveTrial = false;
	bool sampleUncertainty = true;
	double unknownLiberalLaborFirst = 0.5;
	double genericElectionConcentration = 24.0;
	double genericDistrictConcentration = 9.666666666666666;
	std::array<Prior, ProfileCount> priors;
	std::vector<Context> contexts;
	std::map<int, double> independentOverMajor;
	std::optional<double> nationalGreensOverLabor, nationalOneNationOverLabor;
};

// Each successful simulation iteration contributes at most one observation
// per district. The runner combines them after normal result recording so a
// retried scenario does not appear twice in the comparison.
struct Comparison {
	CountResult model;
	PairResult legacy;
	PairResult modelWithLiveEvidence;
	PairResult legacyWithLiveEvidence;
	bool modelLivePairMatched = false;
	std::optional<CountResult> withoutClassicTppMatching;
	std::optional<CountResult> reducedLaborToGreens;
};

// Tally the two counts in each finalist pair. The two sides can reach the
// pair in different draws. Measure the change in TCP separately among the
// draws that reach it in both counts, using the first candidate in ID order.
struct PairComparisonSummary {
	struct Outcomes {
		std::uint64_t draws = 0, firstWins = 0;
		double firstShareSum = 0.0;
	};
	struct PairTotals {
		std::array<Outcomes, 2> outcomes;
		std::uint64_t samePairDraws = 0, samePairWinnerChanges = 0;
		double samePairShareChange = 0.0, samePairAbsoluteChange = 0.0;
	};
	std::uint64_t compared = 0, failed = 0, changedPair = 0, changedWinner = 0;
	std::map<Pair, PairTotals> pairs;
	void record(PairResult const& reference, PairResult const& alternative);
};

struct Summary {
	struct Example {
		int iteration;
		CountResult result;
	};
	std::uint64_t official = 0, compared = 0, infeasible = 0, invalid = 0;
	std::uint64_t conditionedOnLiveSample = 0;
	std::uint64_t jointThreeCandidateDraws = 0;
	std::uint64_t jointThreeCandidateIncompatible = 0;
	Shares jointThreeCandidateFlowSums;
	std::uint64_t changedPair = 0, changedWinner = 0, changedWinnerWithLiveEvidence = 0;
	std::uint64_t livePairMatches = 0, classicPairs = 0, samePair = 0;
	double samePairAbsoluteChange = 0.0, samePairLiveAbsoluteChange = 0.0;
	double maximumClassicError = 0.0, sumLargestRateChange = 0.0;
	std::map<Pair, std::pair<std::uint64_t, std::uint64_t>> pairs; // existing and comparison counts
	std::map<int, std::pair<std::uint64_t, std::uint64_t>> winners;
	std::optional<Example> firstIssue, largestAdjustment;
	PairComparisonSummary standardVsRanking, withoutClassicTppMatching, reducedLaborToGreens;
	// Active counts need the same accounting and failure totals without a
	// second count of the district. The return value marks a usable ranking.
	bool recordCount(CountResult const& model, int iteration);
	void record(Comparison const& comparison, int iteration);
};

}
