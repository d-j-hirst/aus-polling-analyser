// Verify the portable preference comparison with the actual Victorian
// parameter file and districts whose vote arithmetic can be checked.
#include "../PreferenceModel.h"
#include "../LivePreferenceLearning.h"
#include "../json.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {

using namespace PreferenceModel;
constexpr int Labor = 0, Liberal = 1, Greens = 2, Independent = 3, OneNation = 4, National = 7;
constexpr int Others = -1, EmergingIndependent = -2;
constexpr Roles roles{Labor, Liberal, National, Greens, OneNation, Independent, EmergingIndependent, Others};
constexpr double Tolerance = 1e-10;

bool close(double first, double second)
{
	return std::abs(first - second) < Tolerance;
}

std::vector<District> districts{{"Ordinary", "Test Candidate", true}, {"Preston", {}, false},
	{"Prahran", {}, true}};

ConfiguredFlows flows{
	{Labor, {{{Independent, Liberal}, 82.0f}}},
	{Liberal, {{{Independent, Labor}, 80.0f}}},
	{Greens, {{{Independent, Liberal}, 85.0f}}},
	{National, {{{Independent, Labor}, 80.0f}, {{Greens, Labor}, 25.0f}, {{OneNation, Labor}, 56.0f}}}
};

nlohmann::json configuration()
{
	std::ifstream input("analysis/Preferences/2026vic.json");
	assert(input);
	return nlohmann::json::parse(input);
}

nlohmann::json syntheticConfiguration()
{
	auto settings = configuration();
	settings["districts"] = nlohmann::json::object();
	settings.erase("sensitivity_tests");
	return settings;
}

std::optional<Model> load(nlohmann::json const& settings)
{
	std::istringstream input(settings.dump());
	return Model::load(input, roles, districts, flows);
}

Model model(bool sample)
{
	auto settings = syntheticConfiguration();
	settings["mode"] = "compare";
	settings["sample_uncertainty"] = sample;
	settings["unknown_liberal_labor_first_probability"] = 1.0;
	return *load(settings);
}

void checkConfiguration()
{
	auto settings = syntheticConfiguration();
	settings["mode"] = "off";
	assert(!load(settings));
	std::istringstream missing;
	missing.setstate(std::ios::failbit);
	assert(!Model::load(missing, roles, districts, flows));

	settings["mode"] = "compare";
	settings["districts"]["Ordinary"] = {
		{"liberal_advice", "open"}, {"independent", {{"name", "Test Candidate"}, {"position", "left"}}}};
	auto comparison = load(settings);
	assert(comparison && !comparison->isActive());
	settings["mode"] = "active";
	auto active = load(settings);
	assert(active && active->isActive());
	std::vector<Source> const ballot{
		{Labor, 0.35, 2, 2.0, 1.0}, {Liberal, 0.32, 3, 2.0, 0.0},
		{Greens, 0.18, 1, 2.0, 0.8}, {OneNation, 0.15, 4, 2.0, 0.2}
	};
	auto const reference = comparison->count(0, ballot, 0.52, 0.5, comparison->drawElection(21));
	auto const published = active->count(0, ballot, 0.52, 0.5, active->drawElection(21));
	assert(reference.status == Status::Compared && published.status == Status::Compared);
	assert(reference.final.candidates == published.final.candidates);
	assert(reference.final.firstShare == published.final.firstShare);
	Summary activeSummary;
	assert(activeSummary.recordCount(published, 0));
	auto const incompatible = active->count(0, ballot, 0.90, 0.5, active->drawElection(21));
	assert(incompatible.status == Status::InfeasibleClassicTpp);
	assert(!activeSummary.recordCount(incompatible, 1));
	assert(activeSummary.compared == 1 && activeSummary.infeasible == 1);
	assert(activeSummary.firstIssue && activeSummary.firstIssue->iteration == 1);
	assert(activeSummary.pairs.empty() && activeSummary.winners.empty());
	auto rejected = [](nlohmann::json const& values) {
		try {
			load(values);
		}
		catch (std::invalid_argument const&) {
			return true;
		}
		return false;
	};
	settings["districts"]["Ordinary"]["liberal_one_nation_last_weight"] = 1.1;
	assert(rejected(settings));
	settings["districts"]["Ordinary"].erase("liberal_one_nation_last_weight");
	settings["sensitivity_tests"]["Ordinary"]["labor_to_greens_reduction"] = 1.1;
	assert(rejected(settings));
	settings["sensitivity_tests"] = {{"Unknown seat", {{"without_classic_tpp_matching", true}}}};
	assert(rejected(settings));
	settings.erase("sensitivity_tests");
	settings["districts"]["Ordinary"]["independent"]["name"] = "Replacement Candidate";
	assert(rejected(settings));
	settings["districts"] = nlohmann::json::object();
	settings["profiles"]["labor"]["primary_points"][0]["means"][0] = 1.1;
	assert(rejected(settings));
}

void checkExtraCandidatesAgainstEnumeratedOrders()
{
	// Independently enumerate full weighted rankings. Keep the relative
	// weights within each core group and give that group its fitted mass.
	Shares const weights{{0, 3.0}, {1, 2.0}, {2, 0.6}, {3, 1.4}, {4, 0.05}};
	std::vector<Ranking> const core{{{0, 1, 2}, 0.75}, {{2, 1, 0}, 0.25}};
	RankingProfile const profile(core, weights);
	std::vector<std::pair<Ranking, std::size_t>> fullOrders;
	std::array<double, 2> groupWeight{};
	std::vector<int> order{0, 1, 2, 3, 4};
	do {
		std::vector<int> coreOrder;
		for (int id : order) if (id < 3) coreOrder.push_back(id);
		auto const group = std::find_if(core.begin(), core.end(), [&](auto const& row) { return row.order == coreOrder; });
		if (group == core.end()) continue;
		double remaining = 0.0;
		double probability = 1.0;
		for (auto id = order.rbegin(); id != order.rend(); ++id) {
			remaining += weights.at(*id);
			probability *= weights.at(*id) / remaining;
		}
		auto const index = static_cast<std::size_t>(group - core.begin());
		groupWeight[index] += probability;
		fullOrders.emplace_back(Ranking{order, probability}, index);
	} while (std::next_permutation(order.begin(), order.end()));

	std::array<Shares, 32> transfers;
	for (unsigned mask = 1; mask < transfers.size(); ++mask) {
		std::vector<int> survivors;
		for (int i = 0; i < 5; ++i) if (mask & (1U << i)) survivors.push_back(i);
		auto const actual = profile.allocate(survivors);
		assert(close(std::accumulate(actual.begin(), actual.end(), 0.0), 1.0));
		Shares expected;
		for (auto const& [ranking, group] : fullOrders) {
			auto const first = std::find_if(ranking.order.begin(), ranking.order.end(),
				[&](int id) { return (mask & (1U << id)) != 0; });
			expected[*first] += (ranking.fraction / groupWeight[group]) * core[group].fraction;
		}
		for (std::size_t i = 0; i < survivors.size(); ++i) {
			assert(close(actual[i], expected[survivors[i]]));
			transfers[mask].emplace(survivors[i], actual[i]);
		}
	}
	for (unsigned mask = 1; mask < transfers.size(); ++mask) {
		for (auto const& [excluded, previous] : transfers[mask]) {
			auto const next = mask & ~(1U << excluded);
			if (!next) continue;
			double gain = 0.0;
			for (auto const& [id, value] : transfers[next]) {
				assert(value + Tolerance >= transfers[mask].at(id));
				gain += value - transfers[mask].at(id);
			}
			assert(close(gain, previous));
		}
	}
}

