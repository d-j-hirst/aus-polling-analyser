#pragma once

#include <vector>
#include <wx/datetime.h>

const float DefaultVoteTimeScoreMultiplier = 8.0f;
const float DefaultHouseEffectTimeScoreMultiplier = 28.0f;

class PollsterCollection;
class PollCollection;
class EventCollection;




class Model {
public:

	typedef int Id;
	constexpr static Id InvalidId = -1;

	struct Settings {
		// User-defined name.
		std::string name = "";

		// This is effectively a smoothing factor for the 2pp vote aggregate.
		float trendTimeScoreMultiplier = DefaultVoteTimeScoreMultiplier;

		// This is effectively a smoothing factor for the 2pp vote aggregate.
		float houseEffectTimeScoreMultiplier = DefaultHouseEffectTimeScoreMultiplier;

		// the number of iterations the model goes through to smooth the trend 2pp.
		int numIterations = 1000;

		// If set to wxInvalidDateTime then it will start from the earliest data possible.
		wxDateTime startDate = wxInvalidDateTime;

		// If set to wxInvalidDateTime then it will end at the latest data possible.
		wxDateTime endDate = wxInvalidDateTime;

		// For calibration purposes, the bias to the first party amongst pollsters used for calibration.
		float calibrationFirstPartyBias = 0.0f;
	};

	struct SaveData {
		Settings settings;

		// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
		wxDateTime lastUpdated = wxInvalidDateTime;

		// final standard deviation of the trend 2pp. Used to determine, in part,
		// the initial error for projections using this model
		float finalStandardDeviation = 0.0f;

		std::vector<float> trend;
	};

	Model() {}

	Model(Settings settings) : settings(settings) {}

	Model(SaveData saveData);

	void replaceSettings(Settings newSettings);

	Settings const& getSettings() const;

	std::string getStartDateString() const;

	std::string getEndDateString() const;

	std::string getLastUpdatedString() const;

	void run(PollsterCollection const& pollsters, PollCollection const& polls, EventCollection const& events);

	auto begin() const { return day.begin(); }
	auto end() const { return day.end(); }

	int numDays() const { return int(day.size()); }

	wxDateTime getEffectiveStartDate() const { return effStartDate; }
	wxDateTime getEffectiveEndDate() const { return effEndDate; }
	wxDateTime getLastUpdatedTime() const { return lastUpdated; }

	float getFinalStandardDeviation() const { return finalStandardDeviation; }

	void extendToDate(wxDateTime date);

private:
	struct CachedPollster {
		void reset() {
			houseEffect = 0.0f;
			accuracy = 1.0f;
		}
		float houseEffect = 0.0f;
		float weight = 1.0f;
		bool useForCalibration = false;
		bool ignoreInitially = false;
		float accuracy = 1.0f; // RMSE (after house effect adjustment) of the difference of this poll to the model
	};

	struct CachedPoll {
		int pollster;
		float raw2pp; // Raw two-party preferred; this is the score from the poll
		float eff2pp; // Effective two-party preferred; this is adjusted for house effects
		CachedPoll(int pollster, float raw2pp) : pollster(pollster), raw2pp(raw2pp), eff2pp(raw2pp) {}
	};

	struct TimePoint {
		TimePoint(int pollsterCount) {
			houseEffect.resize(pollsterCount, 0);
			houseEffectScore.resize(pollsterCount, 0);
			nextHouseEffect.resize(pollsterCount, 0);
		}
		constexpr static float DefaultTrend2pp = 50.0f;
		void reset() {
			trend2pp = DefaultTrend2pp;
			nextTrend2pp = 0.0f;
			trendScore = 0.0f;
			std::fill(houseEffect.begin(), houseEffect.end(), 0.0f);
			std::fill(nextHouseEffect.begin(), nextHouseEffect.end(), 0.0f);
			std::fill(houseEffectScore.begin(), houseEffectScore.end(), 0.0f);
		}
		std::vector<CachedPoll> polls; // any polls on this day
		float trend2pp = DefaultTrend2pp; // the 2pp that the model thinks is actually occurring on this day
		std::vector<float> houseEffect;
		float nextTrend2pp = 0.0f;
		std::vector<float> nextHouseEffect;
		// These "scores" roughly represent the un-likelihood of the trend scores
		// 
		float trendScore = 0.0f; // the model tries to minimize this value
		std::vector<float> houseEffectScore; // the model tries to minimize this value
		float election = -1.0f;
		bool discontinuity = false;
	};

	// Sets up the run with earliest and latest dates
	// also gets the latest pollster count
	// This needs to be set up first to allow the import of polls and even data
	// because the importing relies on the 
	void initializeRun(PollsterCollection const& pollsters, PollCollection const& polls);

	// sets up the run with earliest and latest dates
	void updateEffectiveDates(PollCollection const& polls);

	void importPollsters(PollsterCollection const& pollsters);

	void importPolls(PollsterCollection const& pollsters, PollCollection const& polls);

	void importEvents(EventCollection const& events);

	// adds a poll to the model's database using the given values.
	void importPoll(float poll2pp, wxDateTime pollDate, int pollsterIndex);

