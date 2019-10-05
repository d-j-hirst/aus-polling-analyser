#include "Model.h"

#include <algorithm>
#include <numeric>

#include "EventCollection.h"
#include "General.h"
#include "Log.h"
#include "PollCollection.h"
#include "PollsterCollection.h"

constexpr float Invalid2pp = std::numeric_limits<float>::lowest();

constexpr float PollZScoreCap = 3.0f;

Model::Model(SaveData saveData)
	: settings(saveData.settings), lastUpdated(saveData.lastUpdated),
	finalStandardDeviation(saveData.finalStandardDeviation)
{
	for (auto trendPoint : saveData.trend) {
		day.push_back(TimePoint(0)); // pollster count is reset when model is run anyway, so leave it at zero
		day.back() = trendPoint;
	}
}

void Model::replaceSettings(Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxDateTime::Now();
}

Model::Settings const & Model::getSettings() const
{
	return settings;
}

std::string Model::getStartDateString() const
{
	if (!settings.startDate.IsValid()) return "";
	else return settings.startDate.FormatISODate().ToStdString();
}

std::string Model::getEndDateString() const
{
	if (!settings.endDate.IsValid()) return "";
	else return settings.endDate.FormatISODate().ToStdString();
}

std::string Model::getLastUpdatedString() const
{
	if (!lastUpdated.IsValid()) return "";
	else return lastUpdated.FormatISODate().ToStdString();
}

void Model::run(PollsterCollection const& pollsters, PollCollection const& polls, EventCollection const& events)
{
	initializeRun(pollsters, polls);
	if (!day.size()) return;
	importPollsters(pollsters);
	importPolls(pollsters, polls);
	importEvents(events);
	setInitialPath();
	doModelIterations();
	determineFinalStandardDeviation();
	logRunStatistics();
	finalizeRun();
}

void Model::extendToDate(wxDateTime date)
{
	if (date < settings.endDate) return;
	settings.endDate = date;
}

void Model::initializeRun(PollsterCollection const& pollsters, PollCollection const& polls) {
	updateEffectiveDates(polls);
	day.clear();

	double startMJD = effStartDate.GetMJD();
	double endMJD = effEndDate.GetMJD();
	int numDays = int(endMJD) - int(startMJD) + 1;
	if (numDays <= 0) return;
	day.resize(numDays, TimePoint(pollsters.count()));
	pollsterCache.resize(pollsters.count());
}

void Model::updateEffectiveDates(PollCollection const& polls) {
	// If the start or end dates have not been set, then default to running the model from first to last polls
	if (!settings.startDate.IsValid()) settings.startDate = mjdToDate(polls.getEarliestDate());
	if (!settings.endDate.IsValid()) settings.endDate = mjdToDate(polls.getLatestDate());
	effStartDate = mjdToDate(polls.getEarliestDateFrom(settings.startDate));
	effEndDate = mjdToDate(polls.getLatestDateUpTo(settings.endDate));
}

void Model::importPollsters(PollsterCollection const& pollsters)
{
	for (auto const& [key, pollster] : pollsters) {
		int pollsterIndex = pollsters.idToIndex(key);
		pollsterCache[pollsterIndex].useForCalibration = pollster.useForCalibration;
		pollsterCache[pollsterIndex].ignoreInitially = pollster.ignoreInitially;
		pollsterCache[pollsterIndex].weight = pollster.weight;
	}
}

void Model::importPolls(PollsterCollection const & pollsters, PollCollection const & polls)
{
	for (auto const& [key, poll] : polls) {
		PollsterCollection::Index pollsterIndex = pollsters.idToIndex(poll.pollster);
		if (pollsters.view(poll.pollster).weight < 0.01f) continue;
		importPoll(poll.getBest2pp(), poll.date, pollsterIndex);
	}
}

void Model::importEvents(EventCollection const& events)
{
	for (auto const& [key, event] : events) {
		if (event.eventType == EventType_Election) {
			importElection(event.vote, event.date);
		}
		else if (event.eventType == EventType_Discontinuity) {
			importDiscontinuity(event.date);
		}
	}
}

void Model::importPoll(float poll2pp, wxDateTime pollDate, int pollsterIndex) {
	int timeIndex = int(pollDate.GetMJD()) - int(effStartDate.GetMJD());
	if (timeIndex >= 0 && timeIndex < int(day.size()) && poll2pp > 0.1f) {
		day[timeIndex].polls.push_back(CachedPoll(pollsterIndex, poll2pp));
	}
}

