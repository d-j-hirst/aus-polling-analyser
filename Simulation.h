#pragma once

#include "Debug.h"

#include <wx/datetime.h>

#include <array>
#include <list>
#include <numeric>
#include <random>
#include <vector>

const int NumProbabilityBoundIndices = 8;

class Projection;
class PollingProject;
class Seat;
struct Party;

struct ClassicSeat {
	ClassicSeat(Seat* seat, int seatIndex) : seat(seat), seatIndex(seatIndex) {}
	Seat* seat;
	int seatIndex;
};

class Simulation {

public:
	Simulation(std::string name) :
		name(name)
	{}

	Simulation()
	{}

	std::string getLastUpdatedString() const {
		if (!lastUpdated.IsValid()) return "";
		else return lastUpdated.FormatISODate().ToStdString();
	}

	void run(PollingProject& project);

	// User-defined name.
	std::string name = "";

	int numIterations = 10000;

	Projection const* baseProjection = nullptr;

	float prevElection2pp = 50.0f;
	float stateSD = 2.0f;
	float stateDecay = 0.001633f;

	float partyOneMajorityPercent = 0.0f;
	float partyOneMinorityPercent = 0.0f;
	float hungPercent = 0.0f;
	float partyTwoMinorityPercent = 0.0f;
	float partyTwoMajorityPercent = 0.0f;

	enum class Mode {
		Projection,
		LiveManual,
		LiveAutomatic
	};

	Mode live = Mode::Projection;

	std::vector<ClassicSeat> classicSeatList;

	std::vector<float> partyWinExpectation;

	std::vector<std::vector<float>> regionPartyWinExpectation;

	// party, then seat
	std::vector<std::vector<int>> partySeatWinFrequency;

	std::vector<int> othersWinFrequency;

	std::vector<float> incumbentWinPercent;

	float getClassicSeatMajorPartyWinRate(int classicSeatIndex, Party const* thisParty) const;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> partyOneProbabilityBounds;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> partyTwoProbabilityBounds;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> othersProbabilityBounds;

	float getPartyOneWinPercent() {
		return partyOneMajorityPercent + partyOneMinorityPercent + hungPercent * 0.5f;
	}

	float getPartyTwoWinPercent() {
		return partyTwoMajorityPercent + partyTwoMinorityPercent + hungPercent * 0.5f;
	}

	float getOthersWinExpectation(int regionIndex) {
		if (regionIndex < 0 || regionIndex >= int(regionPartyWinExpectation.size())) return 0.0f;
		if (regionPartyWinExpectation[regionIndex].size() < 3) return 0.0f;
		return std::accumulate(regionPartyWinExpectation[regionIndex].begin() + 2, regionPartyWinExpectation[regionIndex].end(), 0.0f);
	}

	void updateProbabilityBounds(int partyCount, int numSeats, int probThreshold, int& bound) {
		if (partyCount > numIterations / 200 * probThreshold && bound == -1) bound = numSeats;
	}

	int getMinimumSeatFrequency(int partyIndex) {
		if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
		if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
		for (int i = 0; i < int(partySeatWinFrequency[partyIndex].size()); ++i) {
			if (partySeatWinFrequency[partyIndex][i] > 0) return i;
		}
		return 0;
	}

	int getMaximumSeatFrequency(int partyIndex) {
		if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
		if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
		for (int i = int(partySeatWinFrequency[partyIndex].size()) - 1; i >= 0; --i) {
			if (partySeatWinFrequency[partyIndex][i] > 0) return i;
		}
		return 0;
	}

	int getModalSeatFrequencyCount(int partyIndex) {
		if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
		if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
		return *std::max_element(partySeatWinFrequency[partyIndex].begin(), partySeatWinFrequency[partyIndex].end());
	}

	bool isLiveAutomatic() { return live == Mode::LiveAutomatic; }
	bool isLiveManual() { return live == Mode::LiveManual; }
	bool isLive() { return isLiveManual() || isLiveAutomatic(); }

	int findBestSeatDisplayCenter(Party* partySorted, int numSeatsDisplayed);

	// If set to wxInvalidDateTime then we assume the simulation hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;

	private:
		
	struct OddsInfo {
		float incumbentChance = 1.0f;
		float topTwoChance = 1.0f;
	};

	struct SeatResult {
		Party const* winner = nullptr;
		Party const* runnerUp = nullptr;
		float margin = 0.0f;
		float significance = 0.0f;
	};

	int currentIteration = 0;

	float ppvcBias = 0.0f;
	float ppvcBiasNumerator = 0.0f;
	float ppvcBiasDenominator = 0.0f; // should be the total number of PPVC votes counted
	float ppvcBiasObserved = 0.0f;
	float ppvcBiasConfidence = 0.0f;
	int totalOldPpvcVotes = 0;

	float previousOrdinaryVoteEnrolmentRatio = 1.0f;
	float previousDeclarationVoteEnrolmentRatio = 1.0f;

	void determinePpvcBias();

	void determinePreviousVoteEnrolmentRatios(PollingProject& project);

	void determineSeatCachedBoothData(PollingProject const& project, Seat& seat);

	OddsInfo calculateOddsInfo(Seat const& thisSeat);

	SeatResult calculateLiveResultClassic2CP(PollingProject const& project, Seat const& seat, float priorMargin);

	SeatResult calculateLiveResultNonClassic2CP(PollingProject const& project, Seat const& seat);

	SeatResult calculateLiveResultFromFirstPreferences(PollingProject const& project, Seat const& seat);

	Party const* simulateWinnerFromBettingOdds(Seat const& thisSeat);

	bool seatPartiesMatchBetweenElections(PollingProject const & project, Seat const& seat);

	// determines enrolment change and also returns 
	// estimatedTotalOrdinaryVotes representing an estimate of the total ordinary vote count
	float determineEnrolmentChange(Seat const & seat, int* estimatedTotalOrdinaryVotes);
};