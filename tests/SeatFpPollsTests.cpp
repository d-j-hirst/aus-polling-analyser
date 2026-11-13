// Check the prepared local FP-poll update without running an election forecast.
// The independent comparison retains the old arithmetic as a regression oracle;
// named-party cases check the intended differences and evidence boundaries.
#include "../SeatFpPolls.h"

#include <cassert>
#include <iostream>
#include <limits>

namespace {

using SeatFpPolls::ByParty;
using SeatFpPolls::Readings;
using SeatFpPolls::ReadingTreatment;
constexpr int Greens = 2, OneNation = 5, Independent = 8;

bool close(double actual, double expected, double tolerance = 1e-12)
{
	return std::abs(actual - expected) <= tolerance;
}

double originalIndependentUpdate(Readings const& polls, double prior)
{
	double weightedSum = 0.0;
	double sumOfWeights = 0.0;
	for (auto const& poll : polls) {
		constexpr double QualityWeightBase = 0.6;
		double const weight = myPow(QualityWeightBase, poll.second);
		if (!std::isfinite(weight) || weight <= 0.0) continue;
		double const pollRaw = poll.first * 0.503 + 15.59;
		weightedSum += pollRaw * weight;
		sumOfWeights += weight;
	}
	if (sumOfWeights <= 0.0) return prior;
	double const transformedPollFp = transformVoteShare(
		std::clamp<double>(weightedSum / sumOfWeights, 0.1, 99.9));
	constexpr double MaxPollWeight = 0.8;
	constexpr double PollWeightBase = 0.6;
	double const pollFactor = MaxPollWeight *
		(1.0 - std::pow(PollWeightBase, sumOfWeights));
	return mix(prior, transformedPollFp, pollFactor);
}

void checkIndependentArithmeticUnchanged()
{
	std::vector<Readings> examples{{}, {{40.0f, 1}},
		{{13.0f, 0}, {40.0f, 1}, {27.3f, 3}, {50.5f, 4}},
		{{15.7f, 7}, {29.1f, 9}, {37.9f, 10}, {32.2f, 20}, {41.0f, 22}},
		{{0.0f, 4}, {100.0f, 4}}, {{99.9f, 2}},
		Readings(100, {33.33f, 4})};
	for (auto const& readings : examples) {
		auto const prepared = SeatFpPolls::prepare(
			readings, ReadingTreatment::Independent);
		ByParty const polls{{Independent, prepared}};
		for (double percentage : {0.01, 0.1, 5.0, 15.0, 50.0, 85.0, 99.99}) {
			double const prior = transformVoteShare(percentage);
			assert(SeatFpPolls::apply(polls, Independent, prior, false) ==
				originalIndependentUpdate(readings, prior));
		}
	}
}

void checkPakenhamReadingsAndPartyIdentity()
{
	ByParty const polls{
		{OneNation, SeatFpPolls::prepare({{40.0f, 1}}, ReadingTreatment::Reported)},
		{Greens, SeatFpPolls::prepare({{13.0f, 1}}, ReadingTreatment::Reported)},
		{Independent, SeatFpPolls::prepare({{40.0f, 1}}, ReadingTreatment::Independent)}};
	assert(close(detransformVoteShare(polls.at(OneNation).transformedTarget), 40.0));
	assert(close(detransformVoteShare(polls.at(Greens).transformedTarget), 13.0));
	assert(close(detransformVoteShare(polls.at(Independent).transformedTarget), 35.71));
	double const weight = 0.8 * (1.0 - std::pow(0.6, 0.6));
	assert(close(polls.at(OneNation).contribution, weight));
	double const prior = transformVoteShare(15.0);
	double const updated = SeatFpPolls::apply(polls, OneNation, prior, false);
	assert(close(detransformVoteShare(updated), 18.940041136160232, 1e-10));
	assert(SeatFpPolls::apply(polls, Greens, prior, false) < prior);
	assert(SeatFpPolls::apply(polls, Independent, prior, false) < updated);
	// A differently indexed party must not receive an ONP or GRN poll. The
	// update also does not depend on a previous-election result for that party.
	assert(SeatFpPolls::apply(polls, 15, prior, false) == prior);
	assert(polls.size() == 3);
}

void checkCredibilityAndAccumulation()
{
	auto const strong = SeatFpPolls::prepare({{40.0f, 1}}, ReadingTreatment::Reported);
	auto const weak = SeatFpPolls::prepare({{40.0f, 4}}, ReadingTreatment::Reported);
	assert(weak.contribution < strong.contribution);
	auto const two = SeatFpPolls::prepare(
		{{40.0f, 1}, {40.0f, 1}}, ReadingTreatment::Reported);
	assert(two.contribution > strong.contribution);
	auto const many = SeatFpPolls::prepare(Readings(1000, {40.0f, 1}),
		ReadingTreatment::Reported);
	assert(many.contribution <= 0.8 && many.contribution > 0.799999);

	// Average the percentages with credibility weights before transforming,
	// rather than averaging their log-odds or treating ratings as sample sizes.
	auto const mixed = SeatFpPolls::prepare(
		{{10.0f, 0}, {40.0f, 1}}, ReadingTreatment::Reported);
	double const average = (10.0 + 40.0 * 0.6) / 1.6;
	assert(close(mixed.transformedTarget, transformVoteShare(average)));
	assert(close(mixed.contribution, 0.8 * (1.0 - std::pow(0.6, 1.6))));
}

void checkCalibrationMissingPollsAndEndpoints()
{
	double const prior = transformVoteShare(17.123456789123);
	ByParty const polls{{OneNation, SeatFpPolls::prepare(
		{{40.0f, 1}}, ReadingTreatment::Reported)}};
	assert(SeatFpPolls::apply(polls, OneNation, prior, true) == prior);
	assert(SeatFpPolls::apply(polls, OneNation, prior, false) != prior);
	assert(SeatFpPolls::apply({}, OneNation, prior, false) == prior);
	ByParty const emptyReadings{{OneNation,
		SeatFpPolls::prepare({}, ReadingTreatment::Reported)}};
	assert(SeatFpPolls::apply(emptyReadings, OneNation, prior, false) == prior);
	double const negativeZero = -0.0;
	assert(std::signbit(SeatFpPolls::apply({}, OneNation, negativeZero, false)));
	for (float reading : {0.0f, 100.0f}) {
		auto const prepared = SeatFpPolls::prepare({{reading, 1}}, ReadingTreatment::Reported);
		assert(std::isfinite(prepared.transformedTarget));
		double const percentage = detransformVoteShare(prepared.transformedTarget);
		assert(close(percentage, reading == 0.0f ? 0.1 : 99.9));
		double const result = SeatFpPolls::apply({{OneNation, prepared}}, OneNation, prior, false);
		assert(std::isfinite(result));
		assert(detransformVoteShare(result) > 0.0 && detransformVoteShare(result) < 100.0);
	}
}

void checkDrawsStayPrivateAndSpreadContracts()
{
	Readings readings{{40.0f, 1}};
	auto const prepared = SeatFpPolls::prepare(readings, ReadingTreatment::Reported);
	ByParty const polls{{OneNation, prepared}};
	readings.front().first = 1.0f;
	// Preparation holds detached fixed inputs; each call supplies its own
	// scenario. Neither the first draw nor later source edits alter that input.
	double const low = transformVoteShare(10.0), high = transformVoteShare(25.0);
	double const updatedLow = SeatFpPolls::apply(polls, OneNation, low, false);
	double const updatedHigh = SeatFpPolls::apply(polls, OneNation, high, false);
	assert(close(updatedHigh - updatedLow,
		(high - low) * (1.0 - prepared.contribution)));
	assert(SeatFpPolls::apply(polls, OneNation, low, false) == updatedLow);
	assert(polls.at(OneNation).transformedTarget == prepared.transformedTarget);
	assert(polls.at(OneNation).contribution == prepared.contribution);
}

} // namespace

int main()
{
	checkIndependentArithmeticUnchanged();
	checkPakenhamReadingsAndPartyIdentity();
	checkCredibilityAndAccumulation();
	checkCalibrationMissingPollsAndEndpoints();
	checkDrawsStayPrivateAndSpreadContracts();
	std::cout << "Seat FP poll tests passed\n";
}
