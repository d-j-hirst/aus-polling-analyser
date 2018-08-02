#pragma once

#include <vector>
#include <array>
#include <numeric>
#include <wx/datetime.h>
#include "Debug.h"

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
	float partyOneLeadPercent = 0.0f;
	float tiePercent = 0.0f;
	float partyTwoLeadPercent = 0.0f;
	float partyTwoMajorityPercent = 0.0f;

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
		return partyOneMajorityPercent + partyOneLeadPercent + tiePercent * 0.5f;
	}

	float getPartyTwoWinPercent() {
		return partyTwoMajorityPercent + partyTwoLeadPercent + tiePercent * 0.5f;
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

	int findBestSeatDisplayCenter(Party* partySorted, int numSeatsDisplayed);

	// If set to wxInvalidDateTime then we assume the simulation hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};