void checkClassicReconciliation()
{
	auto const prepared = model(false);
	auto const draw = prepared.drawElection(1);
	std::vector<Source> const fp{
		{Labor, .34, 1, 1.4, 1.0}, {Liberal, .30, 3, 1.4, 0.0},
		{Greens, .16, 0, 1.8, .83}, {OneNation, .14, 4, 1.2, .27}, {Others, .06, 2, 1.4, .50}};
	auto const revised = prepared.count(0, fp, .52, .5, draw);
	assert(revised.status == Status::Compared);
	assert((revised.final.candidates == Pair{Labor, Liberal}));
	assert(close(revised.final.firstShare, .52));
	assert(close(revised.classic.unadjusted, .34 + .16 * .83 + .14 * .27 + .06 * .50));
	assert(revised.classic.largestRateChange > 0.0);
	assert(revised.classic.pairsChecked == 1);
	assert(revised.classic.maximumError < Tolerance);
	auto const unadjusted = prepared.count(0, fp, .52, .5, draw, Sensitivity::WithoutClassicTppMatching);
	assert(unadjusted.status == Status::Compared && unadjusted.final.candidates == revised.final.candidates);
	assert(close(unadjusted.final.firstShare, .52));
	for (double tpp : {.34, .70}) {
		auto const endpoint = prepared.count(0, fp, tpp, .5, draw);
		assert(endpoint.status == Status::Compared);
		assert(endpoint.classic.maximumError < Tolerance);
	}
	auto const impossible = prepared.count(0, fp, .30, .5, draw);
	assert(impossible.status == Status::InfeasibleClassicTpp);
	assert(close(impossible.classic.lower, .34));
	assert(close(impossible.classic.upper, .70));
}

void checkUnadjustedRatesInNonClassicFinal()
{
	// Liberal and One Nation are excluded in both counts. The final TCP
	// reveals whether the test used the original One Nation rate against
	// the Coalition when constructing its Labor versus Greens ranking.
	auto const prepared = model(false);
	auto const draw = prepared.drawElection(1);
	std::array<Source, 4> const fp{{
		{Labor, .42, 1, 1.4, 1.0}, {Liberal, .10, 3, 1.4, 0.0},
		{Greens, .43, 0, 1.8, .75}, {OneNation, .05, 4, 1.2, .10}}};
	auto const matched = prepared.count(0, fp, .86, .5, draw);
	auto const unadjusted = prepared.count(0, fp, .86, .5, draw, Sensitivity::WithoutClassicTppMatching);
	assert(matched.status == Status::Compared && unadjusted.status == Status::Compared);
	assert((matched.final.candidates == Pair{Labor, Greens}));
	assert(unadjusted.final.candidates == matched.final.candidates);
	constexpr double liberalToLabor = .7220215503849514;
	// Of the 10% of One Nation ballots that prefer Labor to the Coalition,
	// only the Greens-first branch bypasses Labor. In the other 90%, the
	// Greens must sit behind both candidates for the ballot to reach Labor.
	double const oneNationToLabor = .10 * (1.0 - .1091502632067437) +
		.90 * (1.0 - .2500315659688799) * .7760781910818585;
	assert(close(unadjusted.final.firstShare, .42 + .10 * liberalToLabor + .05 * oneNationToLabor));
	assert(matched.final.firstShare > unadjusted.final.firstShare);
}

void checkPrahranSensitivity()
{
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	settings["sensitivity_tests"]["Prahran"] = configuration().at("sensitivity_tests").at("Prahran");
	auto const prepared = load(settings);
	assert(prepared);
	assert(close(prepared->sensitivityTests(2).laborToGreensReduction, .10));
	auto const draw = prepared->drawElection(1);
	Source const labor{Labor, .20, 1, 1.4, 1.0};
	Source const liberal{Liberal, .40, 3, 1.4, 0.0};
	Source const greens{Greens, .40, 0, 1.8, .85};
	Source const oneNation{OneNation, .40, 4, 1.2, .27};
	std::array const liberalGreens{labor, liberal, greens};
	auto const baseline = prepared->count(2, liberalGreens, .54, .5, draw);
	auto const reduced = prepared->count(2, liberalGreens, .54, .5, draw, Sensitivity::ReducedLaborToGreens);
	assert(baseline.status == Status::Compared && reduced.status == Status::Compared);
	assert((baseline.final.candidates == Pair{Liberal, Greens}));
	assert(reduced.final.candidates == baseline.final.candidates);
	// The Greens start on 40%. Only Labor transfers can add to that total,
	// so this fixture directly measures the requested relative reduction.
	double const originalGreensGain = 1.0 - baseline.final.firstShare - greens.primary;
	assert(originalGreensGain > 0.0);
	assert(close(1.0 - reduced.final.firstShare - greens.primary, .90 * originalGreensGain));
	assert(reduced.classic.maximumError < Tolerance);

	for (auto const& fp : {std::array{labor, liberal, oneNation}, std::array{labor, greens, oneNation}}) {
		double const tpp = labor.primary + oneNation.primary * oneNation.classicRate;
		auto const before = prepared->count(2, fp, tpp, .5, draw);
		auto const after = prepared->count(2, fp, tpp, .5, draw, Sensitivity::ReducedLaborToGreens);
		assert(before.status == Status::Compared && after.status == Status::Compared);
		assert(before.final.candidates == after.final.candidates);
		assert(close(before.final.firstShare, after.final.firstShare));
	}
}

void checkFittedNonClassicPair()
{
	// Labor is eliminated, then One Nation. The final Greens share combines
	// the original Greens vote with the two fitted original-source splits
	// against Liberal. Reusing the whole One Nation pile would double count
	// some of Labor's ballots or treat them as new One Nation preferences.
	auto const prepared = model(false);
	constexpr double greensRate = .8684671520848514, oneNationRate = .26538421922223543;
	std::vector<Source> const fp{
		{Labor, .05, 1, 1.4, 1.0}, {Liberal, .32, 3, 1.4, 0.0},
		{Greens, .33, 0, 1.8, greensRate}, {OneNation, .30, 4, 1.2, oneNationRate}};
	double const tpp = .05 + .33 * greensRate + .30 * oneNationRate;
	auto const result = prepared.count(0, fp, tpp, .5, prepared.drawElection(4));
	assert(result.status == Status::Compared);
	assert((result.final.candidates == Pair{Liberal, Greens}));
	double const expectedGreens = .33 + .05 * .8203275332737616 + .30 * (1.0 - .7297164517897798);
	assert(close(result.final.firstShare, 1.0 - expectedGreens));
}

