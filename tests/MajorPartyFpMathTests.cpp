#include "../MajorPartyFpMath.h"
#include "../MajorPrimarySafeguard.h"
#include "../SpecialPartyCodes.h"
#include "../SoftPairing.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

namespace {

	void checkJointMajorPrimarySafeguard()
	{
		namespace Guard = MajorPrimarySafeguard;
		static_assert(Guard::enabled("2022vic") && !Guard::enabled("2022vic", true));
		static_assert(Guard::enabled("2026vic") && Guard::enabled("2026vic", true));
		static_assert(Guard::enabled("2026sa") && Guard::enabled("2026sa", true));
		static_assert(!Guard::enabled("2028fed"));
		auto near = [](double a, double b) { return std::abs(a-b) < 1e-9; };
		assert(near(Guard::requestedIncrease(0.,30.,0.,9.),5.98695652173913));
		assert(Guard::requestedIncrease(10.,30.,0.,9.) == 0.);
		assert(Guard::requestedIncrease(2.,30.,50.,9.) == 0.);
		assert(Guard::requestedIncrease(2.,4.,0.,9.) < Guard::requestedIncrease(2.,30.,0.,9.));

		// A 20%-primary donor can explain two extra points of Labor FP
		// at the same TPP. The second seat has twice the turnout and
		// compensates with one point in the opposite direction.
		Guard::District weak, strong;
		weak.turnout = 1000.;
		weak.labor = 4.; weak.coalition = 76.; weak.shoulder = 9.; weak.laborNeed = 2.;
		weak.sources[1] = {20.,.4,.2,.4};
		strong.turnout = 2000.;
		strong.labor = 40.; strong.coalition = 40.; strong.shoulder = 9.;
		strong.sources[1] = {20.,.2,.4,.2};
		std::vector districts{weak,strong};
		Guard::adjustPreferences(districts);
		Guard::adjustPrimaries(districts);
		assert(near(districts[0].labor,6.) && near(districts[1].labor,39.));
		assert(near(districts[0].sources[1].rate,.3) && near(districts[1].sources[1].rate,.25));
		assert(near(districts[0].sources[1].primary,20.) && near(districts[0].laborNeed,0.));

		// Without a compensating preference draw, leave the original rates
		// alone. Transfer some donor FP instead, retaining its statewide
		// vote and the total FP available in each district.
		strong.sources[1].centre = strong.sources[1].rate;
		districts = {weak,strong};
		Guard::adjustPreferences(districts);
		assert(near(districts[0].labor,4.) && near(districts[0].sources[1].rate,.4));
		Guard::adjustPrimaries(districts);
		assert(near(districts[0].labor,6.) && near(districts[0].sources[1].primary,15.));
		assert(near(districts[1].sources[1].primary,22.5));
		assert(near(districts[0].labor+districts[0].coalition+districts[0].sources[1].primary,100.));
		assert(near(districts[0].labor+2.*districts[1].labor,84.));

		// Only one district has the Greens donor in this example. Its
		// FP stays put while part of the separate ON transfer still fits.
		weak.sources[0] = {10.,.8,.8,.8};
		weak.coalition -= 10.;
		districts = {weak,strong};
		Guard::adjustPreferences(districts);
		Guard::adjustPrimaries(districts);
		assert(districts[0].labor > 4. && districts[0].sources[1].primary < 20.);
		assert(near(districts[0].sources[0].primary,10.) && districts[1].sources[0].primary == 0.);
		weak.sources[0] = {};

		// A weak Coalition primary works in the opposite direction. The
		// flow after matching TPP differs from the raw rate. Both must
		// retain their common odds adjustment and allow for partner leakage.
		weak.labor = 76.; weak.coalition = 4.; weak.laborNeed = 0.; weak.coalitionNeed = 2.;
		weak.leakage = .1;
		weak.sources[1] = {20.,.2,.5,.3};
		strong.sources[1] = {20.,.6,.3,.6};
		districts = {weak,strong};
		Guard::adjustPreferences(districts);
		auto const& updated = districts[0];
		assert(near(updated.coalition,6.) && near(updated.sources[1].fitted,.39));
		assert(near(updated.labor + .1*updated.coalition + 20.*updated.sources[1].fitted,82.4));
		assert(near(Guard::shiftOdds(updated.sources[1].rate,.2,.3),.39));
		assert(near(updated.coalitionNeed,0.));
		districts = {weak};
		Guard::adjustPreferences(districts);
		Guard::adjustPrimaries(districts);
		assert(near(districts[0].coalition,4.) && near(districts[0].sources[1].rate,.2));

		// All available Labor TPP already comes from majors. Moving more
		// voters from a zero-flow minor to a leaking Coalition partner is
		// incompatible even though another district could accept the FP.
		weak.labor = 60.; weak.coalition = 4.; weak.laborTpp = 60.4;
		weak.sources[1] = {36.,0.,0.,0.};
		districts = {weak,strong};
		Guard::adjustPreferences(districts);
		Guard::adjustPrimaries(districts);
		assert(near(districts[0].coalition,4.) && near(districts[0].sources[1].primary,36.));
	}

