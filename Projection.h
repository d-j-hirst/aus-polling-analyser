#pragma once

#include "Model.h"

#include <vector>
#include <wx/datetime.h>

class ModelCollection;

class Projection {
public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	Projection(std::string name) :
		name(name) 
		{}

	Projection() 
		{}

	std::string getEndDateString() const {
		if (!endDate.IsValid()) return "";
		else return endDate.FormatISODate().ToStdString();
	}

	std::string getLastUpdatedString() const {
		if (!lastUpdated.IsValid()) return "";
		else return lastUpdated.FormatISODate().ToStdString();
	}

	void run(ModelCollection const& models);

	void logRunStatistics();

	void setAsNowCast(ModelCollection const& models);

	// User-defined name.
	std::string name = "";

	int numIterations = 5000;

	// Proportion of the 2pp lead that is lost per day on average in this projection
	float leaderVoteDecay = 0.001633f;

	// Standard deviation of the daily random movement
	float dailyChange = 0.1695f;

	// Standard deviation of the initial uncertainty from the last model time point
	float initialStdDev = 1.041161f;

	// Number of elections used to determine the initial uncertainty
	int numElections = 2;

	Model::Id baseModel = Model::InvalidId;

	std::vector<double> meanProjection;
	std::vector<double> sdProjection;

	wxDateTime endDate = wxInvalidDateTime;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};