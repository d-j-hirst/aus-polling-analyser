#pragma once

#include "RandomGenerator.h"

#include <array>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <wx/datetime.h>

class StanModel {
public:
	friend class ModelCollection;
	friend class EditModelFrame;
	friend class ProjectFiler;
	friend class Projection;

	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct Spread {
		void calculateExpectation();
		constexpr static size_t Size = 101; // must be odd so that there is a single median value
		std::array<float, Size> values;
		float expectation;
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
		std::map<std::string, float> voteShare;
		std::map<std::string, float> preferenceFlow;
		std::map<std::string, float> exhaustRate;
		int daysToElection;
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

	// Used for telling the model which parties are "major" -
	// for the purpose of 
	typedef std::set<std::string> MajorPartyCodes;

	StanModel(std::string name = "", std::string termCode = "", std::string partyCodes = "");

	std::string getName() const { return name; }

	std::string getTermCode() const { return termCode; }

	std::string getPartyCodes() const { return partyCodes; }

	wxDateTime getStartDate() const { return startDate; }

	// Return the date of this model's last modelled day
	// (NOT one past the end)
	wxDateTime getEndDate() const;

	wxDateTime getLastUpdatedDate() const { return lastUpdatedDate; }

	void loadData(FeedbackFunc feedback = [](std::string) {}, int numThreads = 1);

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
	bool prepareForRun(FeedbackFunc feedback);

	ModelledPolls const& viewModelledPolls() const { return modelledPolls; }

	static void setMajorPartyCodes(MajorPartyCodes codes) { majorPartyCodes = codes; }
	
	// Dump generated data to a temporary file for later reuse
	bool dumpGeneratedData(std::string filename) const;
	
	// Load previously generated data from a file
	bool loadGeneratedData(std::string filename);

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
	typedef std::map<std::string, ParameterSeries> ParameterSeriesByPartyGroup;

	typedef std::map<std::string, double> Fundamentals;

	// Loads the party group data from analysis/Data/party-groups.csv
	void loadPartyGroups();

	// Loads the fundamentals predictions from analysis/Fundamentals
	void loadFundamentalsPredictions();

	// Loads coefficients for model parameters from files
	void loadParameters(FeedbackFunc feedback);

	// Loads parameters specifically relating to emerging others
	void loadEmergingOthersParameters(FeedbackFunc feedback);

	// Not actually needed for running trend adjustment but will eventually need to be queried for simulation reports
	bool loadModelledPolls(FeedbackFunc feedback);
	
	// Load preference flows from a file.
	void loadPreferenceFlows(FeedbackFunc feedback);

	// Generates maps between parties and parameters for their preference flows
	bool generatePreferenceMaps(FeedbackFunc feedback);

	// Returns false on failure to load trend data
	bool loadTrendData(FeedbackFunc feedback);

	// Invalid date/time (default) gives the most recent time point
	SupportSample generateRawSupportSample(wxDateTime date = wxInvalidDateTime) const;

	SupportSample generateAdjustedSupportSample(wxDateTime date = wxInvalidDateTime, int days = 0) const;

	void generateUnnamedOthersSeries();

	SupportSample adjustRawSupportSample(SupportSample const& rawSupportSample, int days = 0) const;

	void updateAdjustedData(FeedbackFunc feedback, int numThreads);

	void addEmergingOthers(StanModel::SupportSample& sample, int days) const;

	static void updateOthersValue(StanModel::SupportSample& sample);

	static void normaliseSample(StanModel::SupportSample& sample);

	void generateTppForSample(StanModel::SupportSample& sample) const;

	void generateMajorFpForSample(StanModel::SupportSample& sample) const;

	static RandomGenerator rng;

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
	std::string preferenceFlow;
	std::string preferenceExhaust;
	std::string preferenceDeviation;
	std::string preferenceSamples;
	wxDateTime startDate = wxInvalidDateTime;
	wxDateTime lastUpdatedDate = wxInvalidDateTime;

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
	EmergingPartyParameterSet emergingParameters;

	ParameterSeriesByPartyGroup parameters;

	PartyCodes partyCodeVec;
	PartyParameters preferenceFlowMap;
	PartyParameters preferenceExhaustMap;
	PartyParameters preferenceDeviationMap;
	PartyParameters preferenceSamplesMap;
};