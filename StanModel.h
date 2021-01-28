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

	struct Series {
		std::vector<StanModel::Spread> timePoint;
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

	StanModel(std::string name = "", std::string termCode = "", std::string partyCodes = "",
		std::string meanAdjustments = "", std::string stdevAdjustments = "");

	std::string getName() const { return name; }

	std::string getTermCode() const { return termCode; }

	std::string getPartyCodes() const { return partyCodes; }

	wxDateTime getStartDate() const { return startDate; }

	// Return the date of this model's last modelled day
	// (NOT one past the end)
	wxDateTime getEndDate() const;

	wxDateTime getLastUpdatedDate() const { return lastUpdatedDate; }

	void loadData(FeedbackFunc feedback = [](std::string) {});

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

	static void setMajorPartyCodes(MajorPartyCodes codes) { majorPartyCodes = codes; }
private:

	// Invalid date/time (default) gives the most recent time point
	SupportSample generateRawSupportSample(wxDateTime date = wxInvalidDateTime) const;

	void generateUnnamedOthersSeries();

	SupportSample adjustRawSupportSample(SupportSample const& rawSupportSample, int days = 0) const;

	void updateAdjustedData(FeedbackFunc feedback);

	// Ensure that adjusted support for minor parties does not exceed the total aggregate values
	void limitMinorParties(FeedbackFunc feedback);

	void generateTppSeries(FeedbackFunc feedback);

	void updateValidationData(FeedbackFunc feedback);

	// Adds a series to the model for the given party name and returns a reference to it
	Series& addSeries(std::string partyCode);

	static void updateOthersValue(StanModel::SupportSample& sample);

	static void normaliseSample(StanModel::SupportSample& sample);

	void generateParameterMaps();

	static RandomGenerator rng;

	static MajorPartyCodes majorPartyCodes;

	PartySupport rawSupport;
	PartySupport adjustedSupport;
	Series tppSupport; // For whatever party is first in the user-defined party list
	PartySupport validationSupport;
	std::string name;
	std::string termCode;
	std::string partyCodes;
	std::string debiasIntercept;
	std::string debiasSlope;
	std::string preferenceFlow;
	std::string preferenceDeviation;
	std::string preferenceSamples;
	std::string maxPollWeight;
	std::string informationHorizon;
	std::string hyperbolaSharpness;
	std::string historicalAverage;
	std::string deviationSlope;
	std::string deviationIntercept;
	wxDateTime startDate = wxInvalidDateTime;
	wxDateTime lastUpdatedDate = wxInvalidDateTime;

	// temporary/cached map between parties and parameters
	PartyCodes partyCodeVec;
	PartyParameters debiasSlopeMap;
	PartyParameters debiasInterceptMap;
	PartyParameters maxPollWeightMap;
	PartyParameters informationHorizonMap;
	PartyParameters hyperbolaSharpnessMap;
	PartyParameters historicalAverageMap;
};