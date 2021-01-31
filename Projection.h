#pragma once

#include "ModelCollection.h"
#include "RandomGenerator.h"
#include "StanModel.h"

#include <vector>
#include <wx/datetime.h>

class ModelCollection;

class Projection {
public:
	friend class ProjectFiler;

	typedef int Id;

	typedef std::function<void(std::string)> FeedbackFunc;

	constexpr static Id InvalidId = -1;

	struct ProjectionDay {
		double mean;
		double sd;
	};

	struct Settings {
		// User-defined name.
		std::string name = "";

		ModelCollection::Id baseModel = ModelCollection::InvalidId;

		int numIterations = 5000;

		wxDateTime endDate = wxDateTime::Now();
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

	void run(ModelCollection const& models, FeedbackFunc feedback = [](std::string) {});

	void logRunStatistics();

	void setAsNowCast(ModelCollection const& models);

	Settings const& getSettings() const { return settings; }

	wxDateTime getLastUpdatedDate() const { return lastUpdated; }

	void invalidate() { lastUpdated = wxInvalidDateTime; }

	double getMeanProjection(int index) const { return projection[index].mean; }
	double getSdProjection(int index) const { return projection[index].sd; }

	int getProjectionLength() const { if (!projectedSupport.size()) return 0; return int(projectedSupport.begin()->second.timePoint.size()); }

	int primarySeriesCount() const { return projectedSupport.size(); }

	StanModel::SeriesOutput viewPrimarySeries(std::string code) const;

	StanModel::SeriesOutput viewPrimarySeriesByIndex(int index) const;

	StanModel::Series const& viewTPPSeries() const { return tppSupport; }

	// Invalid date/time (default) gives the latest time point
	StanModel::SupportSample generateSupportSample(wxDateTime date = wxInvalidDateTime) const;

	int getPartyIndexFromCode(std::string code) const;

	std::string textReport(ModelCollection const& models) const;

private:
	StanModel const& getBaseModel(ModelCollection const& models) const;

	StanModel::PartySupport projectedSupport;
	StanModel::Series tppSupport; // For whatever party is first in the user-defined party list

	static RandomGenerator rng;

	Settings settings;

	// Set when the projection is run
	wxDateTime startDate = wxInvalidDateTime;

	std::vector<ProjectionDay> projection;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;
};