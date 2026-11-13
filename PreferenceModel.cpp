// Allocate compulsory-preferential ballots from fixed original sources.
// The model takes the simulator's prepared FP and TPP as inputs and returns
// a final pair and TCP for either comparison or direct use in the forecast.
#include "PreferenceModel.h"

#include "MajorPartyFpMath.h"
#include "RandomGenerator.h"
#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace PreferenceModel {
namespace {

constexpr double Roundoff = 1e-12;
constexpr double TotalTolerance = 1e-8;
// The sparse evidence for Labor ballots in these contests supports a broad
// starting assumption. A 6.5 district concentration gives about 20 points of
// total standard deviation at a 60% centre with the usual election spread.
constexpr double RightIndependentLaborPreferenceMean = 0.60;
constexpr double RightIndependentLaborDistrictConcentration = 6.5;
using Nodes = std::array<double, 5>;
using Rankings = std::vector<Ranking>;
using nlohmann::json;

bool fraction(double value)
{
	return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

double share(Shares const& shares, int id)
{
	auto const found = shares.find(id);
	return found == shares.end() ? 0.0 : found->second;
}

std::uint64_t key(std::uint64_t seed, int district, int source, int branch)
{
	seed = RandomGenerator::mixKey(seed, RandomGenerator::combinePartyIds(district, source));
	return RandomGenerator::mixKey(seed, static_cast<std::uint64_t>(branch));
}

// Each choice uses its own short random stream. The generator is local to
// this draw, so changing a recipient or querying another pair cannot move
// any of the simulator's other random draws.
class DrawEngine {
public:
	using result_type = std::uint64_t;
	explicit DrawEngine(result_type seed) : state(seed) {}
	static constexpr result_type min() { return 0; }
	static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
	result_type operator()() {
		state += 0x9e3779b97f4a7c15ULL;
		return RandomGenerator::splitmix64(state);
	}
private:
	result_type state;
};

double drawBeta(double mean, double concentration, std::uint64_t seed)
{
	if (mean == 0.0 || mean == 1.0) return mean;
	DrawEngine engine(seed);
	return RandomGenerator::betaFromEngine(
		mean * concentration, (1.0 - mean) * concentration, engine);
}

// Outside both Coalition primaries, the preference pool has to supply the
// difference between Labor's FP and its classic TPP. For example, 34% FP
// and 52% TPP require 18 points of the district vote from preferences.
// Move the source rates together on the log-odds scale to assign this
// difference without drawing a second independent set of classic rates.
bool matchClassicTpp(std::span<Source const> sources, Roles const& roles,
	double laborPrimary, double partnerLeakage, double target,
	Shares& rates, ClassicCheck& check)
{
	struct AdjustableSource { int id; double primary, logOdds; };
	std::vector<AdjustableSource> adjustable;
	check.lower = laborPrimary + partnerLeakage;
	check.unadjusted = check.lower;
	double adjustableTotal = 0.0;
	for (auto const& source : sources) {
		if (source.primary == 0.0 || source.id == roles.labor ||
			source.id == roles.liberal || source.id == roles.national) continue;
		double const rate = source.classicRate;
		rates.emplace(source.id, rate);
		check.unadjusted += source.primary * rate;
		if (rate == 1.0) check.lower += source.primary;
		if (rate > 0.0 && rate < 1.0) {
			adjustable.push_back({source.id, source.primary, std::log(rate) - std::log1p(-rate)});
			adjustableTotal += source.primary;
		}
	}
	check.upper = check.lower + adjustableTotal;
	if (target < check.lower - Roundoff || target > check.upper + Roundoff) return false;
	if (std::abs(check.unadjusted - target) <= Roundoff) return true;

	if (target <= check.lower + Roundoff || target >= check.upper - Roundoff) {
		// A reachable endpoint assigns every adjustable ballot to one side.
		double const endpoint = target - check.lower < check.upper - target ? 0.0 : 1.0;
		for (auto const& source : adjustable) rates.at(source.id) = endpoint;
	}
	else {
		auto shiftedRate = [](double logOdds) {
			double const e = std::exp(-std::abs(logOdds));
			return logOdds < 0.0 ? e / (1.0 + e) : 1.0 / (1.0 + e);
		};
		auto [smallest, largest] = std::minmax_element(adjustable.begin(), adjustable.end(),
			[](auto const& a, auto const& b) { return a.logOdds < b.logOdds; });
		// Moving every adjustable rate near zero or one brackets every
		// attainable interior target. Stop when the district share agrees.
		double low = -largest->logOdds - 40.0;
		double high = -smallest->logOdds + 40.0;
		double shift = 0.0;
		for (int step = 0; step < 60; ++step) {
			shift = (low + high) * 0.5;
			double amount = check.lower;
			for (auto const& source : adjustable) {
				amount += source.primary * shiftedRate(source.logOdds + shift);
			}
			if (std::abs(amount - target) <= Roundoff * 0.1) break;
			if (amount < target) low = shift;
			else high = shift;
		}
		for (auto const& source : adjustable) {
			rates.at(source.id) = shiftedRate(source.logOdds + shift);
		}
	}

	for (auto const& source : adjustable) {
		double const newRate = rates.at(source.id);
		auto const original = std::find_if(sources.begin(), sources.end(),
			[&](Source const& candidate) { return candidate.id == source.id; });
		double const change = std::abs(original->classicRate - newRate);
		if (change > check.largestRateChange) {
			check.largestRateChange = change;
			check.changedSource = source.id;
			check.previousRate = original->classicRate;
			check.adjustedRate = newRate;
		}
	}
	return true;
}

// For three recipients a first choice between A and B gives two groups.
// Within each group the other recipient C can come first, in the middle,
// or last. These five binary shares give the six complete orders. When a
// local how-to-vote card puts C last, move the assumed share of ballots
// that would rank C higher to the corresponding C-last order. Each of
// those ballots keeps its original relative order of A and B.
Rankings threeWayOrders(int a, int b, int c, Nodes const& nodes, double moveCToLast = 0.0,
	double reduceAOverB = 0.0)
{
	auto const [aBeforeB, cFirstWhenA, cLastWhenA, cFirstWhenB, cLastWhenB] = nodes;
	double const aFirst = aBeforeB * (1.0 - cFirstWhenA);
	double const bFirst = (1.0 - aBeforeB) * (1.0 - cFirstWhenB);
	double aThenB = aFirst * cLastWhenA;
	double aThenC = aFirst * (1.0 - cLastWhenA);
	double bThenA = bFirst * cLastWhenB;
	double bThenC = bFirst * (1.0 - cLastWhenB);
	double cThenA = aBeforeB * cFirstWhenA;
	double cThenB = (1.0 - aBeforeB) * cFirstWhenB;
	if (moveCToLast > 0.0) {
		double const keepOriginal = 1.0 - moveCToLast;
		aThenB += moveCToLast * (aThenC + cThenA);
		bThenA += moveCToLast * (bThenC + cThenB);
		aThenC *= keepOriginal;
		bThenC *= keepOriginal;
		cThenA *= keepOriginal;
		cThenB *= keepOriginal;
	}
	if (reduceAOverB > 0.0) {
		// Prahran's open Labor ticket motivates a test of a lower Greens flow
		// against the Coalition. Swap their adjacent positions in a share of
		// Labor ballots that rank the Greens higher. This preserves each
		// party's head-to-head flow against One Nation. If a draw has too
		// few eligible ballots, move all of them and stop at that limit.
		double const eligible = aThenB + cThenA;
		if (eligible > 0.0) {
			double const swapFraction = std::min(1.0, aBeforeB * reduceAOverB / eligible);
			double const swapWhenCIsLast = aThenB * swapFraction;
			double const swapWhenCIsFirst = cThenA * swapFraction;
			aThenB -= swapWhenCIsLast;
			bThenA += swapWhenCIsLast;
			cThenA -= swapWhenCIsFirst;
			cThenB += swapWhenCIsFirst;
		}
	}
	return {
		{{a, b, c}, aThenB},
		{{a, c, b}, aThenC},
		{{b, a, c}, bThenA},
		{{b, c, a}, bThenC},
		{{c, a, b}, cThenA},
		{{c, b, a}, cThenB}
	};
}

std::size_t position(Ranking const& ranking, int id)
{
	return static_cast<std::size_t>(
		std::find(ranking.order.begin(), ranking.order.end(), id) - ranking.order.begin());
}

// Divide an existing ranking group by where another candidate fits. Removing
// that candidate recovers the original ranking and its full fraction.
void addPlacement(Rankings& result, Ranking const& group, int id,
	std::size_t place, double fractionOfGroup)
{
	if (fractionOfGroup <= 0.0 || group.fraction == 0.0) return;
	auto& added = result.emplace_back(group);
	added.order.insert(added.order.begin() + static_cast<std::ptrdiff_t>(place), id);
	added.fraction *= fractionOfGroup;
}

Rankings insertBeside(Rankings const& ranks, int candidate, int opponent, double ahead)
{
	Rankings result;
	for (auto const& group : ranks) {
		std::size_t const place = position(group, opponent);
		addPlacement(result, group, candidate, place, ahead);
		addPlacement(result, group, candidate, place + 1, 1.0 - ahead);
	}
	return result;
}

// Place the independent in the existing ballot orders using the fractions
// that prefer them to each opponent. The original Liberal–One Nation order
// stays fixed. The second target may need to move if the three comparisons
// cannot describe the same ballots.
Rankings insertAgainstTwo(Rankings const& ranks, int candidate, int first, int second,
	double aheadOfFirst, double aheadOfSecond)
{
	double firstBeforeSecond = 0.0;
	for (auto const& group : ranks) {
		if (position(group, first) < position(group, second)) firstBeforeSecond += group.fraction;
	}
	firstBeforeSecond = std::clamp(firstBeforeSecond, 0.0, 1.0);
	double const secondBeforeFirst = 1.0 - firstBeforeSecond;
	aheadOfSecond = std::clamp(aheadOfSecond,
		std::max(0.0, aheadOfFirst - secondBeforeFirst), std::min(1.0, aheadOfFirst + firstBeforeSecond));

	// The two pair targets leave a range for the share ranking the independent
	// above both opponents. For 60% against Liberal and 80% against One Nation
	// at least 40% rank the independent above both, and at most 60% can. The
	// original opponent orders can narrow that range further. Use its midpoint
	// as a modelling assumption about the ballots we cannot observe. For the
	// example, the 50% midpoint leaves 30% ranking the independent between
	// Liberal and One Nation, and 10% between One Nation and Liberal.
	double const maximumAheadOfBoth = std::min(aheadOfFirst, aheadOfSecond);
	double const minimumAheadOfBoth = std::min(maximumAheadOfBoth, std::max({
		0.0, aheadOfFirst + aheadOfSecond - 1.0,
		aheadOfSecond - firstBeforeSecond, aheadOfFirst - secondBeforeFirst
	}));
	double const aheadOfBoth = std::midpoint(minimumAheadOfBoth, maximumAheadOfBoth);
	double const firstGroupBetween = aheadOfSecond - aheadOfBoth;
	double const secondGroupBetween = aheadOfFirst - aheadOfBoth;
	double const outsideBetween = 1.0 - firstGroupBetween - secondGroupBetween;
	double const topFraction = outsideBetween > 0.0 ?
		std::clamp(aheadOfBoth / outsideBetween, 0.0, 1.0) : 0.0;

	Rankings result;
	result.reserve(ranks.size() * 3);
	for (auto const& group : ranks) {
		auto const firstPosition = position(group, first);
		auto const secondPosition = position(group, second);
		bool const firstAhead = firstPosition < secondPosition;
		double const groupSize = firstAhead ? firstBeforeSecond : secondBeforeFirst;
		if (groupSize <= 0.0) continue;
		double const between = std::clamp(
			(firstAhead ? firstGroupBetween : secondGroupBetween) / groupSize, 0.0, 1.0);
		auto const [earlier, later] = std::minmax(firstPosition, secondPosition);
		addPlacement(result, group, candidate, earlier, (1.0 - between) * topFraction);
		addPlacement(result, group, candidate, later, between);
		addPlacement(result, group, candidate, later + 1, (1.0 - between) * (1.0 - topFraction));
	}
	return result;
}

// For Greens ballots, distinguish the direct share ranking the independent
// above both major candidates from the final share against the Coalition.
// The latter includes some ballots which rank Labor first and the independent
// second. Put only the feasible amount in that group and keep the existing
// Labor/Coalition split. The two targets are uncertain prior draws.
Rankings insertGreensIndependent(Rankings const& ranks, int candidate,
	int labor, int coalition, double laborBeforeCoalition, double first, double overCoalition)
{
	double const laborMass = std::clamp(laborBeforeCoalition, 0.0, 1.0);
	double const between = std::clamp(overCoalition - first, 0.0, std::min(laborMass, 1.0 - first));
	double const firstFromLabor = std::min(first * laborMass, laborMass - between);
	double const firstFromCoalition = first - firstFromLabor;
	Rankings result;
	for (auto const& group : ranks) {
		auto const laborPosition = position(group, labor);
		auto const coalitionPosition = position(group, coalition);
		bool const laborFirst = laborPosition < coalitionPosition;
		double const groupSize = laborFirst ? laborMass : 1.0 - laborMass;
		if (groupSize <= 0.0) continue;
		double const top = std::clamp((laborFirst ? firstFromLabor : firstFromCoalition) / groupSize, 0.0, 1.0);
		// Where Coalition precedes Labor, moving the independent between the
		// two has no effect on the IND/Coalition pair. Give this choice a
		// neutral centre in the absence of direct evidence.
		double const middle = laborFirst ? std::clamp(between / groupSize, 0.0, 1.0 - top) : (1.0 - top) * 0.5;
		auto const [earlier, later] = std::minmax(laborPosition, coalitionPosition);
		addPlacement(result, group, candidate, earlier, top);
		addPlacement(result, group, candidate, later, middle);
		addPlacement(result, group, candidate, later + 1, 1.0 - top - middle);
	}
	return result;
}

Rankings insertOwnCoalitionPartner(Rankings const& ranks, int partner, int labor, double leakage)
{
	Rankings result;
	for (auto const& group : ranks) {
		addPlacement(result, group, partner, 0, 1.0 - leakage);
		addPlacement(result, group, partner, position(group, labor) + 1, leakage);
	}
	return result;
}

// Given two supplied Nationals donor splits against Labor, three weights
// reproduce both. Enumerate the six small core orders before extending the
// profile to the rest of the ballot.
Rankings weightedCore(std::array<int, 3> recipients, Shares const& weights)
{
	std::sort(recipients.begin(), recipients.end());
	double const total = weights.at(recipients[0]) + weights.at(recipients[1]) + weights.at(recipients[2]);
	Rankings result;
	do {
		double const first = weights.at(recipients[0]);
		double const second = weights.at(recipients[1]);
		double const third = weights.at(recipients[2]);
		result.push_back({{recipients.begin(), recipients.end()}, (first / total) * (second / (second + third))});
	} while (std::next_permutation(recipients.begin(), recipients.end()));
	return result;
}

// A neighbouring Liberal/National pair in outside voters' rankings has the
// combined attraction of a single Coalition slot. The sampled ordering
// within the slot applies throughout the count, even if both are finalists.
struct SourceProfile {
	int id;
	double primary;
	RankingProfile ranking;
	bool coalitionSlot;
};

PairResult finalPair(std::span<int const> candidates, std::span<double const> totals)
{
	return {{candidates[0], candidates[1]}, totals[0], {}};
}

std::vector<double> allocateBallots(std::span<int const> survivors,
	std::span<SourceProfile const> profiles, Roles const& roles, double nationalIncoming)
{
	std::vector<double> totals(survivors.size(), 0.0);
	auto const lib = std::find(survivors.begin(), survivors.end(), roles.liberal);
	auto const nat = roles.national ?
		std::find(survivors.begin(), survivors.end(), *roles.national) : survivors.end();
	bool const bothPartners = lib != survivors.end() && nat != survivors.end();
	std::vector<int> coalitionSurvivors;
	if (nat != survivors.end()) {
		coalitionSurvivors.assign(survivors.begin(), survivors.end());
		auto national = coalitionSurvivors.begin() + (nat - survivors.begin());
		if (bothPartners) coalitionSurvivors.erase(national);
		else *national = roles.liberal;
	}
	for (auto const& profile : profiles) {
		auto const own = std::find(survivors.begin(), survivors.end(), profile.id);
		if (own != survivors.end()) {
			totals[static_cast<std::size_t>(own - survivors.begin())] += profile.primary;
			continue;
		}
		if (profile.coalitionSlot && nat != survivors.end()) {
			auto const shares = profile.ranking.allocate(coalitionSurvivors);
			for (std::size_t i = 0; i < coalitionSurvivors.size(); ++i) {
				int const id = coalitionSurvivors[i];
				double const amount = shares[i] * profile.primary;
				if (id == roles.liberal) {
					double const natAmount = bothPartners ? amount * nationalIncoming : amount;
					totals[static_cast<std::size_t>(nat - survivors.begin())] += natAmount;
					if (bothPartners) totals[static_cast<std::size_t>(lib - survivors.begin())] += amount - natAmount;
				}
				else {
					auto const recipient = std::find(survivors.begin(), survivors.end(), id);
					totals[static_cast<std::size_t>(recipient - survivors.begin())] += amount;
				}
			}
		}
		else {
			auto const shares = profile.ranking.allocate(survivors);
			for (std::size_t i = 0; i < totals.size(); ++i) totals[i] += shares[i] * profile.primary;
		}
	}
	return totals;
}

}

RankingProfile::RankingProfile(std::vector<Ranking> rankings, Shares weights)
	: recipientWeights(std::move(weights))
{
	if (rankings.empty()) throw std::invalid_argument("A preference profile needs at least one order.");
	core = rankings.front().order;
	std::sort(core.begin(), core.end());
	if (std::adjacent_find(core.begin(), core.end()) != core.end()) {
		throw std::invalid_argument("A preference order repeats a candidate.");
	}
	double total = 0.0;
	orders.reserve(rankings.size());
	for (auto& ranking : rankings) {
		total += ranking.fraction;
		auto sorted = ranking.order;
		std::sort(sorted.begin(), sorted.end());
		if (!fraction(ranking.fraction) || sorted != core) {
			throw std::invalid_argument("Inconsistent preference ranking groups.");
		}
		if (ranking.fraction == 0.0) continue;
		std::vector<double> remaining(ranking.order.size());
		double weight = 0.0;
		for (std::size_t i = ranking.order.size(); i > 0; --i) {
			weight += recipientWeights.at(ranking.order[i - 1]);
			remaining[i - 1] = weight;
		}
		orders.push_back({std::move(ranking), std::move(remaining)});
	}
	if (std::abs(total - 1.0) > Roundoff ||
		std::any_of(recipientWeights.begin(), recipientWeights.end(),
			[](auto const& value) { return !std::isfinite(value.second) || value.second <= 0.0; })) {
		throw std::invalid_argument("Preference group fractions must sum to one and weights must be positive.");
	}
}

std::vector<double> RankingProfile::allocate(std::span<int const> survivors) const
{
	if (survivors.empty()) throw std::invalid_argument("A preference allocation needs a surviving candidate.");
	std::vector<double> allocation(survivors.size(), 0.0);
	std::vector<std::pair<std::size_t, double>> extras;
	double extraWeight = 0.0;
	for (std::size_t i = 0; i < survivors.size(); ++i) {
		if (!std::binary_search(core.begin(), core.end(), survivors[i])) {
			double const weight = recipientWeights.at(survivors[i]);
			extras.emplace_back(i, weight);
			extraWeight += weight;
		}
	}

	// Imagine positive weights as rates at which recipients arrive. Given
	// an already supported core order, the next arrival from that core has
	// the combined rate of its unseen members. Multiply the chances of a
	// core arrival before any extra until a surviving core recipient arrives.
	// Extra recipients divide the remaining fraction by their relative weights.
	double extraFraction = 0.0;
	for (auto const& group : orders) {
		double beforeExtras = 1.0;
		bool assigned = false;
		for (std::size_t i = 0; i < group.ranking.order.size(); ++i) {
			double const weight = group.remainingCoreWeights[i];
			beforeExtras *= weight / (weight + extraWeight);
			auto const found = std::find(survivors.begin(), survivors.end(), group.ranking.order[i]);
			if (found == survivors.end()) continue;
			allocation[static_cast<std::size_t>(found - survivors.begin())] += group.ranking.fraction * beforeExtras;
			extraFraction += group.ranking.fraction * (1.0 - beforeExtras);
			assigned = true;
			break;
		}
		if (!assigned) extraFraction += group.ranking.fraction;
	}
	for (auto const& [index, weight] : extras) allocation[index] = extraFraction * (weight / extraWeight);
	return allocation;
}

int PairResult::winner() const
{
	return selectedWinner.value_or(firstShare > 0.5 ? candidates[0] : candidates[1]);
}

std::optional<Model> Model::load(std::istream& input, Roles roles,
	std::span<District const> districts, ConfiguredFlows const& configuredFlows)
{
	if (!input) return std::nullopt;
	auto const document = json::parse(input);
	auto const mode = document.at("mode").get<std::string>();
	if (mode == "off") return std::nullopt;
	if ((mode != "compare" && mode != "active") || document.at("schema_version").get<int>() != 1) {
		throw std::invalid_argument("Expected schema_version 1 and preference mode off, compare or active.");
	}

	Model model;
	model.active = mode == "active";
	model.roles = roles;
	model.modelVersion = document.at("model_version").get<std::string>();
	if (model.modelVersion.empty()) throw std::invalid_argument("The preference model needs a version.");
	model.versionKey = 0x7abe29471a980c67ULL;
	for (unsigned char const character : model.modelVersion) {
		model.versionKey = RandomGenerator::mixKey(model.versionKey, character);
	}
	model.sampleUncertainty = document.value("sample_uncertainty", true);
	model.unknownLiberalLaborFirst = document.at("unknown_liberal_labor_first_probability").get<double>();
	auto positive = [](double value) { return std::isfinite(value) && value > 0.0; };
	auto const generic = document.at("generic_concentrations").get<std::array<double, 2>>();
	model.genericElectionConcentration = generic[0];
	model.genericDistrictConcentration = generic[1];
	if (!fraction(model.unknownLiberalLaborFirst) || !positive(generic[0]) || !positive(generic[1])) {
		throw std::invalid_argument("Invalid generic uncertainty or unknown Liberal advice probability.");
	}
	std::array mainRoles{roles.labor, roles.liberal, roles.greens, roles.oneNation, roles.independent};
	std::sort(mainRoles.begin(), mainRoles.end());
	bool const invalidNationalRole = roles.national &&
		(*roles.national < 0 || std::binary_search(mainRoles.begin(), mainRoles.end(), *roles.national));
	if (mainRoles.front() < 0 || std::adjacent_find(mainRoles.begin(), mainRoles.end()) != mainRoles.end() ||
		invalidNationalRole) {
		throw std::invalid_argument("The preference model needs distinct Labor, Liberal, Greens, One Nation and independent roles. A separate Nationals role is optional.");
	}

	// The file contains the five fitted branches in the order understood by
	// threeWayOrders. One or more primary-share points describe a source.
	// These are district primary fractions, with the outer points held fixed
	// where the evidence supplies no further relationship with vote share.
	auto const& profiles = document.at("profiles");
	std::array<std::string_view, LiberalOpen> names{
		"labor", "liberal_labor_first", "liberal_greens_first", "greens", "one_nation"};
	for (std::size_t i = 0; i < names.size(); ++i) {
		auto const name = std::string(names[i]);
		try {
			auto const& values = profiles.at(name);
			auto& prior = model.priors[i];
			prior.electionConcentration = values.at("election_concentration").get<Nodes>();
			prior.districtConcentration = values.at("district_concentration").get<Nodes>();
			if (!std::all_of(prior.electionConcentration.begin(), prior.electionConcentration.end(), positive) ||
				!std::all_of(prior.districtConcentration.begin(), prior.districtConcentration.end(), positive)) {
				throw std::invalid_argument("Branch concentrations must be finite and positive.");
			}
			auto const& knots = values.at("primary_points");
			if (!knots.is_array() || knots.empty()) throw std::invalid_argument("Missing primary points.");
			for (auto const& point : knots) {
				auto const primary = point.at("primary").get<double>();
				auto const means = point.at("means").get<Nodes>();
				if (point.at("means").size() != means.size() ||
					!fraction(primary) || !std::all_of(means.begin(), means.end(), fraction) ||
					(!prior.strengths.empty() && primary <= prior.strengths.back().primary)) {
					throw std::invalid_argument("Expected increasing primary fractions and five bounded branch means.");
				}
				prior.strengths.push_back({primary, means});
			}
			if (values.at("election_concentration").size() != prior.electionConcentration.size() ||
				values.at("district_concentration").size() != prior.districtConcentration.size()) {
				throw std::invalid_argument("Expected five branch concentrations at each level.");
			}
			prior.referenceMeans = std::min_element(prior.strengths.begin(), prior.strengths.end(),
				[](Strength const& a, Strength const& b) {
					return std::abs(a.primary - 0.1) < std::abs(b.primary - 0.1);
				})->means;
		}
		catch (std::exception const& error) {
			throw std::invalid_argument(name + ": " + error.what());
		}
	}

	// An open Liberal card supplies no direction. Give the conditional
	// choices a centre between the two directed profiles, with the broader
	// of their spreads. Unknown advice instead draws one of those directions
	// for the election, because a common party decision could affect many seats.
	auto const& laborAdvice = model.priors[LiberalLaborFirst];
	auto const& greensAdvice = model.priors[LiberalGreensFirst];
	auto& open = model.priors[LiberalOpen];
	Nodes openMeans{};
	for (std::size_t node = 0; node < openMeans.size(); ++node) {
		openMeans[node] = (laborAdvice.referenceMeans[node] + greensAdvice.referenceMeans[node]) * 0.5;
		open.electionConcentration[node] = std::min(laborAdvice.electionConcentration[node], greensAdvice.electionConcentration[node]);
		open.districtConcentration[node] = std::min(laborAdvice.districtConcentration[node], greensAdvice.districtConcentration[node]);
	}
	open.strengths.push_back({0.1, openMeans});
	open.referenceMeans = openMeans;

	auto configured = [&](int source, int first, int second) -> std::optional<double> {
		auto const row = configuredFlows.find(source);
		if (row == configuredFlows.end()) return std::nullopt;
		auto const flow = row->second.find({first, second});
		if (flow == row->second.end()) return std::nullopt;
		return double(flow->second) * 0.01;
	};
	// Fitted Labor and Liberal profiles supply the four comparisons among
	// their core recipients. These other rows supply uncertain centres for
	// ranking an independent and for the less well observed Nationals source.
	for (auto const& [source, opponent] : std::array{
		std::pair{roles.labor, roles.liberal}, std::pair{roles.liberal, roles.labor},
		std::pair{roles.greens, roles.liberal}}) {
		if (auto flow = configured(source, roles.independent, opponent)) {
			model.independentOverMajor.emplace(source, *flow);
		}
	}
	if (roles.national) {
		int const national = *roles.national;
		if (auto flow = configured(national, roles.independent, roles.labor)) {
			model.independentOverMajor.emplace(national, *flow);
		}
		model.nationalGreensOverLabor = configured(national, roles.greens, roles.labor);
		model.nationalOneNationOverLabor = configured(national, roles.oneNation, roles.labor);
	}

	auto const overrides = document.value("districts", json::object());
	if (!overrides.is_object()) throw std::invalid_argument("District settings must be keyed by district name.");
	auto remaining = overrides;
	auto sensitivity = document.value("sensitivity_tests", json::object());
	if (!sensitivity.is_object()) throw std::invalid_argument("Sensitivity tests must be keyed by district name.");
	for (auto const& district : districts) {
		Context context;
		context.highIndependentFlow = district.coalitionHeld;
		context.namedIndependent = !district.independentName.empty();
		auto const found = overrides.find(district.name);
		if (found != overrides.end()) {
			auto const advice = found->value("liberal_advice", std::string("unknown"));
			if (advice == "labor_first") context.advice = Advice::LaborFirst;
			else if (advice == "greens_first") context.advice = Advice::GreensFirst;
			else if (advice == "open") context.advice = Advice::Open;
			else if (advice != "unknown") throw std::invalid_argument("Invalid Liberal advice in " + district.name + ".");

			// The Liberal advice above chooses the Labor versus Greens profile.
			// A card placing One Nation below both adds separate information.
			// The supplied weight is the expected fraction of higher One Nation
			// rankings that respond to the card. It is a modelling assumption
			// between zero and one, with zero giving the ordinary profile.
			context.liberalOneNationLastWeight = found->value("liberal_one_nation_last_weight", 0.0);
			if (!fraction(context.liberalOneNationLastWeight)) {
				throw std::invalid_argument("Invalid Liberal One Nation last weight in " + district.name + ".");
			}

			if (found->contains("independent")) {
				auto const& independent = found->at("independent");
				if (!context.namedIndependent ||
					independent.at("name").get<std::string>() != district.independentName) {
					throw std::invalid_argument("Independent preference setting does not match the named candidate in " + district.name + ".");
				}
				auto const positionName = independent.at("position").get<std::string>();
				if (positionName == "left") context.highIndependentFlow = true;
				else if (positionName == "right") {
					context.highIndependentFlow = false;
					context.rightIndependent = true;
				}
				else if (positionName != "centre") throw std::invalid_argument("Invalid independent position in " + district.name + ".");
			}
			remaining.erase(district.name);
		}
		auto const tests = sensitivity.find(district.name);
		if (tests != sensitivity.end()) {
			context.sensitivity.withoutClassicTppMatching = tests->value("without_classic_tpp_matching", false);
			context.sensitivity.laborToGreensReduction = tests->value("labor_to_greens_reduction", 0.0);
			if (!fraction(context.sensitivity.laborToGreensReduction)) {
				throw std::invalid_argument("Invalid Labor to Greens flow reduction in " + district.name + ".");
			}
			sensitivity.erase(tests);
		}
		model.contexts.push_back(context);
	}
	if (!remaining.empty()) throw std::invalid_argument("Unknown preference district: " + remaining.begin().key());
	if (!sensitivity.empty()) throw std::invalid_argument("Unknown sensitivity test district: " + sensitivity.begin().key());
	return model;
}

SensitivityTests const& Model::sensitivityTests(int district) const
{
	return contexts.at(static_cast<std::size_t>(district)).sensitivity;
}

Model::ElectionDraw Model::drawElection(std::uint64_t seed) const
{
	ElectionDraw draw;
	draw.seed = RandomGenerator::mixKey(seed, versionKey);
	draw.liberalLaborFirst = RandomGenerator::uniform01_from_key(key(draw.seed, -1, roles.liberal, 0))
		< unknownLiberalLaborFirst;
	for (std::size_t i = 0; i < priors.size(); ++i) {
		auto const& prior = priors[i];
		for (std::size_t node = 0; node < prior.referenceMeans.size(); ++node) {
			double const mean = prior.referenceMeans[node];
			draw.commonNodes[i][node] = sampleUncertainty ?
				drawBeta(mean, prior.electionConcentration[node],
					key(draw.seed, -1, static_cast<int>(i), 100 + static_cast<int>(node))) : mean;
		}
	}
	return draw;
}

Model::Nodes Model::drawNodes(ProfileIndex profile, double primary,
	std::optional<double> classicRate, int district, ElectionDraw const& election) const
{
	auto const& prior = priors[profile];
	auto const upper = std::upper_bound(prior.strengths.begin(), prior.strengths.end(), primary,
		[](double value, Strength const& strength) { return value < strength.primary; });
	Nodes means;
	if (upper == prior.strengths.begin()) means = upper->means;
	else if (upper == prior.strengths.end()) means = prior.strengths.back().means;
	else {
		auto const lower = std::prev(upper);
		double const t = (primary - lower->primary) / (upper->primary - lower->primary);
		for (std::size_t node = 0; node < means.size(); ++node) {
			means[node] = std::lerp(lower->means[node], upper->means[node], t);
		}
	}
	for (std::size_t node = 0; node < means.size(); ++node) {
		if (node == 0 && classicRate) {
			means[node] = *classicRate;
			continue;
		}
		if (!sampleUncertainty) continue;
		double const referenceMean = prior.referenceMeans[node];
		double const desiredMean = means[node];
		double common = election.commonNodes[profile][node];
		// Reuse the common draw when the mean changes with FP. At a different
		// strength, scale toward the relevant endpoint to give the desired
		// mean and keep the same ordering of shared draws. This is exact
		// at the reference point and an interpolation assumption elsewhere.
		if (desiredMean < referenceMean) common *= desiredMean / referenceMean;
		else if (desiredMean > referenceMean) {
			double const movement = (desiredMean - referenceMean) / (1.0 - referenceMean);
			common = std::lerp(common, 1.0, movement);
		}
		means[node] = drawBeta(common, prior.districtConcentration[node],
			key(election.seed, district, profile, 100 + static_cast<int>(node)));
	}
	return means;
}

double Model::sampleFallback(double mean, int district, int source, int branch,
	ElectionDraw const& election, std::optional<double> districtConcentration) const
{
	if (!sampleUncertainty) return mean;
	double const common = drawBeta(mean, genericElectionConcentration, key(election.seed, -1, source, branch));
	return drawBeta(common, districtConcentration.value_or(genericDistrictConcentration),
		key(election.seed, district, source, branch));
}

double Model::recipientWeight(Source const& source, Source const& recipient,
	int district, ElectionDraw const& election) const
{
	int const distance = source.id == roles.others ? 0 : std::abs(source.ideology - recipient.ideology);
	double weight = std::pow(source.proximityBase, -distance);
	if (sampleUncertainty) {
		// At an even split, moving the difference in log weights by 0.4
		// moves the share by about ten points. Divide the intended spread
		// between the two weights. These assumptions give uncertain
		// proximity estimates, including for the otherwise even Others pool.
		std::uint64_t const ids = RandomGenerator::combinePartyIds(source.id, recipient.id);
		auto const sourceRecipient = RandomGenerator::mixKey(election.seed, ids);
		double const common = RandomGenerator::normal_from_key(key(sourceRecipient, -1, 0, 300), 0.0f, 0.2828427f);
		double const local = RandomGenerator::normal_from_key(key(sourceRecipient, district, 0, 300), 0.0f, 0.4242641f);
		weight *= std::exp(common + local);
	}
	return weight;
}

std::vector<Ranking> Model::coreRankings(Source const& source, int coalition,
	std::optional<double> classicRate, int district, ElectionDraw const& election,
	double laborToGreensReduction) const
{
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	if (source.id == roles.labor) {
		return threeWayOrders(roles.greens, coalition, roles.oneNation,
			drawNodes(Labor, source.primary, {}, district, election), 0.0, laborToGreensReduction);
	}
	if (source.id == roles.liberal) {
		ProfileIndex variant = election.liberalLaborFirst ? LiberalLaborFirst : LiberalGreensFirst;
		if (context.advice == Advice::LaborFirst) variant = LiberalLaborFirst;
		else if (context.advice == Advice::GreensFirst) variant = LiberalGreensFirst;
		else if (context.advice == Advice::Open) variant = LiberalOpen;
		// The card gives the direction of the local change. Draw its uncertain
		// size with the same broad spreads used for other poorly measured flows.
		double moveOneNationToLast = 0.0;
		if (context.liberalOneNationLastWeight > 0.0) {
			moveOneNationToLast = sampleFallback(
				context.liberalOneNationLastWeight, district, source.id, 206, election);
		}
		return threeWayOrders(roles.labor, roles.greens, roles.oneNation,
			drawNodes(variant, source.primary, {}, district, election), moveOneNationToLast);
	}
	if (source.id == roles.national) {
		double const greens = sampleFallback(nationalGreensOverLabor.value_or(0.5), district, source.id, 201, election);
		double const oneNation = sampleFallback(nationalOneNationOverLabor.value_or(0.5), district, source.id, 202, election);
		auto odds = [](double rate) {
			rate = std::clamp(rate, Roundoff, 1.0 - Roundoff);
			return rate / (1.0 - rate);
		};
		return weightedCore({roles.labor, roles.greens, roles.oneNation},
			{{roles.labor, 1.0}, {roles.greens, odds(greens)}, {roles.oneNation, odds(oneNation)}});
	}
	if (source.id == roles.greens) {
		return threeWayOrders(roles.labor, coalition, roles.oneNation,
			drawNodes(Greens, source.primary, classicRate, district, election));
	}
	if (source.id == roles.oneNation) {
		return threeWayOrders(roles.labor, coalition, roles.greens,
			drawNodes(OneNation, source.primary, classicRate, district, election));
	}
	double const rate = classicRate.value_or(source.classicRate);
	return {{{roles.labor, coalition}, rate}, {{coalition, roles.labor}, 1.0 - rate}};
}

CountResult Model::count(int district, std::span<Source const> sources,
	double laborTpp, double nationalIncoming, ElectionDraw const& election, Sensitivity sensitivity) const
{
	CountResult result;
	result.laborTpp = laborTpp;
	if (!fraction(laborTpp) || !fraction(nationalIncoming)) {
		result.issue = "The district TPP or incoming Coalition split is outside 0 to 100%.";
		return result;
	}
	std::vector<Source> ballot(sources.begin(), sources.end());
	std::sort(ballot.begin(), ballot.end(), [](auto const& a, auto const& b) { return a.id < b.id; });
	Shares primaries;
	double total = 0.0;
	std::vector<int> candidates;
	for (auto const& source : ballot) {
		if (!fraction(source.primary) || !fraction(source.classicRate) ||
			!std::isfinite(source.proximityBase) || source.proximityBase <= 0.0 ||
			!primaries.emplace(source.id, source.primary).second) {
			result.issue = "Invalid or repeated original source on the district ballot.";
			return result;
		}
		total += source.primary;
		if (source.primary > 0.0 && source.id != roles.others) candidates.push_back(source.id);
	}
	if (std::abs(total - 1.0) > TotalTolerance || candidates.size() < 2) {
		result.issue = "The district needs two candidates and a complete first-preference account.";
		return result;
	}

	double const liberal = share(primaries, roles.liberal);
	double const national = roles.national ? share(primaries, *roles.national) : 0.0;
	double const labor = share(primaries, roles.labor);
	bool const dual = liberal > 0.0 && national > 0.0;
	bool const classicAvailable = labor > 0.0 && (liberal > 0.0 || national > 0.0);
	bool const matchTpp = classicAvailable && sensitivity != Sensitivity::WithoutClassicTppMatching;
	// Match the prepared Labor TPP for any classic final. If both Coalition
	// partners run, each hypothetical pairing uses the same leakage from the
	// eliminated partner. The primary calculation already reserved this
	// amount, 15.4% of the smaller primary, when solving for Labor's primary.
	double const leakageVotes = MajorPartyFpMath::ClassicPartnerLeakageRate * std::min(liberal, national);
	Shares classicRates;
	if (matchTpp && !matchClassicTpp(ballot, roles, labor, leakageVotes,
		laborTpp, classicRates, result.classic)) {
		result.status = Status::InfeasibleClassicTpp;
		return result;
	}

	int const independent = share(primaries, roles.independent) > 0.0 ? roles.independent : roles.emergingIndependent;
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	double const laborToGreensReduction = sensitivity == Sensitivity::ReducedLaborToGreens ?
		context.sensitivity.laborToGreensReduction : 0.0;
	std::vector<SourceProfile> profiles;
	profiles.reserve(ballot.size());
	for (auto const& source : ballot) {
		if (source.primary == 0.0) continue;
		bool const outsideCoalition = source.id != roles.liberal && source.id != roles.national;
		std::optional<double> classicRate;
		if (matchTpp) {
			auto const found = classicRates.find(source.id);
			if (found != classicRates.end()) classicRate = found->second;
		}
		else if (classicAvailable) {
			// Use the original source rates from the accepted FP calculation.
			// The test varies the aggregate correction to those rates on the
			// same election draw, including when the Greens or One Nation
			// profile would otherwise use its own draw for this branch.
			classicRate = source.classicRate;
		}
		auto rankings = coreRankings(source, roles.liberal, classicRate, district, election, laborToGreensReduction);

		// Insert a relevant independent inside the supported orders. The
		// additional choices have broad priors. Removing that independent
		// from a comparison always recovers the original core ranking.
		auto const independentMean = independentOverMajor.find(source.id);
		bool const rightLaborFlow = source.id == roles.labor &&
			independent == roles.independent && context.rightIndependent;
		if (share(primaries, independent) > 0.0 &&
			(independentMean != independentOverMajor.end() || rightLaborFlow)) {
			// Other independents use the configured Labor–independent
			// comparison. For a named right independent the Labor share ranking
			// them ahead of Liberal starts at 60%, with extra local uncertainty
			// about their appeal. Retain any wider generic district spread.
			double const ahead = rightLaborFlow
				? sampleFallback(RightIndependentLaborPreferenceMean, district, source.id, 207, election,
					std::min(RightIndependentLaborDistrictConcentration, genericDistrictConcentration))
				: sampleFallback(independentMean->second, district, source.id, 203, election);
			if (source.id == roles.labor) {
				double const overOneNation = sampleFallback(0.80, district, source.id, 204, election);
				rankings = insertAgainstTwo(rankings, independent, roles.liberal, roles.oneNation, ahead, overOneNation);
			}
			else if (source.id == roles.greens && context.namedIndependent) {
				double const mean = context.highIndependentFlow ? 0.50 : 0.25;
				double first = mean;
				if (sampleUncertainty) {
					double const sharedVariance = 0.06 * 0.06;
					double const districtSd = context.highIndependentFlow ? 0.10 : 0.14;
					double const maximum = mean * (1.0 - mean);
					double const common = drawBeta(mean, maximum / sharedVariance - 1.0,
						key(election.seed, -1, source.id, 205));
					first = drawBeta(common, (maximum - sharedVariance) / (districtSd * districtSd) - 1.0,
						key(election.seed, district, source.id, 205));
				}
				double laborBeforeCoalition = 0.0;
				for (auto const& group : rankings) {
					if (position(group, roles.labor) < position(group, roles.liberal)) laborBeforeCoalition += group.fraction;
				}
				rankings = insertGreensIndependent(rankings, independent, roles.labor, roles.liberal,
					laborBeforeCoalition, first, ahead);
			}
			else {
				int const opponent = outsideCoalition ? roles.liberal : roles.labor;
				rankings = insertBeside(rankings, independent, opponent, ahead);
			}
		}

		if (dual && !outsideCoalition) {
			int const partner = source.id == roles.liberal ? *roles.national : roles.liberal;
			rankings = insertOwnCoalitionPartner(rankings, partner, roles.labor, leakageVotes / source.primary);
		}
		Shares weights;
		for (auto const& recipient : ballot) {
			if (recipient.id == source.id || recipient.id == roles.others ||
				(outsideCoalition && recipient.id == roles.national)) continue;
			weights.emplace(recipient.id, recipientWeight(source, recipient, district, election));
		}
		// A fitted order can contain a party that is absent from the actual
		// ballot. Its latent place still defines the order of the core, but
		// only candidates with a positive primary can receive votes here.
		for (int const id : rankings.front().order) {
			weights.try_emplace(id, 1.0);
		}
		profiles.push_back({source.id, source.primary, RankingProfile(std::move(rankings), std::move(weights)), outsideCoalition});
	}

	if (matchTpp) {
		auto checkClassicPair = [&](int opponent) {
			std::array const pair{roles.labor, opponent};
			auto const votes = allocateBallots(pair, profiles, roles, nationalIncoming);
			result.classic.maximumError = std::max(result.classic.maximumError, std::abs(votes[0] - laborTpp));
			++result.classic.pairsChecked;
		};
		if (liberal > 0.0) checkClassicPair(roles.liberal);
		if (national > 0.0) checkClassicPair(*roles.national);
	}
	// Restart each stage from original first preferences and allocate the
	// original ballots of all excluded sources. Only the surviving set
	// changes. An inherited ballot therefore keeps its own source ranking.
	while (candidates.size() > 2) {
		auto const totals = allocateBallots(candidates, profiles, roles, nationalIncoming);
		auto const lowest = std::min_element(totals.begin(), totals.end());
		candidates.erase(candidates.begin() + (lowest - totals.begin()));
	}
	auto const totals = allocateBallots(candidates, profiles, roles, nationalIncoming);
	auto validTotal = [](double value) { return value >= -Roundoff && value <= 1.0 + Roundoff; };
	if (!validTotal(totals[0]) || !validTotal(totals[1]) ||
		std::abs(totals[0] + totals[1] - 1.0) > TotalTolerance) {
		result.issue = "The preference count did not conserve the district's formal votes.";
		return result;
	}
	result.final = finalPair(candidates, totals);
	result.final.firstShare = std::clamp(result.final.firstShare, 0.0, 1.0);
	if (classicAvailable && !matchTpp) {
		// Hold the reported result of a classic final at the accepted TPP.
		// A change to the winner here would measure the missing adjustment
		// directly. This test measures its effect on exclusions and on
		// other final pairings.
		bool const laborFirst = candidates[0] == roles.labor;
		int const opponent = candidates[laborFirst ? 1 : 0];
		if ((laborFirst || candidates[1] == roles.labor) &&
			(opponent == roles.liberal || opponent == roles.national)) {
			result.final.firstShare = laborFirst ? laborTpp : 1.0 - laborTpp;
		}
	}
	result.status = Status::Compared;
	return result;
}

void PairComparisonSummary::record(PairResult const& reference, PairResult const& alternative)
{
	++compared;
	bool const winnerChanged = reference.winner() != alternative.winner();
	if (winnerChanged) ++changedWinner;
	auto addOutcome = [this](PairResult const& result, std::size_t side) {
		auto& outcome = pairs[result.candidates].outcomes[side];
		++outcome.draws;
		if (result.winner() == result.candidates[0]) ++outcome.firstWins;
		outcome.firstShareSum += result.firstShare;
	};
	addOutcome(reference, 0);
	addOutcome(alternative, 1);
	if (reference.candidates != alternative.candidates) {
		++changedPair;
	}
	else {
		auto& pair = pairs.at(reference.candidates);
		++pair.samePairDraws;
		if (winnerChanged) ++pair.samePairWinnerChanges;
		double const change = alternative.firstShare - reference.firstShare;
		pair.samePairShareChange += change;
		pair.samePairAbsoluteChange += std::abs(change);
	}
}

bool Summary::recordCount(CountResult const& model, int iteration)
{
	if (model.status == Status::FinalOfficial) {
		++official;
		return false;
	}
	if (model.status != Status::Compared) {
		if (model.status == Status::InfeasibleClassicTpp) ++infeasible;
		else ++invalid;
		if (!firstIssue || iteration < firstIssue->iteration) firstIssue = Example{iteration, model};
		return false;
	}
	++compared;
	classicPairs += static_cast<std::uint64_t>(model.classic.pairsChecked);
	maximumClassicError = std::max(maximumClassicError, model.classic.maximumError);
	sumLargestRateChange += model.classic.largestRateChange;
	if (model.classic.largestRateChange > 0.0 &&
		(!largestAdjustment ||
			model.classic.largestRateChange > largestAdjustment->result.classic.largestRateChange ||
			(model.classic.largestRateChange == largestAdjustment->result.classic.largestRateChange &&
				iteration < largestAdjustment->iteration))) {
		largestAdjustment = Example{iteration, model};
	}
	return true;
}

void Summary::record(Comparison const& comparison, int iteration)
{
	auto const& model = comparison.model;
	if (!recordCount(model, iteration)) return;
	++pairs[comparison.legacy.candidates].first;
	++pairs[model.final.candidates].second;
	if (comparison.legacy.candidates != model.final.candidates) ++changedPair;
	else {
		++samePair;
		samePairAbsoluteChange += std::abs(model.final.firstShare - comparison.legacy.firstShare);
		samePairLiveAbsoluteChange += std::abs(comparison.modelWithLiveEvidence.firstShare -
			comparison.legacyWithLiveEvidence.firstShare);
	}
	if (comparison.legacy.winner() != model.final.winner()) ++changedWinner;
	int const existingWinner = comparison.legacyWithLiveEvidence.winner();
	int const modelWinner = comparison.modelWithLiveEvidence.winner();
	if (existingWinner != modelWinner) ++changedWinnerWithLiveEvidence;
	++winners[existingWinner].first;
	++winners[modelWinner].second;
	if (comparison.modelLivePairMatched) ++livePairMatches;

	if (comparison.withoutClassicTppMatching || comparison.reducedLaborToGreens) {
		standardVsRanking.record(comparison.legacy, model.final);
	}
	auto recordTest = [&](std::optional<CountResult> const& alternative, PairComparisonSummary& summary) {
		if (!alternative) return;
		if (alternative->status == Status::Compared) summary.record(model.final, alternative->final);
		else ++summary.failed;
	};
	recordTest(comparison.withoutClassicTppMatching, withoutClassicTppMatching);
	recordTest(comparison.reducedLaborToGreens, reducedLaborToGreens);
}

}