void checkLiberalOneNationAdvice()
{
	// With just three candidates the Liberal is excluded on 20% FP. Both
	// finalists start on 40%, so the final shares reveal the Liberal flow
	// directly. Preston's card moves some Liberal votes away from One
	// Nation against either finalist while keeping the Labor–Greens split.
	auto const prestonAdvice = configuration().at("districts").at("Preston");
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	settings["districts"]["Preston"] = prestonAdvice;
	Source const labor{Labor, .40, 1, 1.4, 1.0};
	Source const liberal{Liberal, .20, 3, 1.4, 0.0};
	Source const greens{Greens, .40, 0, 1.8, .85};
	Source const oneNation{OneNation, .40, 4, 1.2, .27};
	std::array const alpOn{labor, liberal, oneNation};
	std::array const grnOn{greens, liberal, oneNation};
	std::array const alpGrn{labor, liberal, greens};
	for (double laborFirst : {0.0, 1.0}) {
		settings["unknown_liberal_labor_first_probability"] = laborFirst;
		auto const prepared = load(settings);
		assert(prepared);
		auto const draw = prepared->drawElection(1);
		auto checkFirstShare = [&](int district, auto const& fp, double tpp,
			Pair expectedPair, double expectedShare) {
			auto const result = prepared->count(district, fp, tpp, .5, draw);
			assert(result.status == Status::Compared && result.final.candidates == expectedPair);
			assert(close(result.final.firstShare, expectedShare));
			assert(result.classic.maximumError < Tolerance);
		};
		double const alpOnTpp = labor.primary + oneNation.primary * oneNation.classicRate;
		checkFirstShare(0, alpOn, alpOnTpp, {Labor, OneNation}, .46);
		checkFirstShare(1, alpOn, alpOnTpp, {Labor, OneNation}, .53);
		constexpr double oneNationOverGreens = .8456095858574568;
		checkFirstShare(0, grnOn, .50, {Greens, OneNation}, .40 + .20 * (1.0 - oneNationOverGreens));
		checkFirstShare(1, grnOn, .50, {Greens, OneNation}, .40 + .20 * (1.0 - .5 * oneNationOverGreens));
		constexpr double liberalToLaborAgainstGreens = .7220215503849514;
		double const alpGrnTpp = labor.primary + greens.primary * greens.classicRate;
		checkFirstShare(0, alpGrn, alpGrnTpp, {Labor, Greens},
			.40 + .20 * (laborFirst ? liberalToLaborAgainstGreens : .30));
		checkFirstShare(1, alpGrn, alpGrnTpp, {Labor, Greens}, .40 + .20 * liberalToLaborAgainstGreens);
	}
}

void checkBothCoalitionPartners()
{
	auto const prepared = model(false);
	std::vector<Source> const fp{
		{Labor, .18, 1, 1.4, 1.0}, {Liberal, .37, 3, 1.4, 0.0},
		{National, .34, 3, 1.8, 0.0}, {Greens, .05, 0, 1.8, .83},
		{OneNation, .04, 4, 1.2, .30}, {Others, .02, 2, 1.4, .52}};
	for (double incoming : {.25, .75}) {
		auto const result = prepared.count(0, fp, .29, incoming, prepared.drawElection(2));
		assert(result.status == Status::Compared);
		assert((result.final.candidates == Pair{Liberal, National}));
		assert(close(result.final.firstShare, .37 + .29 * (1.0 - incoming)));
		assert(result.classic.pairsChecked == 2);
		assert(result.classic.maximumError < Tolerance);
	}
	// One National candidate occupies the Coalition slot. Any absent
	// Liberal role remains latent and cannot receive the actual ballots.
	auto nationalOnly = fp;
	nationalOnly[2].primary += nationalOnly[1].primary;
	nationalOnly.erase(nationalOnly.begin() + 1);
	auto const result = prepared.count(0, nationalOnly, .29, .5, prepared.drawElection(2));
	assert(result.status == Status::Compared);
	assert((result.final.candidates == Pair{Labor, National}));
	assert(close(result.final.firstShare, .29));
}

void checkSampledProfilesAndOrderIndependence()
{
	auto const prepared = model(true);
	std::vector<Source> fp{
		{Labor, .27, 1, 1.4, 1.0}, {Liberal, .21, 3, 1.4, 0.0},
		{Greens, .17, 0, 1.8, .87}, {OneNation, .18, 4, 1.2, .27},
		{Independent, .14, 2, 1.4, .60}, {Others, .03, 2, 1.4, .50}};
	double minTcp = 1.0, maxTcp = 0.0;
	for (std::uint64_t seed = 0; seed < 100; ++seed) {
		auto const draw = prepared.drawElection(seed);
		auto const actual = prepared.count(0, fp, .49, .5, draw);
		std::reverse(fp.begin(), fp.end());
		auto const reordered = prepared.count(0, fp, .49, .5, draw);
		assert(actual.status == Status::Compared && reordered.status == Status::Compared);
		assert(actual.final.candidates == reordered.final.candidates);
		assert(actual.final.firstShare == reordered.final.firstShare);
		assert(actual.classic.maximumError < Tolerance);
		minTcp = std::min(minTcp, actual.final.firstShare);
		maxTcp = std::max(maxTcp, actual.final.firstShare);
	}
	assert(maxTcp - minTcp > .02);
}

void checkNamedIndependentPlacement()
{
	auto settings = syntheticConfiguration();
	settings["mode"] = "compare";
	settings["sample_uncertainty"] = false;
	settings["unknown_liberal_labor_first_probability"] = 1.0;
	std::vector<Source> const fp{
		{Labor, .215, 1, 1.4, 1.0}, {Liberal, .38, 3, 1.4, 0.0},
		{Greens, .13, 0, 1.8, .8194}, {OneNation, .05, 4, 1.2, .255},
		{Independent, .205, 2, 1.4, .4925}, {Others, .02, 2, 1.4, .4925}};
	auto high = load(settings);
	settings["districts"]["Ordinary"]["independent"] = {{"name", "Test Candidate"}, {"position", "right"}};
	auto low = load(settings);
	assert(high && low);
	auto const draw = high->drawElection(1);
	auto const highResult = high->count(0, fp, .47, .5, draw);
	auto const lowResult = low->count(0, fp, .47, .5, draw);
	assert(highResult.status == Status::Compared && lowResult.status == Status::Compared);
	assert((highResult.final.candidates == Pair{Liberal, Independent}));
	assert((lowResult.final.candidates == Pair{Labor, Liberal}));
	assert(highResult.classic.maximumError < Tolerance && lowResult.classic.maximumError < Tolerance);
}

void checkSecondaryIndependentTransfers()
{
	constexpr int Secondary = 214003;
	auto ranking = model(false);
	std::array const ballot{
		Source{Labor, .19, 1, 1.8, 1.0}, Source{Liberal, .27, 3, 1.4, 0.0},
		Source{Independent, .18, 2, 1.2, .52, false, true}, Source{OneNation, .22, 4, 1.2, .33},
		Source{Greens, .07, 0, 1.8, .81}, Source{Secondary, .04, 2, 1.2, .48, true},
		Source{Others, .03, 2, 1.2, .48}};
	auto neutral = ballot;
	neutral[5].additionalIndependent = false;
	auto before = ranking.referencePairRates(0, neutral, {Independent, Liberal});
	auto after = ranking.referencePairRates(0, ballot, {Independent, Liberal});
	assert(after.at(Secondary) > before.at(Secondary));
	assert(close(after.at(Others), before.at(Others)));
	auto classic = ranking.referencePairRates(0, ballot, {Labor, Liberal});
	assert(close(classic.at(Secondary), ballot[5].classicRate));
	auto configured = ballot;
	configured[5].id = EmergingIndependent;
	configured[5].additionalIndependent = false;
	configured[5].confirmedIndependent = true;
	auto configuredRates = ranking.referencePairRates(0, configured, {Independent, Liberal});
	assert(close(configuredRates.at(EmergingIndependent), after.at(Secondary)));
	auto result = ranking.count(0, ballot, .432, .5, ranking.drawElection(5));
	assert(result.status == Status::Compared && result.classic.maximumError < Tolerance);

	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	settings["districts"]["Ordinary"]["independent"] = {{"name", "Test Candidate"}, {"position", "right"}};
	auto right = load(settings);
	assert(right);
	auto rightRates = right->referencePairRates(0, ballot, {Independent, Liberal});
	assert(close(rightRates.at(Secondary), before.at(Secondary)));
}

