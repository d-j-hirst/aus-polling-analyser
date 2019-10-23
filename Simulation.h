#pragma once

#include "Party.h"
#include "Projection.h"
#include "Seat.h"
#include "SimulationRun.h"

#include <wx/datetime.h>

#include <array>
#include <list>
#include <numeric>
#include <random>
#include <vector>

const int NumProbabilityBoundIndices = 8;

class Projection;
class PollingProject;
class RegionCollection;
class Seat;
struct Party;

class Simulation {

public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct Settings {

		enum class Mode {
			Projection,
			LiveManual,
			LiveAutomatic
		};

		// User-defined name.
		std::string name = "";

		int numIterations = 10000;

		Mode live = Mode::Projection;

		Projection::Id baseProjection = Projection::InvalidId;

		float prevElection2pp = 50.0f;

		float stateSD = 2.0f;

		float stateDecay = 0.001633f;
	};

	enum MajorParty {
		One,
		Two,
		Others,
		First = One,
		Last = Two
	};

	Simulation()
	{}

	Simulation(Settings settings) : settings(settings)
	{}

	Simulation(Simulation const& other) : settings(other.settings) {}
	Simulation operator=(Simulation const& other) { return Simulation(other.settings); }

	void run(PollingProject& project);

	Settings const& getSettings() const { return settings; }

	void replaceSettings(Simulation::Settings newSettings);

	std::string getLastUpdatedString() const;

	float getPartyMajorityPercent(MajorParty whichParty) const;
	float getPartyMinorityPercent(MajorParty whichParty) const;
	float getHungPercent() const;

	int classicSeatCount() const;
	Seat::Id classicSeatId(int index) const;

	int internalPartyCount() const;

	int internalRegionCount() const;

	float getPartyWinExpectation(int partyIndex) const;

	float getOthersWinExpectation() const;

	float getRegionPartyWinExpectation(int regionIndex, int partyIndex) const;

	float getRegionOthersWinExpectation(int regionIndex) const;

	float getPartySeatWinFrequency(int partyIndex, int seatIndex) const;

	float getOthersWinFrequency(int seatIndex) const;

	float getIncumbentWinPercent(int seatIndex) const;

	float getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex, PollingProject const& project) const;

	int getProbabilityBound(int bound, MajorParty whichParty) const;

	bool isValid() const;

	float getPartyWinPercent(MajorParty whichParty) const;

	int getMinimumSeatFrequency(int partyIndex) const;

	int getMaximumSeatFrequency(int partyIndex) const;

	int getModalSeatFrequencyCount(int partyIndex) const;

	double getPartyOne2pp() const;

	int findBestSeatDisplayCenter(Party::Id partySorted, int numSeatsDisplayed, PollingProject const& project) const;

	bool isLiveAutomatic() const { return settings.live == Settings::Mode::LiveAutomatic; }
	bool isLiveManual() const { return settings.live == Settings::Mode::LiveManual; }
	bool isLive() const { return isLiveManual() || isLiveAutomatic(); }

	float get2cpPercentCounted() const { return total2cpPercentCounted; }

private:

	friend class SimulationRun;

	Settings settings;

	std::array<float, 2> majorityPercent = { 0.0f, 0.0f };
	std::array<float, 2> minorityPercent = { 0.0f, 0.0f };
	float hungPercent = 0.0f;

	double partyOneSwing = 0.0;

	std::vector<Seat::Id> classicSeatIds;

	std::vector<float> partyWinExpectation;

	// region, then party
	std::vector<std::vector<float>> regionPartyWinExpectation;

	// party, then seat
	std::vector<std::vector<int>> partySeatWinFrequency;

	std::vector<int> othersWinFrequency;

	std::vector<float> incumbentWinPercent;

	float total2cpPercentCounted = 0.0f;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> partyOneProbabilityBounds;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> partyTwoProbabilityBounds;

	// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
	std::array<int, NumProbabilityBoundIndices> othersProbabilityBounds;

	std::optional<SimulationRun> latestRun;

	// If set to wxInvalidDateTime then we assume the simulation hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};

inline Simulation::MajorParty operator++(Simulation::MajorParty& party) {
	party = static_cast<Simulation::MajorParty>(static_cast<int>(party) + 1);
	return party;
}