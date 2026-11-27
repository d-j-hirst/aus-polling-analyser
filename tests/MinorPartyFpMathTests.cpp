// Exercise local minor-party FP estimates, including the Greens starting
// distribution in seats with no previous result and centrist/populist viability.
#include "../GreensFpPrior.h"
#include "../GroupedOthersFp.h"
#include "../MinorPartyFpMath.h"
#include "../SeatFpPolls.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

void checkReconciliationPreservesBallotZeros()
{
	using MinorPartyFpMath::reconcilePartyPrimary;
	constexpr int namedParty = 3;
	std::map<int, double> noEntry{{0, 40.0}, {1, 60.0}};
	auto const original = noEntry;
	std::map<int, double> placeholders{{namedParty, 0.0}, {EmergingPartyIndex, 0.0}};
	for (double factor : {2.0, 0.5, 1.0}) {
		for (int party : {namedParty, EmergingPartyIndex}) {
			assert(reconcilePartyPrimary(noEntry, party, 2.0, factor));
			assert(noEntry == original);
			assert(reconcilePartyPrimary(placeholders, party, 2.0, factor));
			assert(placeholders.at(party) == 0.0);
		}
	}

	// The same national targets still act on seats that have support.
	// An increase of 2 pp overall permits a local increase of up to 6 pp.
	std::map<int, double> participating{{EmergingPartyIndex, 20.0}, {namedParty, 20.0}};
	assert(reconcilePartyPrimary(participating, EmergingPartyIndex, 2.0, 2.0));
	assert(participating.at(EmergingPartyIndex) == 26.0);
	assert(reconcilePartyPrimary(participating, EmergingPartyIndex, 4.0, 0.5));
	assert(participating.at(EmergingPartyIndex) == 13.0);
	assert(reconcilePartyPrimary(participating, namedParty, 2.0, 2.0));
	assert(participating.at(namedParty) == predictorCorrectorTransformedSwing(20.0, 6.0));
	participating.at(EmergingPartyIndex) = 0.004;
	assert(reconcilePartyPrimary(participating, EmergingPartyIndex, 4.0, 0.5));
	assert(participating.at(EmergingPartyIndex) == 0.01);
}

void checkGroupedBallotAdjustment()
{
	using namespace GroupedOthersFp;
	assert(ballotGrowthOdds(0, 0) == 1.0);
	assert(ballotGrowthOdds(2, 4) > 1.0 && ballotGrowthOdds(4, 2) < 1.0);
	District local{0, 1000, 1., 1, 100. * 1188 / 19101, true};
	double const single = 10.72 / 4;
	double const expected = 10.72 + 0.5 * (local.knownShareSum - single);
	assert(std::abs(propose(local, 10.72, 80., 4, 3, false) - expected) < 1e-10);
	// A separate party already occupying the last party slot must not
	// supply another allowance even if a generic independent remains.
	assert(propose(local, 5., 80., 1, 1, true) == 5.);
	assert(std::abs(propose(local, 10.72, 80., 3, 3, true)
		- (10.72 + (local.knownShareSum - 10.72 / 3) / 3)) < 1e-10);
	local.growthOdds = ballotGrowthOdds(2, 4);
	assert(propose(local, 10.72, 80., 4, 3, false) > expected);
	assert(propose(local, 1., 0.1, 1, 1, false) <= 1.1);

	// Both upward and downward proposals preserve the turnout-weighted total
	// even with unequal districts and a seat at its maximum possible FP.
	for (double proposed : {0.1, 10., 45.}) {
		std::vector<Allocation> districts{{0, 200, 5, 40, proposed}, {1, 100, 10, 2, 12}};
		preserveTotal(districts);
		assert(std::abs(200*districts[0].proposed + 100*districts[1].proposed - 2000) < 1e-8);
		for (auto const& s : districts) assert(s.proposed >= 0 && s.proposed <= s.original + s.major);
	}
}

void checkHistoricalOthersSupport()
{
	// 2018 Morwell mapped to the 2022 project: individually recognised
	// primaries plus the remaining candidates already grouped into Others.
	std::map<int, double> morwell{{0,34.19},{1,12.34},{2,3.64},{3,19.56},{4,10.67},{-1,19.60}};
	constexpr double cap = 7.72722;
	assert(MinorPartyFpMath::historicalOthersSupport(morwell,0,1,2,4,cap) == 2 * cap);
	// A separately listed non-Coalition party still contributes. When NAT
	// is absent or mapped to Liberal, the negative Others ID stays eligible.
	assert(std::abs(MinorPartyFpMath::historicalOthersSupport(morwell,0,1,2,{},cap) - 3 * cap) < 1e-10);
	assert(MinorPartyFpMath::historicalOthersSupport({{1,42},{-1,4}},0,1,2,{},cap) == 4);
	assert(MinorPartyFpMath::historicalOthersSupport({{4,55},{-1,1}},0,1,2,4,cap) == 2);
}

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
	checkReconciliationPreservesBallotZeros();
	checkGroupedBallotAdjustment();
	checkHistoricalOthersSupport();
	checkMissingGreensFpPrior();
	checkZeroViabilityPreservesExistingResults();
	checkProfileChangesLocalSupport();
	checkProminentSupplementsRespondToViability();
	checkPollEvidenceModeratesProfileAssessment();
	std::cout << "Minor-party FP math tests passed\n";
}
