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

		enum class ReportMode {
			RegularForecast,
			LiveForecast,
			Nowcast
		};

		// User-defined name.
		std::string name = "";

		// Election codes (e.g. fed2019) for previous elections, most recent first
		std::vector<std::string> prevTermCodes;

		int numIterations = 10000;

		Mode live = Mode::Projection;

		ReportMode reportMode = ReportMode::RegularForecast;

		Projection::Id baseProjection = Projection::InvalidId;

		wxDateTime fedElectionDate = wxInvalidDateTime;

		float prevElection2pp = 50.0f;

		std::optional<float> forceTpp;

		std::string previousResultsUrl;
		std::string preloadUrl;
		std::string currentTestUrl; // used for testing, leave blank on the night
		std::string currentRealUrl; // used on the night, leave blank for testing
	};

	enum MajorParty {
		One,
		Two,
		Others,
		First = One,
		Last = Two
	};

	struct Report {
		typedef std::pair<std::pair<std::string, int>, std::array<float, 3>> SaveablePoll;
		typedef std::map<std::string, std::vector<SaveablePoll>> SaveablePolls;

		std::string dateCode;

		// Proportion of times the party wins an absolute majority of seats
		// including only itself and other parties that count as it
		std::map<int, float> majorityPercent;

		// Proportion of times the party doesn't win an absolute majority
		// by itself but can do so with the support parties considered highly likely to be favourable
		std::map<int, float> minorityPercent;

		// Proportion of times the party doesn't make get enough support from favourable parties
		// but still has more seats than any other party
		std::map<int, float> mostSeatsPercent;

		// Proportion of times the top two (or more) parties have exactly the same number of seats
		float tiedPercent = 0.0f;

		double partyOneSwing = 0.0;

		std::map<int, std::map<short, int>> partyPrimaryFrequency;
		std::map<short, int> tppFrequency;
		std::map<short, int> coalitionFpFrequency;

		std::map<int, float> partyWinExpectation;
		std::map<int, float> partyWinMedian;

		float coalitionWinExpectation = 0.0f;
		float coalitionWinMedian = 0.0f;

		// region, then party
		std::vector<std::map<int, float>> regionPartyWinExpectation;

		std::vector<float> regionCoalitionWinExpectation;

		// party, then seat count
		std::map<int, std::vector<int>> partySeatWinFrequency;

		// Margin by which each seat is expected to be on after simulation
		// Note this variable holds the accumulated sum during simulations
		// and is only divided to form the average once simulations are complete
		std::vector<float> seatPartyOneMarginAverage;

		std::vector<int> coalitionSeatWinFrequency;
		std::vector<int> othersSeatWinFrequency;

		float total2cpPercentCounted = 0.0f;

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> partyOneProbabilityBounds{};

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> partyTwoProbabilityBounds{};

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> coalitionProbabilityBounds{};

		// 1,2 = 50% bounds, 3,4 = 80% bounds, 5,6 = 95% bounds, 7,8 = 99% bounds
		std::array<int, NumProbabilityBoundIndices> othersProbabilityBounds{};

		std::map<int, std::string> partyAbbr;
		std::map<int, std::string> partyName;
		std::map<int, Party::Colour> partyColour;

		std::vector<std::string> regionName;

		std::vector<std::string> seatName;

		std::vector<int> seatIncumbents;

		std::vector<float> seatMargins;

		std::vector<float> seatIncumbentMargins;

		std::vector<float> partyOneWinProportion;
		std::vector<float> partyTwoWinProportion;
		std::vector<float> othersWinProportion;

		std::vector<Seat::Id> classicSeatIndices; // deprecated, do not use

		std::vector<std::vector<int>> regionPartyIncumbents;
		std::vector<int> regionCoalitionIncumbents;

		std::vector<std::map<int, float>> seatPartyWinPercent;

		std::vector<std::map<int, float>> seatPartyMeanFpShare;

		std::vector<std::vector<std::string>> swingFactors;

		static const std::vector<float> CurrentlyUsedProbabilityBands;

		std::vector<float> probabilityBands;

		std::vector<float> tppProbabilityBand;

		std::vector<std::vector<float>> fpProbabilityBand;

		std::vector<std::map<int, std::vector<float>>> seatFpProbabilityBand;

		std::vector<std::map<std::pair<int, int>, std::vector<float>>> seatTcpProbabilityBand;
		std::vector<std::map<std::pair<int, int>, float>> seatTcpScenarioPercent;
		std::vector<std::map<std::pair<int, int>, float>> seatTcpWinPercent;

		std::vector<std::vector<float>> seatTppProbabilityBand;

		std::vector<int> seatHideTcps;

		std::vector<int> trendProbBands;

		std::vector<std::map<int, std::string>> seatCandidateNames;
		int trendPeriod = 0;
		int finalTrendValue = 0;
		std::string trendStartDate;

		std::vector<std::vector<float>> tppTrend; // outer: time point, inner: prob band
		std::map<int, std::vector<std::vector<float>>> fpTrend; // outer: party index, middle: time point, inner: prob band

		StanModel::ModelledPolls modelledPolls;

		float prevElection2pp = 0.0f;

		float getPartyMajorityPercent(int whichParty) const;
		float getPartyMinorityPercent(int whichParty) const;
		float getHungPercent() const;

		int internalRegionCount() const;

		float getPartyWinExpectation(int partyIndex) const;

		float getPartyWinMedian(int partyIndex) const;

		float getCoalitionWinExpectation() const;

		float getCoalitionWinMedian() const;

		float getOthersWinExpectation() const;

		float getRegionPartyWinExpectation(int regionIndex, int partyIndex) const;

		float getRegionCoalitionWinExpectation(int regionIndex) const;

		float getRegionOthersWinExpectation(int regionIndex) const;

		float getPartySeatWinFrequency(int partyIndex, int seatIndex) const;

		float getCoalitionWinFrequency(int seatIndex) const;

		float getOthersWinFrequency(int seatIndex) const;

		float getPartyOverallWinPercent(int whichParty) const;

		float getOthersOverallWinPercent() const;

		int getMinimumSeatFrequency(int partyIndex) const;

		int getMaximumSeatFrequency(int partyIndex) const;

		int getPartySeatsSampleCount(int partyIndex) const;

		int getPartySeatsPercentile(int partyIndex, float percentile) const;

		int getCoalitionSeatsSampleCount() const;

		int getCoalitionSeatsPercentile(float percentile) const;

		int getModalSeatFrequencyCount(int partyIndex) const;

		double getPartyOne2pp() const;

		int getProbabilityBound(int bound, Simulation::MajorParty whichParty) const;

		int getFpSampleCount(int partyIndex) const;

		float getFpSampleExpectation(int partyIndex) const;

		// Get the given percentile for this party's overall primary vote.
		// Percentile should be expressed as a percentage e.g. median as 50.0f.
		float getFpSamplePercentile(int partyIndex, float percentile) const;

		float getFpSampleMedian(int partyIndex) const;

		int getTppSampleCount() const;

		float getTppSampleExpectation() const;

		float getTppSampleMedian() const;

		// Get the given percentile for this party's overall primary vote.
		// Percentile should be expressed as a percentage e.g. median as 50.0f.
		float getTppSamplePercentile(float percentile) const;

		int getCoalitionFpSampleCount() const;

		float getCoalitionFpSampleExpectation() const;

		float getCoalitionFpSampleMedian() const;

		// Get the given percentile for this party's overall primary vote.
		// Percentile should be expressed as a percentage e.g. median as 50.0f.
		float getCoalitionFpSamplePercentile(float percentile) const;

		float getTcpPercentCounted() const { return total2cpPercentCounted; }

		// Get the number of seats in this region in which non-major parties are leading
		int getOthersLeading(int regionIndex) const;

		SaveablePolls getSaveablePolls() const;

		void retrieveSaveablePolls(SaveablePolls saveablePolls);
	};

	struct SavedReport {
		Report report;
		wxDateTime dateSaved;
		std::string label;
	};

	typedef std::vector<SavedReport> SavedReports;

	Simulation()
	{}

	Simulation(Settings settings) : settings(settings)
	{}

	void run(PollingProject& project, SimulationRun::FeedbackFunc feedback = [](std::string) {});

	void checkLiveSeats(PollingProject const& project, SimulationRun::FeedbackFunc feedback = [](std::string) {});

	Settings const& getSettings() const { return settings; }

	std::string getLastUpdatedString() const;

	std::string getLiveString() const;

	Report const& getLatestReport() const;

	std::optional<Report> const& getLiveBaselineReport() const;

	SavedReports const& viewSavedReports() const;

	void deleteReport(int reportIndex);

	void replaceSettings(Simulation::Settings newSettings);

	void saveReport(std::string label);

	bool isValid() const;

	std::string textReport(ProjectionCollection const& projections) const;

	bool isLiveAutomatic() const { return settings.live == Settings::Mode::LiveAutomatic; }
	bool isLiveManual() const { return settings.live == Settings::Mode::LiveManual; }
	bool isLive() const { return isLiveManual() || isLiveAutomatic(); }

private:

	friend class SimulationRun;

	friend class SimulationCompletion;
	friend class SimulationPreparation;
	friend class LivePreparation;
	friend class SimulationIteration;

	Settings settings;

	std::optional<Report> liveBaselineReport;

	Report latestReport;

	SavedReports savedReports;

	std::vector<std::map<int, float>> previousLiveSeats;

	std::map<std::pair<int, int>, float> cachedOddsFinalMeans; // store this so it doesn't have to be recalculated live

	//std::unique_ptr<SimulationRun> latestRun;
	std::shared_ptr<SimulationRun> latestRun;

	// If set to wxInvalidDateTime then we assume the simulation hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};

inline Simulation::MajorParty operator++(Simulation::MajorParty& party) {
	party = static_cast<Simulation::MajorParty>(static_cast<int>(party) + 1);
	return party;
}