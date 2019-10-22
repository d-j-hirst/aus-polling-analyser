#pragma once

#include "Model.h"

#include <vector>
#include <wx/datetime.h>

class ModelCollection;

class Projection {
public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct ProjectionDay {
		double mean;
		double sd;
	};

	struct Settings {
		// User-defined name.
		std::string name = "";

		Model::Id baseModel = Model::InvalidId;

		int numIterations = 5000;

		// Proportion of the 2pp lead that is lost per day on average in this projection
		float leaderVoteDecay = 0.001633f;

		// Standard deviation of the daily random movement
		float dailyChange = 0.1695f;

		// Standard deviation of the initial uncertainty from the last model time point
		float initialStdDev = 1.041161f;

		// Number of elections used to determine the initial uncertainty
		int numElections = 2;

		wxDateTime endDate = wxInvalidDateTime;
	};

	struct SaveData {
		Settings settings;

		// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
		wxDateTime lastUpdated = wxInvalidDateTime;

		std::vector<ProjectionDay> projection;
	};

	Projection()
	{}

	Projection(Settings settings)
		: settings(settings) {}

	Projection(SaveData saveData);

	void replaceSettings(Settings newSettings);

	std::string getEndDateString() const {
		if (!settings.endDate.IsValid()) return "";
		else return settings.endDate.FormatISODate().ToStdString();
	}

	std::string getLastUpdatedString() const {
		if (!lastUpdated.IsValid()) return "";
		else return lastUpdated.FormatISODate().ToStdString();
	}

	void run(ModelCollection const& models);

	void logRunStatistics();

	void setAsNowCast(ModelCollection const& models);

	Settings const& getSettings() const { return settings; }

	wxDateTime getLastUpdatedDate() const { return lastUpdated; }

	void invalidate() { lastUpdated = wxInvalidDateTime; }

	double getMeanProjection(int index) const { return projection[index].mean; }
	double getSdProjection(int index) const { return projection[index].sd; }

	int getProjectionLength() const { return int(projection.size()); }

private:

	typedef std::vector<float> InternalProjection;
	typedef std::vector<InternalProjection> InternalProjections;

	void runInternalProjections(InternalProjections& internalProjections, Model const& model);

	void combineInternalProjections(InternalProjections& internalProjections, Model const& model);

	Settings settings;

	std::vector<ProjectionDay> projection;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};