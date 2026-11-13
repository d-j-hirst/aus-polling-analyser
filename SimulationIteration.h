#pragma once

// Calculate one possible whole election using the inputs prepared for the run.
// runIteration in the .cpp file explains the sequence and the recording boundary.

#include "LiveData.h"
#include "Party.h"
#include "SimulationRun.h"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

class PollingProject;
struct Region;
class Seat;
class Simulation;

// One iteration is a possible result for every seat in the same election. The
// project and sim supply settings and the overall vote-share projection; run
// supplies the historical results, model estimates and any prepared live evidence.
// Shared election and regional movements allow seats to move together, alongside
// uncertainty specific to each seat.
//
// Each object keeps its own possible vote shares and winners while other
// iterations are calculated concurrently. A successful result contributes to
// totals in run and sim's report; SimulationCompletion later turns the combined
// results into forecast probabilities and summaries of vote shares and seats.
// Working electoral quantities use double precision, including intermediate
// preference allocations. Stored model inputs can still be floats: promoting
// them once is safe, while rounding a live update back to float during the draw
// would hide small real changes and manufacture differences in unchanged values.
class SimulationIteration {
public:
	// Refer to the shared settings and prepared inputs. iterationIndex identifies
	// this simulated election and is used to select its random draws.
	SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run, int iterationIndex);

	// Calculate and record one election, retrying specified calculation failures
	// within a limit. Return the number of discarded attempts (zero if the first
	// succeeds), so SimulationRun can monitor failures across the whole run.
	// An unrecoverable error is passed to SimulationRun instead of a result.
	int runIteration();
