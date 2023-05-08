#include "Projection.h"

#include "General.h"
#include "Log.h"
#include "ModelCollection.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#undef max

std::mutex detailCreationMutex;

Projection::Projection(SaveData saveData)
	: settings(saveData.settings), lastUpdated(saveData.lastUpdated),
	projection(saveData.projection)
{
}

void Projection::replaceSettings(Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxInvalidDateTime;
}

void Projection::createTimePoint(int time, ModelCollection const& models)
{
	auto const& model = getBaseModel(models);
	std::vector<std::vector<float>> samples(model.partyCodeVec.size(), std::vector<float>(settings.numIterations));
	std::unique_ptr<std::vector<float>> tppSamples; // on heap to avoid 
	tppSamples.reset(new std::vector<float>(std::vector<float>(settings.numIterations)));
	for (int iteration = 0; iteration < settings.numIterations; ++iteration) {
		auto projectedDate = startDate + wxDateSpan::Days(time);
		auto sample = generateSupportSample(models, projectedDate);
		for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
			std::string partyName = model.partyCodeVec[partyIndex];
			if (sample.voteShare.count(partyName)) {
				samples[partyIndex][iteration] = sample.voteShare[partyName];
			}
			if (sample.voteShare.count(TppCode)) {
				(*tppSamples)[iteration] = sample.voteShare[TppCode];
			}
		}

	}
	for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
		std::string partyName = model.partyCodeVec[partyIndex];
		std::sort(samples[partyIndex].begin(), samples[partyIndex].end());
		for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
			int sampleIndex = std::min(settings.numIterations - 1, percentile * settings.numIterations / int(StanModel::Spread::Size));
			projectedSupport[partyName].timePoint[time].values[percentile] = samples[partyIndex][sampleIndex];
		}
		projectedSupport[partyName].timePoint[time].calculateExpectation();
	}
	std::sort(tppSamples->begin(), tppSamples->end());
	for (int percentile = 0; percentile < StanModel::Spread::Size; ++percentile) {
		int sampleIndex = std::min(settings.numIterations - 1, percentile * settings.numIterations / int(StanModel::Spread::Size));
		tppSupport.timePoint[time].values[percentile] = (*tppSamples)[sampleIndex];
	}
	tppSupport.timePoint[time].calculateExpectation();
}

