#pragma once

#include "Party.h"

#include <array>
#include <functional>
#include <map>
#include <string>

class PollingProject;
class RegionCollection;
class Seat;
class SeatCollection;
class Simulation;
class Outcome;

class SimulationRun {
public:
	friend class SimulationPreparation;
	friend class SimulationIteration;
	friend class SimulationCompletion;

	struct PastSeatResult {
		std::map<int, float> fpVotePercent; // map from party index -> percentage
		std::map<int, int> fpVoteCount; // map from party index -> number
		std::map<int, float> tcpVote; // map from party index -> percentage
		int turnoutCount;
		float prevOthers = 0.0f; // Special variable for calculating emergence of independents
	};

	struct SeatStatistics {
		enum class TrendType {
			SwingCoefficient,
			SophomoreCoefficient,
			Offset,
			LowerRmse,
			UpperRmse,
			LowerKurtosis,
			UpperKurtosis,
			RecontestRate,
			RecontestIncumbentRate,
			Num
		};

		float scaleLow = 0.0f;
		float scaleStep = 10.0f;
		float scaleHigh = 0.0f;
		std::array<std::vector<float>, int(TrendType::Num)> trend;
	};

	struct PopulistStatistics {
		float lowerRmse;
		float upperRmse;
		float lowerKurtosis;
		float upperKurtosis;
		float homeStateCoefficient;
	};

	struct IndEmergence {
		float fpThreshold;
		float baseRate;
		float fedRateMod;
		float ruralRateMod;
		float provincialRateMod;
		float outerMetroRateMod;
		float prevOthersRateMod;
		float voteRmse;
		float voteKurtosis;
		float fedVoteCoeff;
		float ruralVoteCoeff;
		float provincialVoteCoeff;
		float outerMetroVoteCoeff;
		float prevOthersVoteCoeff;
		float voteIntercept;
	};

	// Default values for any region not found in a file
	struct RegionBaseBehaviour {
		float overallSwingCoeff = 1.0f;
		float baseSwingDeviation = 0.0f;
		float rmse = 2.0f;
		float kurtosis = 4.0f;
	};

	struct RegionPollBehaviour {
		float overallSwingCoeff = 1.0f;
		float baseSwingDeviation = 0.0f;
	};

	struct RegionMixBehaviour {
		float bias = 1.0f;
		float rmse = 2.0f;
	};

	struct RegionMixParameters {
		float mixFactorA = 1.0f;
		float mixFactorB = 0.015f;
		float rmseA = -0.5f;
		float rmseB = 2.7f;
		float rmseC = 2.0f;
		float kurtosisA = -0.5f;
		float kurtosisB = 2.7f;
	};

	struct TppSwingFactors {
		float meanSwingDeviation;
		float swingKurtosis;
		float federalModifier;
		float retirementUrban;
		float retirementRegional;
		float sophomoreCandidateUrban;
		float sophomoreCandidateRegional;
		float sophomorePartyUrban;
		float sophomorePartyRegional;
		float previousSwingModifier;
	};

	struct IndividualSeatParameters {
		bool loaded = false; // If a seat has parameters for both its current and past name, don't want the past one to overwrite the current one
		float elasticity = 1.0f;
		float trend = 0.0f;
		float volatility = 2.5f;
	};

	enum class SeatType {
		InnerMetro,
		OuterMetro,
		Provincial,
		Rural
	};

	typedef std::function<void(std::string)> FeedbackFunc;

	SimulationRun(PollingProject& project, Simulation& simulation) : project(project), sim(simulation) {}

	SimulationRun(SimulationRun const& otherRun) : project(otherRun.project), sim(otherRun.sim) {}
	SimulationRun operator=(SimulationRun const& otherRun) { return SimulationRun(otherRun.project, otherRun.sim); }

	void run(FeedbackFunc feedback = [](std::string) {});
private:

	PollingProject& project;

	Simulation& sim;

	int currentIteration = 0;

	float ppvcBiasNumerator = 0.0f;
	float ppvcBiasDenominator = 0.0f; // should be the total number of PPVC votes counted
	float ppvcBiasObserved = 0.0f;
	float ppvcBiasConfidence = 0.0f;
	int totalOldPpvcVotes = 0;
	std::string regionCode;
	std::string yearCode;

