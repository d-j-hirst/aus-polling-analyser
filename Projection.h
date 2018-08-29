#pragma once

#include <vector>
#include <wx/datetime.h>
#include "Debug.h"

class Model;

class Projection {

public:
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

	double func_normsdist(double z) const;

	void run();

	void setAsNowCast();

	// User-defined name.
	std::string name = "";

	int numIterations = 5000;

	// Proportion of the 2pp lead that is lost per day on average in this projection
	float leaderVoteLoss = 0.001633f;

	// Standard deviation of the daily random movement
	float dailyChange = 0.1695f;

	// Standard deviation of the initial uncertainty from the last model time point
	float initialStdDev = 1.041161f;

	// Number of elections used to determine the initial uncertainty
	int numElections = 2;

	Model const* baseModel = nullptr;

	std::vector<double> meanProjection;
	std::vector<double> sdProjection;

	wxDateTime endDate = wxInvalidDateTime;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};