void Model::importElection(float election2pp, wxDateTime electionDate) {
	int timeIndex = int(electionDate.GetMJD()) - int(effStartDate.GetMJD());
	if (timeIndex >= 0 && timeIndex < int(day.size()) && election2pp > 0.1f) {
		day[timeIndex].election = election2pp;
	}
}

void Model::importDiscontinuity(wxDateTime electionDate) {
	int timeIndex = int(electionDate.GetMJD()) - int(effStartDate.GetMJD());
	if (timeIndex >= 0 && timeIndex < int(day.size())) {
		day[timeIndex].discontinuity = true;
	}
}

void Model::determineFinalStandardDeviation()
{
	float totalEvidence = determineFinalPollingEvidenceFactor();
	constexpr float MaximumFinalDeviation = 3.0f;
	finalStandardDeviation = (totalEvidence ?
		std::min(MaximumFinalDeviation, 1.0f / totalEvidence) :
		MaximumFinalDeviation);
}

float Model::determineFinalPollingEvidenceFactor()
{
	float totalEvidence = 0.0f;
	int daysBeforeFinal = 0;
	float daysHalfLife = settings.trendTimeScoreMultiplier * 2;
	for (auto thisDay = day.rbegin(); thisDay != day.rend(); ++thisDay) {
		// Generally, the more polls there are and the more consistent they are, the more confidence
		// we should have in the final result
		// However, should also at some point add a correction for herding and also for too little
		// diversity in the pollsters producing polls
		for (std::size_t pollIndex = 0; pollIndex < thisDay->polls.size(); ++pollIndex) {
			float basePollLikelihood = calculatePollLikelihood(&*thisDay, pollIndex);
			CachedPollster const& thisPollster = pollsterCache[thisDay->polls[pollIndex].pollster];
			basePollLikelihood /= thisPollster.accuracy;
			float timeBasedExponentialDecay = pow(2.0f, -float(daysBeforeFinal) / daysHalfLife);
			totalEvidence += pow(basePollLikelihood, 0.5f) * timeBasedExponentialDecay;
		}

		// If there is some evidence after the discontinuity, then we
		// don't want to count evidence before the discontinuity as it's irrelevant to the new situation
		if (thisDay->discontinuity && totalEvidence) break;
		++daysBeforeFinal;
	}
	return totalEvidence;
}

void Model::logRunStatistics()
{
	logger << "--------------------------------" << "\n";
	logger << "Model run completed.\n";
	logger << "Final trend 2PP: " << day.back().trend2pp << "\n";
	logger << "Final additional standard deviation: " << finalStandardDeviation << "\n";
	for (size_t pollsterIndex = 0; pollsterIndex < pollsterCache.size(); ++pollsterIndex) {
		logger << " ---------" << "\n";
		logger << " Pollster index: " << pollsterIndex << " - Accuracy: " << pollsterCache[pollsterIndex].accuracy << "\n";
		logger << " - House effect: " << pollsterCache[pollsterIndex].houseEffect << "\n";
	}
}

void Model::finalizeRun() {
	lastUpdated = wxDateTime::Now();
}

void Model::setInitialPath() {
	setInitialPolling2pp();
	interpolateInitialPolling2pp();
}

void Model::setInitialPolling2pp()
{
	for (int dayIndex = 0; dayIndex < int(day.size()); ++dayIndex) {
		TimePoint& timePoint = day[dayIndex];
		float total2pp = 0.0f;
		float totalWeight = 0.0f;
		if (timePoint.election > 0.00001f) {
			total2pp = timePoint.election;
			totalWeight = 1.0f;
		}
		else {
			for (int pollIndex = 0; pollIndex < int(timePoint.polls.size()); ++pollIndex) {
				CachedPoll const* poll = &timePoint.polls[pollIndex];
				if (pollsterCache[poll->pollster].ignoreInitially) continue;
				total2pp += poll->raw2pp;
				totalWeight += 1.0f;
			}
		}
		if (totalWeight) {
			timePoint.trend2pp = total2pp / totalWeight;
		}
		else if (dayIndex == 0 || dayIndex == int(day.size()) - 1) {
			constexpr float Default2pp = 50.0f;
			timePoint.trend2pp = Default2pp;
		}
		else {
			timePoint.trend2pp = Invalid2pp;
		}
	}
}

