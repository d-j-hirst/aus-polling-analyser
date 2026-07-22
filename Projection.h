#pragma once

#include "ModelCollection.h"
#include "RandomGenerator.h"
#include "StanModel.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <wx/datetime.h>

class ModelCollection;

class Projection {
public:
	friend class ProjectFiler;

	typedef int Id;

	typedef std::function<void(std::string)> FeedbackFunc;

	constexpr static Id InvalidId = -1;

	struct Settings {
		// User-defined name.
		std::string name = "";

		ModelCollection::Id baseModel = ModelCollection::InvalidId;

		int numIterations = 5000;

		std::vector<std::pair<std::string, float>> possibleDates;

		wxDateTime endDate = wxDateTime::Now();
	};

	Projection()
	{}

	Projection(Settings settings)
		: settings(settings) {}

	void replaceSettings(Settings newSettings);

	std::string getEndDateString() const {
		if (!settings.endDate.IsValid()) return "";
		else return settings.endDate.FormatISODate().ToStdString();
	}

	std::string getLastUpdatedString() const {
		if (!lastUpdated.IsValid()) return "";
		else return lastUpdated.FormatISODate().ToStdString();
	}

	bool run(ModelCollection const& models, FeedbackFunc feedback = [](std::string) {}, int numThreads = 1);

	Settings const& getSettings() const { return settings; }

	wxDateTime getLastUpdatedDate() const { return lastUpdated; }

	void invalidate();

	int getProjectionLength() const { if (projectedSupport.empty()) return 0; return int(projectedSupport.begin()->second.timePoint.size()); }

	int primarySeriesCount() const { return int(projectedSupport.size()); }

	StanModel::SeriesOutput viewPrimarySeries(std::string const& code) const;

	StanModel::SeriesOutput viewPrimarySeriesByIndex(int index) const;

	StanModel::Series const& viewTPPSeries() const { return tppSupport; }

	StanModel::SupportSample generateNowcastSupportSample(
		ModelCollection const& models, int iterationIndex, wxDateTime date);

	StanModel::SupportSample generateSupportSample(
		ModelCollection const& models, wxDateTime date,
		int iterationIndex) const;

	int getPartyIndexFromCode(std::string const& code) const;

	std::string textReport(ModelCollection const& models) const;

	StanModel const& getBaseModel(ModelCollection const& models) const;

	float variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag, int iterationIndex) const;

private:

	void clearOutput();

	void createTimePoint(int time, ModelCollection const& models, int numIterations);

	std::vector<bool> detailCreated;
	StanModel::PartySupport projectedSupport;
	StanModel::Series tppSupport; // For whatever party is first in the user-defined party list

	Settings settings;

	// Set when the projection is run
	wxDateTime startDate = wxInvalidDateTime;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;

	std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;
};