	int indPartyIndex = 0;

	float totalPopulation = 0.0f;

	float previousOrdinaryVoteEnrolmentRatio = 1.0f;
	float previousDeclarationVoteEnrolmentRatio = 1.0f;

	float liveOverallSwing = 0.0f; // swing to partyOne
	float liveOverallPercent = 0.0f;
	float classicSeatCount = 0.0f;
	// A bunch of votes from one seat is less likely to be representative than from a wide variety of seats,
	// so this factor is introduced to avoid a small number of seats from having undue influence early in the count
	float sampleRepresentativeness = 0.0f;
	int total2cpVotes = 0;
	int totalEnrolment = 0;
	int totalPreviousTurnout = 0;

	constexpr static int FpBucketCount = 100;

	std::map<int, int> partyMajority;
	std::map<int, int> partyMinority;
	std::map<int, int> partyMostSeats;
	int tiedParliament = 0;

	std::vector<double> seatPartyOneMarginSum;
	std::vector<double> partyOneWinPercent;
	std::vector<double> partyTwoWinPercent;
	std::vector<double> othersWinPercent;
	std::vector<std::map<int, double>> cumulativeSeatPartyFpShare;
	std::vector<std::map<int, std::array<int, FpBucketCount>>> seatPartyFpDistribution;
	std::vector<std::map<int, int>> seatPartyFpZeros; // frequency of seat total being actually zero, i.e. candidate didn't run

	// Store all potential TCP results found in iterations
	// First index is seat
	// Second index is a pair containing the two parties involved in ascending order
	// Third index represents the bucket.
	// E.g. bucket 51 contains all TCP results for the first party (lower index) that fall between 51.0 and 52.0
	std::vector<std::map<std::pair<int, int>, std::array<int, FpBucketCount>>> seatTcpDistribution;
	std::vector<std::map<int, int>> seatPartyWins;
	// region, then party, then seat count
	std::vector<std::map<int, std::vector<int>>> regionPartyWins;

	std::vector<float> seatPreviousTppSwing;
	std::vector<float> seatFirstPartyPreferenceFlow;
	std::vector<float> seatPreferenceFlowVariation;
	std::vector<std::array<int, 2>> seatTcpTally;
	std::vector<float> seatIndividualBoothGrowth;
	std::vector<PastSeatResult> pastSeatResults;
	std::vector<SeatType> seatTypes;
	std::vector<float> seatPopulistModifiers;
	std::vector<float> seatCentristModifiers;
	std::vector<float> seatPartyOneTppModifier;
	std::vector<IndividualSeatParameters> seatParameters;
	std::map<int, float> previousFpVoteShare;
	std::map<int, std::map<std::pair<int, int>, float>> ncPreferenceFlow;

	std::vector<float> regionLocalModifierAverage;
	std::map<int, RegionBaseBehaviour> regionBaseBehaviour;
	std::map<int, RegionPollBehaviour> regionPollBehaviour;
	std::map<int, RegionMixBehaviour> regionMixBehaviour;
	RegionMixParameters regionMixParameters;
	RegionPollBehaviour generalPollBehaviour;
	TppSwingFactors tppSwingFactors;

	std::vector<std::map<int, float>> seatBettingOdds;
	std::vector<std::map<int, std::vector<std::pair<float, int>>>> seatPolls;

	// Live manual results
	std::vector<float> liveSeatTppSwing;
	std::vector<float> liveSeatTcpCounted; // as percentage
	std::vector<std::map<int, float>> liveSeatFpSwing;
	std::vector<float> liveSeatFpCounted; // as percentage

	std::vector<float> liveRegionPercentCounted;
	std::vector<float> liveRegionSwing;
	std::vector<float> liveRegionClassicSeatCount;

	SeatStatistics greensSeatStatistics;
	SeatStatistics indSeatStatistics;
	SeatStatistics othSeatStatistics;
	IndEmergence indEmergence;
	PopulistStatistics populistStatistics;
	PopulistStatistics centristStatistics;
};