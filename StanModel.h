#pragma once

#include "Date.h"
#include "RandomGenerator.h"

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

class WorkspacePaths;
class ForecastSpecificationImporter;

class StanModel {
public:
	friend class ModelCollection;
	friend class EditModelFrame;
	friend class ProjectFiler;
	friend class Projection;
	friend class ForecastSpecificationImporter;

	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct Spread {
		void calculateExpectation();
		constexpr static size_t Size = 101; // must be odd so that there is a single median value
		std::array<float, Size> values{};
		float expectation = 0.0f;
	};

	class Exception : public std::logic_error { 
	public:
		Exception(std::string what) : std::logic_error(what) {}
	};

	struct Series {
		std::vector<StanModel::Spread> timePoint;
		void smooth(int smoothingFactor);
	};

	typedef std::vector<std::string> PartyCodes;

	typedef std::map<std::string, Series> PartySupport;

	struct SupportSample {
		enum class CoherenceBasis {
			FirstPreferences,
			TwoPartyPreferred
		};

		std::map<std::string, float> voteShare;
		std::map<std::string, float> preferenceFlow;
		std::map<std::string, float> exhaustRate;
		int daysToElection = 0;
		CoherenceBasis coherenceBasis = CoherenceBasis::FirstPreferences;
	};

	struct ModelledPoll {
		std::string pollster;
		int day = -1;
		float base = std::numeric_limits<float>::quiet_NaN(); // for fp, the value reported by the poll, for tcp, the figure calculated from the fp values in the poll
		float adjusted = std::numeric_limits<float>::quiet_NaN(); // value for this poll after all adjustments including for house effects
		float reported = std::numeric_limits<float>::quiet_NaN(); // tcp only
	};

	typedef std::map<std::string, std::vector<ModelledPoll>> ModelledPolls;

	typedef std::map<std::string, float> PartyParameters;

	typedef std::function<void(std::string)> FeedbackFunc;

	typedef Series const* SeriesOutput;

	// Parties excluded from the aggregate Others series. This includes the
	// Greens because their trend is modelled separately from the Others group.
	typedef std::set<std::string> MajorPartyCodes;

	StanModel(std::string name = "", std::string termCode = "", std::string partyCodes = "");

	std::string getName() const { return name; }

	std::string getTermCode() const { return termCode; }

	std::string getPartyCodes() const { return partyCodes; }
	std::string getPreferenceDeviation() const { return preferenceDeviation; }
	std::string getPreferenceSamples() const { return preferenceSamples; }

	Date getStartDate() const { return startDate; }

	// Return the date of this model's last modelled day
	// (NOT one past the end)
	Date getEndDate() const;

	Timestamp getLastUpdatedDate() const { return lastUpdatedDate; }

	bool loadData(WorkspacePaths const& paths,
		FeedbackFunc feedback = [](std::string) {}, int numThreads = 1);

	int rawSeriesCount() const;

	int adjustedSeriesCount() const;

	std::string getTextReport() const;

	// Views data for a series in the model corresponding to the given party
	SeriesOutput viewRawSeries(std::string partyCode) const;

	SeriesOutput viewRawSeriesByIndex(int index) const;

	SeriesOutput viewAdjustedSeries(std::string partyCode) const;

	SeriesOutput viewAdjustedSeriesByIndex(int index) const;

	Series const& viewRawTPPSeries() const;

	Series const& viewTPPSeries() const;

	std::string rawPartyCodeByIndex(int index) const;

	bool isReadyForProjection() const { return readyForProjection; }

	// Load everything needed to adjust samples, without running the model
	// Returns false if this fails
	bool prepareForRun(WorkspacePaths const& paths, FeedbackFunc feedback);

	ModelledPolls const& viewModelledPolls() const { return modelledPolls; }

	static void setMajorPartyCodes(MajorPartyCodes codes) { majorPartyCodes = codes; }
	
	// Dump generated data to a temporary file for later reuse
	bool dumpGeneratedData(std::string const& filename) const;
	
	// Load previously generated data from a file
	bool loadGeneratedData(
		std::string const& filename,
		FeedbackFunc feedback = [](std::string) {});

private:

	enum class InputParameters {
		PollBias,
		FundamentalsBias,
		MixedBias,
		LowerError,
		UpperError,
		LowerKurtosis,
		UpperKurtosis,
		MixFactor,
		Max
	};

	enum class EmergingPartyParameters {
		Threshold,
		EmergenceRate,
		Rmse,
		Kurtosis,
		Max
	};

	// Type for temporarily storing party group data
	typedef std::vector<std::string> PartyGroup;
	typedef std::map<std::string, PartyGroup> PartyGroups;
	typedef std::map<std::string, std::string> ReversePartyGroups;

