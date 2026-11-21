// Exercise local minor-party FP estimates, including the Greens starting
// distribution in seats with no previous result and centrist/populist viability.
#include "../GreensFpPrior.h"
#include "../MinorPartyFpMath.h"
#include "../SeatFpPolls.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

void checkMissingGreensFpPrior()
{
	std::ifstream input("analysis/seats/2026sa-greens-fp-priors.json");
	assert(input);
	auto const sa = GreensFpPrior::readDistrictMedians(input);
	GreensFpPrior::DistrictMedians const expected{
		{"Narungga", 3.1}, {"MacKillop", 2.5}, {"Mount Gambier", 3.1}, {"Ngadjuri", 5.6}};
	assert(sa == expected);
	for (auto const& [district, median] : expected) {
		assert(GreensFpPrior::missingResultMedian(false, district, sa) == median);
		// A genuine previous result (even a reported zero) takes priority.
		assert(!GreensFpPrior::missingResultMedian(true, district, sa));
	}
	std::istringstream absent;
	absent.setstate(std::ios::failbit);
	auto const none = GreensFpPrior::readDistrictMedians(absent);
	assert(none.empty());
	auto const generic = GreensFpPrior::missingResultMedian(false, "New district", none);
	assert(generic == 2.0);
	assert(!GreensFpPrior::missingResultMedian(true, "New district", none));
	double const centre = transformVoteShare(*generic);
	double const halfWidth = 1.644854 * GreensFpPrior::TransformedSd;
	assert(std::abs(detransformVoteShare(centre - halfWidth) - 0.5) < 0.05);
	assert(std::abs(detransformVoteShare(centre + halfWidth) - 7.7) < 0.1);
	for (auto invalid : {R"({"districts":{"Narungga":0}})", R"({"districts":{"Narungga":"3%"}})"}) {
		std::istringstream bad(invalid);
		bool rejected = false;
		try { GreensFpPrior::readDistrictMedians(bad); }
		catch (std::exception const&) { rejected = true; }
		assert(rejected);
	}
}

double oldBaseline(double support, double modifier, double incumbent)
{
	double modifiedFp1 = predictorCorrectorTransformedSwing<double>(
		support, support * (modifier - 1.0));
	double modifiedFp2 = support * modifier;
	double modifiedFp = std::max<double>(incumbent, std::min<double>(modifiedFp1, modifiedFp2));
	return std::clamp<double>(modifiedFp, 0.01, 99.0);
}

double oldProminentFp(double percentage, double firstDraw, double secondDraw)
{
	percentage += (1.0 - std::clamp<double>(percentage / 10.0, 0.0, 1.0)) * 5.0;
	return predictorCorrectorTransformedSwing<double>(percentage, firstDraw * secondDraw * 35.0);
}

void checkZeroViabilityPreservesExistingResults()
{
	for (double support : {0.0, 0.1, 1.0, 5.0, 15.0, 35.0, 60.0, 99.0}) {
		for (double modifier : {0.2, 0.75, 1.0, 1.5, 3.0}) {
			for (double incumbent : {0.0, 10.0, 50.0}) {
				double const original = oldBaseline(support, modifier, incumbent);
				double const updated = MinorPartyFpMath::populistBaseline(
					support, modifier, incumbent, 0.0);
				assert(updated == original);
				for (double first : {0.0, 0.2, 0.8, 1.0}) {
					for (double second : {0.0, 0.3, 0.9, 1.0}) {
						assert(MinorPartyFpMath::prominentPopulistFp(
							updated, 0.0, first, second) ==
							oldProminentFp(original, first, second));
					}
				}
			}
		}
	}
}

void checkProfileChangesLocalSupport()
{
	double const ordinary = MinorPartyFpMath::populistBaseline(15.0, 1.0, 0.0, 0.0);
	double const strong = MinorPartyFpMath::populistBaseline(15.0, 1.0, 0.0, 1.0);
	double const weak = MinorPartyFpMath::populistBaseline(15.0, 1.0, 0.0, -1.0);
	assert(std::abs(ordinary - 15.0) < 1e-12);
	assert(std::abs(strong - 21.0) < 1e-12);
	assert(std::abs(weak - 9.0) < 1e-12);

	// A protected historical incumbent supplies the initial baseline; an
	// explicit local assessment can change that baseline in either direction.
	assert(MinorPartyFpMath::populistBaseline(5.0, 0.5, 20.0, 0.0) == 20.0);
	assert(MinorPartyFpMath::populistBaseline(5.0, 0.5, 20.0, 1.0) == 28.0);
	assert(MinorPartyFpMath::populistBaseline(5.0, 0.5, 20.0, -1.0) == 12.0);
	for (double viability : {-10.0, -2.5, -1.0, 0.0, 1.0, 5.0, 20.0}) {
		double const result = MinorPartyFpMath::populistBaseline(60.0, 1.5, 0.0, viability);
		assert(std::isfinite(transformVoteShare(result)));
		assert(result >= 0.01 && result <= 99.0);
	}
}

void checkProminentSupplementsRespondToViability()
{
	for (double percentage : {0.01, 3.0, 8.0, 15.0, 40.0}) {
		double const weak = MinorPartyFpMath::prominentPopulistFp(percentage, -1.0, 0.4, 0.7);
		double const ordinary = MinorPartyFpMath::prominentPopulistFp(percentage, 0.0, 0.4, 0.7);
		double const strong = MinorPartyFpMath::prominentPopulistFp(percentage, 1.0, 0.4, 0.7);
		assert(weak < ordinary && ordinary < strong);
		// Extreme negative assessments must not regain a positive squared boost.
		for (double viability : {-2.5, -3.0, -10.0}) {
			assert(std::abs(MinorPartyFpMath::prominentPopulistFp(
				percentage, viability, 0.4, 0.7) - percentage) < 1e-12);
		}
	}
}

void checkPollEvidenceModeratesProfileAssessment()
{
	int const oneNation = 5;
	SeatFpPolls::ByParty const polls{{oneNation, SeatFpPolls::prepare(
		{{40.0f, 1}}, SeatFpPolls::ReadingTreatment::Reported)}};
	double const neutral = transformVoteShare(
		MinorPartyFpMath::populistBaseline(15.0, 1.0, 0.0, 0.0));
	double const strong = transformVoteShare(
		MinorPartyFpMath::populistBaseline(15.0, 1.0, 0.0, 1.0));
	double const updatedNeutral = SeatFpPolls::apply(polls, oneNation, neutral, false);
	double const updatedStrong = SeatFpPolls::apply(polls, oneNation, strong, false);
	assert(updatedStrong > updatedNeutral);
	assert(updatedStrong - updatedNeutral < strong - neutral);
	assert(std::abs((updatedStrong - updatedNeutral) -
		(strong - neutral) * (1.0 - polls.at(oneNation).contribution)) < 1e-12);
	// Betting-odds calibration still excludes polls. Viability belongs to the
	// local support calculation that calibration must account for, not a second
	// adjustment applied after its resulting shares have been reconciled.
	assert(SeatFpPolls::apply(polls, oneNation, strong, true) == strong);
}

} // namespace

int main()
{
	checkMissingGreensFpPrior();
	checkZeroViabilityPreservesExistingResults();
	checkProfileChangesLocalSupport();
	checkProminentSupplementsRespondToViability();
	checkPollEvidenceModeratesProfileAssessment();
	std::cout << "Minor-party FP math tests passed\n";
}