private:
	void capturePrecisionAudit(char const* stage);
	nlohmann::json precisionStages;
	void reset();
	bool hasInvalidValues(std::string const& location, bool forceDebug = false, bool checkTppMargins = true);
	void initialiseIterationSpecificCounts();
	void determineFedStateCorrelation();
	void determineOverallTpp();
	void determineIntraCoalitionSwing();
	void determineIndDistributionParameters();
	void decideMinorPartyPopulism();
	void determineHomeRegions();
	void determineRegionalSwings();
	void determineMinorPartyContests();
	void loadPastSeatResults();
	void determineBaseRegionalSwing(int regionIndex);
	void correctRegionalSwings();
	void determineSeatInitialResults();
	void correctSeatTppSwings();
	void determineSeatTpp(int seatIndex);
	void determineSeatInitialFp(int seatIndex);
	void determineSpecificPartyFp(
		int seatIndex,
		int partyIndex,
		double& voteShare,
		SimulationRun::SeatStatistics const& seatStatistics);
	double applySeatFpPolls(
		int seatIndex, int partyIndex, double transformedVoteShare) const;
	void determinePopulistFp(int seatIndex, int partyIndex, double& voteShare);
	void determineSeatConfirmedInds(int seatIndex);
	void determineSeatEmergingInds(int seatIndex);
	void determineSeatOthers(int seatIndex);
	void adjustForFpCorrelations(int seatIndex);
	void prepareFpsForNormalisation(int seatIndex);
	void determineSeatEmergingParties(int seatIndex);
	void determineNationalsShare(int seatIndex);
	bool usesSeparateCoalitionPreferenceAccounting(int seatIndex) const;
	void constrainCoalitionPrimariesToTpp(int seatIndex);
	struct MajorPartyFpAllocation {
		double nonExhaustedVote = 0.0;
		double directPartyOneFp = 0.0;
		double directPartyTwoFp = 0.0;
		double coalitionLeakageFraction = 0.0;
	};
	MajorPartyFpAllocation allocateMajorPartyFp(
		int seatIndex,
		double preferenceFlowDeviation = 0.0);
	void allocateMajorPartyFpsWithCompatibleTpp();
	void normaliseSeatFp(int seatIndex, int fixedParty = -10000, double fixedVote = 0.0);
	void reconcileSeatAndOverallFp();
	void calculateNewFpVoteTotals(
		bool finalCheckpoint = false,
		std::map<int, double> const* preTerminalFp = nullptr,
		double preTerminalFpError = 0.0);
	void calculatePreferenceCorrections();
	void applyCorrectionsToSeatFps();
	void correctMajorPartyFpBias(bool accountForSeatNormalisation = false);
	void solveTerminalFpReconciliation();
	void incorporateLiveResults();
	void incorporateLiveTpp();
	void incorporateLegacyLiveTpp();
	void recordLiveTppBaseline();
	void incorporateLiveSeatFp(int seatIndex);
	void prepareLiveFpCandidates(int seatIndex, LiveTurnoutMath::PartyAccount const& account);
	void conditionLiveFpPrior(int seatIndex);
	void recombineLiveCoalition(int seatIndex);
	void integrateLiveFpAccount(int seatIndex, LiveData::VoteCountAccount const& account,
		LiveData::FpIntegrationInformation const& information);
	void restoreLiveFpGroups(int seatIndex, std::map<int,double> integrated, int independentGroup);
	void determineSeatFinalResult(int seatIndex);
	using TcpPair = std::pair<std::pair<int, double>, std::pair<int, double>>;
	bool integrateLiveTcpPair(int seatIndex, TcpPair& pair, bool livePrincipalProxy) const;
	void countSeatPreferences(int seatIndex, double mergedIndependent);
	std::optional<TcpPair> integrateRankingFinalPair(int seatIndex, bool livePrincipalProxy);
	void compareFinalPair(int seatIndex, TcpPair const& beforeLive, TcpPair const& afterLive, bool livePrincipalProxy);
	bool validSeatFinalPair(TcpPair const& pair) const;
	void recordSeatFinalPair(int seatIndex, TcpPair const& pair);
	bool hasNationalsVotesToRestore(int seatIndex) const;
	void assignNationalsVotes(int seatIndex);
	void applyLiveManualOverrides(int seatIndex);
	void recordSeatResult(int seatIndex);
	void assignDirectWins();
	void assignSupportsPartyWins();
	void recordMajorityResult();
	void recordPartySeatWinCounts();
	void recordSeatPartyWinner(int seatIndex);
	void recordSeatFpVotes(int seatIndex);
	void recordSeatTcpVotes(int seatIndex);
	void recordSeatTppVotes(int seatIndex);
	void prepareAggregationWeights();
	void recordRegionFpVotes(int regionIndex);
	void recordRegionTppVotes(int regionIndex);
	void recordElectionFpVotes();
	void recordElectionTppVotes();
	void recordIterationResults();
	void recordVoteTotals();
	void recordSwings();
	void recordSwingFactors();

	double calculateEffectiveSeatModifier(int seatIndex, int partyIndex) const;

	double variabilityNormal(double mean, double sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	double variabilityUniform(double low, double high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	int variabilityUniformInt(int low, int high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	double variabilityGamma(double alpha, double beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	double variabilityBeta(double alpha, double beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	int randomSampleIndex() const;

	struct SeatCandidate { int vote; Party::Id partyId; double weight; };
	typedef std::vector<SeatCandidate> SeatCandidates;

	PollingProject & project;
	Simulation& sim;
	// Workers share prepared inputs, but calculate each scenario in their own
	// local state. A const view prevents a missing map key from being silently
	// inserted by operator[] while other workers are reading the same inputs.
	SimulationRun const& run;
	// Shared output changes belong only in the mutex-protected recording stage,
	// or in recordWarning(), which provides its own synchronization.
	SimulationRun& recordedRun;

	typedef std::map<int, double> ShareByPartyIndex;

	// Per-attempt state. Add new generated values here to reset().
	std::unique_ptr<LiveData::Provider> liveElection;
	// Reporting-only weights, prepared after seat winners are settled. They must
	// not enter the pre-election reconciliation or any seat-level calculation.
	std::vector<double> aggregationFpWeights, aggregationTppWeights;
	
	std::vector<SimulationRun::PastSeatResult> pastSeatResults;
	std::map<int, std::vector<int>> regionSeatCount; // party, then region
	std::map<int, int> partyWins;
	std::vector<double> regionSwing;
	std::vector<double> partyOneNewTppMargin;
	std::vector<Party::Id> seatWinner;
	std::vector<ShareByPartyIndex> seatFpVoteShare;
	std::vector<std::pair<std::pair<int, int>, double>> seatTcpVoteShare;
	std::vector<double> nationalsShare; // Nationals' share of the LIB/NAT vote
	// Preference rates from the accepted FP pass, plus private comparison
	// observations for this attempt. Supporting runs need neither.
	std::vector<std::vector<std::pair<int, double>>> preferenceSourceRates;
	std::vector<PreferenceModel::Comparison> preferenceComparisons;
	std::optional<PreferenceModel::Model::ElectionDraw> preferenceElection;
	std::string preferenceDrawIssue;
	double iterationOverallTpp = 0.0;
	double iterationOverallSwing = 0.0;
	double intraCoalitionSwing = 0.0;
	int daysToElection = 0;
	ShareByPartyIndex overallFpTarget;
	ShareByPartyIndex overallFpSwing;
	ShareByPartyIndex overallPreferenceFlow;
	ShareByPartyIndex overallExhaustRate; // for TPP distribution
	std::map<Party::Id, int> homeRegion;
	std::map<Party::Id, std::vector<bool>> seatContested;
	ShareByPartyIndex centristPopulistFactor; // e.g. 1 = full populist, 0 = full centrist
	std::map<Party::Id, int> partyIdeologies;
	std::map<Party::Id, int> partyConsistencies;
	ShareByPartyIndex fpModificationAdjustment;
	ShareByPartyIndex tempOverallFp;

	std::map<Party::Id, double> postCountFpShift;

	std::vector<double> seatRegionSwing;
	std::vector<double> seatElasticitySwing;
	std::vector<double> seatLocalEffects;
	std::vector<double> seatPreviousSwingEffect;
	std::vector<double> seatFederalSwingEffect;
	std::vector<double> seatByElectionEffect;
	std::vector<double> seatThirdPartyExhaustEffect;
	std::vector<double> seatPollEffect;
	std::vector<double> seatMrpPollEffect;

	double prefCorrection = 0.0;
	double overallFpError = 0.0;
	double othersCorrectionFactor = 0.0;
	double fedStateCorrelation = 0.0;
	double ppvcBias = 0.0;
	double indAlpha = 1.0;
	double indBeta = 1.0;
	bool neededTerminalFpReconciliation = false;

	// Stable identity and retry bookkeeping. These are intentionally not reset
	// with the generated state above.
	int iterationIndex = 0;
	int retryCount = 0;

	std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;

	std::array<int, 2> effectiveWins = std::array<int, 2>();
	std::array<int, 2> partySupport = std::array<int, 2>();
};