void checkLaborPreferencesToRightIndependent()
{
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	auto standard = load(settings);
	settings["districts"]["Ordinary"]["independent"] = {{"name", "Test Candidate"}, {"position", "right"}};
	auto right = load(settings);
	assert(standard && right);

	// Labor is the only excluded source. Recover its pair preference share
	// from the increase in the independent's vote at the final count.
	auto independentFlow = [](Model const& prepared, int opponent, std::uint64_t seed) {
		std::array const ballot{
			Source{Labor, .10, 1, 1.4, 1.0}, Source{opponent, .45, 3, 1.4, .2},
			Source{Independent, .45, 3, 1.4, .5}
		};
		auto const result = prepared.count(0, ballot, .30, .5, prepared.drawElection(seed));
		assert(result.status == Status::Compared);
		assert((result.final.candidates == Pair{std::min(opponent, Independent), std::max(opponent, Independent)}));
		double const independentTcp = result.final.candidates[0] == Independent
			? result.final.firstShare : 1.0 - result.final.firstShare;
		return (independentTcp - .45) / .10;
	};
	assert(close(independentFlow(*standard, Liberal, 1), .82));
	assert(close(independentFlow(*right, Liberal, 1), .60));
	assert(close(independentFlow(*right, OneNation, 1), .80));

	// Check the spread through the full count, where it affects the reported
	// TCP margin. The right prior should remain broad around the lower centre.
	settings["sample_uncertainty"] = true;
	auto sampled = load(settings);
	assert(sampled);
	constexpr int Draws = 1000;
	double sum = 0.0, squaredSum = 0.0;
	for (int i = 0; i < Draws; ++i) {
		double const share = independentFlow(*sampled, Liberal, i);
		sum += share;
		squaredSum += share * share;
	}
	double const mean = sum / Draws;
	double const sd = std::sqrt(squaredSum / Draws - mean * mean);
	assert(std::abs(mean - .60) < .025 && sd > .18 && sd < .225);
}

void checkLaborPreferenceOverlapAtExclusion()
{
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	settings["districts"]["Ordinary"]["independent"] = {{"name", "Test Candidate"}, {"position", "right"}};
	settings["profiles"]["labor"]["primary_points"] = nlohmann::json::array({
		{{"primary", .10}, {"means", {.50, .20, .75, .25, .50}}}
	});
	auto prepared = load(settings);
	assert(prepared);
	auto const draw = prepared->drawElection(1);

	// These core orders put 67.5% of Labor ballots with Liberal over One
	// Nation. The midpoint sends 50% directly to the independent and 36.25%
	// to Liberal. With Labor on 10% the independent gains 1.375 points on
	// Liberal. Test just on either side of that gap in first preferences.
	for (double difference : {.01374, .01376}) {
		std::array const ballot{
			Source{Labor, .10, 1, 1.4, 1.0}, Source{Liberal, .21 + difference, 3, 1.4, 0.0},
			Source{Independent, .21, 3, 1.4, .5}, Source{OneNation, .48 - difference, 4, 1.4, .2}
		};
		auto const result = prepared->count(0, ballot, .30, .5, draw);
		assert(result.status == Status::Compared);
		auto const expected = difference < .01375 ? Pair{Independent, OneNation} : Pair{Liberal, OneNation};
		assert(result.final.candidates == expected);
	}
}

void checkSouthAustralianBallot()
{
	// The SA project models Liberal and One Nation but has no separate
	// Nationals slot. Exercise the actual replay settings on all three
	// common types of final, with a share of votes grouped in Others.
	std::ifstream input("analysis/Preferences/2026sa.json");
	assert(input);
	constexpr int saOneNation = 7, saIndependent = 6;
	constexpr Roles saRoles{Labor, Liberal, std::nullopt, Greens, saOneNation, saIndependent, EmergingIndependent, Others};
	std::vector<District> const saDistricts{{"Elder", {}, false}, {"King", {}, false}, {"Chaffey", {}, true},
		{"MacKillop", "Nick McBride", true}, {"Narungga", "Fraser Ellis", true},
		{"Finniss", "Lou Nicholson", true}};
	auto prepared = Model::load(input, saRoles, saDistricts, {});
	assert(prepared && prepared->isActive() && !prepared->partyRoles().national);
	auto const draw = prepared->drawElection(52);
	auto checkPair = [&](int district, double labor, double liberal, double oneNation,
		double greens, double others, double laborTpp, Pair expectedPair) {
		std::array const ballot{
			Source{Labor, labor, 2, 1.4, 1.0}, Source{Liberal, liberal, 3, 1.4, 0.0},
			Source{saOneNation, oneNation, 4, 1.2, 0.25}, Source{Greens, greens, 1, 1.8, 0.82},
			Source{Others, others, 2, 1.4, 0.5}};
		auto const result = prepared->count(district, ballot, laborTpp, 0.5, draw);
		assert(result.status == Status::Compared);
		assert(result.final.candidates == expectedPair);
		assert(result.classic.pairsChecked == 1 && result.classic.maximumError < Tolerance);
		if (expectedPair == Pair{Labor, Liberal}) assert(close(result.final.firstShare, laborTpp));
	};
	checkPair(0, .44, .41, .09, .035, .025, .52, {Labor, Liberal});
	checkPair(1, .43, .09, .40, .05, .03, .55, {Labor, saOneNation});
	checkPair(2, .10, .43, .41, .04, .02, .20, {Liberal, saOneNation});
}

