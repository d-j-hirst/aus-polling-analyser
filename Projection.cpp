#include "Projection.h"

#include "General.h"
#include "Log.h"
#include "ModelCollection.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>

#undef max

namespace {
	std::mutex detailCreationMutex;

	enum class VariabilityTag : std::uint32_t {
		SelectDate = 1,
	};

	int percentileSampleIndex(int percentile, int sampleCount)
	{
		return int(std::lround(
			double(percentile) * double(sampleCount - 1) /
			double(StanModel::Spread::Size - 1)));
	}
}

void Projection::replaceSettings(Settings newSettings)
{
	settings = newSettings;
	clearOutput();
	startDate = {};
}

void Projection::invalidate()
{
	clearOutput();
	startDate = {};
}

void Projection::clearOutput()
{
	projectedSupport.clear();
	tppSupport.timePoint.clear();
	detailCreated.clear();
	lastUpdated = {};
}

void Projection::createTimePoint(int time, ModelCollection const& models, int numIterations)
{
	// Projection runs build a low-cost series for display, then replace the few
	// points required by nowcast adjustment with the configured sample count.
	auto const& model = getBaseModel(models);
	if (numIterations <= 0) {
		throw std::logic_error("Projection iteration count must be positive.");
	}
	std::vector<std::vector<float>> samples(
		model.partyCodeVec.size(), std::vector<float>(numIterations));
	std::vector<float> tppSamples(numIterations);
	auto const projectedDate = startDate + time;
	for (int iteration = 0; iteration < numIterations; ++iteration) {
		// Explicit iteration keys make parallel time-point generation repeatable.
		auto sample = generateSupportSample(models, projectedDate, iteration);
		for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
			auto const& partyName = model.partyCodeVec[partyIndex];
			samples[partyIndex][iteration] = sample.voteShare.at(partyName);
		}
		tppSamples[iteration] = sample.voteShare.at(TppCode);
	}
	for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
		auto const& partyName = model.partyCodeVec[partyIndex];
		std::sort(samples[partyIndex].begin(), samples[partyIndex].end());
		for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
			int const sampleIndex = percentileSampleIndex(percentile, numIterations);
			projectedSupport.at(partyName).timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
		}
		projectedSupport.at(partyName).timePoint[time].calculateExpectation();
	}
	std::sort(tppSamples.begin(), tppSamples.end());
	for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
		int const sampleIndex = percentileSampleIndex(percentile, numIterations);
		tppSupport.timePoint[time].values[percentile] = tppSamples[sampleIndex];
	}
	tppSupport.timePoint[time].calculateExpectation();
}

bool Projection::run(ModelCollection const& models, FeedbackFunc feedback, int numThreads) {
	if (!settings.endDate.isValid()) {
		feedback("Could not generate projection: the projection end date is invalid.");
		return false;
	}
	if (settings.numIterations <= 0) {
		feedback("Could not generate projection: the iteration count must be positive.");
		return false;
	}
	if (models.idToIndex(settings.baseModel) == ModelCollection::InvalidIndex) {
		feedback("Could not generate projection: the configured base model does not exist.");
		return false;
	}
	numThreads = std::max(1, numThreads);
	auto const& model = getBaseModel(models);
	if (!model.isReadyForProjection()) {
		feedback("The base model (" + model.name + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return false;
	}
	auto const projectionStartDate = model.getEndDate() + 1;
	if (settings.endDate < projectionStartDate) {
		feedback("Could not generate projection: the end date is earlier than the first available projection date.");
		return false;
	}

	logger << "Starting projection run: " << Timestamp::now().formatIsoLocal() << "\n";

	// Initial run is only for visual purposes so don't do too many iterations for that.
	constexpr static int PreliminaryIterations = 300;
	// Clear all cached output together so a failed rerun cannot leave a mixture of
	// old and partially generated projection data available to simulations.
	clearOutput();
	startDate = projectionStartDate;
	try {
		int const seriesLength = settings.endDate - startDate + 1;
		for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
			auto const& partyName = model.partyCodeVec[partyIndex];
			projectedSupport[partyName].timePoint.resize(seriesLength);
		}
		tppSupport.timePoint.resize(seriesLength);
		detailCreated.resize(seriesLength, false);

		constexpr int BatchSize = 10;
		for (int timeStart1 = 0; timeStart1 < seriesLength; timeStart1 += numThreads * BatchSize) {
			auto calculateTimeSupport = [&](int timeStart) {
				for (int time = timeStart; time < timeStart + BatchSize && time < seriesLength; ++time) {
					createTimePoint(time, models, PreliminaryIterations);
				}
			};
			std::vector<std::future<void>> workers;
			for (int timeStart = timeStart1; timeStart < timeStart1 + numThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
				workers.push_back(std::async(
					std::launch::async, calculateTimeSupport, timeStart));
			}
			for (auto& worker : workers) {
				worker.get();
			}
		}

		const int ProjectionSmoothingDays = 1;
		for (auto& [key, party] : projectedSupport) {
			party.smooth(ProjectionSmoothingDays); // also automatically calculates expectations
		}
		tppSupport.smooth(ProjectionSmoothingDays);
	}
	catch (std::logic_error const& e) {
		clearOutput();
		startDate = {};
		feedback(std::string("Could not generate projection\n") +
			"Specific information: " + e.what());
		return false;
	}

	try {
		lastUpdated = Timestamp::now();
		std::string report = textReport(models);
		auto reportMessages = splitString(report, ";");
		PA_LOG_VAR(reportMessages);
	}
	catch (std::logic_error const& e) {
		clearOutput();
		startDate = {};
		feedback(std::string("Could not finalise projection\n") +
			"Specific information: " + e.what());
		return false;
	}

	logger << "Completed projection run: " << Timestamp::now().formatIsoLocal() << "\n";
	return true;
}
StanModel::SeriesOutput Projection::viewPrimarySeries(std::string const& partyCode) const
{
	auto const series = projectedSupport.find(partyCode);
	return series == projectedSupport.end() ? nullptr : &series->second;
}

