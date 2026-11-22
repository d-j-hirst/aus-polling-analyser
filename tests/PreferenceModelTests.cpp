// Verify the portable preference comparison with the actual Victorian
// parameter file and districts whose vote arithmetic can be checked.
#include "../PreferenceModel.h"
#include "../LivePreferenceLearning.h"
#include "../LiveStatewidePreferences.h"
#include "../LiveFpTpp.h"
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
	{National, {{{Independent, Labor}, 80.0f}, {{Greens, Labor}, 25.0f}, {{OneNation, Labor}, 82.94f}}}
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

void checkLiberalProxyForNationals()
{
	std::pair const toOneNation{OneNation, Labor};
	std::pair const toLabor{Labor, OneNation};
	ConfiguredFlows configured{
		{Liberal, {{toOneNation, 63.0f}, {toLabor, 37.0f}, {{Greens, Labor}, 24.0f}}}
	};
	auto const liberalOnly = configured;
	applyLiberalProxyForNationals(configured, Liberal, -1, Labor, OneNation);
	applyLiberalProxyForNationals(configured, Liberal, National, Labor, -1);
	assert(configured == liberalOnly);

	applyLiberalProxyForNationals(configured, Liberal, National, Labor, OneNation);
	auto const& national = configured.at(National);
	assert(national.size() == 2);
	assert(national.at(toOneNation) == 63.0f && national.at(toLabor) == 37.0f);
	assert(configured.at(Liberal) == liberalOnly.at(Liberal));

	configured[National] = {{toOneNation, 82.94f}, {toLabor, 17.06f}};
	auto const explicitRates = configured;
	applyLiberalProxyForNationals(configured, Liberal, National, Labor, OneNation);
	assert(configured == explicitRates);

	ConfiguredFlows otherPair{{Liberal, {{{Greens, Labor}, 24.0f}}}};
	auto const noProxy = otherPair;
	applyLiberalProxyForNationals(otherPair, Liberal, National, Labor, OneNation);
	assert(otherPair == noProxy);
}

