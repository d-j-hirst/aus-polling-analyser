#pragma once

// Make prepared overall support available for forecast dates. Projection::run
// in the .cpp file explains how the stored daily summaries are built.

#include "Date.h"
#include "ModelCollection.h"
#include "RandomGenerator.h"
#include "StanModel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ModelCollection;

// Connect a prepared StanModel to the dates used by an election forecast. A
// "draw" is one possible combination of overall vote shares and preferences,
// selected at random according to the model's uncertainty. Each simulated
// election requests its own draw before distributing support across seats.
//
// run builds daily FP and TPP summaries from the day after the model's last date
// through the configured end date. They store averages and percentiles: points
// across the ordered range of possible vote shares. Simulation draws are generated
// afresh from the base model's prepared inputs. A nowcast primarily estimates
// current voting intention. It uses selected medians (middle values) from these
// summaries to change the timing of expected support movements, while keeping
// the same full-election projection and election-date endpoint. The explanation
// beside generateNowcastSupportSample below describes why this adjustment is used.
class Projection {
public:
	friend class ProjectFiler;

	typedef int Id;

	typedef std::function<void(std::string)> FeedbackFunc;

	constexpr static Id InvalidId = -1;

	struct Settings {
		// User-defined name.
		std::string name = "";

		// The prepared polling model supplying trends and forecast adjustments.
		ModelCollection::Id baseModel = ModelCollection::InvalidId;

		// Draws used when recalculating a date's stored summary for the nowcast
		// adjustment. Simulation settings separately choose how many whole
		// elections are calculated; run initially uses 300 draws per date.
		int numIterations = 5000;

		// Configured election dates (YYYY-MM-DD) and decimal odds, expected to
		// correspond to observed betting odds on those election dates. When no
		// explicit date is requested, a draw chooses among usable entries: lower
		// odds mean greater relative probability. Odds of 2 give twice the weight
		// of odds 4.
		std::vector<std::pair<std::string, float>> possibleDates;

		// Last date of the stored projection, normally the intended election date.
		// For an unspecified sampling date, it is the fallback when no usable
		// possible dates remain, and the endpoint used to adjust an earlier nowcast.
		Date endDate = Date::todayLocal();
	};

	Projection()
	{}

	Projection(Settings settings)
		: settings(settings) {}

	void replaceSettings(Settings newSettings);

	std::string getEndDateString() const {
		return settings.endDate.formatIso();
	}

	std::string getLastUpdatedString() const {
		return lastUpdated.formatIsoDateLocal();
	}

	// Build daily support summaries using the selected prepared model in models.
	// feedback reports input problems and handled calculation failures; numThreads
	// controls concurrent work on dates. Success records the projection's update
	// time. The definition explains the summary range and later nowcast calculations.
	bool run(ModelCollection const& models, FeedbackFunc feedback = [](std::string) {}, int numThreads = 1);

	Settings const& getSettings() const { return settings; }

	Timestamp getLastUpdatedDate() const { return lastUpdated; }

	// Identify a new/replaced projection for simulation cache reuse. Copies to
	// a worker retain the identity; lazy refinement of the same daily summaries
	// does not change it. This marker never changes the sampled vote shares.
	std::shared_ptr<int const> preparedDataRevision() const { return preparedRevision; }

	void invalidate();

	int getProjectionLength() const { if (projectedSupport.empty()) return 0; return int(projectedSupport.begin()->second.timePoint.size()); }

	int primarySeriesCount() const { return int(projectedSupport.size()); }

	StanModel::SeriesOutput viewPrimarySeries(std::string const& code) const;

	StanModel::SeriesOutput viewPrimarySeriesByIndex(int index) const;

	StanModel::Series const& viewTPPSeries() const { return tppSupport; }

	// Supply a possible level of voting intention for an explicit nowcast date,
	// normally the fixed current date selected by SimulationRun.
	//
	// The model expects support to shift most rapidly in the final stretch before
	// an election. An ordinary draw for a nearby date treats the short interval
	// since the polling model's last day as that final pre-election stretch, so
	// its median can move rapidly. But the nowcast primarily estimates voting
	// intention now: the elapsed period did not actually lead up to an election.
	// It is not intended as a strict hypothetical forecast of an election today.
	//
	// Reverse the timing of median changes so the intended path moves slowly at
	// first and accelerates towards the actual election date. Remove the ordinary
	// median's movement from the projection's start to the nowcast date, then add
	// the median change over an equally long interval ending at the full projection's
	// endpoint. This retains the initial and final median levels while reversing
	// the order of the intervening changes, not their overall direction. Individual
	// draws still represent uncertainty around this intended path. The calculation
	// in the .cpp definition uses four medians to make this adjustment.
	//
	// Needed summary dates are recalculated using settings.numIterations.
	// At the endpoint, or without the required stored series, use the ordinary
	// draw for that date. A date after the endpoint is rejected.
	StanModel::SupportSample generateNowcastSupportSample(
		ModelCollection const& models, int iterationIndex, Date date);

	// Generate possible overall FP/TPP shares and preferences from the base model
	// for date. An unspecified Date chooses from usable possibleDates entries,
	// or endDate if none remain. Invalid date odds raise an error. The gap from the
	// model's last date drives its forecast adjustments. StanModel adjusts the
	// returned day count to match the timing covered by its historical correction
	// tables. iterationIndex identifies the result generated from prepared inputs.
	StanModel::SupportSample generateSupportSample(
		ModelCollection const& models, Date date,
		int iterationIndex) const;

	int getPartyIndexFromCode(std::string const& code) const;

	std::string textReport(ModelCollection const& models) const;

	StanModel const& getBaseModel(ModelCollection const& models) const;

	float variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag, int iterationIndex) const;

private:
	std::shared_ptr<int const> preparedRevision = std::make_shared<int const>(0);

	void clearOutput();

	void createTimePoint(int time, ModelCollection const& models, int numIterations);

	std::vector<bool> detailCreated;
	StanModel::PartySupport projectedSupport;
	StanModel::Series tppSupport; // For whatever party is first in the user-defined party list

	Settings settings;

	// Set when the projection is run
	Date startDate;

	// An invalid timestamp indicates that the projection has not been run.
	Timestamp lastUpdated;

	std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;
};
