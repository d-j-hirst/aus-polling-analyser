#pragma once

#include "RandomGenerator.h"

#include <array>
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

	typedef std::map<std::string, float> SupportSample;

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

	Series const& viewTPPSeries() const;

	// Invalid date/time (default) gives the most recent time point
	SupportSample generateSupportSample(wxDateTime date = wxInvalidDateTime) const;

	std::string rawPartyCodeByIndex(int index) const;

	bool isReadyForProjection() const { return readyForProjection; }

	// Load everything needed to adjust samples, without running the model
	// Returns false if this fails
	bool prepareForRun(FeedbackFunc feedback);

	static void setMajorPartyCodes(MajorPartyCodes codes) { majorPartyCodes = codes; }
private:

	enum InputParameters {
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

	// Type for temporarily storing party group data
	typedef std::vector<std::string> PartyGroup;
	typedef std::map<std::string, PartyGroup> PartyGroups;
	typedef std::map<std::string, std::string> ReversePartyGroups;

	typedef std::array<double, InputParameters::Max> ParameterSet;
	typedef std::vector<ParameterSet> ParameterSeries;
	typedef std::map<std::string, ParameterSeries> ParameterSeriesByPartyGroup;

	typedef std::map<std::string, double> Fundamentals;

	// Loads the party group data from python/Data/party-groups.csv
	void loadPartyGroups();

	// Loads the fundamentals predictions from python/Fundamentals
	void loadFundamentalsPredictions();

	// Loads coefficients for model parameters from 
	void loadParameters(FeedbackFunc feedback);

	// Generates maps between 
	bool generatePreferenceMaps(FeedbackFunc feedback);

	// Returns false on failure to load trend data
	bool loadTrendData(FeedbackFunc feedback);

	// Invalid date/time (default) gives the most recent time point
	SupportSample generateRawSupportSample(wxDateTime date = wxInvalidDateTime) const;

	SupportSample generateAdjustedSupportSample(wxDateTime date = wxInvalidDateTime, int days = 0) const;

	void generateUnnamedOthersSeries();

	SupportSample adjustRawSupportSample(SupportSample const& rawSupportSample, int days = 0) const;

	void updateAdjustedData(FeedbackFunc feedback, int numThreads);

	// Adds a series to the model for the given party name and returns a reference to it
	Series& addSeries(std::string partyCode);

	static void updateOthersValue(StanModel::SupportSample& sample);

	static void normaliseSample(StanModel::SupportSample& sample);

	void generateTppForSample(StanModel::SupportSample& sample) const;

	static RandomGenerator rng;

	static MajorPartyCodes majorPartyCodes;

	bool readyForProjection = false;

	PartySupport rawSupport;
	PartySupport adjustedSupport;
	Series tppSupport; // For whatever party is first in the user-defined party list
	PartySupport validationSupport;
	std::string name;
	std::string termCode;
	std::string partyCodes;
	std::string preferenceFlow;
	std::string preferenceDeviation;
	std::string preferenceSamples;
	wxDateTime startDate = wxInvalidDateTime;
	wxDateTime lastUpdatedDate = wxInvalidDateTime;


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

	ParameterSeriesByPartyGroup parameters;

	PartyCodes partyCodeVec;
	PartyParameters preferenceFlowMap;
	PartyParameters preferenceDeviationMap;
	PartyParameters preferenceSamplesMap;
};