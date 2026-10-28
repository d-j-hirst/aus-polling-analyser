#pragma once

#include "Date.h"
#include "LiveData.h"
#include "LiveResultsInput.h"
#include "Party.h"
#include "Projection.h"
#include "Seat.h"
#include "SimulationRun.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

const int NumProbabilityBoundIndices = 8;

class Projection;
class ProjectionCollection;
class PollingProject;
class RegionCollection;
class Seat;
struct Party;

// One configured election simulation, including the settings that select its
// projection and live inputs, its latest report, and explicitly saved reports.
// A report summarises many possible elections; it is not one simulated outcome.
// The project supplies shared parties, seats, models and projections when run()
// is called. SimulationRun holds the calculation's working information, while
// this object retains the results used for later inspection and website export.
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
		std::string name;

		// Election codes (e.g. fed2019) for previous elections, most recent first
		std::vector<std::string> prevTermCodes;

		int numIterations = 10000;

		Mode live = Mode::Projection;

		// Controls both how the report is labelled and whether projection support
		// is sampled as a nowcast at the current date.
		ReportMode reportMode = ReportMode::RegularForecast;

		Projection::Id baseProjection = Projection::InvalidId;

		// Date of the federal election supplying the state seats' transposed TPP
		// swings. Its distance from the projection date controls their influence
		// on the state forecast; it is independent of the state election date.
		Date fedElectionDate;

		float prevElection2pp = 50.0f;

		std::optional<float> forceTpp;

		std::string previousResultsUrl;
		std::string preloadUrl;
		std::string currentTestUrl; // used for testing, leave blank on the night
		std::string currentRealUrl; // used on the night, leave blank for testing

		// Machine-local directory scanned for state current-results feeds.
		std::string currentResultsDirectory =
			LiveResultsInput::defaultDirectory();

		// Single folder name under live_runs/ for optional diagnostic JSON.
		// Empty disables export. Not a filesystem path.
		std::string liveOutputFolder;
	};

	enum MajorParty {
		One,
		Two,
		Others,
		First = One,
		Last = Two
	};

	// Report data assembled for one simulation run and retained when it succeeds.
	// It supplies the forecast values for Australian Election Forecasts: electoral
	// outcome probabilities, seat and vote-share summaries, and distributions of
	// possible results. It also carries names, historical comparison values,
	// polling-model trends and poll data to help explain those forecasts.
	//
	// Iterations fill some frequency tables directly: their entries count simulated
	// elections, not observed votes. SimulationCompletion::completeRun derives
	// averages, medians and uncertainty bounds from these tables and the run's
	// other totals. The frequency tables remain available so consumers can request
	// additional summaries, such as the percentiles used by ReportUploader.
	//
	// This structure is also used for supporting calibration and live-baseline
	// results. The live baseline includes extra uncertainty information for use
	// before adding reported votes. Availability and units depend on the field:
	// some probabilities are stored as 0-1 proportions and others as percentages.
	// Report selection and export combine the chosen main report with simulation
	// and project metadata; see SimulationCollectionReports.cpp and
	// ReportUploader.cpp.
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

		// Proportion of times the party doesn't get enough support from favourable parties
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

		// Ordered lower-to-upper: 99%, 95%, 80%, then 50% bounds.
		std::array<int, NumProbabilityBoundIndices> partyOneProbabilityBounds{};

		// Ordered lower-to-upper: 99%, 95%, 80%, then 50% bounds.
		std::array<int, NumProbabilityBoundIndices> partyTwoProbabilityBounds{};

		// Ordered lower-to-upper: 99%, 95%, 80%, then 50% bounds.
		std::array<int, NumProbabilityBoundIndices> coalitionProbabilityBounds{};

		// Ordered lower-to-upper: 99%, 95%, 80%, then 50% bounds.
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
		// Runtime-only: regenerated by the live baseline, deliberately omitted
		// from .pol2 serialization and the ordinary forecast report exports.
		std::vector<LiveData::FpDistributionMoments> liveBaselineFpMoments;
		LiveTppIntegration::Moments liveBaselineTppMoments;

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

		std::vector<std::map<int, std::vector<float>>> regionFpProbabilityBand;
		std::vector<std::vector<float>> regionTppProbabilityBand;

		std::map<int, std::vector<float>> electionFpProbabilityBand;
		std::vector<float> electionTppProbabilityBand;

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

		// Get the given percentile for the party-one two-party-preferred vote.
		// Percentile should be expressed as a percentage e.g. median as 50.0f.
		float getTppSamplePercentile(float percentile) const;

		int getCoalitionFpSampleCount() const;

		float getCoalitionFpSampleExpectation() const;

		float getCoalitionFpSampleMedian() const;

		// Get the given percentile for the combined Coalition primary vote.
		// Percentile should be expressed as a percentage e.g. median as 50.0f.
		float getCoalitionFpSamplePercentile(float percentile) const;

		float getTcpPercentCounted() const { return total2cpPercentCounted; }

		// Get the number of seats in this region in which non-major parties are leading
		int getOthersLeading(int regionIndex) const;

		SaveablePolls getSaveablePolls() const;

		void retrieveSaveablePolls(SaveablePolls const& saveablePolls);
	};

	// A retained copy of a report, with a label and date for later selection.
	// Simulation::saveReport creates a named copy; the latest-live export route
	// also uses this wrapper when preparing the report for ReportUploader.
	struct SavedReport {
		Report report;
		Timestamp dateSaved;
		std::string label;
	};

	typedef std::vector<SavedReport> SavedReports;

	Simulation() = default;

	Simulation(Settings settings) : settings(std::move(settings))
	{}

	// Calculate and retain a new latest report. Returns false for failures
	// handled by the run; other exceptions can reach the caller. The previous
	// report is protected during calculation, but live input diagnostics may
	// change after a failed attempt. See the definition for the input sources
	// and message handlers. Saving a named report and website export are separate.
	bool run(
		PollingProject& project,
		SimulationRun::FeedbackFunc feedback = [](std::string) {},
		SimulationRun::ActionRequiredFunc actionRequired = {},
		std::optional<LiveResultsInput::CurrentFile> snapshot = {},
		SimulationRun::FeedbackFunc progress = {});

	void checkLiveSeats(PollingProject const& project, SimulationRun::FeedbackFunc feedback = [](std::string) {});

	Settings const& getSettings() const { return settings; }

	std::string getLastUpdatedString() const;

	std::string getLiveString() const;

	Report const& getLatestReport() const;

	std::optional<Report> const& getLiveBaselineReport() const;

	std::vector<LiveData::BoothSnapshot> getLiveBoothSnapshots() const;
	nlohmann::json const& getLiveInputDiagnostic() const { return liveInputDiagnostic; }
	nlohmann::json const& getLiveInputDetails() const { return liveInputDetails; }

	SavedReports const& viewSavedReports() const;

	void deleteReport(int reportIndex);

	void replaceSettings(Simulation::Settings newSettings);

	// Mark the current output as stale without discarding either the latest
	// report or any explicitly saved reports.
	void invalidate();

	void saveReport(std::string label);

	bool isValid() const;

	bool hasLatestReport() const;

	// Independently owned output transferred from a worker. No SimulationRun
	// pointers escape the worker context, including through the booth getter.
	struct PublishedLiveState {
		Report report;
		std::optional<Report> baseline;
		std::vector<std::map<int, float>> previousSeats;
		std::map<std::pair<int, int>, float> oddsMeans, oddsInputs;
		int oddsIterations = 0;
		Timestamp updated;
		std::vector<LiveData::BoothSnapshot> booths;
		nlohmann::json diagnostic, details;
	};
	Simulation copyForLiveRun() const;
	PublishedLiveState completedLiveState() const;
	void publishLiveState(PublishedLiveState state);
	void publishLiveDiagnostics(nlohmann::json diagnostic, nlohmann::json details);
	std::optional<std::filesystem::path> completedSnapshotPath() const;
	std::shared_ptr<SimulationRun> releaseRetainedLiveRun() { return std::move(latestRun); }
	std::shared_ptr<SimulationRun> detachRetainedLiveRun();

	std::string textReport(ProjectionCollection const& projections) const;

	bool isLiveAutomatic() const { return settings.live == Settings::Mode::LiveAutomatic; }
	bool isLiveManual() const { return settings.live == Settings::Mode::LiveManual; }
	bool isLive() const { return isLiveManual() || isLiveAutomatic(); }
	bool isNowcast() const { return settings.reportMode == Settings::ReportMode::Nowcast; }

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

	// Live runs reuse betting calibration only while its effective inputs and
	// sample size remain unchanged.
	std::map<std::pair<int, int>, float> cachedOddsFinalMeans;
	std::map<std::pair<int, int>, float> cachedOddsInputs;
	int cachedOddsIterations = 0;

	std::shared_ptr<SimulationRun> latestRun;
	std::vector<LiveData::BoothSnapshot> publishedBooths;
	// Transient operator diagnostics are not serialized in the project. Private
	// snapshot exports carry the summary for later Live Booths inspection.
	nlohmann::json liveInputDiagnostic, liveInputDetails;

	// An invalid timestamp indicates that the simulation has not been run, or
	// that its latest report is stale relative to upstream configuration.
	Timestamp lastUpdated;
};

inline Simulation::MajorParty operator++(Simulation::MajorParty& party) {
	party = static_cast<Simulation::MajorParty>(static_cast<int>(party) + 1);
	return party;
}