void checkHistoricalFinnissTransfers()
{
	std::ifstream input("analysis/Preferences/2026sa.json");
	assert(input);
	auto const sa = nlohmann::json::parse(input);
	auto settings = syntheticConfiguration();
	settings["districts"]["Ordinary"] = sa.at("districts").at("Finniss");
	auto& transfers = settings["districts"]["Ordinary"]["historical_transfers"];
	transfers[0]["independent_name"] = "Test Candidate";
	auto const ranking = load(settings);
	assert(ranking);
	auto const generic = load(syntheticConfiguration());
	assert(generic);
	std::array const ballot{
		Source{Labor, .202, 2, 1.4, 1.0}, Source{Liberal, .302, 3, 1.4, 0.0},
		Source{OneNation, .203, 4, 1.2, .25}, Source{Greens, .055, 1, 1.8, .82},
		Source{Independent, .196, 2, 1.4, .55}, Source{Others, .042, 2, 1.4, .5}};
	std::array const four{Labor, Liberal, Independent, OneNation};
	auto const before = generic->referenceTransferRates(0, ballot, four).at(Greens);
	auto const after = ranking->referenceTransferRates(0, ballot, four).at(Greens);
	assert(close(std::accumulate(after.begin(), after.end(), 0.0), 1.0));
	assert(after[2] > before[2] && after[0] < before[0]);
	assert(close(after[3], before[3]) && after[3] > 0.0 && after[3] < 0.15);
	auto const mismatch = ranking->referenceTransferRates(0, ballot, four, false).at(Greens);
	assert(mismatch == before);
	std::array const withoutLiberal{
		Source{Labor, .504, 2, 1.4, 1.0}, ballot[2], ballot[3], ballot[4], ballot[5]};
	std::array const three{Labor, Independent, OneNation};
	assert(ranking->referenceTransferRates(0, withoutLiberal, three) ==
		generic->referenceTransferRates(0, withoutLiberal, three));
	std::array const classic{Labor, Liberal};
	assert(close(ranking->referencePairRates(0, ballot, classic).at(Greens), .82));
	for (std::uint64_t seed = 0; seed < 80; ++seed) {
		auto const result = ranking->count(0, ballot, .433, .5, ranking->drawElection(seed));
		assert(result.status == Status::Compared);
		assert(result.classic.maximumError < Tolerance);
	}
	// A party without a fitted ranking can also be a named recipient.
	// It normally competes by proximity and retains the classic split
	// after its positions are made explicit for the old transfer.
	constexpr int Minor = 8;
	std::array const withMinor{ballot[0], ballot[1], ballot[2], ballot[3], ballot[4],
		Source{Minor, .042, 2, 1.4, .5}};
	auto withMinorHistory = settings;
	auto& row = withMinorHistory["districts"]["Ordinary"]["historical_transfers"][0];
	row["transfers"] = {{"ALP", 537}, {"LIB", 164}, {"MIN", 1102}};
	std::istringstream minorInput(withMinorHistory.dump());
	auto const minorRanking = Model::load(minorInput, roles, districts, flows, {{"MIN", Minor}});
	assert(minorRanking);
	std::array const withMinorFinalists{Labor, Liberal, Minor};
	auto const minorRates = minorRanking->referenceTransferRates(0, withMinor, withMinorFinalists).at(Greens);
	auto const baseMinorRates = generic->referenceTransferRates(0, withMinor, withMinorFinalists).at(Greens);
	assert(minorRates[2] > baseMinorRates[2]);
	assert(close(minorRanking->referencePairRates(0, withMinor, classic).at(Greens), .82));
	transfers[0]["inherited_votes"] = 0;
	bool rejected = false;
	try { load(settings); }
	catch (std::invalid_argument const&) { rejected = true; }
	assert(rejected);
}

void checkLivePreferenceCountRevisions()
{
	auto settings = nlohmann::json::parse(R"({
		"schema_version":1,
		"observations":[
		  {"district":"Finniss","available_at":"2026-03-30T12:00:00+10:30",
		   "sample":"Second batch","sampled_preference_ballots":1200,
		   "flow_percent":{"IND:Lou Nicholson":57,"ALP":23,"LNP":13,"ON":7},
		   "additional_sd_pp":7,"source":"test"},
		  {"district":"Finniss","available_at":"2026-03-30T11:30:00+10:30",
		   "sample":"First batch","sampled_preference_ballots":1053,
		   "sampled_first_preferences":{"GRN":560,"IND:Bron Lewis":493},
		   "flow_percent":{"IND:Lou Nicholson":58,"ALP":22,"LNP":13,"ON":7},
		   "additional_sd_pp":7,"source":"test"},
		  {"district":"Forrest","available_at":"2026-03-29T23:00:00Z",
		   "sample":"Eight booths","sampled_preference_ballots":4297,
		   "flow_percent":{"ALP":31.6,"LNP":25.7,"IND:Sue Chapman":42.6},
		   "additional_sd_pp":10,"source":"test"}
		]
	})");
	auto select = [&](char const* time, char const* jurisdiction) {
		std::istringstream input(settings.dump());
		return loadCountObservations(input, time, jurisdiction);
	};
	assert(select("2026-03-30T08:00:00", "fed").empty());
	assert(!select("2026-03-30T11:59:59", "fed").contains("Finniss"));
	auto const first = select("2026-03-30T12:00:00", "fed");
	assert(first.at("Finniss").ballots == 1053 && first.at("Finniss").firstPreferences.size() == 2);
	assert(first.at("Finniss").candidateTotals.empty());
	assert(first.at("Forrest").ballots == 4297);
	assert(close(first.at("Forrest").flows.at("ALP"), 31.6 / 99.9));
	assert(select("2026-03-30T12:00:00", "sa").at("Finniss").ballots == 1200);
	assert(select("2026-03-30T01:30:00Z", "fed").at("Finniss").ballots == 1200);
	auto& threeWay = settings["observations"][2];
	threeWay["sampled_first_preferences"] = {{"OTH",4297}};
	threeWay["sampled_candidate_totals"] = {{"ALP",4800},{"LNP",6700},{"IND:Sue Chapman",6100}};
	threeWay["sampled_finalist_first_preferences"] = {{"ALP",3441},{"LNP",5595},{"IND:Sue Chapman",4267}};
	threeWay["tpp_prior"] = {{"third_primary_to_labor_percent",60},{"third_inherited_to_labor_percent",45}};
	auto const counted = select("2026-03-30T12:00:00", "fed").at("Forrest");
	assert(counted.candidateTotals.at("ALP") == 4800 && counted.tppPrior->inheritedToLabor == .45);
	threeWay["sampled_candidate_totals"]["ALP"] = 1000;
	bool exceedsTally = false;
	try { select("2026-03-30T12:00:00", "fed"); }
	catch (std::invalid_argument const&) { exceedsTally = true; }
	assert(exceedsTally);
	threeWay.erase("sampled_candidate_totals");
	threeWay.erase("sampled_finalist_first_preferences");
	threeWay.erase("tpp_prior");
	settings["observations"][0]["available_at"] = "2026-03-30T12:00:00+11:00";
	bool rejected = false;
	try { select("2026-03-31T12:00:00", "sa"); }
	catch (std::invalid_argument const&) { rejected = true; }
	assert(rejected);
}