	typedef std::array<double, int(InputParameters::Max)> ParameterSet;
	typedef std::array<double, int(EmergingPartyParameters::Max)> EmergingPartyParameterSet;
	typedef std::vector<ParameterSet> ParameterSeries;

	// Adjustment parameters are generated at several transformed poll-support
	// anchors. Runtime values are linearly interpolated between adjacent anchors.
	struct ParameterLevel {
		double trendLevel = 0.0;
		ParameterSeries series;
	};
	typedef std::vector<ParameterLevel> ParameterGrid;
	typedef std::map<std::string, ParameterGrid> ParameterGridByPartyGroup;

	typedef std::map<std::string, double> Fundamentals;

	// Parses and validates the comma-separated party codes configured in the model.
	bool loadPartyCodes(FeedbackFunc feedback);

	// Loads the party group data from analysis/Data/party-groups.csv
	bool loadPartyGroups(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Loads the fundamentals predictions from analysis/Fundamentals
	bool loadFundamentalsPredictions(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Loads coefficients for model parameters from files
	bool loadParameters(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Loads parameters specifically relating to emerging others
	bool loadEmergingOthersParameters(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Not actually needed for running trend adjustment but will eventually need to be queried for simulation reports
	bool loadModelledPolls(WorkspacePaths const& paths, FeedbackFunc feedback);
	
	// Load preference flows from a file.
	bool loadPreferenceFlows(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Generates maps between parties and parameters for their preference flows
	bool generatePreferenceMaps(WorkspacePaths const& paths, FeedbackFunc feedback);

	// Returns false on failure to load trend data
	bool loadTrendData(WorkspacePaths const& paths, FeedbackFunc feedback);

	SupportSample generateRawSupportSample(Date date, int iterationIndex) const;

	SupportSample generateAdjustedSupportSample(Date date, int days,
		int iterationIndex) const;

	void generateUnnamedOthersSeries();

	SupportSample adjustRawSupportSample(SupportSample const& rawSupportSample, Date date,
		int days, int iterationIndex) const;

	int rawSupportDayOffset(Date date) const;

	double rawMedianTrend(std::string const& partyCode, Date date) const;

	ParameterSet interpolatedParameters(std::string const& partyGroup,
		int day, double transformedTrend) const;

	bool updateAdjustedData(FeedbackFunc feedback, int numThreads);

	void addEmergingOthers(StanModel::SupportSample& sample, int days,
		int iterationIndex) const;

	static void updateOthersValue(StanModel::SupportSample& sample);

	static void normaliseSample(StanModel::SupportSample& sample);

	void finaliseSupportSample(StanModel::SupportSample& sample,
		int iterationIndex) const;

	void generateTppForSample(StanModel::SupportSample& sample,
		int iterationIndex) const;

	void generateMajorFpForSample(StanModel::SupportSample& sample,
		int iterationIndex) const;

	float variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag, int iterationIndex) const;

	float mirroredQuantile(int iterationIndex, std::uint32_t tag,
		int index) const;

	static MajorPartyCodes majorPartyCodes;

	bool readyForProjection = false;

	PartySupport rawSupport;
	Series rawTppSupport; // For whatever party is first in the user-defined party list
	PartySupport adjustedSupport;
	Series tppSupport; // For whatever party is first in the user-defined party list
	PartySupport validationSupport;
	std::string name;
	std::string termCode;
	std::string partyCodes;
	// Retained only for .pol2 compatibility. Current models load preference
	// flow and exhaust estimates from analysis/Data/preference-estimates.csv.
	std::string preferenceFlow;
	// These ordered lists remain source configuration and correspond to
	// partyCodes. The parsed maps below are generated runtime state.
	std::string preferenceDeviation;
	std::string preferenceSamples;
	Date startDate;
	Timestamp lastUpdatedDate;

	std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;

	ModelledPolls modelledPolls; // this is for the simulation reports

	// temporary/cached data

	// Projections only go for this number of days, longer time periods
	// will be treated as if they are this number of days
	int numDays = 0;
	// party groupings by rough type, loaded from a text (CSV) file
	// classifies parties based on electoral behaviour, with minor
	// parties combined into groups to get useful sample sizes
	PartyGroups partyGroups;
	// Reverse of the previous map, for efficiency of calculations
	ReversePartyGroups reversePartyGroups;

	Fundamentals fundamentals;
	EmergingPartyParameterSet emergingParameters{};

	ParameterGridByPartyGroup parameters;

	PartyCodes partyCodeVec;
	PartyParameters preferenceFlowMap;
	PartyParameters preferenceExhaustMap;
	PartyParameters preferenceDeviationMap;
	PartyParameters preferenceSamplesMap;
};
