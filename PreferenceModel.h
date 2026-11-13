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
#include <utility>
#include <vector>

namespace PreferenceModel {

using Shares = std::map<int, double>;
using Pair = std::array<int, 2>;
using ConfiguredFlows = std::map<int, std::map<std::pair<int, int>, float>>;

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
		std::span<District const> districts, ConfiguredFlows const& configuredFlows);

	struct ElectionDraw {
		std::uint64_t seed = 0;
		std::array<std::array<double, 5>, ProfileCount> commonNodes{};
		bool liberalLaborFirst = false;
		bool central = false;
	};

	ElectionDraw drawElection(std::uint64_t seed) const;
	CountResult count(int district, std::span<Source const> sources,
		double laborTpp, double nationalIncoming, ElectionDraw const& election,
		Sensitivity sensitivity = Sensitivity::None, bool reportFinalSources = false) const;
	// Expected original-source rates from the same ranking profiles, using
	// the supplied district primaries and pre-count classic flow assumptions.
	Shares referencePairRates(int district, std::span<Source const> sources, Pair pair) const;
	bool learnsPair(Pair pair) const;
	SensitivityTests const& sensitivityTests(int district) const;
	bool isActive() const { return active; }
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
	struct Context {
		Advice advice = Advice::Unknown;
		double liberalOneNationLastWeight = 0.0;
		bool highIndependentFlow = false;
		bool namedIndependent = false;
		bool rightIndependent = false;
		SensitivityTests sensitivity;
	};

	struct SourceProfile {
		int id;
		double primary;
		RankingProfile ranking;
		bool coalitionSlot;
	};
	std::vector<SourceProfile> buildSourceProfiles(int district, std::span<Source const> ballot,
		Shares const& primaries, double leakageVotes, bool dual, bool classicAvailable,
		Shares const& classicRates, ElectionDraw const& election, Sensitivity sensitivity) const;

	Nodes drawNodes(ProfileIndex profile, double primary, std::optional<double> classicRate,
		int district, ElectionDraw const& election) const;
	std::vector<Ranking> coreRankings(Source const& source, int coalition,
		std::optional<double> classicRate, int district, ElectionDraw const& election,
		double laborToGreensReduction) const;
	double sampleFallback(double mean, int district, int source, int branch,
		ElectionDraw const& election, std::optional<double> districtConcentration = {}) const;
	double recipientWeight(Source const& source, Source const& recipient,
		int district, ElectionDraw const& election) const;

	Roles roles{};
	std::string modelVersion;
	std::uint64_t versionKey = 0;
	bool active = false;
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