void checkIndependentsReceivingGroupedOthers()
{
	using IndependentCandidacy::OtherCandidateRoster;
	using IndependentCandidacy::othersPreferenceWeight;
	OtherCandidateRoster const roster{5,3}; // OTH**** and IND**
	assert(roster.remaining(1,false) == 7 && roster.remaining(2,true) == 5);
	assert(roster.remaining(4,false) == 5);
	assert(close(othersPreferenceWeight(3,30,2),.5)); // 20% of the group is easier.
	assert(close(othersPreferenceWeight(2,30,6),.5)); // 80% of an even share is easier.
	assert(othersPreferenceWeight(4,30,6) == 1 && othersPreferenceWeight(6,30,2) == 1);
	assert(othersPreferenceWeight(0,30,0) == 0 && othersPreferenceWeight(0,0,0) == 1);
	assert(close(othersPreferenceWeight(3,30,0),.5));
	// In a legacy round reduce a weak independent even if they have
	// accumulated transfers. Redistribute all continuing ballots, including
	// when two affected independents are the only remaining candidates.
	std::vector<std::pair<int,double>> remaining{{Independent,45.},{EmergingIndependent,55.}};
	std::vector<double> weights{60.,40.};
	double total = IndependentCandidacy::adjustLegacyOthersWeights(remaining,weights,
		{{Independent,.5},{EmergingIndependent,.25}},100.);
	assert(close(weights[0]/total,.75) && close(weights[1]/total,.25));
	assert(IndependentCandidacy::adjustLegacyOthersWeights(remaining,weights,{{Greens,.1}},total) == total);

	auto settings = syntheticConfiguration();
	settings["sample_uncertainty"] = false;
	std::istringstream input(settings.dump());
	auto seats = districts;
	seats[0].otherCandidates = roster;
	auto ranking = Model::load(input, roles, seats, flows);
	assert(ranking);
	constexpr int secondary = 1001;
	std::vector<Source> ballot{{Labor,.30,1,2,1},{Liberal,.32,3,2,0},{Greens,.08,0,2,.8},
		{Independent,.05,2,2,.5},{secondary,.05,2,2,.5,true},{Others,.20,2,2,.5}};
	std::array const recipients{Labor,Liberal,Independent,secondary};
	auto splits = [&]() { return ranking->referenceTransferRates(0,ballot,recipients,true); };
	auto const full = splits();
	ballot[0].primary += .02;
	ballot[3].primary -= .02; // 3% is above the 2.67% threshold (six in the pool).
	auto const qualified = splits();
	assert(full.at(Others) == qualified.at(Others));
	ballot[0].primary += .02999;
	ballot[3].primary -= .02999;
	auto const weak = splits();
	auto const& others = weak.at(Others);
	assert(others[2] < .001 && others[3] >= full.at(Others)[3]);
	assert(close(std::accumulate(others.begin(),others.end(),0.),1.));
	assert(weak.at(Greens) == full.at(Greens));
	std::swap(ballot[3].primary,ballot[4].primary);
	auto const swapped = splits().at(Others);
	assert(close(others[2],swapped[3]) && close(others[3],swapped[2]));
	auto classic = ranking->referencePairRates(0,ballot,{Labor,Liberal},true);
	assert(close(classic.at(Others),.5));
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
	// Each conditional Labor TCP uses its own excluded partner. Either one
	// can be the fixed starting margin, including when its party has fewer FP.
	for (bool nationalReference : {false,true}) {
		auto rates = CoalitionPreference::Leakage{.10,.20,nationalReference};
		double tpp = .18+.06+rates.reference(.37,.34);
		auto result = prepared.count(0,fp,tpp,.5,prepared.drawElection(2),
			Sensitivity::None,false,true,nullptr,{},rates);
		assert(result.status == Status::Compared);
		assert(result.classic.pairsChecked == 2 && result.classic.maximumError < Tolerance);
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

void checkGreensPreferencesToMultipleIndependents()
{
	constexpr int DistrictWithoutNamedIndependent = 1;
	constexpr int Secondary = 214003, Minor = 8;
	auto ranking = model(false);
	std::array ballot{
		Source{Labor, .30, 1, 1.8, 1.0}, Source{Liberal, .25, 3, 1.4, 0.0},
		Source{Greens, .10, 0, 1.8, .85}, Source{Independent, .12, 2, 1.4, .50, false, true},
		Source{Secondary, .11, 2, 1.2, .50, true}, Source{Minor, .07, 1, 1.2, .65},
		Source{Others, .05, 2, 1.2, .50}};
	std::array const survivors{Labor, Liberal, Independent, Secondary, Minor};
	auto rates = ranking.referenceTransferRates(DistrictWithoutNamedIndependent, ballot, survivors).at(Greens);
	assert(close(rates[2], rates[3]));
	assert(rates[4] > rates[2]);

	// Two configured independent slots use the same rule as a second live candidate.
	ballot[4].id = EmergingIndependent;
	ballot[4].additionalIndependent = false;
	ballot[4].confirmedIndependent = true;
	std::array const configuredSurvivors{Labor, Liberal, Independent, EmergingIndependent, Minor};
	auto configuredRates = ranking.referenceTransferRates(DistrictWithoutNamedIndependent, ballot, configuredSurvivors).at(Greens);
	assert(configuredRates == rates);

	// An unused slot leaves the configured sole-independent comparison in place.
	ballot.back().primary += ballot[4].primary;
	ballot[4].primary = 0.0;
	auto singleRates = ranking.referencePairRates(DistrictWithoutNamedIndependent, ballot, {Independent, Liberal});
	assert(close(singleRates.at(Greens), .85));
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

void checkVictorianReplaySettings()
{
	// The 2022 Liberal ticket put Greens ahead of Labor. Copying the 2026
	// profile would reverse that comparison and import evidence from later counts.
	std::ifstream input("analysis/Preferences/2022vic.json");
	assert(input);
	ConfiguredFlows configured;
	constexpr int vicNational = 4;
	constexpr Roles vicRoles{Labor, Liberal, vicNational, Greens, std::nullopt, Independent, EmergingIndependent, Others};
	configured[vicNational][{Greens, Labor}] = 75.0f;
	std::array const seats{District{"Melbourne"}, District{"Morwell"}};
	auto const model = Model::load(input, vicRoles, seats, configured);
	assert(model && model->isActive() && !model->isLiveTrial() && model->coversDistrict(1));
	std::array<Source,6> const ballot{{
		{Labor,.32,1,1.4,1.}, {Liberal,.24,3,1.4,0.}, {Greens,.23,0,1.8,.8194},
		{vicNational,.10,3,1.4,.1}, {Independent,.07,2,1.,.5}, {Others,.04,2,1.,.5}}};
	auto const toGreens = model->referencePairRates(0, ballot, {Greens,Labor});
	assert(close(toGreens.at(Liberal), .75) && close(toGreens.at(vicNational), .75));
	auto classic = model->referencePairRates(0, ballot, {Labor,Liberal});
	assert(close(classic.at(Greens), .8194));
	std::vector<Model::ReportFlow> expected{{Greens,Labor,Liberal}, {Liberal,Labor,Greens}};
	assert(model->liveReportFlows() && *model->liveReportFlows() == expected);
	assert(LiveStatewidePreferences::reportRows(vicRoles, true) == expected);
	input.clear();
	input.seekg(0);
	auto settings = nlohmann::json::parse(input);
	// A missing party and a repeated comparison with reversed opponents
	// both indicate a mistake in the election's publication settings.
	for (auto const& invalid : {nlohmann::json{{"GRN","ALP","ON"}},
		nlohmann::json{{"GRN","ALP","LIB"},{"GRN","LIB","ALP"}}}) {
		settings["live_report_flows"] = invalid;
		std::istringstream bad(settings.dump());
		bool rejected = false;
		try { Model::load(bad, vicRoles, seats, configured); }
		catch (std::invalid_argument const&) { rejected = true; }
		assert(rejected);
	}
	for (int seed = 0; seed < 12; ++seed) {
		auto result = model->count(1, ballot, .55, .25, model->drawElection(seed));
		assert(result.status == Status::Compared && result.classic.maximumError < Tolerance);
	}
}

void checkFederalPreferenceModes()
{
	std::ifstream input("analysis/Preferences/2025fed.json");
	assert(input);
	auto settings = nlohmann::json::parse(input);
	std::array const fedDistricts{District{"Forrest", "Sue Chapman", true},
		District{"Ryan", {}, false}, District{"Menzies", {}, false}, District{"Blaxland"}, District{"Fisher"},
		District{"Flinders"}, District{"Grey"}, District{"Hunter"}, District{"Maranoa"}, District{"Mayo"},
		District{"Monash"}, District{"Richmond"}, District{"Watson"}};
	auto federalFlows = flows;
	federalFlows[National].erase({OneNation, Labor});
	federalFlows[Liberal][{OneNation, Labor}] = 70.0f;
	federalFlows[Liberal][{Labor, OneNation}] = 30.0f;
	applyLiberalProxyForNationals(federalFlows, Liberal, National, Labor, OneNation);
	std::istringstream activeInput(settings.dump());
	auto const active = Model::load(activeInput, roles, fedDistricts, federalFlows);
	assert(active && active->isActive() && !active->isLiveTrial());

	settings["mode"] = "compare";
	std::istringstream comparisonInput(settings.dump());
	auto const comparison = Model::load(comparisonInput, roles, fedDistricts, federalFlows);
	assert(comparison && !comparison->isActive() && !comparison->isLiveTrial());
	for (int district = 0; district < static_cast<int>(fedDistricts.size()); ++district) {
		assert(active->coversDistrict(district));
		assert(comparison->coversDistrict(district));
	}

	settings["mode"] = "live_trial";
	std::istringstream trialInput(settings.dump());
	auto const ranking = Model::load(trialInput, roles, fedDistricts, federalFlows);
	assert(ranking && ranking->isLiveTrial() && ranking->coversDistrict(0) &&
		ranking->coversDistrict(1) && !ranking->coversDistrict(2));
	std::array const ballot{
		Source{Labor, .226, 1, 1.8, 1}, Source{Liberal, .311, 3, 1.4, 0},
		Source{Independent, .181, 2, 1.4, .7}, Source{Greens, .079, 0, 1.8, .85},
		Source{National, .048, 3, 1.8, .15}, Source{OneNation, .087, 4, 1.2, .19},
		Source{Others, .068, 2, 1.4, .48}};
	auto const oneNationOverLabor = active->referencePairRates(0, ballot, {OneNation, Labor});
	assert(close(oneNationOverLabor.at(Liberal), .70) && close(oneNationOverLabor.at(National), .70));
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

void checkFederalPreferenceSettings()
{
	constexpr int CentreAlliance = 5;
	constexpr int KatterParty = 9;
	std::ifstream input("analysis/Preferences/2028fed.json");
	assert(input);
	auto settings = nlohmann::json::parse(input);
	auto federalFlows = flows;
	federalFlows[Labor][{CentreAlliance, Liberal}] = 80.0f;
	auto const loadFederal = [&](bool mayoMarked = true, bool withBlend = false) {
		auto configuration = settings;
		if (!withBlend) configuration.erase("pair_history_blend");
		std::istringstream configurationInput(configuration.dump());
		std::vector<int> independentLikeParties;
		if (mayoMarked) independentLikeParties.push_back(CentreAlliance);
		std::vector<District> federalDistricts{
			{"Wills"}, {"Bendigo"},
			{"Mayo", {}, false, independentLikeParties, District::NamedIncumbent{CentreAlliance, "Rebekha Sharkie"}},
			{"Fowler", "Dai Le", false, {}, District::NamedIncumbent{Independent, "Dai Le"}}};
		auto const& configuredDistricts = settings.at("districts");
		for (auto district = configuredDistricts.begin(); district != configuredDistricts.end(); ++district) {
			auto const& districtName = district.key();
			if (districtName == "Wills" || districtName == "Mayo" || districtName == "Fowler") continue;
			if (district->contains("incumbent_pair_history")) {
				std::string const name = district->at("incumbent_pair_history").at("name");
				int const party = districtName == "Kennedy" ? KatterParty : districtName == "Ryan" ? Greens : Independent;
				federalDistricts.push_back({districtName, party == Independent ? name : "", false, {},
					District::NamedIncumbent{party, name}});
			}
			else federalDistricts.push_back({districtName});
		}
		// This fixture tests candidate history in a synthetic all-metropolitan
		// roster. The mixed-geography centres are checked separately below.
		for (auto& district : federalDistricts) district.regional = false;
		return Model::load(configurationInput, roles, federalDistricts, federalFlows,
			{{"CA", CentreAlliance}, {"KAP", KatterParty}});
	};
	auto const ranking = loadFederal();
	// The maintained 2028 forecast publishes the existing count while the
	// ranking count is reviewed on identical draws in Standard and Now-cast.
	assert(ranking && !ranking->isActive() && !ranking->isLiveTrial());
	assert(ranking->coversDistrict(0) && ranking->coversDistrict(1));

	// Check the advice-sensitive Labor–Greens split and the separately
	// maintained preferences in the other two-candidate contests.
	std::array const ballot{
		Source{Labor, .27, 1, 1.8, 1}, Source{Liberal, .21, 3, 1.4, 0},
		Source{National, .10, 3, 1.8, .15}, Source{Greens, .16, 0, 1.8, .85},
		Source{OneNation, .20, 4, 1.2, .27}, Source{Others, .06, 2, 1.4, .50}};
	constexpr double laborCentre = .8 * .7220215503849514 + .2 * .30;
	auto const laborGreens = ranking->referencePairRates(0, ballot, {Labor, Greens});
	assert(close(laborGreens.at(Liberal), laborCentre) && close(laborGreens.at(National), laborCentre));
	auto const laborOneNation = ranking->referencePairRates(0, ballot, {Labor, OneNation});
	assert(close(laborOneNation.at(Liberal), .30));
	assert(close(laborOneNation.at(National), 1.0 - double(82.94f) * .01));
	auto const liberalGreens = ranking->referencePairRates(0, ballot, {Liberal, Greens});
	assert(close(liberalGreens.at(Labor), 1.0 - .8203275332737616));

	// The previous Wills result for Others supplies its central
	// split against Labor. It gives no new information on the classic split.
	auto const willsOthers = ranking->referencePairRates(0, ballot, {Greens, Labor});
	auto const genericOthers = ranking->referencePairRates(1, ballot, {Greens, Labor});
	assert(close(willsOthers.at(Others), 10005.0 / 13915.0));
	assert(close(genericOthers.at(Others), .50));
	auto const willsClassic = ranking->referencePairRates(0, ballot, {Labor, Liberal});
	assert(close(willsClassic.at(Others), .50));
	auto const wills = ranking->count(0, ballot, .51, .5, ranking->drawElection(31));
	assert(wills.status == Status::Compared && wills.classic.maximumError < Tolerance);

	// A Liberal ballot can rank Le ahead of One Nation even if it also
	// ranks One Nation ahead of Labor. Keep the existing pair constraints.
	std::array const fowlerBallot{
		Source{Labor, .2662, 1, 1.8, 1}, Source{Liberal, .1283, 3, 1.4, 0},
		Source{Greens, .0670, 0, 1.8, .8693}, Source{Independent, .25, 2, 1.4, .5824},
		Source{OneNation, .2396, 4, 1.2, .4177}, Source{Others, .0489, 2, 1.2, .6075}};
	std::array const fowlerFinalThree{Independent, Labor, OneNation};
	auto const fowlerTransfers = ranking->referenceTransferRates(3, fowlerBallot, fowlerFinalThree);
	auto const fowlerLabor = ranking->referencePairRates(3, fowlerBallot, {Independent, Labor});
	auto const fowlerOneNation = ranking->referencePairRates(3, fowlerBallot, {Labor, OneNation});
	assert(close(fowlerLabor.at(Liberal), 8082.0 / 11404.0));
	assert(close(fowlerLabor.at(Greens), 2556.0 / 6288.0));
	assert(close(fowlerLabor.at(OneNation), 2882.0 / 3835.0));
	assert(close(fowlerOneNation.at(Liberal), .30));
	assert(fowlerTransfers.at(Liberal).front() > fowlerOneNation.at(Liberal));
	auto fowlerDraw = ranking->drawElection(1);
	fowlerDraw.central = true;
	for (bool laborFirst : {true, false}) {
		fowlerDraw.liberalLaborFirst = laborFirst;
		auto const fowler = ranking->count(3, fowlerBallot, .59, .5, fowlerDraw);
		assert(fowler.status == Status::Compared && fowler.classic.maximumError < Tolerance);
		assert((fowler.final.candidates == Pair{Labor, Independent}));
	}

	// Published final-pair flows can follow an independent, a Green or
	// a minor-party incumbent. The same candidate must be present, even
	// when a replacement could otherwise use the same party slot.
	auto districtIndex = [&](std::string_view name) {
		int index = 4;
		for (auto const& [seatName, district] : settings.at("districts").items()) {
			if (seatName == "Wills" || seatName == "Mayo" || seatName == "Fowler") continue;
			if (seatName == name) return index;
			++index;
		}
		throw std::invalid_argument("Missing district in preference test.");
	};
	std::array const incumbentBallot{
		Source{Labor, .18, 1, 1.8, 1}, Source{Liberal, .03, 3, 1.4, 0}, Source{National, .31, 3, 1.4, 0},
		Source{Independent, .30, 2, 1.4, .65}, Source{Greens, .06, 0, 1.8, .85},
		Source{OneNation, .10, 4, 1.2, .25}, Source{Others, .02, 2, 1.4, .5}};
	int const calare = districtIndex("Calare");
	auto const gee = ranking->referencePairRates(calare, incumbentBallot, {Independent, National});
	auto const geeLiberal = ranking->referencePairRates(calare, incumbentBallot, {Independent, Liberal});
	assert(close(gee.at(Labor), 9121.0 / 11086.0));
	assert(close(gee.at(Greens), 3109.0 / 3753.0));
	assert(close(gee.at(OneNation), 3601.0 / 8200.0));
	assert(!close(gee.at(OneNation), geeLiberal.at(OneNation)));
	auto nationalOnly = incumbentBallot;
	nationalOnly[2].primary += nationalOnly[1].primary;
	nationalOnly[1].primary = 0;
	assert(close(ranking->referencePairRates(calare, nationalOnly, {Independent, National}).at(OneNation),
		gee.at(OneNation)));
	for (int seed = 0; seed < 20; ++seed) {
		auto const calareCount = ranking->count(calare, incumbentBallot, .46, .73, ranking->drawElection(seed));
		assert(calareCount.status == Status::Compared && calareCount.classic.pairsChecked == 2 &&
			calareCount.classic.maximumError < Tolerance);
	}

	auto namedPartyBallot = nationalOnly;
	namedPartyBallot[1].primary = nationalOnly[2].primary;
	namedPartyBallot[2].primary = 0;
	namedPartyBallot[3].id = KatterParty;
	int const kennedy = districtIndex("Kennedy");
	auto const katter = ranking->referencePairRates(kennedy, namedPartyBallot, {KatterParty, Liberal}, false);
	assert(close(katter.at(Labor), 11971.0 / 15917.0));
	assert(close(katter.at(Greens), 3425.0 / 5761.0));
	assert(close(katter.at(OneNation), 5542.0 / 7541.0));
	namedPartyBallot[3].primary = 0;
	namedPartyBallot[4].primary += incumbentBallot[3].primary;
	int const ryan = districtIndex("Ryan");
	auto const watsonBrown = ranking->referencePairRates(ryan, namedPartyBallot, {Greens, Liberal}, false);
	assert(close(watsonBrown.at(Labor), 22590.0 / 29217.0));
	assert(close(watsonBrown.at(OneNation), 415.0 / 2327.0));
	assert(close(ranking->referencePairRates(ryan, namedPartyBallot, {Labor, Liberal}).at(OneNation), .25));
	namedPartyBallot[4].matchesConfiguredIncumbent = false;
	auto const replacement = ranking->referencePairRates(ryan, namedPartyBallot, {Greens, Liberal});
	assert(!close(replacement.at(Labor), watsonBrown.at(Labor)));

	int const clark = districtIndex("Clark");
	auto const wilkie = ranking->referencePairRates(clark, incumbentBallot, {Independent, Labor});
	assert(close(wilkie.at(Greens), 6417.0 / 8908.0));
	assert(close(wilkie.at(Liberal), 6197.0 / 9239.0));
	assert(close(wilkie.at(OneNation), 1906.0 / 2834.0));
	int const bradfield = districtIndex("Bradfield");
	assert(!ranking->incumbentForPairHistory(0));
	assert(ranking->incumbentForPairHistory(bradfield)->name == "Nicolette Boele");
	auto bradfieldBallot = incumbentBallot;
	bradfieldBallot[1].primary += bradfieldBallot[2].primary;
	bradfieldBallot[2].primary = 0;
	assert(close(ranking->referencePairRates(bradfield, bradfieldBallot, {Independent, Liberal}).at(Labor),
		15626.0 / 22768.0));
	auto& clarkHistory = settings["districts"]["Clark"]["incumbent_pair_history"];
	auto const originalClarkHistory = clarkHistory;
	clarkHistory["flows"][0]["donor"] = "IND";
	try {
		loadFederal();
		assert(false);
	}
	catch (std::invalid_argument const&) {}
	clarkHistory = originalClarkHistory;

	// An exact primary match uses the historical centre. At three times
	// the old odds, the general and historical ballots have equal weight.
	// Both groups keep their original Labor–Coalition comparison.
	auto const weighted = loadFederal(true, true);
	int const mackellar = districtIndex("Mackellar");
	auto mackellarBallot = nationalOnly;
	mackellarBallot[1].primary = nationalOnly[2].primary;
	mackellarBallot[2].primary = 0.;
	auto const halfWeightOdds = settings["pair_history_blend"]["half_weight_odds_ratio"];
	double const oldOn = 2825.0 / 111847.0;
	auto ratesAt = [&](double p, Model const& model) {
		mackellarBallot[1].primary += mackellarBallot[5].primary - p;
		mackellarBallot[5].primary = p;
		return model.referencePairRates(mackellar, mackellarBallot, {Independent, Liberal}).at(OneNation);
	};
	double const observedOn = 754.0 / 2825.0;
	assert(close(ratesAt(oldOn, *weighted), observedOn));
	double const tripleOddsShare = 3.0 * oldOn / (1.0 + 2.0 * oldOn);
	settings["pair_history_blend"]["half_weight_odds_ratio"] = 1.0001;
	auto const nearZeroWeight = loadFederal(true, true);
	double const fallbackOn = ratesAt(tripleOddsShare, *nearZeroWeight);
	assert(!close(fallbackOn, observedOn));
	assert(close(ratesAt(tripleOddsShare, *weighted), std::midpoint(fallbackOn, observedOn)));
	settings["pair_history_blend"]["half_weight_odds_ratio"] = halfWeightOdds;
	assert(close(weighted->referencePairRates(mackellar, mackellarBallot, {Independent, OneNation}).at(Labor), .80));
	for (int seed = 0; seed < 20; ++seed) {
		auto const draw = weighted->drawElection(seed);
		auto const mack = weighted->count(mackellar, mackellarBallot, .46, .5, draw);
		auto const dual = weighted->count(calare, incumbentBallot, .46, .73, draw);
		assert(mack.status == Status::Compared && mack.classic.maximumError < Tolerance);
		assert(dual.status == Status::Compared && dual.classic.pairsChecked == 2 &&
			dual.classic.maximumError < Tolerance);
	}

	// A district Liberal card changes only Liberal ballots.
	settings["districts"]["Wills"]["liberal_advice"] = "greens_first";
	auto const localAdvice = loadFederal()->referencePairRates(0, ballot, {Labor, Greens});
	assert(close(localAdvice.at(Liberal), .30) && close(localAdvice.at(National), laborCentre));

	settings.erase("national_follows_election_liberal_advice");
	auto const separateAdvice = loadFederal()->referencePairRates(0, ballot, {Labor, Greens});
	assert(close(separateAdvice.at(National), .75));

	// Mayo still counts Centre Alliance as its own party. Its recorded
	// comparison rates apply to the specified opponents, and Greens ballots
	// still rank Labor against Liberal as they would without Centre Alliance.
	std::array const mayoBallot{
		Source{Labor, .22, 1, 1.8, 1}, Source{Liberal, .26, 3, 1.4, 0},
		Source{Greens, .14, 0, 1.8, .9}, Source{OneNation, .10, 4, 1.2, .25},
		Source{CentreAlliance, .26, 2, 1.4, .6}, Source{Others, .02, 2, 1.4, .5}};
	auto const mayoLiberal = ranking->referencePairRates(2, mayoBallot, {CentreAlliance, Liberal});
	auto const mayoLabor = ranking->referencePairRates(2, mayoBallot, {CentreAlliance, Labor});
	auto const mayoClassic = ranking->referencePairRates(2, mayoBallot, {Labor, Liberal});
	assert(close(mayoLiberal.at(Labor), .80));
	assert(close(mayoLiberal.at(Greens), .8164));
	assert(close(mayoLabor.at(Greens), .4572));
	assert(close(mayoLabor.at(Liberal), 25692.0 / 29551.0));
	assert(close(mayoLabor.at(OneNation), 5458.0 / 7493.0));
	assert(close(mayoClassic.at(Greens), ranking->referencePairRates(1, mayoBallot, {Labor, Liberal}).at(Greens)));
	auto const mayo = ranking->count(2, mayoBallot, .537, .5, ranking->drawElection(0));
	assert(mayo.status == Status::Compared && mayo.classic.maximumError < Tolerance);

	try {
		loadFederal(false);
		assert(false);
	}
	catch (std::invalid_argument const&) {}

	// A different live independent cannot inherit the returning person's
	// preference evidence. The usual party-level Liberal prior still applies.
	auto const fowlerReplacement = ranking->referencePairRates(3, fowlerBallot, {Independent, Labor}, false);
	assert(close(fowlerReplacement.at(Liberal), .80));
	assert(!close(fowlerReplacement.at(OneNation), fowlerLabor.at(OneNation)));
	settings["districts"]["Fowler"]["independent_pair_history"]["name"] = "Different candidate";
	try {
		loadFederal();
		assert(false);
	}
	catch (std::invalid_argument const&) {}

	// Remove the local observations to test the underlying distance rule.
	// Against Labor, the independent is closer to One Nation voters. Move
	// the independent to Labor's position to check the equal-distance case.
	// Against Liberal, the same voters are closer to Liberal in both cases.
	settings["districts"]["Fowler"].erase("independent_pair_history");
	auto const withBoost = loadFederal();
	settings.erase("independent_proximity_odds_multiplier");
	auto const withoutBoost = loadFederal();
	auto genericBallot = fowlerBallot;
	for (int independentIdeology : {2, 1}) {
		genericBallot[3].ideology = independentIdeology;
		double const factor = independentIdeology == 2 ? 2.5 : std::sqrt(2.5);
		auto const before = withoutBoost->referencePairRates(3, genericBallot, {Independent, Labor});
		auto const after = withBoost->referencePairRates(3, genericBallot, {Independent, Labor});
		double const base = before.at(OneNation);
		assert(close(after.at(OneNation), base * factor / std::lerp(1.0, factor, base)));
		assert(close(after.at(Liberal), before.at(Liberal)));
		assert(close(after.at(Others), before.at(Others)));
		auto const beforeLiberal = withoutBoost->referencePairRates(3, genericBallot, {Independent, Liberal});
		auto const afterLiberal = withBoost->referencePairRates(3, genericBallot, {Independent, Liberal});
		assert(close(afterLiberal.at(OneNation), beforeLiberal.at(OneNation)));
		auto const beforeClassic = withoutBoost->referencePairRates(3, genericBallot, {Labor, Liberal});
		auto const afterClassic = withBoost->referencePairRates(3, genericBallot, {Labor, Liberal});
		assert(close(afterClassic.at(OneNation), beforeClassic.at(OneNation)));
	}
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
	assert((samePair.samePairFirstWins == std::array<std::uint64_t, 2>{0, 1}));
	assert((trial.winnerTransitions.at({OneNation, Labor}) == std::array<std::uint64_t, 2>{1, 0}));
	assert((summary.standardVsRanking.winnerTransitions.at({Labor, OneNation}) == std::array<std::uint64_t, 2>{0, 1}));
	// Pair/winner diagnostics are available without enabling extra sensitivity
	// counts. The pre-live winner differs from the live-integrated winner here.
	observation.withoutClassicTppMatching.reset();
	observation.reducedLaborToGreens.reset();
	summary.record(observation, 4);
	assert(summary.standardVsRanking.compared == 2 && summary.changedWinnerWithLiveEvidence == 0);
}

void checkSourceComparison()
{
	// Labor/ON, with the existing count combining Coalition ballots. The
	// Greens change alone reverses this final; the Coalition change alone does
	// not. Together they add 4 points to Labor's final share, not 4% more votes.
	Comparison observation;
	observation.reportSources = true;
	observation.combinedCoalitionSources = Pair{Liberal, National};
	observation.model.status = Status::Compared;
	observation.legacy = {{Labor, OneNation}, .485, {}};
	observation.model.final = {{Labor, OneNation}, .525, {}};
	observation.legacySources = {{Labor,.295,1}, {OneNation,.305,0}, {Liberal,.20,.20}, {Greens,.20,.75}};
	observation.model.finalSources = {{Labor,.295,1}, {OneNation,.305,0},
		{Liberal,.10,.30}, {National,.10,.20}, {Greens,.20,.90}};
	SourceComparisonSummary summary;
	summary.record(observation);
	auto const& pair = summary.pairs.at({Labor, OneNation});
	assert(pair.draws == 1 && pair.unavailable == 0);
	assert(pair.referenceFirstWins == 0 && pair.alternativeFirstWins == 1);
	auto const& coalition = pair.sources.at(Liberal);
	assert(!pair.sources.contains(National));
	assert(close(coalition.primarySum, .20) && close(coalition.rateSum[1], .25));
	assert(coalition.firstWinChangeOnlyThisSource == 0);
	assert(pair.sources.at(Greens).firstWinChangeOnlyThisSource == 1);
	double difference = 0.0;
	for (auto const& [source, values] : pair.sources) {
		(void)source;
		difference += values.firstVotesSum[1] - values.firstVotesSum[0];
	}
	assert(close(difference, .04) && close(difference, pair.tcpChangeSum));
	assert(pair.maximumAccountError < 1e-12);

	// Missing accounts and altered FP must be visible exclusions, not assigned
	// zero flow. A different finalist pair has no same-pair source comparison.
	auto unmatched = observation;
	unmatched.legacySources.clear();
	summary.record(unmatched);
	unmatched = observation;
	unmatched.model.finalSources[2].primary += .01;
	summary.record(unmatched);
	// A matching total and TCP are insufficient if source primaries differ.
	unmatched = observation;
	unmatched.model.finalSources[2].primary = .11;
	unmatched.model.finalSources[4].primary = .19;
	unmatched.model.finalSources[4].firstRate = .177 / .19;
	summary.record(unmatched);
	unmatched = observation;
	unmatched.model.final.candidates = {Labor, Liberal};
	summary.record(unmatched);
	assert(pair.draws == 1 && pair.unavailable == 3 && summary.pairs.size() == 1);

	// Reversing which candidate is first reverses the TCP contribution. This
	// also checks that winner counts are not hard-wired to Labor or One Nation.
	auto reversed = observation;
	reversed.legacy.firstShare = .515;
	reversed.model.final.firstShare = .475;
	for (auto& source : reversed.legacySources) source.firstRate = 1 - source.firstRate;
	for (auto& source : reversed.model.finalSources) source.firstRate = 1 - source.firstRate;
	SourceComparisonSummary reverseSummary;
	reverseSummary.record(reversed);
	auto const& reversePair = reverseSummary.pairs.begin()->second;
	assert(close(reversePair.tcpChangeSum, -.04));
	assert(reversePair.referenceFirstWins == 1 && reversePair.alternativeFirstWins == 0);
	assert(reversePair.sources.at(Greens).firstWinChangeOnlyThisSource == -1);
}

void checkSourceReportingDoesNotChangeCount()
{
	auto ranking = model(true);
	std::array const ballot{Source{Labor,.29,1,1.4,1}, Source{Liberal,.16,3,1.4,0},
		Source{National,.04,3,1.4,.15}, Source{Greens,.12,0,1.4,.85},
		Source{OneNation,.30,4,1.4,.27}, Source{Others,.09,2,1.4,.50}};
	int compared = 0;
	for (int iteration = 0; iteration < 100; ++iteration) {
		auto const draw = ranking.drawElection(static_cast<std::uint64_t>(iteration));
		auto const original = ranking.count(0, ballot, .51, .5, draw);
		auto const reported = ranking.count(0, ballot, .51, .5, draw, Sensitivity::None, true);
		assert(original.status == reported.status);
		assert(original.final.candidates == reported.final.candidates);
		assert(original.final.firstShare == reported.final.firstShare);
		assert(original.final.winner() == reported.final.winner());
		if (reported.status != Status::Compared) continue;
		++compared;
		double first = 0.0, total = 0.0;
		for (auto const& source : reported.finalSources) {
			assert(source.primary > 0 && std::isfinite(source.firstRate));
			first += source.primary * source.firstRate;
			total += source.primary;
		}
		assert(close(first, reported.final.firstShare) && close(total, 1.0));
	}
	assert(compared == 100);
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

void checkPreparedLiberalGeography()
{
	std::ifstream input("analysis/Preferences/2028fed.json");
	assert(input);
	auto settings = nlohmann::json::parse(input);
	assert(close(settings.at("liberal_one_nation_regional_gap").get<double>(), .10));
	settings["districts"] = nlohmann::json::object();
	settings["sample_uncertainty"] = false;
	std::vector<District> roster;
	for (int seat = 0; seat < 150; ++seat) {
		District district;
		district.name = std::to_string(seat);
		district.regional = seat < 63;
		roster.push_back(district);
	}
	auto configured = flows;
	configured[National][{OneNation, Labor}] = 82.94f;
	auto prepare = [&](nlohmann::json const& config) {
		std::istringstream stream(config.dump());
		return *Model::load(stream, roles, roster, configured);
	};
	constexpr int Regional = 0, Metro = 63;
	constexpr double metroRate = .70 - .10 * 63.0 / 150;
	constexpr double regionalRate = metroRate + .10;
	std::array const ballot{
		Source{Labor, .40, 1, 1.4, 1}, Source{Liberal, .20, 3, 1.4, 0},
		Source{OneNation, .40, 4, 1.2, .27}};
	std::array const allSources{
		Source{Labor, .27, 1, 1.8, 1}, Source{Liberal, .21, 3, 1.4, 0},
		Source{National, .10, 3, 1.8, .15}, Source{Greens, .16, 0, 1.8, .85},
		Source{OneNation, .20, 4, 1.2, .27}, Source{Others, .06, 2, 1.4, .50}};
	for (double advice : {0.0, .8, 1.0}) {
		settings["unknown_liberal_labor_first_probability"] = advice;
		auto baseSettings = settings;
		baseSettings.erase("liberal_one_nation_regional_gap");
		auto const base = prepare(baseSettings);
		auto const adjusted = prepare(settings);
		for (int district : {Regional, Metro}) {
			double const expected = district == Regional ? regionalRate : metroRate;
			auto const rates = adjusted.referencePairRates(district, allSources, {OneNation, Labor});
			assert(close(rates.at(Liberal), expected));
			assert(close(rates.at(National), double(82.94f) * .01));
			for (Pair const pair : {Pair{Labor, Greens}, Pair{Greens, OneNation},
				Pair{Labor, Liberal}, Pair{Labor, OneNation}}) {
				auto const before = base.referencePairRates(district, allSources, pair);
				auto const after = adjusted.referencePairRates(district, allSources, pair);
				for (auto const& [source, rate] : before) {
					if (source == Liberal && pair == Pair{Labor, OneNation}) continue;
					assert(close(rate, after.at(source)));
				}
			}
			auto const result = adjusted.count(district, ballot, .508, .5, adjusted.drawElection(1));
			assert(result.status == Status::Compared && (result.final.candidates == Pair{Labor, OneNation}));
			assert(close(result.final.firstShare, .4 + .2 * (1 - expected)));
		}
		assert(close((63 * regionalRate + 87 * metroRate) / 150, .70));
	}

	// Both uncertainty levels remain active, including their shared ordering
	// across seats. Sample actual counts so these checks exercise the forecast
	// path as well as the central reference used by live preference learning.
	settings["sample_uncertainty"] = true;
	settings["unknown_liberal_labor_first_probability"] = .8;
	auto const adjusted = prepare(settings);
	auto baseSettings = settings;
	baseSettings.erase("liberal_one_nation_regional_gap");
	auto const base = prepare(baseSettings);
	baseSettings["liberal_one_nation_regional_gap"] = 0.0;
	auto const zeroGap = prepare(baseSettings);
	std::array<double, 2> totals{}, squares{}, commonSquares{}, commonTotals{};
	constexpr int Draws = 8000;
	for (int seed = 0; seed < Draws; ++seed) {
		auto const draw = adjusted.drawElection(seed);
		for (int group = 0; group < 2; ++group) {
			int const district = group == 0 ? Metro : Regional;
			auto const count = adjusted.count(district, ballot, .508, .5, draw);
			assert(count.status == Status::Compared && (count.final.candidates == Pair{Labor, OneNation}));
			double const rate = (1 - count.final.firstShare - .4) / .2;
			assert(rate >= 0 && rate <= 1);
			totals[group] += rate;
			squares[group] += rate * rate;
			if (seed >= 120) continue;
			auto commonDraw = draw;
			commonDraw.sharedOnly = true;
			auto const common = adjusted.count(district, ballot, .508, .5, commonDraw);
			double const commonRate = (1 - common.final.firstShare - .4) / .2;
			commonTotals[group] += commonRate;
			commonSquares[group] += commonRate * commonRate;
			auto const unchanged = base.count(district, ballot, .508, .5, draw);
			auto const explicitZero = zeroGap.count(district, ballot, .508, .5, draw);
			assert(unchanged.final.firstShare == explicitZero.final.firstShare);
			// Changing Liberal geography cannot change a count with no Liberal
			// ballots. Check both the Nationals and the Greens as donors.
			for (Source const donor : {allSources[2], allSources[3]}) {
				auto otherBallot = ballot;
				otherBallot[1] = donor;
				otherBallot[1].primary = .2;
				double const tpp = .508 + .2 * donor.classicRate;
				auto const oldCount = base.count(district, otherBallot, tpp, .5, draw);
				auto const newCount = adjusted.count(district, otherBallot, tpp, .5, draw);
				assert(oldCount.status == Status::Compared && newCount.status == Status::Compared);
				assert(oldCount.final.firstShare == newCount.final.firstShare);
			}
		}
	}
	for (int group = 0; group < 2; ++group) {
		double const mean = totals[group] / Draws;
		double const variance = squares[group] / Draws - mean * mean;
		double const commonMean = commonTotals[group] / 120;
		double const commonVariance = commonSquares[group] / 120 - commonMean * commonMean;
		assert(std::abs(mean - (group == 0 ? metroRate : regionalRate)) < .005);
		assert(commonVariance > .001 && variance > commonVariance);
	}

	auto rejects = [&](nlohmann::json const& config) {
		bool rejected = false;
		try { prepare(config); }
		catch (std::invalid_argument const&) { rejected = true; }
		assert(rejected);
	};
	for (double gap : {-.1, 1.1, .9}) {
		auto invalid = settings;
		invalid["liberal_one_nation_regional_gap"] = gap;
		rejects(invalid);
	}
	roster.front().regional.reset();
	rejects(settings);
	prepare(baseSettings); // An absent/zero adjustment does not require geography.
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

void checkStatewideCountedPreferenceAccounts()
{
	namespace S = LiveStatewidePreferences;
	// Chaffey, 18:57:46 on SA election night. The feed identifies 41 of
	// the 60 transfers to Liberal, without identifying the 29 Labor ballots.
	S::Pair pair{Liberal,OneNation};
	S::Shares fp{{Labor,29},{Liberal,85},{OneNation,72},{Greens,11},{Others,20}};
	S::Shares tcp{{Liberal,126},{OneNation,91}};
	S::Contest contest{};
	contest.pair = pair;
	for (int source : {Labor,Greens,Others}) contest.rows.push_back({source,Liberal,OneNation});
	S::addUnit(contest,0,fp,fp,tcp,nullptr,0,0);
	S::Accounts account;
	S::Shares rates{{Labor,.68},{Greens,.85},{Others,.40}};
	S::addContest(contest,rates,33,account);
	double total = 0;
	for (auto const& [row,amount] : account) {
		assert(amount.donors == fp.at(row[0]));
		total += amount.first;
	}
	assert(std::abs(total-41.0) < 1e-8);
	auto oddsMultiplier = [&](int source) {
		auto const& amount = account.at({source,Liberal,OneNation});
		return (amount.first/(amount.donors-amount.first)) / (rates.at(source)/(1-rates.at(source)));
	};
	assert(close(oddsMultiplier(Labor),oddsMultiplier(Greens)));
	assert(close(oddsMultiplier(Labor),oddsMultiplier(Others)));

	// After realignment the original count still constrains its own pair.
	// The same batch cannot also use the updated projection as new ballots.
	LivePartialCount::TcpBasis matched{{fp.begin(),fp.end()},{tcp.begin(),tcp.end()},{}};
	S::Contest retained{};
	retained.pair = pair;
	retained.rows = contest.rows;
	S::addUnit(retained,0,fp,fp,{{Labor,107},{OneNation,110}},&matched,0,0);
	S::Accounts old;
	S::addContest(retained,rates,33,old);
	for (auto const& [row,amount] : account) assert(close(old.at(row).first,amount.first));

	// An exact single-source or source-labelled count stays fixed on every
	// draw. Outstanding ballots (including a subsequent batch) may still vary.
	S::Contest known{};
	known.pair = pair;
	known.rows = {{Labor,Liberal,OneNation},{Labor,OneNation,Liberal}};
	matched.transfers = {{Labor,{{Liberal,20},{OneNation,9}}},{Greens,{{Liberal,10},{OneNation,1}}},
		{Others,{{Liberal,11},{OneNation,9}}}};
	fp[Labor] += 20;
	tcp[Liberal] += 8;
	tcp[OneNation] += 2;
	S::addUnit(known,0,fp,fp,tcp,&matched,0,0);
	for (double rate : {.1,.9}) {
		rates[Labor] = rate;
		S::Accounts result;
		S::addContest(known,rates,33,result);
		auto const& forward = result.at({Labor,Liberal,OneNation});
		auto const& reverse = result.at({Labor,OneNation,Liberal});
		assert(close(forward.donors,49) && close(forward.first,28+10*rate));
		assert(close(forward.first+reverse.first,49));
	}
	// A zero reset is not another count. A larger replacement for the SAME
	// pair that reduces a finalist supersedes its earlier allocation.
	S::Contest reset{};
	reset.pair = pair;
	reset.rows = contest.rows;
	auto originalFp = S::Shares(matched.fp.begin(),matched.fp.end());
	S::addUnit(reset,0,originalFp,originalFp,{{Liberal,0},{OneNation,0}},&matched,0,0);
	assert(reset.categories.at(0).measuredFirst.at(Labor) == 20);
	S::Contest corrected{};
	corrected.pair = pair;
	corrected.rows = contest.rows;
	S::addUnit(corrected,0,originalFp,originalFp,{{Liberal,120},{OneNation,97}},&matched,0,0);
	S::Accounts revised;
	S::addContest(corrected,rates,33,revised);
	assert(std::abs(revised.at(contest.rows[0]).first+revised.at(contest.rows[1]).first+
		revised.at(contest.rows[2]).first-35) < 1e-8);
	// Where TCP arrives before FP, use the projected primary mix. A possible
	// discrepancy beyond that whole projection has no named source.
	S::Contest missingFp{};
	missingFp.pair = pair;
	missingFp.rows = {contest.rows[0]};
	S::addUnit(missingFp,0,{{Labor,10},{Liberal,12},{OneNation,8}}, {},
		{{Liberal,18},{OneNation,14}},nullptr,0,0);
	auto const& mixed = missingFp.categories.at(0).batches.front();
	assert(mixed.unknownOrigin == 2 && mixed.first == 6 && mixed.donors.at(Labor) == 10);
}

void checkStatewidePreferenceDraws()
{
	namespace S = LiveStatewidePreferences;
	S::Election inputs;
	inputs.classicReference = {{OneNation,.2},{Greens,.8}};
	S::District district;
	district.index = 0;
	district.sources = {{Labor,.35,2,2,1},{Liberal,.32,3,2,0},{Greens,.18,1,2,.8},{OneNation,.15,4,2,.2}};
	S::Pair const pair{Liberal,OneNation};
	S::Row const laborRow{Labor,Liberal,OneNation}, onRow{OneNation,Labor,Liberal};
	S::Contest nonClassic{}, classic{};
	nonClassic.pair = pair;
	nonClassic.rows = {laborRow};
	classic.pair = {Labor,Liberal};
	classic.classic = true;
	classic.rows = {onRow};
	S::addUnit(classic,0,{{OneNation,100}}, {},{},nullptr,0,0);
	S::addUnit(nonClassic,0,{{Labor,100}}, {},{},nullptr,0,0);
	district.nonClassicPairs = {pair};
	district.contests = {classic,nonClassic};
	inputs.districts.push_back(district);
	auto ranking = model(true);
	auto const draw = ranking.drawElection(41);
	// The actual ballot establishes the two possible recipients. A projected
	// zero for a recipient can still leave Labor voters to allocate, whereas
	// a similarly named projected candidate absent from the ballot is ineligible.
	std::array<S::Row,2> requested{laborRow,onRow};
	S::Shares projected{{Labor,50},{OneNation,50}};
	std::set<int> present{Labor,Liberal,OneNation};
	assert(S::eligibleContests(requested,present,projected).at(pair).front() == laborRow);
	present.erase(Liberal);
	assert(S::eligibleContests(requested,present,projected).empty());
	auto zeroRecipient = district.sources;
	zeroRecipient[1].primary = 0;
	zeroRecipient[0].primary += .32;
	auto ratesWithZero = ranking.sharedPairRates(0,zeroRecipient,district.nonClassicPairs,draw,true);
	assert(ratesWithZero.at(pair).at(Labor) > 0 && ratesWithZero.at(pair).at(Labor) < 1);
	assert(!ratesWithZero.at(pair).contains(Liberal));
	auto before = S::estimate(inputs,ranking,draw,{{OneNation,27}});
	assert(close(before.at(onRow),27)); // actual accepted classic flow draw
	// Adding another eligible district with the same mix does not shrink
	// election-wide uncertainty. New matched evidence can move the estimate.
	district.index = 1;
	inputs.districts.push_back(district);
	auto unchanged = S::estimate(inputs,ranking,draw,{{OneNation,27}});
	assert(close(before.at(laborRow),unchanged.at(laborRow)));
	auto& second = inputs.districts.back().contests[1];
	second.learned = true;
	second.reference[Labor] = before.at(laborRow)*.01;
	auto& fit = second.categories.at(0).adjustment;
	fit.ordinary = .6;
	fit.otherDistrictWeight = 1;
	S::addUnit(second,0,{{Labor,200}},{},{},nullptr,0,0);
	auto updated = S::estimate(inputs,ranking,draw,{{OneNation,27}});
	double const original = before.at(laborRow)*.01;
	double const learned = LivePreferenceLearning::transferRate(original,original,fit,0);
	assert(close(updated.at(laborRow),25*original+75*learned));
	assert(updated.at(onRow) == unchanged.at(onRow));
	S::Samples samples{{laborRow,{100,50,0}}};
	std::array<float,5> thresholds{.1f,5,50,95,99.9f};
	auto bands = S::percentiles(samples,thresholds);
	assert(bands.at(laborRow) == std::vector<float>(thresholds.begin(),thresholds.end()));
	for (int size : {1,1234,4096,12345,200000}) {
		int count = 0;
		for (int i = 0; i < size; ++i) if (S::includeDraw(i,size)) ++count;
		assert(count == std::min(size,S::MaximumDraws));
	}
	// The fitted common Greens error is expressed as predicted minus actual
	// Labor flow. It applies when inferring transfers from measured primaries.
	S::Row greensRow{Greens,Labor,Liberal};
	S::Contest greensClassic{};
	greensClassic.classic = true;
	greensClassic.pair = {Labor,Liberal};
	greensClassic.rows = {greensRow};
	S::addUnit(greensClassic,0,{{Greens,200}},{{Greens,100}}, {},nullptr,0,0);
	inputs.districts.resize(1);
	inputs.districts.front().contests = {greensClassic};
	inputs.classicErrorMean[0] = .04;
	assert(close(S::estimate(inputs,ranking,draw,{{Greens,80}}).at(greensRow),78));
	// A classic district calculated from its simulated FP supplies actual
	// source totals. Applying the earlier four-point error a second time here
	// would produce a different public flow from the one used for its TPP.
	S::DistrictClassicTotals calculated{{inputs.districts.front().index,{{Greens,{83,100}}}}};
	assert(close(S::estimate(inputs,ranking,draw,{{Greens,80}},calculated).at(greensRow),83));
}

void checkLivePrimaryToTpp() {
	namespace F = LiveFpTpp;
	F::Shares const prior{{0,40},{1,20},{2,20},{3,20}};
	F::Shares const rates{{2,.38},{3,.62}};
	auto shifted = prior;
	shifted[0] -= 1;
	shifted[2] += 1;
	F::District district;
	auto calculate = [&](auto const& fp, double n, double a, double b) {
		auto result = F::project(district,prior,60,fp,rates,n,a,b);
		assert(result);
		double first = 0, donors = 0;
		for (auto const& [id,row] : result->sources) { first += row.first; donors += row.donors; }
		assert(close(100*first/donors,result->laborPercent));
		return result->laborPercent;
	};
	assert(calculate(prior,1000,0,0) == 60.);
	assert(close(calculate(shifted,1000,0,0),59.38));
	assert(close(calculate(shifted,100000,0,0),59.38));
	F::addBooth(district,prior,prior,{{0,60},{1,40}});
	assert(close(calculate(prior,1000,60,40),60.));
	assert(close(calculate(shifted,1000,60,40),59.38));
	assert(close(calculate(prior,1000,64,36),60.4));
	assert(close(calculate(prior,1000,63,37),60.3));
	district.rateChange[2] = .10;
	assert(close(calculate(prior,1000,64,36),62.2));
	assert(calculate(prior,100,64,36) == 64.);

	// Hold the starting draw exactly across a range of TPPs, including a
	// district whose donor rates need a substantial common adjustment.
	for (int j = 1; j < 2000; ++j) {
		double target = 40.+j*.02;
		auto zero = F::project({},prior,target,prior,rates,1000,0,0);
		assert(zero && zero->laborPercent == target);
	}
	// A TCP-first booth fixes both finalists without attributing a transfer
	// change elsewhere. A mixed, directly counted donor table keeps each
	// named source's measured transfers when the complete count is consumed.
	F::District tcpFirst;
	F::addBooth(tcpFirst,{},prior,{{0,64},{1,36}});
	assert(close(F::project(tcpFirst,prior,60,prior,rates,1000,64,36)->laborPercent,60.4));
	F::District official;
	F::addBooth(official,prior,prior,{{0,64},{1,36}},{{2,{{0,17},{1,3}}},{3,{{0,7},{1,13}}}});
	auto final = F::project(official,prior,60,prior,rates,100,64,36);
	assert(final && final->laborPercent == 64. && final->sources.at(2).first == 17.);
	assert(final->sources.at(3).first == 7.);
}

void checkBroadPreferenceMovement() {
	namespace F = LiveFpTpp;
	F::Shares const primaries{{Labor,40},{Liberal,30},{Greens,10},{OneNation,20}};
	F::Shares const rates{{Greens,.75},{OneNation,.25}};
	constexpr double priorTpp = 52.5;
	F::District district;
	district.transferOddsMultiplier = 2.;
	// Even with unchanged FP and no local TCP, doubling the odds moves
	// Greens from 3/4 to 6/7 and ON from 1/4 to 2/5. A further five
	// points for Greens comes from the separate party-specific evidence.
	district.rateChange[Greens] = .05;
	auto result = F::project(district,primaries,priorTpp,primaries,rates,1000,0,0);
	assert(result);
	double const broad = 10*(6./7.-.75) + 20*(.4-.25);
	assert(close(result->broadPreferenceChange,broad));
	assert(close(result->learnedChange,broad+.5));
	assert(close(result->laborPercent,priorTpp+broad+.5));
	assert(close(result->rates.at(OneNation),.4));
	assert(close(result->rates.at(Greens),6./7.+.05));

	// Start another draw well above the TPP implied by these same generic
	// rates. A movement against Labor must act on its fitted starting rates.
	F::District againstLabor;
	againstLabor.transferOddsMultiplier = .5;
	auto starting = F::project({},primaries,61.,primaries,rates,1000,0,0);
	auto revised = F::project(againstLabor,primaries,61.,primaries,rates,1000,0,0);
	assert(starting && revised && revised->laborPercent < 61.);
	for (int p : {Greens,OneNation}) {
		double const rate = starting->rates.at(p);
		assert(close(revised->rates.at(p),rate/(2.-rate)));
	}

	// A new primary point shifting from Labor to ON still costs Labor
	// the share of those voters who finish with the Coalition.
	auto shifted = primaries;
	shifted[Labor] -= 1.;
	shifted[OneNation] += 1.;
	auto moved = F::project(district,primaries,priorTpp,shifted,rates,1000,0,0);
	assert(moved && close(result->laborPercent - moved->laborPercent,.60));

	// A measured batch accounts for half of each source, including exact
	// transfers by party. Only the uncounted half can follow the new rates.
	F::Shares const half{{Labor,200},{Liberal,150},{Greens,50},{OneNation,100}};
	F::addBooth(district,half,half,{{Labor,280},{Liberal,220}},
		{{Greens,{{Labor,45},{Liberal,5}}},{OneNation,{{Labor,35},{Liberal,65}}}});
	auto partial = F::project(district,primaries,priorTpp,primaries,rates,1000,280,220);
	assert(partial && close(partial->broadPreferenceChange,broad*.5));
	assert(close(partial->sources.at(OneNation).first,35+100*.4));
	assert(close(partial->rates.at(OneNation),(35+100*.4)/200));
	assert(close(partial->laborPercent,28.+.5*result->laborPercent));
	auto final = F::project(district,primaries,priorTpp,primaries,rates,500,280,220);
	assert(final && final->broadPreferenceChange == 0. && final->learnedChange == 0.);
	assert(final->laborPercent == 56. && final->sources.at(Greens).first == 45.);
}

int main()
{
	checkIndependentsReceivingGroupedOthers();
	checkLivePrimaryToTpp();
	checkBroadPreferenceMovement();
	checkLiberalProxyForNationals();
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
	checkGreensPreferencesToMultipleIndependents();
	checkLaborPreferencesToRightIndependent();
	checkLaborPreferenceOverlapAtExclusion();
	checkSouthAustralianBallot();
	checkHistoricalFinnissTransfers();
	checkLivePreferenceCountRevisions();
	checkLivePreferenceCountConditioning();
	checkVictorianReplaySettings();
	checkFederalPreferenceModes();
	checkFederalPreferenceSettings();
	checkJointThreeCandidateCount();
	checkJointThreeCandidateExtraFinalist();
	checkJointThreeCandidateCoalitionCount();
	checkComparisonSummary();
	checkSourceComparison();
	checkSourceReportingDoesNotChangeCount();
	checkLivePreferenceLearning();
	checkPreparedLiberalGeography();
	checkLivePreferenceGeography();
	checkStatewideCountedPreferenceAccounts();
	checkStatewidePreferenceDraws();
}