	void checkLocalOneNationPreferences()
	{
		using MajorPartyFpMath::localOneNationToLabor;
		using MajorPartyFpMath::usesLocalOneNationFlow;
		static_assert(usesLocalOneNationFlow("2026sa") && usesLocalOneNationFlow("2026vic"));
		static_assert(!usesLocalOneNationFlow("2022vic") && !usesLocalOneNationFlow("2028fed"));
		double const centre = transformVoteShare(40.);
		assert(std::abs(localOneNationToLabor(25., 40., centre) - 25.) < 1e-12);
		// From 40% to 25% Labor TPP, Labor's odds halve. Starting with
		// one in four ON ballots going to Labor, reduce their 1:3 odds too.
		double const expectedOdds = std::pow(.5, .20) / 3.;
		assert(std::abs(localOneNationToLabor(25., 25., centre) -
			100. * expectedOdds / (1. + expectedOdds)) < 1e-12);
		double previous = 0.;
		for (double tpp : {.000001, 1., 10., 20., 27., 28., 40., 75., 85., 99.999999}) {
			double const flow = localOneNationToLabor(25., tpp, centre);
			assert(previous < flow && flow < 100.);
			previous = flow;
		}
		assert(localOneNationToLabor(0., 85., centre) == 0.);
		assert(localOneNationToLabor(100., 10., centre) == 100.);
	}

	void checkVictorianPairingCentres()
	{
		using namespace SoftPairing;
		assert(weight(.2999) == 0. && weight(.30) == 0.);
		assert(std::abs(weight(.65) - .5) < 1e-12 && weight(1.) == 1.);
		// Richmond, 2022. The independent Python trial uses a different
		// optimizer and supplies the expected adjustments in district points.
		Centre c{32.5310567, 7.77154857, 43.7218034, 72.977, 10.59960294,
			8.16883654, 8.30321598, 10.28572470, 2.554576789, .444174098, .25};
		auto const shift = fit(c, .926);
		assert(std::abs(shift.labor + 2.51421) < .001);
		assert(std::abs(-shift.labor - shift.greens - 7.48782) < .001);
		assert(std::abs(shift.tpp - 1.64207) < .001);
		auto const inactive = fit(c, .3);
		assert(inactive.labor == 0. && inactive.greens == 0. && inactive.tpp == 0.);
		auto const justEligible = fit(c, .300001);
		assert(std::abs(justEligible.labor) + std::abs(justEligible.greens) + std::abs(justEligible.tpp) < .0001);
		// If the major-pair preference account already gives the reference
		// TPP, even a certain Labor–Greens finish requires no primary change.
		c.tpp = c.labor + c.greens * probability(c.greenIntercept + c.greenSlope * logOdds(.01 * c.greens)) + c.otherToLabor;
		auto const consistent = fit(c, 1.);
		assert(std::abs(consistent.labor) + std::abs(consistent.greens) + std::abs(consistent.tpp) < 1e-8);
	}

	void checkBendigoEastHistoricalPartner()
	{
		using namespace MajorPartyFpMath;
		assert(excludedPartnerInLaborLiberalCount(26776, 16332, 0, 6864));
		assert(!excludedPartnerInLaborLiberalCount(26776, 0, 16332, 6864));
		assert(!excludedPartnerInLaborLiberalCount(0, 16332, 0, 6864));
		assert(!excludedPartnerInLaborLiberalCount(26776, 16332, 0, 0));
		double const expected = (6864 * HistoricalPartnerToLabor + 3465 * .8194 + 2079 * .4925) / 12408;
		assert(std::abs(expected - .3943) < .0001);
	}