StanModel::SeriesOutput Projection::viewPrimarySeriesByIndex(int index) const
{
	if (index < 0 || index >= int(projectedSupport.size())) return StanModel::SeriesOutput();
	return &std::next(projectedSupport.begin(), index)->second;
}

StanModel::SupportSample Projection::generateNowcastSupportSample(ModelCollection const& models, int iterationIndex, Date date)
{
	if (!date.isValid()) {
		throw std::logic_error("A valid date is required when requesting a nowcast sample.");
	}
	auto projectionEndDate = settings.endDate;
	if (!projectionEndDate.isValid()) {
		throw std::logic_error("A valid projection end date is required for nowcast sampling.");
	}
	if (date > projectionEndDate) {
		throw std::logic_error("The requested nowcast date is after the projection end date.");
	}

	// Get an as-if-election-now sample
	auto electionNowSupportSample = generateSupportSample(models, date, iterationIndex);

	// At the projection endpoint this is an ordinary election forecast, with no
	// need for the retrospective adjustment used for an earlier nowcast.
	if (date == projectionEndDate) {
		return electionNowSupportSample;
	}

	// startDate and detailCreated are transient caches and are not saved with a
	// project, so derive their required state from the persisted model and series.
	auto const projectionStartDate =
		getBaseModel(models).getEndDate() + 1;
	int const endProjIndex = projectionEndDate - projectionStartDate;
	if (endProjIndex < 0) {
		return electionNowSupportSample;
	}
	int sampleProjIndex = date - projectionStartDate;
	sampleProjIndex = std::clamp(sampleProjIndex, 0, endProjIndex);

	// Test that the projected support trend actually exists and extends to the
	// projection end date. If not, return the "election-now" sample
	auto const endProjectionIndex = static_cast<std::size_t>(endProjIndex);
	if (endProjectionIndex >= tppSupport.timePoint.size()) {
		return electionNowSupportSample;
	}
	for (auto const& vote : electionNowSupportSample.voteShare) {
		auto const& party = vote.first;
		if (party == TppCode) continue;
		auto const series = projectedSupport.find(party);
		if (series == projectedSupport.end() ||
			endProjectionIndex >= series->second.timePoint.size()) {
			return electionNowSupportSample;
		}
	}

	int const inverseProjIndex = endProjIndex - sampleProjIndex;
	constexpr int MedianIndex = StanModel::Spread::Size / 2;

	{
		std::lock_guard lock(detailCreationMutex);
		startDate = projectionStartDate;
		if (detailCreated.size() != tppSupport.timePoint.size()) {
			detailCreated.assign(tppSupport.timePoint.size(), false);
		}
		auto createDetailIfNeeded = [&](int index) {
			if (!detailCreated[index]) {
				logger << "Detailed projection:\n";
				PA_LOG_VAR(index);
				createTimePoint(index, models, settings.numIterations);
				PA_LOG_VAR(tppSupport.timePoint.at(index).values[MedianIndex]);
				for (auto const& [party, series] : projectedSupport) {
					PA_LOG_VAR(party);
					PA_LOG_VAR(series.timePoint.at(index).values[MedianIndex]);
					PA_LOG_VAR(series.timePoint.at(index).expectation);
				}
				detailCreated[index] = true;
			}
		};
		createDetailIfNeeded(inverseProjIndex);
		createDetailIfNeeded(endProjIndex);
		createDetailIfNeeded(sampleProjIndex);
		createDetailIfNeeded(0);
	}


	for (auto& [party, voteShare] : electionNowSupportSample.voteShare) {
		auto const& series = party == TppCode ? tppSupport : projectedSupport.at(party);
		float const inverseExpectation = series.timePoint.at(inverseProjIndex).values[MedianIndex];
		float const finalExpectation = series.timePoint.at(endProjIndex).values[MedianIndex];
		float const sampleExpectation = series.timePoint.at(sampleProjIndex).values[MedianIndex];
		float const initialExpectation = series.timePoint.at(0).values[MedianIndex];
		float const adjustment = initialExpectation - sampleExpectation + finalExpectation - inverseExpectation;
		voteShare = predictorCorrectorTransformedSwing(voteShare, adjustment);
	}
	// Retain the sample's original FP-first or TPP-first basis. The retrospective
	// adjustments above move every series independently, so normalisation alone
	// would leave the adjusted FP, TPP and preference flows incompatible.
	getBaseModel(models).finaliseSupportSample(
		electionNowSupportSample, iterationIndex);
	return electionNowSupportSample;
}

