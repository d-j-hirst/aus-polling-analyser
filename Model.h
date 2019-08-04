#pragma once

#include <vector>
#include <wx/datetime.h>

const float DefaultVoteTimeScoreMultiplier = 8.0f;
const float DefaultHouseEffectTimeScoreMultiplier = 28.0f;

struct SmallPoll {
	int pollster;
	float raw2pp; // Raw two-party preferred; this is the score from the poll
	float eff2pp; // Effective two-party preferred; this is adjusted for house effects
	SmallPoll(int pollster, float raw2pp) : pollster(pollster), raw2pp(raw2pp), eff2pp(raw2pp) {}
};

struct ModelTimePoint {
	ModelTimePoint(int pollsterCount)  { 
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
	std::vector<SmallPoll> polls; // any polls on this day
	float trend2pp = DefaultTrend2pp; // the 2pp that the model thinks is actually occurring on this day
	std::vector<float> houseEffect;
	float nextTrend2pp = 0.0f;
	std::vector<float> nextHouseEffect;
	float trendScore = 0.0f; // the model tries to minimize this value
	std::vector<float> houseEffectScore; // the model tries to minimize this value
	float election = -1.0f;
	bool discontinuity = false;
};

struct ModelPollster {
	void reset() {
		houseEffect = 0.0f;
		accuracy = 1.0f;
	}
	float houseEffect = 0.0f;
	float weight = 1.0f;
	bool useForCalibration = false;
	bool ignoreInitially = false;
	float accuracy = 1.0f; // RMSE (after house effect adjustment) of the difference of this poll to the model
	int index;
};

class Model {
public:
	typedef int Id;
	constexpr static Id InvalidId = -1;

	Model(std::string name, float trendTimeScoreMultiplier, float houseEffectTimeScoreMultiplier, wxDateTime startDate, wxDateTime endDate) :
		name(name),
		trendTimeScoreMultiplier(trendTimeScoreMultiplier),
		houseEffectTimeScoreMultiplier(houseEffectTimeScoreMultiplier),
		startDate(startDate),
		endDate(endDate){}

	Model() {}

	std::string getStartDateString() const {
		if (!startDate.IsValid()) return "";
		else return startDate.FormatISODate().ToStdString();
	}

	std::string getEndDateString() const {
		if (!endDate.IsValid()) return "";
		else return endDate.FormatISODate().ToStdString();
	}

	std::string getLastUpdatedString() const {
		if (!lastUpdated.IsValid()) return "";
		else return lastUpdated.FormatISODate().ToStdString();
	}

	void run();

	// sets the calibrated pollsters' combined bias to the
	// first listed party (ALP by default).
	void setCalibrationFirstPartyBias(float bias);

	// sets up the run with earliest and latest dates
	void updateEffectiveDates(wxDateTime earliestPoll, wxDateTime latestPoll);

	// Sets up the run with earliest and latest dates
	// also gets the latest pollster count.
	void initializeRun(wxDateTime earliestPoll, wxDateTime latestPoll, int nPollsters);

	// Called once the model has been run, this determines the final standard deviation
	// to be used in projections (in addition to the base deviation).
	// This value is higher when there are fewer or dispersed polls and lower when there
	// are many, mutually confirming polls
	void determineFinalStandardDeviation();

	// Outputs the most important run statistics to the log file.
	void logRunStatistics();

	// finalizes the run (update this description later).
	void finalizeRun();

	// adds a poll to the model's database using the given values.
	void importPoll(float poll2pp, wxDateTime pollDate, int pollsterIndex);

	// adds an election to the model's database using the given values.
	void importElection(float election2pp, wxDateTime electionDate);

	// adds an discontinuity (from this day to the day after) to the model's database using the given values.
	void importDiscontinuity(wxDateTime electionDate);

	// imports pollster-relevant data.
	void setPollsterData(int pollsterIndex, bool useForCalibration, bool ignoreInitially, float weight);

	// adds a poll to the model's database using the given values.
	void setInitialPath();

	// Sets the initial guess for house effects
	void setInitialHouseEffectPath();

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
	float calculateTrendScore(ModelTimePoint const* thisDay, int dayIndex, float usetrend2pp = -1.0f) const;

	// calculates the error scores for this day's house effect for pollster pollsterIndex in the model
	// from the differences between this day's trend and the surrounding days
	// and also any polls on this day.
	float calculateHouseEffectScore(ModelTimePoint const* thisDay, int dayIndex, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// calculates the error scores for the time point's poll with index "pollIndex".
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculatePollScore(ModelTimePoint const* timePoint, int pollIndex, float usetrend2pp = -1.0f) const;

	// calculates the probability of the trend 2pp being further than this distance from the poll
	float calculatePollLikelihood(ModelTimePoint const* timePoint, int pollIndex, float usetrend2pp = -1.0f) const;

	// calculates the error scores for the time point's poll with index "pollIndex".
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculateHouseEffectPollScore(ModelTimePoint const* timePoint, int pollIndex, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// calculates the error scores for the time point "timePoint"
	// in relation with the neighbouring time point "otherTimePoint".
	// Can tolerate nullptr for "otherTimePoint", and will give a return value of 0.0f.
	// if usetrend2pp is given (and positive/zero), then substitutes it for the current trend 2pp.
	float calculateTimeScore(ModelTimePoint const* timePoint, ModelTimePoint const* otherTimePoint, float usetrend2pp = -1.0f) const;

	// calculates the error scores for the time point "timePoint"
	// in relation with the neighbouring time point "otherTimePoint".
	// Can tolerate nullptr for "otherTimePoint", and will give a return value of 0.0f.
	// if useHouseEffect is given (and positive/zero), then substitutes it for the current house effect.
	float calculateHouseEffectTimeScore(ModelTimePoint const* timePoint, ModelTimePoint const* otherTimePoint, int pollsterIndex, float useHouseEffect = -1000.0f) const;

	// adjusts the daily trend scores to the lowest possible error score.
	void calculateDailyTrendAdjustments();

	// adjusts the daily house effect scores to the lowest possible error score.
	void calculateDailyHouseEffectAdjustments();

	// adjusts the daily values (trend and house effects) in accordance with the above functions
	void adjustDailyValues();

	float getRecentTrendScore() const;

	static double func_normsdist(double z);

	static double Model::normsinv(double p);

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

	// If set to wxInvalidDateTime then we assume the model hasn't been run at all.
	wxDateTime lastUpdated = wxInvalidDateTime;

	// The effective start day that is being used for the current run.
	wxDateTime effStartDate = wxInvalidDateTime;

	// The effective start day that is being used for the current run.
	wxDateTime effEndDate = wxInvalidDateTime;

	float calibrationFirstPartyBias = 0.0f;

	// final standard deviation of the trend 2pp. Used to determine, in part,
	// the initial error for projections using this model
	float finalStandardDeviation = 0.0f;

	// vector of time points
	std::vector<ModelTimePoint> day;

	// vector of pollster data
	std::vector<ModelPollster> pollster;

	int pollsterCount = 0;
	int pollsterCalibrationCount = 0;

private:

	int iteration = 0;
};