	// adds an election to the model's database using the given values.
	void importElection(float election2pp, wxDateTime electionDate);

	// adds an discontinuity (from this day to the day after) to the model's database using the given values.
	void importDiscontinuity(wxDateTime electionDate);

	// Called once the model has been run, this determines the final standard deviation
	// to be used in projections (in addition to the base deviation).
	// This value is higher when there are fewer or dispersed polls and lower when there
	// are many, mutually confirming polls
	void determineFinalStandardDeviation();

	// Determines the combined evidence factor of polls near the end of the distribution
	float determineFinalPollingEvidenceFactor();

	// Outputs the most important run statistics to the log file.
	void logRunStatistics();

	// finalizes the run, including setting the "last updated" value to the present time.
	void finalizeRun();

	// Sets the initial path for the model based on raw poll data
	void setInitialPath();

	// Set the initial 2pp for days with polling or elections
	void setInitialPolling2pp();

	// Interpolate between 2pp for days that don't have polling
	void interpolateInitialPolling2pp();

	// Sets the initial guess for house effects
	void setInitialHouseEffectPath();

	// Set the initial house effect for days with polling or elections
	void setInitialHouseEffectFromPolls(int pollsterIndex);

	// Set the initial house effect for days with polling or elections
	void interpolateInitialHouseEffects(int pollsterIndex);

	// goes through the model iterations that successively adjust the implied 2pp values.
	void doModelIterations();

	// compares polls to the trend and calculates an overall house effect for each pollster.
	void calculateOverallHouseEffects();

	// uses the previously given bias measure to calibrate the pollsters'
	// house effects towards the "first" party (ALP by default).
	void calibrateOverallHouseEffects();

	// Uses the calibration procedure to adjust the recorded house effects.
	void debiasDailyHouseEffects();

	// Sets the local house effect at all time points to the overall house effect.
	void setUniformDailyHouseEffects();

	// Adjusts daily house effects so that the calibrated polls' house effects on the day add to the national poll bias
	void calibrateDailyHouseEffects();

	// Calculates the accuracy levels for each pollster
	void calculatePollsterAccuracy();

	// Adjusts the model's "effective 2pp" of each poll by shifting it by the model's estimated house effect on that day
	void recalculateEffectiveDaily2pps();

	// calculates the error scores for each day's trend in the model
	// from the differences between this day's trend and the surrounding days
	// and also any polls on this day.
	void calculateDailyErrorScores();

	// calculates the error scores for this day's trend in the model
	// from the differences between this day's trend and the surrounding days
	// and also any polls on this day.
	float calculateTrendScore(int dayIndex, float usetrend2pp = -1.0f) const;

	// calculates the error scores for this day's house effect for pollster pollsterIndex in the model
	// from the differences between this day's trend and the surrounding days
	// and also any polls on this day.
	float calculateHouseEffectScore(int dayIndex, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// calculates the error scores for the time point's poll with index "pollIndex".
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculatePollScore(TimePoint const* timePoint, int pollIndex, float usetrend2pp = -1.0f) const;

	// calculates the probability of the trend 2pp being further than this distance from the poll
	float calculatePollLikelihood(TimePoint const* timePoint, int pollIndex, float usetrend2pp = -1.0f) const;

	// calculates the error scores for the time point's poll with index "pollIndex".
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculateHouseEffectPollScore(TimePoint const* timePoint, int pollIndex, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// calculates the error scores for the time point "timePoint"
	// in relation with the neighbouring time point "otherTimePoint".
	// Can tolerate nullptr for "otherTimePoint", and will give a return value of 0.0f.
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculateTimeScore(TimePoint const* timePoint, TimePoint const* otherTimePoint, float usetrend2pp = -1.0f) const;

	// calculates the error scores for the time point "timePoint"
	// in relation with the neighbouring time point "otherTimePoint".
	// Can tolerate nullptr for "otherTimePoint", and will give a return value of 0.0f.
	// if useHouseEffect is given (and positive/zero), then substitutes it for the current house effect.
	float calculateHouseEffectTimeScore(TimePoint const* timePoint, TimePoint const* otherTimePoint, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// adjusts the daily trend scores to the lowest possible error score.
	void calculateDailyTrendAdjustments();

	// adjusts the daily house effect scores to the lowest possible error score.
	void calculateDailyHouseEffectAdjustments();

	// adjusts the daily values (trend and house effects) in accordance with the above functions
	void adjustDailyValues();

	static double normalDistribution(double z);

	static double Model::inverseNormalDistribution(double p);

	// vector of time points
	std::vector<TimePoint> day;

	// vector of pollster data
	std::vector<CachedPollster> pollsterCache;

	// The effective start day that is being used for the current run.
	// May be different to startDate if that is invalid
	wxDateTime effStartDate = wxInvalidDateTime;

	// The effective end day that is being used for the current run.
	// May be different to endDate if that is invalid
	wxDateTime effEndDate = wxInvalidDateTime;

	int pollsterCalibrationCount = 0;

	int iteration = 0;

	Settings settings;

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;

	// final standard deviation of the trend 2pp. Used to determine, in part,
	// the initial error for projections using this model
	float finalStandardDeviation = 0.0f;
};