StanModel::SupportSample Projection::generateSupportSample(ModelCollection const& models, Date date, int iterationIndex) const
{
	auto const& model = getBaseModel(models);
	if (!date.isValid()) {
		float totalOdds = 0.0f;
		std::vector<std::pair<Date, float>> cumulativeOdds;
		// Entries are decimal odds, so their relative sampling weights are 1 / odds.
		for (auto [thisDate, odds] : settings.possibleDates) {
			auto const tempDate = Date::parseIso(thisDate);
			if (!tempDate) continue;
			if (*tempDate < model.getEndDate()) continue;
			if (!std::isfinite(odds) || odds <= 0.0f) {
				throw std::logic_error(
					"Possible election-date odds must be finite and greater than zero.");
			}
			totalOdds += 1.0f / odds;
			if (!std::isfinite(totalOdds)) {
				throw std::logic_error("Possible election-date odds produced a non-finite total weight.");
			}
			cumulativeOdds.push_back(std::pair(*tempDate, totalOdds));
		}
		if (cumulativeOdds.empty()) {
			date = settings.endDate;
		}
		else {
			float selectedOdds = variabilityUniform(
				0.0f, totalOdds, 0, 0,
				uint32_t(VariabilityTag::SelectDate), iterationIndex);
			date = cumulativeOdds.back().first;
			for (int index = 0; index < int(cumulativeOdds.size()); ++index) {
				if (selectedOdds < cumulativeOdds[index].second) {
					date = cumulativeOdds[index].first;
					break;
				}
			}
		}
	}
	if (!date.isValid()) {
		throw std::logic_error("No valid date is available for projection sampling.");
	}
	int daysAfterModelEnd = date - model.getEndDate();
	daysAfterModelEnd = std::max(daysAfterModelEnd, MinDaysBeforeElection);
	auto sample = model.generateAdjustedSupportSample(model.getEndDate(), daysAfterModelEnd, iterationIndex);

	return sample;
}

int Projection::getPartyIndexFromCode(std::string const& code) const
{
	int index = 0;
	for (auto const& [key, series] : projectedSupport) {
		if (code == key) return index;
		++index;
	}
	return -1;
}

std::string Projection::textReport(ModelCollection const& models) const
{
	std::stringstream report;
	report << "Reporting Projection: \n";
	report << " Name: " << settings.name << "\n";
	report << " Number of iterations: " << settings.numIterations << "\n";
	report << " Base model: " << models.view(settings.baseModel).getName() << "\n";
	report << " End Date: " << getEndDateString() << "\n";
	report << " Last Updated: " << getLastUpdatedString() << "\n";
	report << ";"; // delimiter
	auto sample = generateSupportSample(models, Date{}, 0);
	report << "Final sample: \n";
	for (auto const& [key, vote] : sample.voteShare) {
		report << key << ": " << vote << "\n";
	}
	return report.str();
}

StanModel const& Projection::getBaseModel(ModelCollection const& models) const
{
	return models.view(settings.baseModel);
}

float Projection::variabilityUniform(float low, float high, int itemIndex, std::uint64_t partyId, std::uint32_t tag, int iterationIndex) const {
	std::uint64_t key = variabilityBaseSeed;
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(iterationIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
	key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
	return RandomGenerator::uniform01_from_key(key) * (high - low) + low;
}