	// Fictional seat shares. Normalisation preserves the locally established
	// candidate and redistributes the rest between major and other minor votes.
	struct Seat {
		float major;
		float protectedMinor;
		float otherMinor;
		float turnout;
	};

	template<std::size_t N>
	float normalisedMajorShare(std::array<Seat, N> const& seats, float factor)
	{
		double majorVotes = 0.0;
		double turnout = 0.0;
		for (auto const& seat : seats) {
			float const adjusted = seat.major * factor;
			float const share = adjusted * (100.0f - seat.protectedMinor) /
				(adjusted + seat.otherMinor);
			majorVotes += double(share) * seat.turnout;
			turnout += seat.turnout;
		}
		return float(majorVotes / turnout);
	}

	void checkReachableTargets()
	{
		std::array<Seat, 3> const seats{{
			{60.0f, 30.0f, 10.0f, 1000.0f},
			{70.0f, 20.0f, 10.0f, 3000.0f},
			{40.0f, 45.0f, 15.0f, 2000.0f}}};
		auto const projected = [&](float factor) {
			return normalisedMajorShare(seats, factor);
		};
		float const current = projected(1.0f);
		// Exercise the common case across a broad range of targets, using the
		// same closed-form starting factor as the simulator. The final share
		// must reflect separate seat normalisation and turnout weighting.
		for (float target : {5.0f, 20.0f, 40.0f, 55.0f, 65.0f, 69.0f}) {
			float const initial = target * (current - 100.0f) /
				(current * (target - 100.0f));
			auto const result = MajorPartyFpMath::findNormalisedCorrection(
				target, 0.0001f, initial, projected);
			assert(result.status == MajorPartyFpMath::SearchStatus::Matched);
			assert(std::isfinite(result.factor) && result.factor > 0.0f);
			assert(std::abs(projected(result.factor) - target) < 0.001f);
		}
	}

	void checkProtectedLocalSupport()
	{
		std::array<Seat, 1> const seats{{{60.0f, 30.0f, 10.0f, 1000.0f}}};
		auto const projected = [&](float factor) {
			return normalisedMajorShare(seats, factor);
		};
		// No finite multiplier can put 75% with the major parties while keeping
		// 30% with the protected candidate. The previous bracket check threw for
		// this valid account; the caller must now hand it to its terminal solver
		// without modifying the incoming seat shares or retrying the scenario.
		auto const constrained = MajorPartyFpMath::findNormalisedCorrection(
			75.0f, 0.0001f, 2.0f, projected);
		assert(constrained.status == MajorPartyFpMath::SearchStatus::Constrained);
		assert(constrained.upperShare < 70.0f && constrained.upperShare < 75.0f);
		assert(projected(1.0f) == 60.0f);

		// A lower target is reachable in the same seat. Keeping that candidate
		// fixed must not prevent the ordinary correction from being applied.
		auto const reachable = MajorPartyFpMath::findNormalisedCorrection(
			65.0f, 0.0001f, 1.25f, projected);
		assert(reachable.status == MajorPartyFpMath::SearchStatus::Matched);
		assert(std::abs(reachable.factor - 13.0f / 6.0f) < 0.00001f);

		// The positivity bound from preference compensation can also put the
		// minimum attainable share above a low target. This is a constraint,
		// provided the evaluated accounts themselves remain numerically valid.
		auto const lowerConstraint = MajorPartyFpMath::findNormalisedCorrection(
			20.0f, 0.5f, 1.0f, projected);
		assert(lowerConstraint.status == MajorPartyFpMath::SearchStatus::Constrained);
		assert(lowerConstraint.lowerShare > 20.0f);
	}

