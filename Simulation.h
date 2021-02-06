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
class ProjectionCollection;
class PollingProject;
class RegionCollection;
class Seat;
struct Party;

class Simulation {

public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	friend class ProjectFiler;

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

	struct Report {
		std::array<float, 2> majorityPercent = { 0.0f, 0.0f };
		std::array<float, 2> minorityPercent = { 0.0f, 0.0f };
		float hungPercent = 0.0f;

		double partyOneSwing = 0.0;

		std::vector<float> partyWinExpectation;

		// region, then party
		std::vector<std::vector<float>> regionPartyWinExpectation;

		// party, then seat
		std::vector<std::vector<int>> partySeatWinFrequency;

		std::vector<int> othersWinFrequency;

		float total2cpPercentCounted = 0.0f;

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> partyOneProbabilityBounds;

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> partyTwoProbabilityBounds;

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> othersProbabilityBounds;

		std::vector<std::string> partyAbbr;
		std::vector<std::string> partyName;
		std::vector<Party::Colour> partyColour;

		std::vector<std::string> regionName;

		std::vector<std::string> seatName;

		std::vector<int> seatIncumbents;

		std::vector<float> seatMargins;

		std::vector<float> incumbentWinPercent;

		// gives the indices of classic seats
		std::vector<Seat::Id> classicSeatIndices;

		std::vector<std::vector<int>> regionPartyLeading;

		float prevElection2pp = 0.0f;

		float getPartyMajorityPercent(Simulation::MajorParty whichParty) const;
		float getPartyMinorityPercent(Simulation::MajorParty whichParty) const;
		float getHungPercent() const;

		int classicSeatCount() const;
		Seat::Id classicSeatIndex(int index) const;

		int internalPartyCount() const;

		int internalRegionCount() const;

		float getPartyWinExpectation(int partyIndex) const;

		float getOthersWinExpectation() const;

		float getRegionPartyWinExpectation(int regionIndex, int partyIndex) const;

		float getRegionOthersWinExpectation(int regionIndex) const;

		float getPartySeatWinFrequency(int partyIndex, int seatIndex) const;

		float getOthersWinFrequency(int seatIndex) const;

		float getIncumbentWinPercent(int seatIndex) const;

		float getPartyWinPercent(Simulation::MajorParty whichParty) const;

		int getMinimumSeatFrequency(int partyIndex) const;

		int getMaximumSeatFrequency(int partyIndex) const;

		int getModalSeatFrequencyCount(int partyIndex) const;

		double getPartyOne2pp() const;

		float getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex) const;

		int getProbabilityBound(int bound, Simulation::MajorParty whichParty) const;

		int findBestSeatDisplayCenter(Party::Id partySorted, int numSeatsDisplayed) const;

		float get2cpPercentCounted() const { return total2cpPercentCounted; }

		// Get the number of seats in this region in which non-major parties are leading
		int getOthersLeading(int regionIndex) const;
	};

	Simulation()
	{}

	Simulation(Settings settings) : settings(settings)
	{}

	void run(PollingProject& project, SimulationRun::FeedbackFunc feedback = [](std::string) {});

	Settings const& getSettings() const { return settings; }

	std::string getLastUpdatedString() const;

	std::string getLiveString() const;

	Report const& getLatestReport() const;

	void replaceSettings(Simulation::Settings newSettings);

	bool isValid() const;

	std::string textReport(ProjectionCollection const& projections) const;

	bool isLiveAutomatic() const { return settings.live == Settings::Mode::LiveAutomatic; }
	bool isLiveManual() const { return settings.live == Settings::Mode::LiveManual; }
	bool isLive() const { return isLiveManual() || isLiveAutomatic(); }

private:

	friend class SimulationRun;

	friend class SimulationCompletion;
	friend class SimulationPreparation;
	friend class SimulationIteration;

	Settings settings;

	Report latestReport;

	std::optional<SimulationRun> latestRun;

	// If set to wxInvalidDateTime then we assume the simulation hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};

inline Simulation::MajorParty operator++(Simulation::MajorParty& party) {
	party = static_cast<Simulation::MajorParty>(static_cast<int>(party) + 1);
	return party;
}