void checkLivePreferenceCountConditioning()
{
	auto settings = configuration();
	settings["mode"] = "live_trial";
	settings["districts"] = {{"Ordinary", {{"live_trial",true}}}};
	settings.erase("sensitivity_tests");
	auto const ranking = load(settings);
	assert(ranking && ranking->isLiveTrial() && ranking->isActive());
	assert(ranking->coversDistrict(0) && !ranking->coversDistrict(1));
	constexpr int Lewis = 114003, Missing = 999;
	std::array const ballot{
		Source{Labor, .187, 1, 1.8, 1}, Source{Liberal, .272, 3, 1.4, 0},
		Source{Greens, .062, 0, 1.8, .8115}, Source{Independent, .181, 2, 1.2, .4766, false, true},
		Source{OneNation, .227, 4, 1.2, .3615}, Source{Others, .030, 2, 1.4, .4766},
		Source{Lewis, .041, 2, 1.2, .4766, true, false}};
	PreparedCountObservation const four{
		{{Labor,.22},{Liberal,.13},{Independent,.58},{OneNation,.07}}, {{Greens,560},{Lewis,493}}, .0051};
	PreparedCountObservation const three{
		{{Labor,.316},{Liberal,.257},{Independent,.427}}, {}, .0101};
	auto missingDonor = four;
	missingDonor.firstPreferences = {{Greens,560},{Missing,493}};
	int before = 0, after = 0, changedThree = 0;
	for (std::uint64_t seed = 0; seed < 600; ++seed) {
		auto const election = ranking->drawElection(seed);
		auto count = [&](PreparedCountObservation const* sample) {
			return ranking->count(0, ballot, .43, .5, election, Sensitivity::None, false, true, sample);
		};
		auto const base = count(nullptr);
		auto const conditioned = count(&four);
		auto const partial = count(&three);
		auto const absent = count(&missingDonor);
		for (auto const* result : {&base,&conditioned,&partial,&absent}) {
			assert(result->status == Status::Compared);
			assert(result->classic.maximumError < Tolerance);
		}
		assert(base.final.candidates == absent.final.candidates && base.final.firstShare == absent.final.firstShare);
		assert(conditioned.conditionedOnLiveSample && partial.conditionedOnLiveSample);
		assert(!base.conditionedOnLiveSample && !absent.conditionedOnLiveSample);
		before += base.final.candidates == Pair{Liberal, Independent};
		after += conditioned.final.candidates == Pair{Liberal, Independent};
		changedThree += partial.final.candidates != base.final.candidates;
	}
	assert(after > before && changedThree > 0);
}

void checkFederalComparisonAndLiveTrialConfiguration()
{
	std::ifstream input("analysis/Preferences/2025fed.json");
	assert(input);
	auto settings = nlohmann::json::parse(input);
	std::array const fedDistricts{District{"Forrest", "Sue Chapman", true},
		District{"Ryan", {}, false}, District{"Menzies", {}, false}, District{"Blaxland"}, District{"Fisher"},
		District{"Flinders"}, District{"Grey"}, District{"Hunter"}, District{"Maranoa"}, District{"Mayo"},
		District{"Monash"}, District{"Richmond"}, District{"Watson"}};
	std::istringstream comparisonInput(settings.dump());
	auto const comparison = Model::load(comparisonInput, roles, fedDistricts, flows);
	assert(comparison && !comparison->isActive() && !comparison->isLiveTrial());
	for (int district = 0; district < static_cast<int>(fedDistricts.size()); ++district) {
		assert(comparison->coversDistrict(district));
	}

	settings["mode"] = "live_trial";
	std::istringstream trialInput(settings.dump());
	auto const ranking = Model::load(trialInput, roles, fedDistricts, flows);
	assert(ranking && ranking->isLiveTrial() && ranking->coversDistrict(0) &&
		ranking->coversDistrict(1) && !ranking->coversDistrict(2));
	std::array const ballot{
		Source{Labor, .226, 1, 1.8, 1}, Source{Liberal, .311, 3, 1.4, 0},
		Source{Independent, .181, 2, 1.4, .7}, Source{Greens, .079, 0, 1.8, .85},
		Source{National, .048, 3, 1.8, .15}, Source{OneNation, .087, 4, 1.2, .19},
		Source{Others, .068, 2, 1.4, .48}};
	PreparedCountObservation const sample{
		{{Labor,31.6/99.9},{Liberal,25.7/99.9},{Independent,42.6/99.9}},
		{{Greens,1342},{OneNation,1258},{National,818},{Others,879}}, .0101};
	int changes = 0;
	for (std::uint64_t seed = 0; seed < 120; ++seed) {
		auto const election = ranking->drawElection(seed);
		auto const base = ranking->count(0, ballot, .48, .5, election);
		auto const revised = ranking->count(0, ballot, .48, .5, election, Sensitivity::None, false, true, &sample);
		assert(base.status == Status::Compared && revised.status == Status::Compared);
		assert(revised.classic.pairsChecked == 2 && revised.classic.maximumError < Tolerance);
		changes += base.final.firstShare != revised.final.firstShare || base.final.candidates != revised.final.candidates;
	}
	assert(changes > 0);
}

void checkJointThreeCandidateCount()
{
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	auto const model = load(settings);
	std::array const ballot{Source{Labor,.300,1,1.8,1}, Source{Liberal,.320,3,1.4,0},
		Source{Greens,.306,0,1.8,.85}, Source{Others,.074,2,1.2,.50}};
	// Every ballot has been counted to three candidates. The Greens take 15
	// more transfers than Labor and survive. Only their Labor-LNP ordering
	// remains uncertain for the TPP, supplied here as fixed rates for the test.
	PreparedCountObservation sample{{{Labor,150./740},{Greens,165./740},{Liberal,425./740}},
		{{Others,740}}, .01, {{Labor,3150},{Greens,3225},{Liberal,3625}},
		{{Labor,3000},{Greens,3060},{Liberal,3200}}, 740, .10,
		ThreeCandidateTppPrior{.90,.50}};
	auto count = [&](PreparedCountObservation const& observation, ThreeCandidateCountContext context) {
		return model->count(0, ballot, .57, .5, model->drawElection(1), Sensitivity::None, false, true,
			&observation, context);
	};
	auto const joint = count(sample, {10000, false});
	assert(joint.status == Status::Compared && joint.conditionedOnLiveSample);
	assert((joint.final.candidates == Pair{Liberal, Greens}));
	assert(close(joint.laborTpp, .315 + .306 * .90 + .0165 * .50));
	assert(close(joint.jointThreeCandidateFlows.at(Liberal), 425./740));
	auto const countedTpp = count(sample, {10000, true});
	assert(countedTpp.laborTpp == .57 && countedTpp.final.candidates == joint.final.candidates);
	assert(countedTpp.jointThreeCandidateFlows == joint.jointThreeCandidateFlows);
	// With 1,000 ballots outside the sample, the expected split stays near
	// the observed one, and the same Greens pile determines the TPP.
	auto const partial = count(sample, {11000, false});
	assert(std::abs(partial.jointThreeCandidateFlows.at(Liberal) - 425./740) < .0001);
	auto fallback = count(sample, {9900, false});
	assert(fallback.status == Status::Compared && fallback.jointThreeCandidateFlows.empty());
	assert(!fallback.conditionedOnLiveSample && fallback.laborTpp == .57);
	// If Greens are third, the revised TPP is also the reported classic TCP.
	sample.candidateTotals = {{Labor,3250},{Greens,3210},{Liberal,3540}};
	sample.flows = {{Labor,250./740},{Greens,150./740},{Liberal,340./740}};
	auto const classic = count(sample, {10000, false});
	assert((classic.final.candidates == Pair{Labor,Liberal}));
	assert(close(classic.final.firstShare, classic.laborTpp));
	auto const countedClassic = count(sample, {10000, true});
	assert(close(countedClassic.final.firstShare, .57));
}