	void checkInvalidCalculations()
	{
		float const nan = std::numeric_limits<float>::quiet_NaN();
		float const infinity = std::numeric_limits<float>::infinity();
		auto const valid = [](float factor) { return 100.0f * factor / (1.0f + factor); };
		for (float target : {-1.0f, 0.0f, 100.0f, nan, infinity}) {
			assert(MajorPartyFpMath::findNormalisedCorrection(
				target, 0.0001f, 1.0f, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		for (float badFactor : {-1.0f, 0.0f, nan, infinity}) {
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, badFactor, 1.0f, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, 0.0001f, badFactor, valid).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		for (float badShare : {-1.0f, 101.0f, nan, infinity}) {
			auto const bad = [badShare](float) { return badShare; };
			assert(MajorPartyFpMath::findNormalisedCorrection(
				50.0f, 0.0001f, 1.0f, bad).status ==
				MajorPartyFpMath::SearchStatus::Invalid);
		}
		// Failures part way through bracket expansion or binary search remain
		// failures, even when the endpoints examined so far were valid.
		assert(MajorPartyFpMath::findNormalisedCorrection(
			90.0f, 0.0001f, 1.0f, [&](float factor) {
				return factor > 1.0f ? nan : valid(factor);
			}).status == MajorPartyFpMath::SearchStatus::Invalid);
		assert(MajorPartyFpMath::findNormalisedCorrection(
			50.0f, 0.0001f, 2.0f, [&](float factor) {
				return factor > 0.25f && factor < 1.75f ? -1.0f : valid(factor);
			}).status == MajorPartyFpMath::SearchStatus::Invalid);
	}

	void checkHistoricalCoalitionPreferences()
	{
		auto const close = [](double a, double b) { return std::abs(a - b) < 1e-12; };
		// Euroa 2022 ended Labor/National. The Liberal exclusion sent 1,353
		// ballots to Labor, at most 1,172 of which had come from other piles.
		auto const euroa = MajorPartyFpMath::historicalMinorPreferenceRange({
			42242, 12840, 13496, 9227, 16926, 181, 1353});
		assert(euroa && close(euroa->lower, 2733.0 / 6679) && close(euroa->upper, 3905.0 / 6679));
		assert(euroa->supportedDifference(0.50) == 0.0);
		assert(close(euroa->supportedDifference(0.65), euroa->upper - 0.65));

		// The separate 2018 TPP counts used in the 2022 replay. Morwell's
		// Liberal exclusion bounds the original Liberal ballots reaching Labor.
		// Mildura had only one Coalition candidate. For Shepparton only the
		// total National primary bounds that party's contribution to Labor.
		auto const morwell2018 = MajorPartyFpMath::historicalMinorPreferenceRange({
			40142, 13725, 4283, 4955, 21037, 0, 1963});
		assert(morwell2018 && close(morwell2018->lower, 5349.0 / 17179) &&
			close(morwell2018->upper, 7312.0 / 17179));
		auto const mildura2018 = MajorPartyFpMath::historicalMinorPreferenceRange({
			37207, 6404, 14654, 0, 16515, 0, 0});
		assert(mildura2018 && mildura2018->lower == mildura2018->upper &&
			close(mildura2018->upper, 10111.0 / 16149));
		auto const shepparton2018 = MajorPartyFpMath::historicalMinorPreferenceRange({
			41290, 4772, 10967, 5382, 15806, 0, 5382});
		assert(shepparton2018 && close(shepparton2018->lower, 5652.0 / 20169) &&
			close(shepparton2018->upper, 11034.0 / 20169));

		// Bass reverses the roles. The 5,506 National primaries are the
		// excluded partner's original votes in its Labor/Liberal count.
		auto const bass = MajorPartyFpMath::historicalMinorPreferenceRange({
			41404, 13478, 12482, 5506, 20803, 0, 2700});
		assert(bass && close(bass->lower, 4625.0 / 9938) && close(bass->upper, 7325.0 / 9938));

		// The separate Labor/National scrutiny in Mallee identifies exactly
		// 2,152 original Liberal ballots reaching Labor.
		auto const mallee = MajorPartyFpMath::historicalMinorPreferenceRange({
			85834, 15020, 33270, 23363, 22610, 2152, 2152});
		assert(mallee && close(mallee->lower, 5438.0 / 14181) && mallee->lower == mallee->upper);

		// An unknown partner split can still establish an upper bound. A
		// seat without an excluded partner yields a single measured rate.
		auto const shepparton = MajorPartyFpMath::historicalMinorPreferenceRange({
			41301, 3285, 13242, 8491, 10597, 0, 8491});
		assert(shepparton && shepparton->lower == 0.0 && close(shepparton->upper, 7312.0 / 16283));
		auto const nationalOnly = MajorPartyFpMath::historicalMinorPreferenceRange({
			1000, 250, 600, 0, 320, 0, 0});
		assert(nationalOnly && close(nationalOnly->lower, 70.0 / 150));
		assert(nationalOnly->lower == nationalOnly->upper);
		assert(!MajorPartyFpMath::historicalMinorPreferenceRange({1000, 250, 600, 50, 500, 0, 50}));
		assert(!MajorPartyFpMath::historicalMinorPreferenceRange({1000, 250, 600, 50, 320, 45, 40}));
	}

	void checkClassicPrimaryAllocation()
	{
		constexpr double tolerance = 1e-12;
		// Reconstruct the first incompatible now-cast examples in Euroa,
		// Morwell and Mildura using the old split transfer in reverse. With
		// the single National candidate, the new primary equation allocates
		// the entire preference requirement to the minor pool. Check that a
		// later adjustment is also limited by the fixed TPP.
		struct Example {
			double laborTpp;
			double combinedMajor;
			double minorPreferences;
			double oldLaborPrimary;
		};
		for (auto const& example : std::array{
			Example{29.523, 55.126, 1.493, 29.725},
			Example{28.062, 52.533, 0.47467, 29.139},
			Example{23.819, 70.971, 2.469, 24.598}}) {
			double const labor = MajorPartyFpMath::classicLaborPrimary(
				example.laborTpp, example.combinedMajor, example.minorPreferences, 0.0);
			assert(std::abs(labor + example.minorPreferences - example.laborTpp) < tolerance);
			assert(labor < example.oldLaborPrimary);
			auto const corrected = MajorPartyFpMath::compatibleClassicLaborPrimary(
				example.oldLaborPrimary, example.combinedMajor, example.laborTpp, 0.0, 0.1);
			assert(corrected && *corrected == example.laborTpp);
		}

		// With both running, this quantity is paid from the Coalition primary
		// once. Swapping Liberal and National leaves the smaller primary and
		// the hypothetical Labor/Coalition TPP unchanged.
		for (double nationalFraction : {0.0, 0.32, 0.5, 0.68, 1.0}) {
			double const leakageFraction = MajorPartyFpMath::coalitionLeakageFraction(nationalFraction);
			double const laborTpp = 29.0 + 10.0 + leakageFraction * 41.0;
			double const primary = MajorPartyFpMath::classicLaborPrimary(laborTpp, 70.0, 10.0, leakageFraction);
			assert(std::abs(primary - 29.0) < tolerance);
			auto const same = MajorPartyFpMath::compatibleClassicLaborPrimary(primary, 70.0, laborTpp, leakageFraction, 0.1);
			assert(same && *same == primary);
			auto const upper = MajorPartyFpMath::compatibleClassicLaborPrimary(55.0, 70.0, laborTpp, leakageFraction, 0.1);
			assert(upper && std::abs(*upper + (70.0 - *upper) * leakageFraction - laborTpp) < tolerance);
			auto const lower = MajorPartyFpMath::compatibleClassicLaborPrimary(1.0, 70.0, laborTpp, leakageFraction, 0.1);
			assert(lower && std::abs(*lower + (70.0 - *lower) * leakageFraction + 30.0 - laborTpp) < tolerance);
		}
		assert(!MajorPartyFpMath::compatibleClassicLaborPrimary(0.1, 90.0, 2.0, 0.077, 0.1));
	}
}

int main()
{
	checkJointMajorPrimarySafeguard();
	checkLocalOneNationPreferences();
	checkVictorianPairingCentres();
	assert(!canAttractTacticalPreferences(OthersIndex, 2));
	assert(!canAttractTacticalPreferences(EmergingPartyIndex, 2));
	assert(canAttractTacticalPreferences(EmergingIndIndex, 2));
	assert(canAttractTacticalPreferences(5, 2));
	assert(!canAttractTacticalPreferences(5, 3));
	checkBendigoEastHistoricalPartner();
	checkReachableTargets();
	checkProtectedLocalSupport();
	checkInvalidCalculations();
	checkHistoricalCoalitionPreferences();
	checkClassicPrimaryAllocation();
}