void Model::interpolateInitialPolling2pp()
{
	int prevPollIndex = 0;
	float prevPoll2pp = Invalid2pp;
	for (int dayIndex = 0; dayIndex < int(day.size()); ++dayIndex) {
		TimePoint& timePoint = day[dayIndex];
		float thisDay2pp = timePoint.trend2pp;
		if (thisDay2pp != Invalid2pp) {
			if (prevPoll2pp == Invalid2pp) prevPoll2pp = thisDay2pp;
			if (dayIndex - prevPollIndex > 1) {
				int dayRangeLength = dayIndex - prevPollIndex;
				for (int dayRangeIndex = prevPollIndex + 1; dayRangeIndex < dayIndex; ++dayRangeIndex) {
					TimePoint& rangeTimePoint = day[dayRangeIndex];
					rangeTimePoint.trend2pp = prevPoll2pp * (dayIndex - dayRangeIndex) / dayRangeLength +
						thisDay2pp * (dayRangeIndex - prevPollIndex) / dayRangeLength;
				}
			}
			prevPoll2pp = thisDay2pp;
			prevPollIndex = dayIndex;
		}
	}
}

void Model::setInitialHouseEffectPath() {
	for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
		setInitialHouseEffectFromPolls(pollsterIndex);
		interpolateInitialHouseEffects(pollsterIndex);
	}
}

void Model::setInitialHouseEffectFromPolls(int pollsterIndex)
{
	int prevPollIndex = 0;
	for (int dayIndex = 0; dayIndex < int(day.size()); ++dayIndex) {
		TimePoint& timePoint = day[dayIndex];
		// If there's an election just set it to the vote for the election
		timePoint.houseEffect[pollsterIndex] = Invalid2pp;
		if (!timePoint.polls.size() && dayIndex != int(day.size()) - 1) continue;
		float totalHouseEffect = 0.0f;
		float totalWeight = 0.0f;
		for (int pollIndex = 0; pollIndex < int(timePoint.polls.size()); ++pollIndex) {
			CachedPoll const* poll = &timePoint.polls[pollIndex];
			if (poll->pollster != pollsterIndex) continue;
			if (pollsterCache[poll->pollster].ignoreInitially) continue;
			totalHouseEffect += poll->raw2pp - day[dayIndex].trend2pp;
			totalWeight += 1.0f;
		}
		if (totalWeight) {
			timePoint.houseEffect[pollsterIndex] = totalHouseEffect / totalWeight;
			prevPollIndex = dayIndex;
		}
		else if (dayIndex > prevPollIndex + 100 || dayIndex == 0 || dayIndex == int(day.size()) - 1)
		{		// If we've gone a long time without a poll, just set the house effect
				// to the pollster's overall house effect.
				// Also do this 
			timePoint.houseEffect[pollsterIndex] = pollsterCache[pollsterIndex].houseEffect;
			prevPollIndex = dayIndex;
		}
	}
}

void Model::interpolateInitialHouseEffects(int pollsterIndex)
{
	int prevPollIndex = 0;
	float prevPollHouseEffect = Invalid2pp;
	for (int dayIndex = 0; dayIndex < int(day.size()); ++dayIndex) {
		TimePoint& timePoint = day[dayIndex];
		float thisDayHouseEffect = timePoint.houseEffect[pollsterIndex];
		if (thisDayHouseEffect != Invalid2pp) {
			if (prevPollHouseEffect == Invalid2pp) prevPollHouseEffect = thisDayHouseEffect;
			if (dayIndex - prevPollIndex > 1) {
				int dayRangeLength = dayIndex - prevPollIndex;
				for (int dayRangeIndex = prevPollIndex + 1; dayRangeIndex < dayIndex; ++dayRangeIndex) {
					TimePoint& rangeTimePoint = day[dayRangeIndex];
					rangeTimePoint.houseEffect[pollsterIndex] = prevPollHouseEffect * (dayIndex - dayRangeIndex) / dayRangeLength +
						thisDayHouseEffect * (dayRangeIndex - prevPollIndex) / dayRangeLength;
				}
			}
			prevPollHouseEffect = thisDayHouseEffect;
			prevPollIndex = dayIndex;
		}
	}
}