void checkJointThreeCandidateExtraFinalist()
{
	constexpr int CentreAlliance = 5;
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	settings["unknown_liberal_labor_first_probability"] = 1.0;
	auto const model = load(settings);
	std::array const ballot{Source{Labor,.24,1,1.8,1}, Source{Liberal,.28,3,1.4,0},
		Source{CentreAlliance,.31,2,1.4,.56}, Source{Others,.17,2,1.2,.50}};
	auto check = [&](int excluded, int remaining, int laborTransfers, int liberalTransfers) {
		// The full sample settles the exclusion. Check the final margin using
		// the ordinary preference rates for the eliminated major's voters.
		int const centreTransfers = 1700 - laborTransfers - liberalTransfers;
		PreparedCountObservation const sample{
			{{Labor,laborTransfers/1700.},{Liberal,liberalTransfers/1700.},{CentreAlliance,centreTransfers/1700.}},
			{{Others,1700}}, .01, {{Labor,2400+laborTransfers},{Liberal,2800+liberalTransfers},
				{CentreAlliance,3100+centreTransfers}},
			{{Labor,2400},{Liberal,2800},{CentreAlliance,3100}}, 1700, .10};
		auto const result = model->count(0, ballot, .55, .5, model->drawElection(1),
			Sensitivity::None, false, true, &sample, ThreeCandidateCountContext{10000, false});
		Pair const pair{remaining,CentreAlliance};
		auto const ordinaryRates = model->referencePairRates(0, ballot, pair);
		double const expectedShare = (sample.candidateTotals.at(remaining)
			+ sample.candidateTotals.at(excluded) * ordinaryRates.at(excluded)) / 10000;
		assert(result.status == Status::Compared && result.conditionedOnLiveSample);
		assert(result.final.candidates == pair);
		assert(close(result.final.firstShare, expectedShare));
	};
	check(Labor,Liberal,300,500);
	check(Liberal,Labor,1000,100);
}

void checkJointThreeCandidateCoalitionCount()
{
	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	auto const model = load(settings);
	std::array const ballot{Source{Labor,.26,1,1.8,1}, Source{Liberal,.31,3,1.4,0},
		Source{Greens,.28,0,1.8,.85}, Source{National,.08,3,1.8,.154},
		Source{Others,.07,2,1.2,.50}};
	// National ballots are already included in the measured transfers.
	// A complete sample leaves no further votes to distribute at this stage.
	PreparedCountObservation const complete{{{Labor,400./1500},{Liberal,480./1500},{Greens,620./1500}},
		{{National,800},{Others,700}}, .01, {{Labor,3000},{Liberal,3580},{Greens,3420}},
		{{Labor,2600},{Liberal,3100},{Greens,2800}}, 1500, .10, ThreeCandidateTppPrior{.90,.50}};
	auto count = [&](auto const& sources, PreparedCountObservation const& observation, bool countedTpp) {
		return model->count(0, sources, .52, .95, model->drawElection(1), Sensitivity::None, false, true,
			&observation, ThreeCandidateCountContext{10000, countedTpp});
	};
	auto const joint = count(ballot, complete, false);
	assert(joint.status == Status::Compared && joint.conditionedOnLiveSample);
	assert((joint.final.candidates == Pair{Liberal, Greens}));
	assert(close(joint.classic.lower, .300) && close(joint.classic.upper, 1.0 - .358));
	assert(close(joint.laborTpp, .300 + .28 * .90 + .062 * .50));
	assert(close(joint.jointThreeCandidateFlows.at(Liberal), 480./1500));
	auto const measured = count(ballot, complete, true);
	assert(measured.laborTpp == .52 && measured.final.candidates == joint.final.candidates);
	assert(measured.jointThreeCandidateFlows == joint.jointThreeCandidateFlows);

	// The measured Coalition candidate can also be the National. The
	// smaller Liberal primary belongs to the sampled transfer pool here.
	auto nationalSurvives = ballot;
	nationalSurvives[1].primary = .08;
	nationalSurvives[3].primary = .31;
	PreparedCountObservation const nationalSample{
		{{Labor,400./1500},{National,480./1500},{Greens,620./1500}}, {{Liberal,800},{Others,700}}, .01,
		{{Labor,3000},{National,3580},{Greens,3420}}, {{Labor,2600},{National,3100},{Greens,2800}}, 1500, .10};
	auto const nationalFinal = count(nationalSurvives, nationalSample, true);
	assert(nationalFinal.status == Status::Compared && nationalFinal.conditionedOnLiveSample);
	assert((nationalFinal.final.candidates == Pair{Greens, National}));
	assert(close(nationalFinal.jointThreeCandidateFlows.at(National), 480./1500));

	// The same sample can only inform the last three if Liberal survives
	// the earlier rounds. A smaller sample can fit either FP scenario.
	PreparedCountObservation const partial{{{Labor,420./1080},{Liberal,450./1080},{Greens,210./1080}},
		{{National,500},{Others,580}}, .01, {{Labor,820},{Liberal,550},{Greens,630}},
		{{Labor,400},{Liberal,100},{Greens,420}}, 1080, .10};
	assert(count(ballot, partial, true).conditionedOnLiveSample);
	auto strongerNationals = ballot;
	strongerNationals[1].primary = .02;
	strongerNationals[3].primary = .37;
	auto const fallback = count(strongerNationals, partial, true);
	assert(fallback.status == Status::Compared && fallback.jointThreeCandidateIncompatible);
	assert(!fallback.conditionedOnLiveSample && fallback.laborTpp == .52);
	assert(std::find(fallback.final.candidates.begin(), fallback.final.candidates.end(), Liberal)
		== fallback.final.candidates.end());
}

void checkComparisonSummary()
{
	Summary summary;
	Comparison observation;
	observation.model.status = Status::FinalOfficial;
	summary.record(observation, 0);
	observation.model.status = Status::InfeasibleClassicTpp;
	summary.record(observation, 2);
	summary.record(observation, 1);
	observation.model.status = Status::Compared;
	observation.model.final = {{Labor, OneNation}, .48, {}};
	observation.model.classic.pairsChecked = 1;
	observation.legacy = {{Labor, Liberal}, .52, {}};
	observation.modelWithLiveEvidence = {{Labor, OneNation}, .51, {}};
	observation.legacyWithLiveEvidence = observation.legacy;
	observation.modelLivePairMatched = true;
	observation.withoutClassicTppMatching = CountResult{};
	observation.withoutClassicTppMatching->status = Status::Compared;
	observation.withoutClassicTppMatching->final = {{Labor, OneNation}, .53, {}};
	observation.reducedLaborToGreens = CountResult{};
	summary.record(observation, 3);
	assert(summary.official == 1 && summary.infeasible == 2 && summary.compared == 1);
	assert(summary.firstIssue->iteration == 1 && summary.classicPairs == 1);
	assert(summary.changedPair == 1 && summary.changedWinner == 1 && summary.changedWinnerWithLiveEvidence == 0);
	assert(summary.livePairMatches == 1);
	assert((summary.pairs.at({Labor, Liberal}) == std::pair<std::uint64_t, std::uint64_t>{1, 0}));
	assert((summary.winners.at(Labor) == std::pair<std::uint64_t, std::uint64_t>{1, 1}));
	assert(summary.standardVsRanking.compared == 1 && summary.standardVsRanking.changedPair == 1);
	assert(summary.reducedLaborToGreens.compared == 0 && summary.reducedLaborToGreens.failed == 1);
	auto const& trial = summary.withoutClassicTppMatching;
	assert(trial.compared == 1 && trial.changedPair == 0 && trial.changedWinner == 1);
	auto const& samePair = trial.pairs.at({Labor, OneNation});
	assert(samePair.outcomes[0].draws == 1 && samePair.outcomes[0].firstWins == 0);
	assert(samePair.outcomes[1].draws == 1 && samePair.outcomes[1].firstWins == 1);
	assert(samePair.samePairDraws == 1 && samePair.samePairWinnerChanges == 1);
	assert(close(samePair.samePairShareChange, .05) && close(samePair.samePairAbsoluteChange, .05));
}

}

