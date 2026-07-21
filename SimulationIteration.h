#pragma once

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

class SimulationIteration {
public:
	SimulationIteration(PollingProject& project, Simulation& sim, SimulationRun& run, int iterationIndex);

	// Returns the number of discarded attempts before a valid result was produced.
	int runIteration();
private:
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
		float& voteShare,
		SimulationRun::SeatStatistics const& seatStatistics);
	void determinePopulistFp(int seatIndex, int partyIndex, float& voteShare);
	void determineSeatConfirmedInds(int seatIndex);
	void determineSeatEmergingInds(int seatIndex);
	void determineSeatOthers(int seatIndex);
	void adjustForFpCorrelations(int seatIndex);
	void prepareFpsForNormalisation(int seatIndex);
	void determineSeatEmergingParties(int seatIndex);
	void determineNationalsShare(int seatIndex);
	struct MajorPartyFpDiagnostics {
		float availableMajorFp = 0.0f;
		float nonExhaustedVote = 0.0f;
		float partyOnePreferences = 0.0f;
		float partyTwoPreferences = 0.0f;
		float directPartyOneFp = 0.0f;
		float directPartyTwoFp = 0.0f;
		float transformedPartyOneFp = 0.0f;
		float transformedPartyTwoFp = 0.0f;
		float preNormalisationPartyOneFp = 0.0f;
		float preNormalisationPartyTwoFp = 0.0f;
		float originalPartyOneTpp = 0.0f;
		float adjustedPartyOneTpp = 0.0f;
		int compatibilityPasses = 0;
		bool usedDirectAllocation = false;
	};
	void allocateMajorPartyFp(
		int seatIndex,
		float preferenceFlowDeviation = 0.0f,
		MajorPartyFpDiagnostics* diagnostics = nullptr);
	void allocateMajorPartyFpsWithCompatibleTpp(
		std::vector<MajorPartyFpDiagnostics>* diagnostics = nullptr);
	void normaliseSeatFp(int seatIndex, int fixedParty = -10000, float fixedVote = 0.0f);
	void reconcileSeatAndOverallFp();
	void calculateNewFpVoteTotals(
		bool finalCheckpoint = false,
		std::map<int, float> const* preTerminalFp = nullptr,
		float preTerminalFpError = 0.0f);
	void calculatePreferenceCorrections();
	void applyCorrectionsToSeatFps(int reconciliationCycle);
	void correctMajorPartyFpBias(bool accountForSeatNormalisation = false);
	void solveTerminalFpReconciliation();
	std::map<int, float> calculateCurrentFpTotalsForDiagnostics() const;
	void logDetailedFpReconciliationStage(
		int reconciliationCycle,
		std::string const& stage,
		std::map<int, float> const& totals) const;
	void incorporateLiveResults();
	void determineSeatFinalResult(int seatIndex);
	void assignNationalsVotes(int seatIndex, bool updateFromLive = true);
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
	void recordRegionFpVotes(int regionIndex);
	void recordRegionTppVotes(int regionIndex);
	void recordElectionFpVotes();
	void recordElectionTppVotes();
	void recordIterationResults();
	void recordVoteTotals();
	void recordSwings();
	void recordSwingFactors();

	float calculateEffectiveSeatModifier(int seatIndex, int partyIndex) const;

	float variabilityNormal(float mean, float sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	float variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	int variabilityUniformInt(int low, int high, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	float variabilityGamma(float alpha, float beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	float variabilityBeta(float alpha, float beta, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
	int randomSampleIndex() const;

	struct SeatCandidate { int vote; Party::Id partyId; float weight; };
	typedef std::vector<SeatCandidate> SeatCandidates;

	PollingProject & project;
	Simulation& sim;
	SimulationRun& run;

	typedef std::map<int, float> FloatByPartyIndex;

	// Per-attempt state. Add new generated values here to reset().
	std::unique_ptr<LiveV2::Election> liveElection;
	
	std::vector<SimulationRun::PastSeatResult> pastSeatResults;
	std::map<int, std::vector<int>> regionSeatCount; // party, then region
	std::map<int, int> partyWins;
	std::vector<float> regionSwing;
	std::vector<float> partyOneNewTppMargin;
	std::vector<Party::Id> seatWinner;
	std::vector<FloatByPartyIndex> seatFpVoteShare;
	std::vector<std::pair<std::pair<int, int>, float>> seatTcpVoteShare;
	std::vector<float> nationalsShare; // Nationals' share of the LIB/NAT vote
	float iterationOverallTpp = 0.0f;
	float iterationOverallSwing = 0.0f;
	float intraCoalitionSwing = 0.0f;
	int daysToElection = 0;
	FloatByPartyIndex overallFpTarget;
	FloatByPartyIndex overallFpSwing;
	FloatByPartyIndex overallPreferenceFlow;
	FloatByPartyIndex overallExhaustRate; // for TPP distribution
	std::map<Party::Id, int> homeRegion;
	std::map<Party::Id, std::vector<bool>> seatContested;
	FloatByPartyIndex centristPopulistFactor; // e.g. 1 = full populist, 0 = full centrist
	std::map<Party::Id, int> partyIdeologies;
	std::map<Party::Id, int> partyConsistencies;
	FloatByPartyIndex fpModificationAdjustment;
	FloatByPartyIndex tempOverallFp;

	std::map<Party::Id, float> postCountFpShift;

	std::vector<double> seatRegionSwing;
	std::vector<double> seatElasticitySwing;
	std::vector<double> seatLocalEffects;
	std::vector<double> seatPreviousSwingEffect;
	std::vector<double> seatFederalSwingEffect;
	std::vector<double> seatByElectionEffect;
	std::vector<double> seatThirdPartyExhaustEffect;
	std::vector<double> seatPollEffect;
	std::vector<double> seatMrpPollEffect;

	float prefCorrection = 0.0f;
	float overallFpError = 0.0f;
	float othersCorrectionFactor = 0.0f;
	float fedStateCorrelation = 0.0f;
	float ppvcBias = 0.0f;
	float indAlpha = 1.0f;
	float indBeta = 1.0f;
	bool neededTerminalFpReconciliation = false;

	// Stable identity and retry bookkeeping. These are intentionally not reset
	// with the generated state above.
	int iterationIndex = 0;
	int retryCount = 0;

	std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;

	std::array<int, 2> effectiveWins = std::array<int, 2>();
	std::array<int, 2> partySupport = std::array<int, 2>();
};