void Projection::run(ModelCollection const& models, FeedbackFunc feedback, int numThreads) {
	if (!settings.endDate.IsValid()) return;
	auto const& model = getBaseModel(models);
	if (!model.isReadyForProjection()) {
		feedback("The base model (" + model.name + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return;
	}
	startDate = model.getEndDate() + wxDateSpan::Days(1);

	logger << "Starting projection run: " << wxDateTime::Now().FormatISOCombined() << "\n";

	// Initial run is only for visual purposes so don't do too many iterations for that.
	constexpr static int PreliminaryIterations = 300;
	int iterationsMemory = settings.numIterations;
	settings.numIterations = PreliminaryIterations;
	projectedSupport.clear(); // do this first as it should not be left with previous data
	try {
		int seriesLength = (settings.endDate - startDate).GetDays() + 1;
		for (int partyIndex = 0; partyIndex < int(model.partyCodeVec.size()); ++partyIndex) {
			std::string partyName = model.partyCodeVec[partyIndex];
			projectedSupport[partyName] = StanModel::Series();
			projectedSupport[partyName].timePoint.resize(seriesLength);
		}
		tppSupport.timePoint.resize(seriesLength);
		detailCreated.resize(seriesLength, false);

		constexpr int BatchSize = 10;
		for (int timeStart1 = 0; timeStart1 < seriesLength; timeStart1 += numThreads * BatchSize) {
			auto calculateTimeSupport = [&](int timeStart) {
				for (int time = timeStart; time < timeStart + BatchSize && time < seriesLength; ++time) {
					createTimePoint(time, models);
				}
			};
			std::vector<std::thread> threads;
			for (int timeStart = timeStart1; timeStart < timeStart1 + numThreads * BatchSize && timeStart < seriesLength; timeStart += BatchSize) {
				threads.push_back(std::thread(std::bind(calculateTimeSupport, timeStart)));
			}
			for (auto& thread : threads) {
				if (thread.joinable()) thread.join();
			}
		}

		const int ProjectionSmoothingDays = 1;
		for (auto& [key, party] : projectedSupport) {
			party.smooth(ProjectionSmoothingDays); // also automatically calculates expectations
		}
		tppSupport.smooth(ProjectionSmoothingDays);
	}
	catch (std::logic_error & e) {
		feedback(std::string("Could not generate projection\n") +
			"Specific information: " + e.what());
		return;
	}
	settings.numIterations = iterationsMemory;

	logger << "Completed projection run: " << wxDateTime::Now().FormatISOCombined() << "\n";

	for (auto& [key, support] : projectedSupport) {
		for (int timeCount = 0; timeCount < int(support.timePoint.size()); timeCount += 50) {
			std::stringstream ss;
			ss << key << " - day " << timeCount << "\n";
			for (int spreadVal = 1; spreadVal < 100; ++spreadVal) {
				ss << spreadVal << "%: " << support.timePoint[timeCount].values[spreadVal] << "\n";
			}
		}
	}

	std::string report = textReport(models);
	auto reportMessages = splitString(report, ";");
	for (auto message : reportMessages) feedback(message);
	lastUpdated = wxDateTime::Now();
}


void Projection::logRunStatistics()
{
	logger << "--------------------------------\n";
	logger << "Projection completed.\n";
	logger << "Final 2PP mean value: " << projection.back().mean << "\n";
	logger << "Final 2PP standard deviation: " << projection.back().sd << "\n";
}

void Projection::setAsNowCast(ModelCollection const& models) {
	auto model = models.view(settings.baseModel);
	// Make sure the nowcast doesn't go past the end of the election period
	auto electionPeriodLines = extractElectionDataFromFile("analysis/Data/election-cycles.csv", model.getTermCode());
	wxDateTime parsedDate;
	parsedDate.ParseDate(electionPeriodLines[0][3]);
	settings.endDate = std::min(parsedDate, std::max(model.getEndDate() + wxDateSpan(0, 0, 0, MinDaysBeforeElection), wxDateTime::Now()));
}

StanModel::SeriesOutput Projection::viewPrimarySeries(std::string partyCode) const
{
	if (!projectedSupport.count(partyCode)) return StanModel::SeriesOutput();
	return &projectedSupport.at(partyCode);
}

StanModel::SeriesOutput Projection::viewPrimarySeriesByIndex(int index) const
{
	if (index < 0 || index >= int(projectedSupport.size())) return StanModel::SeriesOutput();
	return &std::next(projectedSupport.begin(), index)->second;
}

StanModel::SupportSample Projection::generateNowcastSupportSample(ModelCollection const& models, wxDateTime date)
{
	// Get an as-if-election-now sample
	auto electionNowSupportSample = generateSupportSample(models, date);
	// convert dates to projection indices, adding 4 additional hours to smooth over any DST etc. related issues
	int sampleProjIndex = (date - startDate).GetDays();
	int endProjIndex = (settings.endDate - startDate).GetDays();
	sampleProjIndex = std::clamp(sampleProjIndex, 0, endProjIndex);

	// an invalid date indicates forecasting an actual election
	// (possibly for a range of different election dates)
	// so don't interpret as a now-cast
	// similarly, if we're at the projection end date, also assume it's for an actual election
	if (!date.IsValid() || (date - settings.endDate).GetDays() == 0) {
		return electionNowSupportSample;
	}
	
	// Test that the projected support trend actually exists and extends to the
	// projection end date. If not, return the "election-now" sample
	// It is assumed that the other parties and TPP will also be projected to the same point (or more)
	if (endProjIndex >= std::ssize(tppSupport.timePoint)) {
		return electionNowSupportSample;
	}

	int inverseProjIndex = endProjIndex - sampleProjIndex;

	auto createDetailIfNeeded = [&](int index) {
		if (!detailCreated[index]) {
			logger << "Detailed projection:\n";
			PA_LOG_VAR(index);
			createTimePoint(index, models);
			PA_LOG_VAR(tppSupport.timePoint.at(index).values[50]);
			for (auto [party, series] : projectedSupport) {
				PA_LOG_VAR(party);
				PA_LOG_VAR(series.timePoint.at(index).values[50]);
			}
			detailCreated[index] = true;
		}
	};

	{
		std::lock_guard lock(detailCreationMutex);
		createDetailIfNeeded(inverseProjIndex);
		createDetailIfNeeded(endProjIndex);
		createDetailIfNeeded(sampleProjIndex);
		createDetailIfNeeded(0);
	}

	for (auto [party, voteShare] : electionNowSupportSample.voteShare) {
		// It is important that the expectation (rather than median) is used here
		// as this guarantees that the resultant adjusted sample will still add to 100 without needing adjustments
		float inverseExpectation = party == TppCode ? tppSupport.timePoint.at(inverseProjIndex).expectation : projectedSupport.at(party).timePoint.at(inverseProjIndex).expectation;
		float finalExpectation = party == TppCode ? tppSupport.timePoint.at(endProjIndex).expectation : projectedSupport.at(party).timePoint.at(endProjIndex).expectation;
		float sampleExpectation = party == TppCode ? tppSupport.timePoint.at(sampleProjIndex).expectation : projectedSupport.at(party).timePoint.at(sampleProjIndex).expectation;
		float initialExpectation = party == TppCode ? tppSupport.timePoint.at(0).expectation : projectedSupport.at(party).timePoint.at(0).expectation;
		float adjustment = initialExpectation - sampleExpectation + finalExpectation - inverseExpectation;
		electionNowSupportSample.voteShare[party] = predictorCorrectorTransformedSwing(electionNowSupportSample.voteShare[party], adjustment);
	}
	// due to the transformations required to keep vote shares in (0, 100),
	// the sample might not quite add to 100, so normalise it now
	StanModel::normaliseSample(electionNowSupportSample);
	return electionNowSupportSample;
}

StanModel::SupportSample Projection::generateSupportSample(ModelCollection const& models, wxDateTime date) const
{
	auto const& model = getBaseModel(models);
	if (!date.IsValid()) {
		float totalOdds = 0.0f;
		std::vector<std::pair<wxDateTime, float>> cumulativeOdds;
		for (auto [thisDate, odds] : settings.possibleDates) {
			wxDateTime tempDate;
			bool success = tempDate.ParseISODate(thisDate);
			if (!success) continue;
			if (tempDate < model.getEndDate()) continue;
			totalOdds += 1.0f / odds;
			cumulativeOdds.push_back(std::pair(tempDate, totalOdds));
		}
		if (!cumulativeOdds.size()) {
			date = settings.endDate;
		}
		else {
			float selectedOdds = rng.uniform(0.0f, totalOdds);
			for (int index = 0; index < int(cumulativeOdds.size()); ++index) {
				if (selectedOdds < cumulativeOdds[index].second) {
					date = cumulativeOdds[index].first;
					break;
				}
			}
		}
	}
	date += wxTimeSpan(4); // adding four hours to make sure date comparisons are consistent
	int daysAfterModelEnd = (date - model.getEndDate()).GetDays();
	daysAfterModelEnd = std::max(daysAfterModelEnd, MinDaysBeforeElection);
	auto sample = model.generateAdjustedSupportSample(model.getEndDate(), daysAfterModelEnd);

	return sample;
}

int Projection::getPartyIndexFromCode(std::string code) const
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
	auto sample = generateSupportSample(models);
	report << "Final sample: \n";
	for (auto [key, vote] : sample.voteShare) {
		report << key << ": " << vote << "\n";
	}
	return report.str();
}

StanModel const& Projection::getBaseModel(ModelCollection const& models) const
{
	return models.view(settings.baseModel);
}