void checkLivePreferenceLearning()
{
	using namespace LivePreferenceLearning;
	Pair const pair{Labor, OneNation};
	LivePartialCount::Counts fp{{Labor,300},{Liberal,200},{Greens,100},{OneNation,400}};
	LivePartialCount::Flows rates{{Labor,1},{Liberal,.3},{Greens,.8},{OneNation,0}};
	auto neutral = observedOffset(fp,{{Labor,440},{OneNation,560}},rates,pair);
	assert(neutral && std::abs(*neutral) < .01);
	auto shift = observedOffset(fp,{{Labor,470},{OneNation,530}},rates,pair);
	assert(shift && *shift > .4);
	assert(!observedOffset(fp,{{Labor,470},{OneNation,430}},rates,pair));

	Districts observations;
	std::array const seatTypes{0,0,0,0,0,0};
	for (int seat : {1,2,3,4}) {
		for (int booth = 0; booth < 12; ++booth) observations[seat][Ordinary].add(.2,600);
		observations[seat][6].add(.6,1600);
	}
	// The target's early measurement can update its own projection, but it
	// never contributes to the shared ordinary estimate or early contrast.
	auto heldOut = estimate(observations,1,6,seatTypes);
	observations[1][6].add(-.9,2000);
	auto withLocal = estimate(observations,1,6,seatTypes);
	assert(close(heldOut.ordinary,withLocal.ordinary) && close(heldOut.category,withLocal.category));
	assert(withLocal.local < heldOut.local);
	assert(heldOut.ordinary > 0 && heldOut.category > 0);
	auto unsupportedCategory = estimate(observations,5,8,seatTypes);
	assert(unsupportedCategory.ordinary > 0 && unsupportedCategory.category == 0);
	assert(close(transferRate(.42,.55,{},0),.42));
	assert(close(transferRate(1,.55,heldOut,1),1));
	assert(close(transferRate(0,.55,heldOut,1),0));
	auto matched = LivePartialCount::measure(fp,{{Labor,440},{OneNation,560}},
		rates,Labor,OneNation,true);
	auto newer = fp;
	newer.at(Liberal) += 100;
	auto pending = pendingPrimaries(newer,matched,pair,1000,false);
	assert(pending.size() == 1 && pending.at(Liberal) == 100);
	auto revised = pendingPrimaries(newer,matched,pair,1000,true);
	assert(revised.size() == 2 && !revised.contains(Labor) && !revised.contains(OneNation));
	assert(close(revised.at(Liberal)+revised.at(Greens),100));
	assert(pendingPrimaries(fp,matched,pair,1000,false).empty());

	auto ranking = model(false);
	std::array const ballot{Source{Labor,.43,2,1.4,1},Source{OneNation,.38,4,1.4,0},
		Source{Greens,.19,1,1.4,.8}};
	auto const draw = ranking.drawElection(42);
	auto original = ranking.count(0,ballot,.5,.5,draw);
	auto live = ranking.count(0,ballot,.5,.5,draw,Sensitivity::None,true);
	assert(original.status == Status::Compared && live.status == Status::Compared);
	assert(original.final.candidates == pair && original.final.firstShare == live.final.firstShare);
	double first = 0;
	for (auto const& source : live.finalSources) first += source.primary * source.firstRate;
	assert(close(first,live.final.firstShare));
	auto reference = ranking.referencePairRates(0,ballot,pair);
	assert(close(reference.at(Labor),1) && close(reference.at(OneNation),0));
	assert(reference.at(Greens) > 0 && reference.at(Greens) < 1);
}

void checkLivePreferenceGeography()
{
	using namespace LivePreferenceLearning;
	constexpr int Early = 6, Target = 4;
	std::array const seatTypes{0,1,2,3,3};
	std::array const sameType{3,3,3,3,3};
	std::array const ordinaryOffsets{.8,.4,.1,-.2};
	Districts observations;
	for (int booth = 0; booth < 4; ++booth) {
		for (int district = 0; district < Target; ++district) {
			observations[district][Ordinary].add(ordinaryOffsets[district],400);
		}
		observations[0][Early].add(.4,400);
		observations[3][Early].add(.2,400);
	}
	// All districts start at half an effective observation. Only two have
	// early counts, so the ordinary and early weights are restored separately.
	auto const pooled = estimate(observations,Target,Early,sameType);
	auto const geographic = estimate(observations,Target,Early,seatTypes);
	double const ordinaryAverage = (.343*.8 + .49*.4 + .7*.1 - .2) / (.343 + .49 + .7 + 1);
	double const earlyAverage = (.343*(-.4) + .4) / (.343 + 1);
	assert(close(geographic.ordinary,ordinaryAverage * 2 / 4));
	assert(close(geographic.category,earlyAverage / 3));
	assert(geographic.otherDistrictWeight == pooled.otherDistrictWeight);
	assert(geographic.categoryDistrictWeight == pooled.categoryDistrictWeight);
	assert(geographic.retainedPriorSpread == pooled.retainedPriorSpread);

	// Two equally distant donors retain their full combined influence even
	// when the target's own seat type has supplied no comparisons.
	observations.erase(2);
	observations.erase(3);
	std::array const equallyDistant{0,0,2,3,3};
	auto const distant = estimate(observations,Target,Early,equallyDistant);
	auto const same = estimate(observations,Target,Early,sameType);
	assert(close(distant.ordinary,same.ordinary) && close(distant.category,same.category));
	assert(distant.retainedPriorSpread == same.retainedPriorSpread);
}

int main()
{
	checkConfiguration();
	checkExtraCandidatesAgainstEnumeratedOrders();
	checkClassicReconciliation();
	checkUnadjustedRatesInNonClassicFinal();
	checkPrahranSensitivity();
	checkFittedNonClassicPair();
	checkLiberalOneNationAdvice();
	checkBothCoalitionPartners();
	checkSampledProfilesAndOrderIndependence();
	checkNamedIndependentPlacement();
	checkSecondaryIndependentTransfers();
	checkLaborPreferencesToRightIndependent();
	checkLaborPreferenceOverlapAtExclusion();
	checkSouthAustralianBallot();
	checkHistoricalFinnissTransfers();
	checkLivePreferenceCountRevisions();
	checkLivePreferenceCountConditioning();
	checkFederalComparisonAndLiveTrialConfiguration();
	checkJointThreeCandidateCount();
	checkJointThreeCandidateExtraFinalist();
	checkJointThreeCandidateCoalitionCount();
	checkComparisonSummary();
	checkLivePreferenceLearning();
	checkLivePreferenceGeography();
}