void Model::doModelIterations() {
	for (iteration = 0; iteration < settings.numIterations; iteration++) {
		calculateOverallHouseEffects();
		calibrateOverallHouseEffects();
		constexpr int NumIterationsFreedHouseEffects = 50;
		if (iteration > NumIterationsFreedHouseEffects) {
			debiasDailyHouseEffects();
		}
		else if (iteration == NumIterationsFreedHouseEffects) {
			setInitialHouseEffectPath();
		}
		else {
			setUniformDailyHouseEffects();
		}
		calibrateDailyHouseEffects();
		recalculateEffectiveDaily2pps();
		calculatePollsterAccuracy();
		calculateDailyErrorScores();
		calculateDailyTrendAdjustments();
		calculateDailyHouseEffectAdjustments();
		adjustDailyValues();
	}
}

void Model::calculateOverallHouseEffects() {

	// set up vectors that contain numerators/denominators for
	// calculating each pollster's overall house effect.
	std::vector<float> houseEffectNum, houseEffectDenom;
	houseEffectNum.resize(pollsterCache.size(), 0);
	houseEffectDenom.resize(pollsterCache.size(), 0);

	// go through each poll and use the difference between it and the trend
	// to add to the numerator/denominator of the relevant pollster's house effect
	for (int dayIndex = 0; dayIndex < int(day.size()); ++dayIndex) {
		TimePoint* thisDay = &day[dayIndex];
		int nPolls = int(thisDay->polls.size());
		if (nPolls) {
			for (int pollIndex = 0; pollIndex < nPolls; ++pollIndex) {
				int thisPollsterIndex = thisDay->polls[pollIndex].pollster;
				houseEffectNum[thisPollsterIndex] += thisDay->polls[pollIndex].raw2pp - thisDay->trend2pp;
				houseEffectDenom[thisPollsterIndex]++;
			}
		}
	}

	// actually calculate the house effect of each pollster,
	// assuming zero if the pollster doesn't have any polls
	// (because the house effect may still be used for calibration purposes).
	for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
		if (houseEffectDenom[pollsterIndex])
			pollsterCache[pollsterIndex].houseEffect = houseEffectNum[pollsterIndex] / houseEffectDenom[pollsterIndex];
		else
			pollsterCache[pollsterIndex].houseEffect = 0;
	}
}

void Model::calibrateOverallHouseEffects() {
	float houseEffectsSum = 0.0f;
	pollsterCalibrationCount = 0;

	// Go through all calibrating pollsters and add their house effects.
	for (CachedPollster& thisPollster : pollsterCache) {
		// only use pollsters that have been marked for used with calibration.
		if (!thisPollster.useForCalibration) continue;

		houseEffectsSum += thisPollster.houseEffect;
		pollsterCalibrationCount++;
	}

	// work out how much to adjust the house effects by.
	float houseEffectsAdjust = houseEffectsSum / float(pollsterCalibrationCount) - settings.calibrationFirstPartyBias;

	// Go through all pollsters and adjust their house effects accordingly.
	for (CachedPollster& thisPollster : pollsterCache) {
		thisPollster.houseEffect -= houseEffectsAdjust;
	}
}

void Model::debiasDailyHouseEffects() {
	// Uses the calibration procedure to adjust the recorded house effects.
	// The average house effect is summed across all time points
	// and compared to the overall house effect in terms of polls vs. model
	// (previously calculated)

	std::vector<float> localHouseEffectSum;
	std::vector<float> localHouseEffectDenom;
	std::vector<float> localHouseEffectBias;
	localHouseEffectSum.resize(pollsterCache.size(), 0.0f);
	localHouseEffectDenom.resize(pollsterCache.size(), 0.0f);
	localHouseEffectBias.resize(pollsterCache.size(), 0.0f);

	// calculate the total house effect across all days for each pollster.
	for (TimePoint const& thisDay : day) {
		for (CachedPoll const& thisPoll : thisDay.polls) {
			localHouseEffectSum[thisPoll.pollster] += thisDay.houseEffect[thisPoll.pollster];
			localHouseEffectDenom[thisPoll.pollster]++;
		}
	}

	// work out how much this value deviates from the calibrated overall house effect.
	for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
		CachedPollster const& thisPollster = pollsterCache[pollsterIndex];
		if (localHouseEffectDenom[pollsterIndex]) {
			localHouseEffectBias[pollsterIndex] = localHouseEffectSum[pollsterIndex] / localHouseEffectDenom[pollsterIndex] - thisPollster.houseEffect;
		}
		else {
			localHouseEffectBias[pollsterIndex] = day[0].houseEffect[pollsterIndex] - thisPollster.houseEffect;
		}
	}

	// adjust all the daily house effect values appropriately.
	for (TimePoint& thisDay : day) {
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			thisDay.houseEffect[pollsterIndex] -= localHouseEffectBias[pollsterIndex];
		}
	}
}

void Model::setUniformDailyHouseEffects() {
	// Used for the first few iterations - here we just want an uniform average estimate
	// of the house effects
	for (TimePoint& thisDay : day) {
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			CachedPollster& thisPollster = pollsterCache[pollsterIndex];
			thisDay.houseEffect[pollsterIndex] = thisPollster.houseEffect;
		}
	}
}

void Model::calibrateDailyHouseEffects() {
	for (TimePoint& thisDay : day) {
		float houseEffectsCalibrationSum = 0; // sum of house effects of calibrated pollsters
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			CachedPollster& thisPollster = pollsterCache[pollsterIndex];
			if (thisPollster.useForCalibration) {
				houseEffectsCalibrationSum += thisDay.houseEffect[pollsterIndex];
			}
		}
		float houseEffectsCalibrationAdjustment = settings.calibrationFirstPartyBias - houseEffectsCalibrationSum / pollsterCalibrationCount;
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			thisDay.houseEffect[pollsterIndex] += houseEffectsCalibrationAdjustment;
		}

	}
}

void Model::recalculateEffectiveDaily2pps() {
	for (TimePoint& thisDay : day) {
		for (CachedPoll& thisPoll : thisDay.polls) {
			float houseEffect = thisDay.houseEffect[thisPoll.pollster];
			thisPoll.eff2pp = thisPoll.raw2pp - houseEffect;
		}
	}
}

void Model::calculatePollsterAccuracy() {
	std::vector<float> totalErrorSquared;
	std::vector<float> numPolls;
	totalErrorSquared.resize(pollsterCache.size());
	numPolls.resize(pollsterCache.size());
	for (TimePoint& thisDay : day) {
		for (CachedPoll const& thisPoll : thisDay.polls) {
			float error = thisPoll.raw2pp - pollsterCache[thisPoll.pollster].houseEffect - thisDay.trend2pp;
			totalErrorSquared[thisPoll.pollster] += error * error;
			++numPolls[thisPoll.pollster];
		}
	}
	for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
		constexpr float PollAccuracyFloorZeroPolls = 1.0f;
		constexpr float PollAccuracyFloorPerPoll = 0.05f;
		constexpr float PollAccuracyFloorLimit = 0.4f;
		float accuracyFloor = std::max(PollAccuracyFloorLimit, 
			PollAccuracyFloorZeroPolls - PollAccuracyFloorPerPoll * numPolls[pollsterIndex]);
		pollsterCache[pollsterIndex].accuracy = std::max(accuracyFloor, 
			std::sqrt(totalErrorSquared[pollsterIndex] / float(std::max(1.0f, numPolls[pollsterIndex] - 1))));
	}
}

void Model::calculateDailyErrorScores() {
	for (int i = 0; i < int(day.size()); ++i) {
		TimePoint* thisDay = &day[i];
		thisDay->trendScore = calculateTrendScore(i);
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			thisDay->houseEffectScore[pollsterIndex] = calculateHouseEffectScore(i, pollsterIndex);
		}
	}
}

float Model::calculateTrendScore(int dayIndex, float usetrend2pp) const {
	TimePoint const* thisDay = &day[dayIndex];

	float tempTrendScore = 0;

	// get previous and next days using (fast) pointer arithmetic
	TimePoint const* prevDay = (dayIndex > 0 ? thisDay - 1 : nullptr);
	TimePoint const* nextDay = (dayIndex + 1 < int(day.size()) ? thisDay + 1 : nullptr);

	int numPolls = thisDay->polls.size();

	// Add the poll scores (score is higher for polls further from the trend)
	if (numPolls) {
		for (int pollIndex = 0; pollIndex < numPolls; ++pollIndex) {
			tempTrendScore += calculatePollScore(thisDay, pollIndex, usetrend2pp);
		}
	}

	// Add the time scores (score is higher for more rapid movement in the trends)
	if (prevDay && !prevDay->discontinuity) tempTrendScore += calculateTimeScore(thisDay, prevDay, usetrend2pp);
	if (thisDay && !thisDay->discontinuity) tempTrendScore += calculateTimeScore(thisDay, nextDay, usetrend2pp);

	return tempTrendScore;
}

float Model::calculateHouseEffectScore(int dayIndex, int pollsterIndex, float useHouseEffect) const {
	TimePoint const* thisDay = &day[dayIndex];

	float tempTrendScore = 0;

	// get previous and next days using (fast) pointer arithmetic
	TimePoint const* prevDay = (dayIndex > 0 ? thisDay - 1 : nullptr);
	TimePoint const* nextDay = (dayIndex + 1 < int(day.size()) ? thisDay + 1 : nullptr);

	int numPolls = thisDay->polls.size();

	// Add the poll scores (score is higher for polls further from the trend)
	if (numPolls) {
		for (int pollIndex = 0; pollIndex < numPolls; ++pollIndex) {
			if (thisDay->polls[pollIndex].pollster == pollsterIndex) {
				tempTrendScore += calculateHouseEffectPollScore(thisDay, pollIndex, pollsterIndex, useHouseEffect);
			}
		}
	}

	// Add the time scores (score is higher for more rapid movement in the trends)
	tempTrendScore += calculateHouseEffectTimeScore(thisDay, prevDay, pollsterIndex, useHouseEffect);
	tempTrendScore += calculateHouseEffectTimeScore(thisDay, nextDay, pollsterIndex, useHouseEffect);

	return tempTrendScore;
}

float Model::calculatePollScore(TimePoint const* timePoint, int pollIndex, float usetrend2pp) const {
	CachedPollster const& thisPollster = pollsterCache[timePoint->polls[pollIndex].pollster];
	// Extra 0.001f added means there's a ceiling to the score than any individual poll can have
	float pollScoreIncrease = 1.0f / (calculatePollLikelihood(timePoint, pollIndex, usetrend2pp) + 0.001f) - 1.0f;
	constexpr float PollScoreMultipler = 10.0f;
	pollScoreIncrease *= PollScoreMultipler;
	pollScoreIncrease *= thisPollster.weight;
	return pollScoreIncrease;
}

float Model::calculatePollLikelihood(TimePoint const* timePoint, int pollIndex, float usetrend2pp) const
{
	// if we haven't been given a proper alternative 2pp, just use the actual trend 2pp.
	if (usetrend2pp < 0.0f) usetrend2pp = timePoint->trend2pp;

	CachedPollster const& thisPollster = pollsterCache[timePoint->polls[pollIndex].pollster];

	// work out the difference between the house effect-adjusted poll and the trend 2pp.
	float pollDiff = usetrend2pp - timePoint->polls[pollIndex].eff2pp;

	// normalized z-score for this difference between this pollster and the trend line
	// capping this ensures outliers don't have too much of an influence on things
	float pollDeviation = std::min(std::max(pollDiff / thisPollster.accuracy, -PollZScoreCap), PollZScoreCap);

	return 1.0f - abs((0.5f - float(normalDistribution(pollDeviation))) * 2.0f);
}

float Model::calculateHouseEffectPollScore(TimePoint const* timePoint, int pollIndex, int pollsterIndex, float useHouseEffect) const {
	// if we haven't been given a proper alternative 2pp, just use the actual trend 2pp.
	float usetrend2pp = timePoint->trend2pp;

	if (useHouseEffect < -100.0f) useHouseEffect = timePoint->houseEffect[pollsterIndex];

	CachedPollster const& thisPollster = pollsterCache[timePoint->polls[pollIndex].pollster];

	// work out the difference between the house effect-adjusted poll and the trend 2pp.
	float pollDiff = usetrend2pp - timePoint->polls[pollIndex].raw2pp + useHouseEffect;

	// normalized z-score for this difference between this pollster and the trend line
	// capping this ensures outliers don't have too much of an influence on things
	float pollDeviation = std::min(std::max(pollDiff / thisPollster.accuracy, -PollZScoreCap), PollZScoreCap);

	const float HouseEffectPollScoreMultipler = 3.0f;

	// Extra 0.01f added means there's a ceiling to the score than any individual poll can have
	float pollScoreIncrease = 1.0f / (1.0f - abs((0.5f - float(normalDistribution(pollDeviation))) * 2.0f) + 0.01f) - 1.0f;

	pollScoreIncrease *= HouseEffectPollScoreMultipler;

	return pollScoreIncrease;
}

float Model::calculateTimeScore(TimePoint const* timePoint, TimePoint const* otherTimePoint, float usetrend2pp) const {
	if (!otherTimePoint) return 0.0f;
	if (usetrend2pp < 0.0f) usetrend2pp = timePoint->trend2pp;
	float temp = (settings.trendTimeScoreMultiplier * abs(usetrend2pp - otherTimePoint->trend2pp));
	return abs(temp * temp * temp); // done this way to ensure most efficient computation
}

float Model::calculateHouseEffectTimeScore(TimePoint const* timePoint, TimePoint const* otherTimePoint, int pollsterIndex, float useHouseEffect) const {
	if (!otherTimePoint) return 0.0f;
	if (useHouseEffect < -100.0f) useHouseEffect = timePoint->houseEffect[pollsterIndex];
	float temp = (settings.houseEffectTimeScoreMultiplier * abs(useHouseEffect - otherTimePoint->houseEffect[pollsterIndex]));
	return abs(temp * temp * temp); // done this way to ensure most efficient computation
}

// adjusts the daily trend scores to the lowest possible error score.
void Model::calculateDailyTrendAdjustments() {
	int lastResult = int(day.size()) - 1;
	for (int i = 0; i <= lastResult; ++i) {
		TimePoint* thisDay = &day[i];
		if (thisDay->election > 0.00001f) {
			thisDay->nextTrend2pp = thisDay->trend2pp;
			continue;
		}
		TimePoint* prevDay = (i > 0 ? thisDay - 1 : nullptr);
		TimePoint* nextDay = (i < lastResult ? thisDay + 1 : nullptr);

		// Will be changing around the trend 2pp for this day
		// in order to find the best value for the next model iteration,
		// so keep a record here so that it can be reset once done
		float origTrend2pp = thisDay->trend2pp;

		// the scores to be compared combine those for this day and the neighbouring days (if they exist).
		float origScore = thisDay->trendScore;
		if (prevDay) origScore += prevDay->trendScore;
		if (nextDay) origScore += nextDay->trendScore;
		float bestChangeMod = 0.0f;
		float bestScore = origScore;

		for (float changeMod = 0.001f; changeMod < 1.0f; changeMod *= 2.0f) {
			// loop through trend 2pp slightly lower and higher than this one.
			for (float signedChangeMod = -changeMod, j = 0; j < 2; signedChangeMod += changeMod * 2.0f, ++j) {

				// set the new trend2pp to be adjusted by an increment
				thisDay->trend2pp = origTrend2pp + signedChangeMod;
				float newScore = calculateTrendScore(i);
				if (prevDay) newScore += calculateTrendScore(i - 1);
				if (nextDay) newScore += calculateTrendScore(i + 1);
				if (newScore < bestScore) {
					bestScore = newScore;
					bestChangeMod = signedChangeMod;
				}
			}
		}

		thisDay->nextTrend2pp = origTrend2pp + bestChangeMod * 0.5f; // Factor of 0.5 prevents permanent oscillations
																	 // from neighbouring time-points swapping places
		thisDay->trend2pp = origTrend2pp;
	}
}

// adjusts the daily house effect scores to the lowest possible error score.
void Model::calculateDailyHouseEffectAdjustments() {
	int lastResult = int(day.size()) - 1;
	for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
		for (int i = 0; i < lastResult + 1; ++i) {
			TimePoint* thisDay = &day[i];
			TimePoint* prevDay = (i > 0 ? thisDay - 1 : nullptr);
			TimePoint* nextDay = (i < lastResult ? thisDay + 1 : nullptr);

			// Will be changing around the house effect for this day
			// in order to find the best value for the next model iteration,
			// so keep a record here so that it can be reset once done
			float origHouseEffect = thisDay->houseEffect[pollsterIndex];

			// the scores to be compared combine those for this day and the neighbouring days (if they exist).
			float origScore = thisDay->houseEffectScore[pollsterIndex];
			if (prevDay) origScore += prevDay->houseEffectScore[pollsterIndex];
			if (nextDay) origScore += nextDay->houseEffectScore[pollsterIndex];
			float bestChangeMod = 0.0f;
			float bestScore = origScore;
			for (float changeMod = 0.001f; changeMod < 1.0f; changeMod *= 2.0f) {
				// loop through trend 2pp slightly lower and higher than this one.
				for (float signedChangeMod = -changeMod, j = 0; j < 2; signedChangeMod += changeMod * 2.0f, ++j) {

					// set the new trend2pp to be adjusted by an increment
					thisDay->houseEffect[pollsterIndex] = origHouseEffect + signedChangeMod;
					float newScore = calculateHouseEffectScore(i, pollsterIndex);
					if (prevDay) newScore += calculateHouseEffectScore(i - 1, pollsterIndex);
					if (nextDay) newScore += calculateHouseEffectScore(i + 1, pollsterIndex);
					if (newScore < bestScore) {
						bestScore = newScore;
						bestChangeMod = signedChangeMod;
					}
				}
			}

			// make sure these values are the ones we want.
			thisDay->nextHouseEffect[pollsterIndex] = origHouseEffect + bestChangeMod;
			thisDay->houseEffect[pollsterIndex] = origHouseEffect;
		}
	}
}

void Model::adjustDailyValues() {
	for (TimePoint& thisDay : day) {
		thisDay.trend2pp = thisDay.nextTrend2pp;
		for (int pollsterIndex = 0; pollsterIndex < int(pollsterCache.size()); ++pollsterIndex) {
			thisDay.houseEffect[pollsterIndex] = thisDay.nextHouseEffect[pollsterIndex];
		}
	}
}

double Model::normalDistribution(double z) {
	//******************************************************************
	//*  Adapted from http://lib.stat.cmu.edu/apstat/66
	//******************************************************************

	const double a0 = 0.5;
	const double a1 = 0.398942280444;
	const double a2 = 0.399903438505;
	const double a3 = 5.75885480458;
	const double a4 = 29.8213557808;
	const double a5 = 2.62433121679;
	const double a6 = 48.6959930692;
	const double a7 = 5.92885724438;

	const double b0 = 0.398942280385;
	const double b1 = 0.000000038052;
	const double b2 = 1.00000615302;
	const double b3 = 0.000398064794;
	const double b4 = 1.98615381364;
	const double b5 = 0.151679116635;
	const double b6 = 5.29330324926;
	const double b7 = 4.8385912808;
	const double b8 = 15.1508972451;
	const double b9 = 0.742380924027;
	const double b10 = 30.789933034;
	const double b11 = 3.99019417011;

	double zabs;
	double pdf;
	double q;
	double y;
	double temp;

	zabs = abs(z);

	if (zabs <= 12.7) {
		y = a0 * z * z;
		pdf = exp(-y) * b0;
		if (zabs <= 1.28) {
			temp = y + a3 - a4 / (y + a5 + a6 / (y + a7));
			q = a0 - zabs * (a1 - a2 * y / temp);
		}
		else {
			temp = (zabs - b5 + b6 / (zabs + b7 - b8 / (zabs + b9 + b10 / (zabs + b11))));
			q = pdf / (zabs - b1 + (b2 / (zabs + b3 + b4 / temp)));
		}
	}
	else {
		pdf = 0;
		q = 0;
	}

	if (z < 0)
		return q;
	else
		return 1 - q;
}

// inverse normal distribution. Insert probability p, get a z-score.
// Use when we know the probability (from a poll-score difference, perhaps)
// and want to find the standard deviation
// adapted from https://github.com/rozgo/UE4-DynamicalSystems/blob/master/Source/DynamicalSystems/Private/SignalGenerator.cpp
double Model::inverseNormalDistribution(double p)
{

	// Coefficients in rational approximations
	double a[] = { -39.696830f, 220.946098f, -275.928510f, 138.357751f, -30.664798f, 2.506628f };

	double b[] = { -54.476098f, 161.585836f, -155.698979f, 66.801311f, -13.280681f };

	double c[] = { -0.007784894002f, -0.32239645f, -2.400758f, -2.549732f, 4.374664f, 2.938163f };

	double d[] = { 0.007784695709f, 0.32246712f, 2.445134f, 3.754408f };

	// Define break-points.
	double plow = 0.02425f;
	double phigh = 1 - plow;

	// Rational approximation for lower region:
	if (p < plow) {
		double q = std::sqrt(-2 * std::log(p));
		return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
			((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
	}

	// Rational approximation for upper region:
	if (phigh < p) {
		double q = std::sqrt(-2 * std::log(1 - p));
		return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
			((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
	}

	// Rational approximation for central region:
	{
		double q = p - 0.5f;
		double r = q * q;
		return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
			(((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
	}
}
