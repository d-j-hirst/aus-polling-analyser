// Allocate compulsory-preferential ballots from fixed original sources.
// The model takes the simulator's prepared FP and TPP as inputs and returns
// a final pair and TCP for either comparison or direct use in the forecast.
#include "PreferenceModel.h"

#include "Date.h"
#include "MajorPartyFpMath.h"
#include "RandomGenerator.h"
#include "json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>

namespace PreferenceModel {

void applyLiberalProxyForNationals(ConfiguredFlows& flows, int liberal, int national,
	int labor, int oneNation)
{
	if (national < 0 || oneNation < 0) return;
	auto const liberalFlows = flows.find(liberal);
	if (liberalFlows == flows.end()) return;

	// A project can supply direct evidence from Nationals voters, such as
	// the Hunter count from 2025. For earlier elections the configured
	// Liberal split is a proxy. Copy only this pair, in both orders, so the
	// answer is the same whichever finalist the count asks about first.
	for (auto const& pair : {std::pair{oneNation, labor}, std::pair{labor, oneNation}}) {
		auto const liberalRate = liberalFlows->second.find(pair);
		if (liberalRate != liberalFlows->second.end()) {
			flows[national].try_emplace(pair, liberalRate->second);
		}
	}
}

double proximityForConsistency(int consistency) {
	static constexpr std::array weights{1.2,1.4,1.8};
	return weights[std::clamp(consistency,0,2)];
}

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
Rankings threeWayOrders(int a, int b, std::optional<int> third, Nodes const& nodes, double moveCToLast = 0.0,
	double reduceAOverB = 0.0)
{
	auto const [aBeforeB, cFirstWhenA, cLastWhenA, cFirstWhenB, cLastWhenB] = nodes;
	// When the project groups the third party in Others, the first fitted
	// branch already gives the share preferring A to B. Only those two can
	// enter the ranking. A local reduction still applies to the A-first share.
	if (!third) {
		double const ahead = aBeforeB * (1.0 - reduceAOverB);
		return {{{a, b}, ahead}, {{b, a}, 1.0 - ahead}};
	}
	int const c = *third;
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

// Prepare a different Liberal-to-One-Nation centre against Labor without
// changing either candidate's head-to-head centre against the Greens. The
// input describes Liberal ballots ranked among Labor, Greens and One Nation;
// the signed change is a fraction of those ballots, not of the seat vote.
// Swap only adjacent Labor/One Nation positions, leaving the Greens above
// both or below both. For example, ALP > ON > GRN becomes ON > ALP > GRN.
// This keeps a single coherent set of orders for all later exclusions.
Nodes liberalGeographicMeans(Nodes const& means, double change)
{
	if (change == 0.0) return means;
	// threeWayOrders returns ALP/GRN/ON, ALP/ON/GRN, GRN/ALP/ON,
	// GRN/ON/ALP, ON/ALP/GRN and ON/GRN/ALP, in that order.
	auto orders = threeWayOrders(0, 1, 2, means);
	int const firstFrom = change > 0.0 ? 1 : 4;
	int const secondFrom = change > 0.0 ? 2 : 3;
	int const firstTo = change > 0.0 ? 4 : 1;
	int const secondTo = change > 0.0 ? 3 : 2;
	double const available = orders[firstFrom].fraction + orders[secondFrom].fraction;
	if (std::abs(change) > available + Roundoff) {
		// Reject incompatible input rather than quietly changing another pair's
		// expectation or reducing the requested regional contrast.
		throw std::invalid_argument("Liberal regional preference gap cannot preserve the configured Greens comparisons.");
	}
	double const fractionToMove = available > 0.0 ? std::min(1.0, std::abs(change) / available) : 0.0;
	for (auto const [from, to] : {std::pair{firstFrom, firstTo}, std::pair{secondFrom, secondTo}}) {
		double const moved = orders[from].fraction * fractionToMove;
		orders[from].fraction -= moved;
		orders[to].fraction += moved;
	}
	// Convert the revised orders back to the five conditional shares used
	// by the usual election/district sampling. An empty branch has no votes;
	// retain its old conditional share rather than dividing by zero.
	Nodes adjusted = means;
	if (means[0] > 0.0) adjusted[1] = orders[4].fraction / means[0];
	double const laborFirst = orders[0].fraction + orders[1].fraction;
	if (laborFirst > 0.0) adjusted[2] = orders[0].fraction / laborFirst;
	if (means[0] < 1.0) adjusted[3] = orders[5].fraction / (1.0 - means[0]);
	double const greensFirst = orders[2].fraction + orders[3].fraction;
	if (greensFirst > 0.0) adjusted[4] = orders[2].fraction / greensFirst;
	return adjusted;
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

// Place the independent or independent-like candidate in the existing
// ballot orders using the fractions that prefer them to each opponent.
// The two opponents retain their relative order. The second target may
// need to move if the three comparisons cannot describe the same ballots.
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

struct RecipientComparison {
	int opponent;
	double ahead;
};

// Spell out all possible positions for a recipient while keeping the order
// of the other candidates. The weights describe relative appeal for the
// source's voters, using the same placement probabilities as the ordinary
// allocator. A known head-to-head fraction can set the total on each side
// of one opponent. Within each side the weights still decide where the
// recipient fits. For example, a Liberal ballot that ranks One Nation above
// Labor can put an independent ahead of either when the independent beats
// Labor, and both choices leave the One Nation–Labor order intact.
Rankings expandRecipient(Rankings const& rankings, int recipient, Shares const& weights,
	std::optional<RecipientComparison> comparison = {})
{
	Rankings expanded;
	expanded.reserve(rankings.size() * (rankings.front().order.size() + 1));
	std::vector<double> placements(rankings.front().order.size() + 1);
	double const recipientRate = weights.at(recipient);
	for (auto const& ranking : rankings) {
		if (ranking.fraction == 0.0) continue;
		double remainingRate = 0.0;
		for (int candidate : ranking.order) remainingRate += weights.at(candidate);
		double survival = 1.0;
		for (std::size_t i = 0; i < ranking.order.size(); ++i) {
			double const before = recipientRate / (remainingRate + recipientRate);
			placements[i] = survival * before;
			survival *= 1.0 - before;
			remainingRate -= weights.at(ranking.order[i]);
		}
		placements.back() = survival;
		if (comparison) {
			auto const split = position(ranking, comparison->opponent) + 1;
			auto reweight = [](std::span<double> side, double target) {
				double const total = std::accumulate(side.begin(), side.end(), 0.0);
				for (double& value : side) value = target * (value / total);
			};
			auto shares = std::span(placements);
			reweight(shares.first(split), comparison->ahead);
			reweight(shares.subspan(split), 1.0 - comparison->ahead);
		}
		for (std::size_t i = 0; i < placements.size(); ++i) {
			addPlacement(expanded, ranking, recipient, i, placements[i]);
		}
	}
	return expanded;
}

int historicalVoteCount(json const& value)
{
	double const votes = value.get<double>();
	if (!value.is_number_integer() || votes < 0 || votes > std::numeric_limits<int>::max()) {
		throw std::invalid_argument("Historical transfers need nonnegative integer vote counts.");
	}
	return static_cast<int>(votes);
}

// Commissioner feeds use Australian civil clocks without an offset. Apply
// their jurisdiction's daylight-saving rules independently of the computer
// running the replay. Dated manual observations supply their offset directly.
int sourceOffset(Date day, int hour, std::string_view jurisdiction)
{
	if (jurisdiction == "wa") return 8 * 60;
	if (jurisdiction == "qld") return 10 * 60;
	auto firstSunday = [&](int month) {
		auto const first = *Date::fromYmd(day.year(), month, 1);
		int const weekday = ((first.modifiedJulianDay() + 3) % 7 + 7) % 7; // Sunday is zero
		return first + (7 - weekday) % 7;
	};
	// These are the harmonised rules used by the supported ranking replays.
	// AEC Created and the eastern capture archive use eastern time.
	auto const april = firstSunday(4);
	auto const october = firstSunday(10);
	bool const daylight = day < april || day > october ||
		(day == april && hour < 2) || (day == october && hour >= 3);
	return (jurisdiction == "sa" ? 570 : 600) + (daylight ? 60 : 0);
}

std::int64_t observationInstant(std::string_view stamp, std::string_view jurisdiction,
	bool requireOffset)
{
	auto invalid = [&]() { return std::invalid_argument("Invalid preference-count timestamp: " + std::string(stamp)); };
	auto number = [&](std::size_t start, std::size_t length) {
		int value = 0;
		auto const end = stamp.data() + start + length;
		auto const parsed = std::from_chars(stamp.data() + start, end, value);
		if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0 || stamp[start] == '-') throw invalid();
		return value;
	};
	if (stamp.size() < 19 || stamp[10] != 'T' || stamp[13] != ':' || stamp[16] != ':') throw invalid();
	auto const date = Date::parseIso(stamp.substr(0, 10));
	int const hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
	if (!date || hour > 23 || minute > 59 || second > 59) throw invalid();
	std::size_t zone = 19;
	int millis = 0;
	if (zone < stamp.size() && stamp[zone] == '.') {
		std::size_t const first = ++zone;
		int place = 100;
		while (zone < stamp.size() && stamp[zone] >= '0' && stamp[zone] <= '9') {
			millis += (stamp[zone++] - '0') * place;
			place /= 10;
		}
		if (zone == first) throw invalid();
	}
	int offset = 0;
	if (zone == stamp.size()) {
		if (requireOffset) throw invalid();
		offset = sourceOffset(*date, hour, jurisdiction);
	}
	else if (stamp.substr(zone) != "Z") {
		if (stamp.size() != zone + 6 || (stamp[zone] != '+' && stamp[zone] != '-') ||
			stamp[zone + 3] != ':') throw invalid();
		int const hours = number(zone + 1, 2), minutes = number(zone + 4, 2);
		if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) throw invalid();
		offset = (hours * 60 + minutes) * (stamp[zone] == '-' ? -1 : 1);
	}
	return ((std::int64_t(date->modifiedJulianDay()) * 24 + hour) * 60 * 60 +
		(minute - offset) * 60 + second) * 1000 + millis;
}

PairResult finalPair(std::span<int const> candidates, std::span<double const> totals)
{
	return {{candidates[0], candidates[1]}, totals[0], {}};
}

// A neighbouring Liberal/National pair in outside voters' rankings has the
// combined attraction of a single Coalition slot. The sampled ordering
// within the slot applies throughout the count, even if both are finalists.
std::vector<double> allocateBallots(std::span<int const> survivors,
	auto const& profiles, Roles const& roles, double nationalIncoming)
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
		// Both sets of rankings belong to the same original first-preference
		// source. Keep their weights through every exclusion so ballots
		// inherited from that source retain their ordering.
		auto allocate = [&](auto const& ballots, double primary) {
			if (ballots.coalitionSlot && nat != survivors.end()) {
				auto const shares = ballots.ranking.allocate(coalitionSurvivors);
				for (std::size_t i = 0; i < coalitionSurvivors.size(); ++i) {
					int const id = coalitionSurvivors[i];
					double const amount = shares[i] * primary;
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
				auto const shares = ballots.ranking.allocate(survivors);
				for (std::size_t i = 0; i < totals.size(); ++i) totals[i] += shares[i] * primary;
			}
		};
		if (profile.historical) {
			allocate(profile.general, (1.0 - profile.historicalWeight) * profile.primary);
			allocate(*profile.historical, profile.historicalWeight * profile.primary);
		}
		else allocate(profile.general, profile.primary);
	}
	return totals;
}

}

std::map<std::string, CountObservation> loadCountObservations(std::istream& input,
	std::string_view cutoff, std::string_view jurisdiction)
{
	auto const document = json::parse(input);
	auto const& rows = document.at("observations");
	if (document.at("schema_version") != 1 || !rows.is_array())
		throw std::invalid_argument("Expected preference-count schema version 1 and an observations list.");
	if (rows.empty()) return {};
	auto const cutoffInstant = observationInstant(cutoff, jurisdiction, false);
	std::set<std::pair<std::string, std::int64_t>> versions;
	std::map<std::string, std::int64_t> latest;
	std::map<std::string, CountObservation> selected;
	for (auto const& row : rows) {
		CountObservation entry;
		entry.district = row.at("district").get<std::string>();
		entry.availableAt = row.at("available_at").get<std::string>();
		auto invalid = [&](std::string_view details) {
			return std::invalid_argument(entry.district + " (" + entry.availableAt + "): " + std::string(details));
		};
		auto const instant = observationInstant(entry.availableAt, jurisdiction, true);
		if (instant > cutoffInstant) continue;
		if (!versions.emplace(entry.district, instant).second)
			throw invalid("Two observations for the same district have the same availability time.");
		entry.sample = row.at("sample").get<std::string>();
		entry.source = row.at("source").get<std::string>();
		auto const& ballots = row.at("sampled_preference_ballots");
		entry.ballots = ballots.get<int>();
		entry.additionalSd = row.at("additional_sd_pp").get<double>() / 100.0;
		auto const& flows = row.at("flow_percent");
		if (entry.district.empty() || entry.sample.empty() || entry.source.empty() ||
			!ballots.is_number_integer() || ballots.get<double>() != entry.ballots || entry.ballots <= 0 ||
			!std::isfinite(entry.additionalSd) || entry.additionalSd <= 0 || entry.additionalSd > 1 ||
			!flows.is_object() || flows.size() < 3 || flows.size() > 4) {
			throw invalid("Expected a positive ballot count, uncertainty, source, and a three- or four-way split.");
		}
		double total = 0.0;
		for (auto const& [candidate, value] : flows.items()) {
			double const rate = value.get<double>() / 100.0;
			if (candidate.empty() || !fraction(rate)) throw invalid("Invalid recipient or transfer percentage.");
			entry.flows.emplace(candidate, rate);
			total += rate;
		}
		// Whole percentages can sum to 99 or 101 through rounding. Treat
		// those as proportions of the same pool and normalise them together.
		if (std::abs(total - 1.0) > .02000001) throw invalid("Transfer percentages must sum to 100% within rounding.");
		for (auto& item : entry.flows) item.second /= total;
		if (row.contains("sampled_first_preferences")) {
			auto const& firstPreferences = row.at("sampled_first_preferences");
			if (!firstPreferences.is_object()) throw invalid("Expected first-preference counts by candidate.");
			std::int64_t counted = 0;
			for (auto const& [candidate, value] : firstPreferences.items()) {
				int const votes = value.get<int>();
				if (candidate.empty() || !value.is_number_integer() || value.get<double>() != votes || votes < 0)
					throw invalid("Invalid sampled first-preference count.");
				if (!votes) continue;
				entry.firstPreferences.emplace(candidate, votes);
				counted += votes;
			}
			if (counted != entry.ballots) throw invalid("Sampled first preferences must sum to the redistributed ballots.");
		}
		if (row.contains("sampled_candidate_totals")) {
			// These are published three-way tallies for a defined set of ballots.
			// Subtracting the transfers leaves each finalist's sampled FP count.
		auto const& candidateTotals = row.at("sampled_candidate_totals");
		auto const& candidateFp = row.at("sampled_finalist_first_preferences");
		if (!candidateTotals.is_object() || candidateTotals.size() != 3 || entry.flows.size() != 3
			|| !candidateFp.is_object() || candidateFp.size() != 3 || entry.firstPreferences.empty()) {
			throw invalid("Sampled candidate totals require a three-way split and all sampled first preferences.");
			}
		std::int64_t transferred = 0;
			for (auto const& [candidate, value] : candidateTotals.items()) {
				int const votes = value.get<int>();
			if (!candidateFp.contains(candidate)) throw invalid("No sampled first preference count for " + candidate + ".");
			auto const& first = candidateFp.at(candidate);
			int const firstVotes = first.get<int>();
				if (!entry.flows.contains(candidate) || entry.firstPreferences.contains(candidate)
					|| !value.is_number_integer() || value.get<double>() != votes || votes <= 0
				|| !first.is_number_integer() || first.get<double>() != firstVotes || firstVotes < 0 || firstVotes > votes) {
				throw invalid("Invalid sampled candidate total or sampled first preferences exceed that candidate's tally.");
				}
			transferred += votes - firstVotes;
				entry.candidateTotals.emplace(candidate, votes);
			entry.candidateFirstPreferences.emplace(candidate, firstVotes);
			}
		if (transferred != entry.ballots) throw invalid("Published totals and sampled first preferences must account for all redistributed ballots.");
		// Integer tallies define the observed split more precisely than the
		// rounded percentages used by a transfer-only observation.
		for (auto& [candidate, flow] : entry.flows) {
			flow = double(entry.candidateTotals.at(candidate) - entry.candidateFirstPreferences.at(candidate)) / entry.ballots;
		}
		}
		if (row.contains("tpp_prior")) {
			auto const& prior = row.at("tpp_prior");
			double const primary = prior.at("third_primary_to_labor_percent").get<double>() / 100.0;
			double const inherited = prior.at("third_inherited_to_labor_percent").get<double>() / 100.0;
			if (entry.candidateTotals.empty() || !fraction(primary) || !fraction(inherited)) {
				throw invalid("A TPP prior requires sampled candidate totals and two Labor preference percentages.");
			}
			entry.tppPrior = ThreeCandidateTppPrior{primary, inherited};
		}
		if (latest.contains(entry.district) && latest.at(entry.district) > instant) continue;
		latest[entry.district] = instant;
		auto const district = entry.district;
		selected.insert_or_assign(district, std::move(entry));
	}
	return selected;
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
	std::span<District const> districts, ConfiguredFlows const& configuredFlows,
	std::map<std::string, int> const& partyCodes)
{
	if (!input) return std::nullopt;
	auto const document = json::parse(input);
	auto const mode = document.at("mode").get<std::string>();
	if (mode == "off") return std::nullopt;
	if ((mode != "compare" && mode != "active" && mode != "live_trial") ||
		document.at("schema_version").get<int>() != 1) {
		throw std::invalid_argument("Expected schema_version 1 and preference mode off, compare, active or live_trial.");
	}

	Model model;
	model.liveTrial = mode == "live_trial";
	model.active = mode != "compare";
	model.roles = roles;
	model.modelVersion = document.at("model_version").get<std::string>();
	if (model.modelVersion.empty()) throw std::invalid_argument("The preference model needs a version.");
	model.versionKey = 0x7abe29471a980c67ULL;
	for (unsigned char const character : model.modelVersion) {
		model.versionKey = RandomGenerator::mixKey(model.versionKey, character);
	}
	model.sampleUncertainty = document.value("sample_uncertainty", true);
	model.unknownLiberalLaborFirst = document.at("unknown_liberal_labor_first_probability").get<double>();
	model.nationalFollowsElectionLiberalAdvice = document.value("national_follows_election_liberal_advice", false);
	model.independentProximityOddsMultiplier = document.value("independent_proximity_odds_multiplier", 1.0);
	if (!std::isfinite(model.independentProximityOddsMultiplier) || model.independentProximityOddsMultiplier < 1.0) {
		throw std::invalid_argument("The independent proximity odds multiplier must be finite and at least one.");
	}
	auto positive = [](double value) { return std::isfinite(value) && value > 0.0; };
	auto const generic = document.at("generic_concentrations").get<std::array<double, 2>>();
	model.genericElectionConcentration = generic[0];
	if (document.contains("pair_history_blend")) {
		auto const& settings = document.at("pair_history_blend");
		double const halfOddsRatio = settings.at("half_weight_odds_ratio").get<double>();
		double const districtSpread = settings.at("matched_district_sd_multiplier").get<double>();
		if (!std::isfinite(halfOddsRatio) || halfOddsRatio <= 1.0 || !fraction(districtSpread)) {
			throw std::invalid_argument("Invalid primary-share weighting or district spread for incumbent pair history.");
		}
		model.pairHistoryBlend = PairHistoryBlend{std::log(halfOddsRatio), districtSpread};
	}
	model.genericDistrictConcentration = generic[1];
	if (!fraction(model.unknownLiberalLaborFirst) || !positive(generic[0]) || !positive(generic[1])) {
		throw std::invalid_argument("Invalid generic uncertainty or unknown Liberal advice probability.");
	}
	std::vector mainRoles{roles.labor, roles.liberal, roles.greens, roles.independent};
	if (roles.oneNation) mainRoles.push_back(*roles.oneNation);
	if (roles.national) mainRoles.push_back(*roles.national);
	std::sort(mainRoles.begin(), mainRoles.end());
	if (mainRoles.front() < 0 || std::adjacent_find(mainRoles.begin(), mainRoles.end()) != mainRoles.end()) {
		throw std::invalid_argument("The preference model needs distinct registered parties for Labor, Liberal, Greens, independent and any separate Nationals or One Nation role.");
	}

	// Five fitted choices specify the rankings between three other parties.
	// With One Nation grouped in Others only the first choice (A over B)
	// remains. A replay can supply that alone or use an existing full profile.
	auto readBranches = [&](nlohmann::json const& values) {
		Nodes result{};
		if (!values.is_array() || (values.size() != result.size() && (roles.oneNation || values.size() != 1))) {
			throw std::invalid_argument("Expected five preference branches, or one when there is no One Nation role.");
		}
		for (std::size_t i = 0; i < values.size(); ++i) result[i] = values.at(i).get<double>();
		return result;
	};
	auto const branchCount = roles.oneNation ? Nodes{}.size() : std::size_t{1};
	// One or more primary-share points describe a source.
	// These are district primary fractions, with the outer points held fixed
	// where the evidence supplies no further relationship with vote share.
	auto const& profiles = document.at("profiles");
	std::array<std::string_view, LiberalOpen> names{
		"labor", "liberal_labor_first", "liberal_greens_first", "greens", "one_nation"};
	for (std::size_t i = 0; i < names.size(); ++i) {
		if (i == OneNation && !roles.oneNation) continue;
		auto const name = std::string(names[i]);
		try {
			auto const& values = profiles.at(name);
			auto& prior = model.priors[i];
			prior.electionConcentration = readBranches(values.at("election_concentration"));
			prior.districtConcentration = readBranches(values.at("district_concentration"));
			if (!std::all_of(prior.electionConcentration.begin(), prior.electionConcentration.begin() + branchCount, positive) ||
				!std::all_of(prior.districtConcentration.begin(), prior.districtConcentration.begin() + branchCount, positive)) {
				throw std::invalid_argument("Branch concentrations must be finite and positive.");
			}
			auto const& knots = values.at("primary_points");
			if (!knots.is_array() || knots.empty()) throw std::invalid_argument("Missing primary points.");
			for (auto const& point : knots) {
				auto const primary = point.at("primary").get<double>();
				auto const means = readBranches(point.at("means"));
				if (!fraction(primary) || !std::all_of(means.begin(), means.begin() + branchCount, fraction) ||
					(!prior.strengths.empty() && primary <= prior.strengths.back().primary)) {
					throw std::invalid_argument("Expected increasing primary fractions and preference shares between zero and one.");
				}
				prior.strengths.push_back({primary, means});
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
	if (roles.oneNation) model.prepareLiberalGeography(document.value("liberal_one_nation_regional_gap", 0.0), districts);

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
		if (roles.oneNation) model.nationalOneNationOverLabor = configured(national, *roles.oneNation, roles.labor);
	}

	auto const overrides = document.value("districts", json::object());
	if (!overrides.is_object()) throw std::invalid_argument("District settings must be keyed by district name.");
	auto remaining = overrides;
	auto sensitivity = document.value("sensitivity_tests", json::object());
	if (!sensitivity.is_object()) throw std::invalid_argument("Sensitivity tests must be keyed by district name.");
	// Canonical names cover the fitted profiles. Other named parties use their
	// project abbreviations. An independent must also match the named person.
	auto historyParty = [&](std::string const& code) {
		if (code == "ALP") return roles.labor;
		if (code == "LIB") return roles.liberal;
		if (code == "GRN") return roles.greens;
		if ((code == "ON" || code == "ONP") && roles.oneNation) return *roles.oneNation;
		if (code == "IND") return roles.independent;
		if (code == "NAT" && roles.national) return *roles.national;
		auto const found = partyCodes.find(code);
		if (found != partyCodes.end() && found->second >= 0) return found->second;
		throw std::invalid_argument("Unknown party in preference settings: " + code);
	};
	// A public flow is the share of the first party's voters preferring the
	// second named party to the third. Elections may select just the contests
	// with meaningful evidence. This list only selects rows for publication.
	if (document.contains("live_report_flows")) {
		auto const& rows = document.at("live_report_flows");
		if (!rows.is_array()) throw std::invalid_argument("Expected a list of live preference flows.");
		auto& selected = model.reportFlows.emplace();
		std::set<ReportFlow> seen;
		for (auto const& row : rows) {
			if (!row.is_array() || row.size() != 3) throw std::invalid_argument("A live flow needs three parties.");
			ReportFlow flow{historyParty(row.at(0)), historyParty(row.at(1)), historyParty(row.at(2))};
			if (flow[0] == flow[1] || flow[0] == flow[2] || flow[1] == flow[2] ||
				!seen.insert({flow[0], std::min(flow[1],flow[2]), std::max(flow[1],flow[2])}).second)
				throw std::invalid_argument("Live preference flows must be distinct comparisons between three parties.");
			selected.push_back(flow);
		}
	}
	for (auto const& district : districts) {
		Context context;
		context.otherCandidates = district.otherCandidates;
		context.regional = district.regional.value_or(false);
		context.highIndependentFlow = district.coalitionHeld;
		context.namedIndependent = !district.independentName.empty();
		context.incumbent = district.incumbent;
		if (context.incumbent) {
			int const party = context.incumbent->party;
			context.incumbentUsesIndependentPreferences = party == roles.independent ||
				std::ranges::find(district.independentLikeParties, party) != district.independentLikeParties.end();
		}
		auto const found = overrides.find(district.name);
		if (found != overrides.end()) {
			context.liveTrial = found->value("live_trial", false);
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

			if (found->contains("others_labor_greens")) {
				// Use original ballots from sources modelled together as
				// Others. These two counts give the local preference split,
				// with uncertainty about how well the old mix will carry over.
				auto const& votes = found->at("others_labor_greens");
				int const labor = historicalVoteCount(votes.at("labor_votes"));
				int const greens = historicalVoteCount(votes.at("greens_votes"));
				double const total = double(labor) + greens;
				if (total <= 0.0 || votes.at("election").get<std::string>().empty()) {
					throw std::invalid_argument("Others preference history needs an election and positive total votes in " + district.name + ".");
				}
				context.othersGreensOverLabor = greens / total;
			}

			if (found->contains("independent")) {
				auto const& independent = found->at("independent");
				if (!context.namedIndependent ||
					independent.at("name").get<std::string>() != district.independentName) {
					throw std::invalid_argument("Independent preference setting does not match the named candidate in " + district.name + ".");
				}
				auto const positionName = independent.at("position").get<std::string>();
				if (positionName == "left") context.leftIndependent = true;
				else if (positionName == "right") {
					context.highIndependentFlow = false;
					context.rightIndependent = true;
				}
				else if (positionName != "centre") throw std::invalid_argument("Invalid independent position in " + district.name + ".");
			}
			if (found->contains("independent_like")) {
				// A party-labelled candidate can have a personal following.
				// Require the seat file to identify that party explicitly.
				// The party keeps its own vote share and identity in the count.
				auto const& settings = found->at("independent_like");
				auto const code = settings.at("party").get<std::string>();
				auto const party = partyCodes.find(code);
				if (party == partyCodes.end() ||
					std::ranges::find(district.independentLikeParties, party->second) == district.independentLikeParties.end() ||
					party->second == roles.greens || party->second == roles.oneNation ||
					party->second == roles.independent) {
					throw std::invalid_argument("Expected a separate minor-party code from sIndependentLikePreferences in " + district.name + ".");
				}
				double const greensOverLabor = settings.value("greens_over_labor", 0.70);
				auto greensOverLiberal = configured(roles.greens, party->second, roles.liberal);
				if (!greensOverLiberal) greensOverLiberal = configured(roles.greens, roles.independent, roles.liberal);
				if (settings.contains("greens_over_liberal")) greensOverLiberal = settings.at("greens_over_liberal").get<double>();
				if (!fraction(greensOverLabor) || (greensOverLiberal && !fraction(*greensOverLiberal))) {
					throw std::invalid_argument("Invalid independent-like Greens preference rate in " + district.name + ".");
				}
				context.independentLike = Context::IndependentLike{party->second, greensOverLabor,
					configured(roles.labor, party->second, roles.liberal), greensOverLiberal};
			}
			bool const legacyPairHistory = found->contains("independent_pair_history");
			if (legacyPairHistory || found->contains("incumbent_pair_history")) {
				// An AEC final-pair table identifies where each donor's original
				// ballots ended up. Record that fraction for the same incumbent,
				// donor and opponent. An independent donor would also require
				// their own verified name, which these party-code rows cannot give.
				if (legacyPairHistory && found->contains("incumbent_pair_history")) {
					throw std::invalid_argument("Specify one incumbent pair history in " + district.name + ".");
				}
				auto const& history = found->at(legacyPairHistory ? "independent_pair_history" : "incumbent_pair_history");
				if (!district.incumbent || history.at("name").get<std::string>() != district.incumbent->name ||
					history.at("election").get<std::string>().empty() || history.at("source").get<std::string>().empty()) {
					throw std::invalid_argument("Pair history needs a matching named incumbent, election and source in " + district.name + ".");
				}
				auto const& flows = history.at("flows");
				if (!flows.is_array() || flows.empty()) {
					throw std::invalid_argument("Incumbent pair history needs a list of recorded flows in " + district.name + ".");
				}
				// The numerator for the old primary share is the original
				// ballots of this donor in the final-pair table. The
				// denominator is all formal votes in that historical district.
				int const formalVotes = model.pairHistoryBlend ? historicalVoteCount(history.at("formal_votes")) : 0;
				std::set<int> donors;
				for (auto const& flow : flows) {
					int const donor = historyParty(flow.at("donor").get<std::string>());
					int const opponent = historyParty(flow.at("opponent").get<std::string>());
					int const incumbentVotes = historicalVoteCount(flow.at(legacyPairHistory ? "independent_votes" : "incumbent_votes"));
					int const opponentVotes = historicalVoteCount(flow.at("opponent_votes"));
					double const total = double(incumbentVotes) + opponentVotes;
					bool const majorOpponent = opponent == roles.labor || opponent == roles.liberal || opponent == roles.national;
					if (!majorOpponent || donor == opponent || donor == district.incumbent->party ||
						donor == roles.independent || donor == roles.emergingIndependent ||
						opponent == district.incumbent->party || total <= 0.0) {
						throw std::invalid_argument("Invalid incumbent pair flow in " + district.name + ".");
					}
					std::optional<double> concentration;
					if (flow.contains("election_concentration")) {
						concentration = flow.at("election_concentration").get<double>();
						if (!std::isfinite(*concentration) || *concentration <= 0.0) {
							throw std::invalid_argument("Invalid incumbent preference uncertainty in " + district.name + ".");
						}
					}
					std::optional<double> primaryLogOdds;
					if (model.pairHistoryBlend) {
						if (total >= formalVotes || !donors.insert(donor).second) {
							throw std::invalid_argument("Blended pair history needs the formal vote total and one row per donor in " + district.name + ".");
						}
						primaryLogOdds = std::log(total / (formalVotes - total));
					}
					Context::ObservedPairFlow const observation{incumbentVotes / total, concentration, primaryLogOdds};
					if (!context.incumbentPairHistory.emplace(std::pair{donor, opponent}, observation).second) {
						throw std::invalid_argument("Repeated incumbent pair flow in " + district.name + ".");
					}
				}
			}
			if (found->contains("historical_transfers")) {
				try {
					auto const& entries = found->at("historical_transfers");
					if (!entries.is_array()) throw std::invalid_argument("Expected a list of historical transfers.");
					for (auto const& entry : entries) {
						int const donor = historyParty(entry.at("donor").get<std::string>());
						int const original = historicalVoteCount(entry.at("original_votes"));
						int const inherited = historicalVoteCount(entry.at("inherited_votes"));
						double const weight = entry.at("weight").get<double>();
						auto const& transfers = entry.at("transfers");
						// A few remaining candidates keep explicit rankings small.
						// Each row describes one exclusion from one earlier election.
						if (!transfers.is_object() || transfers.size() < 2 || transfers.size() > 4 ||
							original == 0 || !fraction(weight) || entry.at("election").get<std::string>().empty() ||
							entry.at("source").get<std::string>().empty()) {
							throw std::invalid_argument("Expected two to four recipients, positive original votes, a weight, election and source.");
						}
						std::int64_t total = 0;
						std::map<int, int> recipients;
						for (auto const& [code, value] : transfers.items()) {
							int const recipient = historyParty(code);
							int const votes = historicalVoteCount(value);
							if (recipient == donor || !recipients.emplace(recipient, votes).second) {
								throw std::invalid_argument("Historical recipients must be distinct and exclude the donor.");
							}
							total += votes;
						}
						if (total != std::int64_t(original) + inherited) {
							throw std::invalid_argument("Historical transfers must account for all original and inherited votes.");
						}
						bool const usesIndependent = donor == roles.independent || recipients.contains(roles.independent);
						if (usesIndependent && (!context.namedIndependent ||
							entry.at("independent_name").get<std::string>() != district.independentName)) {
							throw std::invalid_argument("Historical transfers must identify the configured independent by name.");
						}
						HistoricalTransfer transfer{{recipients.begin(), recipients.end()}, original, inherited, weight, usesIndependent};
						if (!context.historicalTransfers.emplace(donor, std::move(transfer)).second) {
							throw std::invalid_argument("Only one historical exclusion per donor can be used.");
						}
					}
				}
				catch (std::exception const& error) {
					throw std::invalid_argument(district.name + ": " + error.what());
				}
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
		model.contexts.push_back(std::move(context));
	}
	if (!remaining.empty()) throw std::invalid_argument("Unknown preference district: " + remaining.begin().key());
	if (!sensitivity.empty()) throw std::invalid_argument("Unknown sensitivity test district: " + sensitivity.begin().key());
	if (model.liveTrial && std::none_of(model.contexts.begin(), model.contexts.end(),
		[](Context const& context) { return context.liveTrial; })) {
		throw std::invalid_argument("A live preference trial needs at least one enabled district.");
	}
	return model;
}

SensitivityTests const& Model::sensitivityTests(int district) const
{
	return contexts.at(static_cast<std::size_t>(district)).sensitivity;
}

// The optional gap describes the regional-minus-metropolitan difference in
// Liberal preferences to ON against Labor. Centre it across equally weighted
// seats in this project, not across ballots: regional centres rise by
// gap * (1 - regional seat fraction), metropolitan centres fall by
// gap * regional seat fraction. A 10-point gap with 63/150 regional seats
// therefore moves a 70% centre to 65.8% metropolitan and 75.8% regional.
//
// The gap is an election-specific assumption, supported in direction by the
// DemosAU 2026 comparisons documented in analysis/Preferences/README.md.
// Those surveys combine Coalition voters and do not precisely estimate a
// Liberal-only gap. Keep the existing broad shared/district uncertainty.
// Preparation does this once for each advice profile and FP strength point;
// no simulated votes, sampled advice or live evidence are cached here.
void Model::prepareLiberalGeography(double regionalGap, std::span<District const> districts)
{
	if (!fraction(regionalGap)) {
		throw std::invalid_argument("Liberal regional preference gap must be a fraction between zero and one.");
	}
	if (regionalGap == 0.0) return;
	if (districts.empty() || std::any_of(districts.begin(), districts.end(),
		[](District const& district) { return !district.regional.has_value(); })) {
		throw std::invalid_argument("Liberal regional preferences require a seat type for every district.");
	}
	auto const regionalCount = std::count_if(districts.begin(), districts.end(),
		[](District const& district) { return *district.regional; });
	double const regionalFraction = double(regionalCount) / double(districts.size());
	for (int group = 0; group < 2; ++group) {
		double const change = regionalGap * (double(group) - regionalFraction);
		for (ProfileIndex const profile : {LiberalLaborFirst, LiberalGreensFirst, LiberalOpen}) {
			auto& strengths = liberalGeographicStrengths[group][profile];
			strengths = priors[profile].strengths;
			for (auto& strength : strengths) {
				strength.means = liberalGeographicMeans(strength.means, change);
			}
		}
	}
}

Model::ElectionDraw Model::drawElection(std::uint64_t seed) const
{
	ElectionDraw draw;
	draw.seed = RandomGenerator::mixKey(seed, versionKey);
	draw.liberalLaborFirst = RandomGenerator::uniform01_from_key(key(draw.seed, -1, roles.liberal, 0))
		< unknownLiberalLaborFirst;
	for (std::size_t i = 0; i < priors.size(); ++i) {
		if (i == OneNation && !roles.oneNation) continue;
		auto const& prior = priors[i];
		auto const branchCount = roles.oneNation ? prior.referenceMeans.size() : std::size_t{1};
		for (std::size_t node = 0; node < branchCount; ++node) {
			double const mean = prior.referenceMeans[node];
			draw.commonNodes[i][node] = sampleUncertainty ?
				drawBeta(mean, prior.electionConcentration[node],
					key(draw.seed, -1, static_cast<int>(i), 100 + static_cast<int>(node))) : mean;
		}
	}
	return draw;
}

Model::Nodes Model::drawNodes(ProfileIndex profile, double primary,
	std::optional<double> classicRate, int district, ElectionDraw const& election, std::uint64_t localSeed) const
{
	auto const& prior = priors[profile];
	auto const& geographic = liberalGeographicStrengths[contexts.at(district).regional ? 1 : 0][profile];
	auto const& strengths = geographic.empty() ? prior.strengths : geographic;
	auto const upper = std::upper_bound(strengths.begin(), strengths.end(), primary,
		[](double value, Strength const& strength) { return value < strength.primary; });
	Nodes means;
	if (upper == strengths.begin()) means = upper->means;
	else if (upper == strengths.end()) means = strengths.back().means;
	else {
		auto const lower = std::prev(upper);
		double const t = (primary - lower->primary) / (upper->primary - lower->primary);
		for (std::size_t node = 0; node < means.size(); ++node) {
			means[node] = std::lerp(lower->means[node], upper->means[node], t);
		}
	}
	auto const branchCount = roles.oneNation ? means.size() : std::size_t{1};
	for (std::size_t node = 0; node < branchCount; ++node) {
		if (node == 0 && classicRate) {
			means[node] = *classicRate;
			continue;
		}
		if (!sampleUncertainty || election.central) continue;
		double const referenceMean = prior.referenceMeans[node];
		double const desiredMean = means[node];
		double common = election.commonNodes[profile][node];
		// Reuse the common draw when the mean changes with FP or geography.
		// Scale toward the relevant endpoint to give the desired
		// mean and keep the same ordering of shared draws. This is exact
		// at the reference point and an interpolation assumption elsewhere.
		// Geography retains both levels of uncertainty and the same random
		// streams; it does not add a second preference draw. Unconfigured
		// elections use the original means and calculations unchanged.
		if (desiredMean < referenceMean) common *= desiredMean / referenceMean;
		else if (desiredMean > referenceMean) {
			double const movement = (desiredMean - referenceMean) / (1.0 - referenceMean);
			common = std::lerp(common, 1.0, movement);
		}
		means[node] = election.sharedOnly ? common : drawBeta(common, prior.districtConcentration[node],
			key(localSeed, district, profile, 100 + static_cast<int>(node)));
	}
	return means;
}

double Model::sampleFallback(double mean, int district, int source, int branch,
	ElectionDraw const& election, std::uint64_t localSeed, std::optional<double> districtConcentration,
	std::optional<double> electionConcentration) const
{
	if (!sampleUncertainty || election.central) return mean;
	double const common = drawBeta(mean, electionConcentration.value_or(genericElectionConcentration),
		key(election.seed, -1, source, branch));
	if (election.sharedOnly) return common;
	return drawBeta(common, districtConcentration.value_or(genericDistrictConcentration),
		key(localSeed, district, source, branch));
}

double Model::recipientWeight(Source const& source, Source const& recipient,
	int district, ElectionDraw const& election, std::uint64_t localSeed) const
{
	int const distance = source.id == roles.others ? 0 : std::abs(source.ideology - recipient.ideology);
	double weight = std::pow(source.proximityBase, -distance);
	if (sampleUncertainty && !election.central) {
		// At an even split, moving the difference in log weights by 0.4
		// moves the share by about ten points. Divide the intended spread
		// between the two weights. These assumptions give uncertain
		// proximity estimates, including for the otherwise even Others pool.
		std::uint64_t const ids = RandomGenerator::combinePartyIds(source.id, recipient.id);
		auto const sharedSourceRecipient = RandomGenerator::mixKey(election.seed, ids);
		auto const localSourceRecipient = RandomGenerator::mixKey(localSeed, ids);
		double const common = RandomGenerator::normal_from_key(key(sharedSourceRecipient, -1, 0, 300), 0.0f, 0.2828427f);
		double const local = election.sharedOnly ? 0.0 :
			RandomGenerator::normal_from_key(key(localSourceRecipient, district, 0, 300), 0.0f, 0.4242641f);
		weight *= std::exp(common + local);
	}
	return weight;
}

bool Model::isIndependent(Source const& source) const
{
	return source.additionalIndependent || (source.confirmedIndependent &&
		(source.id == roles.independent || source.id == roles.emergingIndependent));
}

std::vector<Ranking> Model::coreRankings(Source const& source, int coalition,
	std::optional<double> classicRate, int district, ElectionDraw const& election,
	double laborToGreensReduction, std::uint64_t localSeed) const
{
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	if (source.id == roles.labor) {
		return threeWayOrders(roles.greens, coalition, roles.oneNation,
			drawNodes(Labor, source.primary, {}, district, election, localSeed), 0.0, laborToGreensReduction);
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
				context.liberalOneNationLastWeight, district, source.id, 206, election, localSeed);
		}
		return threeWayOrders(roles.labor, roles.greens, roles.oneNation,
			drawNodes(variant, source.primary, {}, district, election, localSeed), moveOneNationToLast);
	}
	if (source.id == roles.national) {
		double greensCentre = nationalGreensOverLabor.value_or(0.5);
		if (nationalFollowsElectionLiberalAdvice) {
			// The federal forecast can draw one election-wide Coalition advice
			// direction for Labor versus Greens. Use the corresponding Liberal
			// head-to-head centre for Nationals voters while retaining the
			// broader Nationals uncertainty. Local Liberal cards apply only to
			// Liberal ballots. For Nationals voters, One Nation versus Labor
			// still uses its separately configured preference rate.
			ProfileIndex const advice = election.liberalLaborFirst ? LiberalLaborFirst : LiberalGreensFirst;
			greensCentre = 1.0 - priors[advice].referenceMeans.front();
		}
		double const greens = sampleFallback(greensCentre, district, source.id, 201, election, localSeed);
		if (!roles.oneNation) {
			return {{{roles.greens, roles.labor}, greens}, {{roles.labor, roles.greens}, 1.0 - greens}};
		}
		double const oneNation = sampleFallback(nationalOneNationOverLabor.value_or(0.5), district, source.id, 202, election, localSeed);
		auto odds = [](double rate) {
			rate = std::clamp(rate, Roundoff, 1.0 - Roundoff);
			return rate / (1.0 - rate);
		};
		return weightedCore({roles.labor, roles.greens, *roles.oneNation},
			{{roles.labor, 1.0}, {roles.greens, odds(greens)}, {*roles.oneNation, odds(oneNation)}});
	}
	if (source.id == roles.greens) {
		return threeWayOrders(roles.labor, coalition, roles.oneNation,
			drawNodes(Greens, source.primary, classicRate, district, election, localSeed));
	}
	if (source.id == roles.oneNation) {
		return threeWayOrders(roles.labor, coalition, roles.greens,
			drawNodes(OneNation, source.primary, classicRate, district, election, localSeed));
	}
	double const rate = classicRate.value_or(source.classicRate);
	return {{{roles.labor, coalition}, rate}, {{coalition, roles.labor}, 1.0 - rate}};
}

// The historical count tells us the highest-ranked candidate among those
// still standing at the time. Update that part of the ranking, retaining
// the classic TPP split and the chances of other candidates ranking ahead
// of the entire observed group. The same adjusted orders then serve every
// exclusion, including counts with a different group of survivors.
void Model::applyHistoricalTransfer(Rankings& rankings, Shares const& weights, int district,
	int source, int independent, Shares const& primaries, ElectionDraw const& election,
	bool namedIndependentMatches, std::uint64_t localSeed) const
{
	auto const& transfers = contexts.at(static_cast<std::size_t>(district)).historicalTransfers;
	auto found = transfers.find(source);
	if (found == transfers.end() && source == independent) found = transfers.find(roles.independent);
	if (found == transfers.end()) return;
	auto const& observation = found->second;
	if (observation.weight == 0.0 || (observation.usesNamedIndependent && !namedIndependentMatches)) return;

	Shares historicalVotes;
	for (auto const& [historicalId, votes] : observation.destinations) {
		int candidate = historicalId == roles.independent ? independent : historicalId;
		// The old round does not say where an absent recipient's ballots go.
		// For an outside source with only a National candidate on the current
		// ballot, the Liberal ranking slot stands for that National candidate.
		if (share(primaries, candidate) <= 0.0) return;
		if (candidate == roles.national && share(primaries, roles.liberal) == 0.0 &&
			source != roles.liberal && source != roles.national) candidate = roles.liberal;
		if (!weights.contains(candidate)) return;
		historicalVotes.emplace(candidate, votes);
	}
	for (auto const& recipient : historicalVotes) {
		if (position(rankings.front(), recipient.first) == rankings.front().order.size()) {
			rankings = expandRecipient(rankings, recipient.first, weights);
		}
	}

	// The published totals include ballots passed on from earlier exclusions.
	// At the centre, spread those ballots in proportion to the recorded
	// transfers. In a draw, put the destination totals end to end on a circle
	// and mark a randomly placed stretch the size of the inherited pile.
	// This allows inherited votes to favour particular candidates. Every
	// ballot has the same chance of being marked, so the average matches
	// the central estimate, and a destination cannot lose more than it got.
	Shares historicalRates;
	double const pool = double(observation.originalVotes) + observation.inheritedVotes;
	if (sampleUncertainty && !election.central && !election.averageHistoricalComposition && observation.inheritedVotes > 0) {
		std::vector<std::pair<int, double>> destinations(historicalVotes.begin(), historicalVotes.end());
		DrawEngine engine(key(localSeed, district, source, 208));
		std::shuffle(destinations.begin(), destinations.end(), engine);
		double const start = pool * std::generate_canonical<double, 53>(engine);
		double const end = start + observation.inheritedVotes;
		double left = 0.0;
		for (auto const& [candidate, votes] : destinations) {
			double const right = left + votes;
			double const overlap = std::max(0.0, std::min(right, end) - std::max(left, start));
			double const wrapped = std::max(0.0, std::min(right, end - pool) - left);
			historicalRates[candidate] = std::max(0.0, votes - overlap - wrapped) / observation.originalVotes;
			left = right;
		}
	}
	else {
		for (auto const& [candidate, votes] : historicalVotes) historicalRates[candidate] = votes / pool;
	}

	// For example, with One Nation ahead of all three old recipients on 7%
	// of the orders, that group keeps its 7%. Within each group, give more
	// weight to orders that put an old recipient first more often in the
	// observed count. Separate Labor-first and Coalition-first orders so
	// evidence about an exclusion cannot change the supplied classic TPP.
	using Group = std::pair<std::vector<int>, bool>;
	struct GroupTotals { double prior = 0.0, adjusted = 0.0; };
	struct OrderUpdate { int recipient; GroupTotals* totals; double adjusted = 0.0; };
	std::map<Group, GroupTotals> groups;
	std::vector<OrderUpdate> updates;
	updates.reserve(rankings.size());
	Shares priorRates;
	int const coalitionOpponent = source == roles.liberal && roles.national ? *roles.national : roles.liberal;
	for (auto const& ranking : rankings) {
		auto const first = std::find_if(ranking.order.begin(), ranking.order.end(),
			[&](int candidate) { return historicalRates.contains(candidate); });
		bool const laborFirst = position(ranking, roles.labor) < position(ranking, coalitionOpponent);
		std::vector<int> unobservedLeaders;
		std::copy_if(ranking.order.begin(), first, std::back_inserter(unobservedLeaders),
			[&](int candidate) {
				if (share(primaries, candidate) > 0.0) return true;
				// A Liberal slot can also represent a National finalist.
				return candidate == roles.liberal && source != roles.liberal && source != roles.national &&
					roles.national && share(primaries, *roles.national) > 0.0;
			});
		auto& totals = groups[{std::move(unobservedLeaders), laborFirst}];
		totals.prior += ranking.fraction;
		priorRates[*first] += ranking.fraction;
		updates.push_back({*first, &totals});
	}
	// The configured weight blends the old and current shares before the
	// group constraints. Those constraints can reduce the realised change
	// when the old split is hard to reconcile with this draw's other inputs.
	for (std::size_t i = 0; i < rankings.size(); ++i) {
		auto& update = updates[i];
		double const prior = priorRates.at(update.recipient);
		double const target = std::lerp(prior, historicalRates.at(update.recipient), observation.weight);
		if (prior > 0.0) update.adjusted = (rankings[i].fraction / prior) * target;
		update.totals->adjusted += update.adjusted;
	}
	for (std::size_t i = 0; i < rankings.size(); ++i) {
		auto const& update = updates[i];
		if (update.totals->adjusted > 0.0) {
			double const adjusted = (update.adjusted / update.totals->adjusted) * update.totals->prior;
			rankings[i].fraction = std::min(1.0, adjusted);
		}
	}
}

// Use the observed final split of this donor's ballots to place the returning
// candidate in the ordering used throughout the count. Keep the other
// candidates in their original order, including Labor against Coalition.
// Where there is no observation, the proximity rule still applies to
// independent incumbents. Party incumbents only use the observed comparisons.
void Model::applyIncumbentPairPreferences(Rankings& rankings, Shares const& weights,
	int district, Source const& donor, Source const& incumbent,
	std::array<Source const*, 3> const& majorOpponents, bool explicitCoalitionPartners,
	bool independentIncumbent, ElectionDraw const& election, std::uint64_t localSeed,
	bool usePairHistory, std::optional<double> anchoredRate) const
{
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	bool const alreadyRanked = position(rankings.front(), incumbent.id) != rankings.front().order.size();
	bool const coalitionDonor = donor.id == roles.liberal || donor.id == roles.national;
	struct Target {
		int opponentSlot;
		Context::ObservedPairFlow const* observed;
		double multiplier = 1.0;
		int randomBranch = 0;
		double ahead = 0.0;
	};
	std::array<Target, 2> targets{};
	std::size_t targetCount = 0;
	bool hasAdjustment = false;
	for (std::size_t i = explicitCoalitionPartners ? 1 : 0; i < majorOpponents.size(); ++i) {
		auto const* opponent = majorOpponents[i];
		if (!opponent || donor.id == opponent->id || incumbent.id == opponent->id) continue;
		auto const found = context.incumbentPairHistory.find({donor.id, opponent->id});
		auto const* observed = usePairHistory && found != context.incumbentPairHistory.end() ? &found->second : nullptr;
		if (i > 0 && coalitionDonor && !observed) continue;
		if (!explicitCoalitionPartners && i == 2 && majorOpponents[1] && !coalitionDonor) continue;
		// Without a Liberal on the ballot, outside donors still use the
		// Coalition slot for a National. Always look up the evidence using
		// the actual opponent before translating to the internal slot.
		int const opponentSlot = i == 2 && !explicitCoalitionPartners && !coalitionDonor ? roles.liberal : opponent->id;
		double multiplier = 1.0;
		if (!observed && independentIncumbent && !alreadyRanked && !context.rightIndependent) {
			int const incumbentDistance = std::abs(donor.ideology - incumbent.ideology);
			int const opponentDistance = std::abs(donor.ideology - opponent->ideology);
			if (incumbentDistance < opponentDistance) multiplier = independentProximityOddsMultiplier;
			else if (incumbentDistance == opponentDistance) multiplier = std::sqrt(independentProximityOddsMultiplier);
		}
		int const branch = i == 0 ? 213 : i == 1 ? 214 : 215;
		targets[targetCount++] = {opponentSlot, observed, multiplier, branch};
		hasAdjustment = hasAdjustment || observed || multiplier > 1.0;
	}
	if (!hasAdjustment) return;

	// Labor voters may already have a separate comparison with One Nation.
	// Keep that sampled comparison when adding evidence against the
	// Coalition. The observed comparison takes priority if the two cannot
	// fit the same rankings. The older unweighted independent setting keeps
	// its original placement rule.
	if ((!independentIncumbent || anchoredRate) && alreadyRanked && targetCount == 1 &&
		donor.id == roles.labor && roles.oneNation && incumbent.id != roles.oneNation && weights.contains(*roles.oneNation)) {
		targets[targetCount++] = {*roles.oneNation, nullptr};
	}

	std::optional<RankingProfile> baseline;
	for (auto& target : std::span(targets).first(targetCount)) {
		if (target.observed) {
			if (anchoredRate) target.ahead = *anchoredRate;
			else target.ahead = sampleFallback(target.observed->incumbentShare, district, donor.id,
				target.randomBranch, election, localSeed, {}, target.observed->electionConcentration);
		}
		else {
			if (!baseline) baseline.emplace(rankings, weights);
			std::array const pair{incumbent.id, target.opponentSlot};
			double const prior = baseline->allocate(pair).front();
			target.ahead = target.multiplier * prior / std::lerp(1.0, target.multiplier, prior);
		}
	}

	// Removing the old placements recovers the distribution of orders for
	// all the other parties. Insert the incumbent back using the new targets.
	if (alreadyRanked) {
		std::map<std::vector<int>, double> otherOrders;
		for (auto& ranking : rankings) {
			std::erase(ranking.order, incumbent.id);
			otherOrders[std::move(ranking.order)] += ranking.fraction;
		}
		rankings.clear();
		for (auto const& [order, fraction] : otherOrders) rankings.push_back({order, fraction});
	}
	if (targetCount == 1) {
		auto const& target = targets.front();
		rankings = expandRecipient(rankings, incumbent.id, weights, RecipientComparison{target.opponentSlot, target.ahead});
		return;
	}
	if (!targets[0].observed && targets[1].observed) std::swap(targets[0], targets[1]);
	auto const& [first, second] = targets;
	rankings = insertAgainstTwo(rankings, incumbent.id, first.opponentSlot, second.opponentSlot, first.ahead, second.ahead);
}

// Move the general forecast's preference movement to the observed centre.
// A share such as 0.95 has little room to rise. Shift on the log-odds
// scale to allow a natural tail without piling ballots at 0% or 100%.
// When the new centre is closer to 50%, the cap keeps a small movement
// from becoming larger on the percentage-point scale.
double anchoredHistoricalRate(double observed, double central, double shared, double sampled,
	double districtSpread)
{
	auto interior = [](double rate) { return std::clamp(rate, Roundoff, 1.0 - Roundoff); };
	auto logOdds = [](double rate) { return std::log(rate / (1.0 - rate)); };
	double const reference = interior(central);
	double const centre = interior(observed);
	double const movement = std::lerp(shared, sampled, districtSpread);
	if (movement == central) return observed;
	double const scale = std::min(1.0, reference * (1.0 - reference) / (centre * (1.0 - centre)));
	double const adjustedLogOdds = logOdds(centre) + scale * (logOdds(interior(movement)) - logOdds(reference));
	return 1.0 / (1.0 + std::exp(-adjustedLogOdds));
}

// Build the rankings once for the original first-preference groups. Live
// preparation queries these same profiles at their central assumptions, while
// the count uses a sampled set of profiles for every exclusion in a district.
// When only one source needs to transfer, build just its profile. The full
// ballot still supplies the possible recipients and their political positions.
std::vector<Model::SourceProfile> Model::buildSourceProfiles(int district,
	std::span<Source const> ballot, Shares const& primaries, CoalitionPreference::Leakage leakage, double nationalIncoming,
	bool dual, bool classicAvailable, Shares const& classicRates, ElectionDraw const& election,
	Sensitivity sensitivity, bool namedIndependentMatches, std::optional<std::uint64_t> alternativeSeed,
	std::optional<int> onlySource, bool usePairHistory) const
{
	auto const localSeed = alternativeSeed.value_or(election.seed);
	int const independent = share(primaries, roles.independent) > 0.0 ? roles.independent : roles.emergingIndependent;
	bool const hasAdditionalIndependent = std::any_of(ballot.begin(), ballot.end(), [&](Source const& candidate) {
		return candidate.primary > 0.0 && candidate.id != independent && isIndependent(candidate);
	});
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	// The main and emerging IND slots can each hold a candidate, and the
	// live count can identify another by commission ID. Treat all those
	// candidates alike using the FP actually represented in this draw.
	auto independentRecipient = [&](Source const& candidate) {
		return candidate.id == roles.independent || candidate.id == roles.emergingIndependent ||
			candidate.additionalIndependent;
	};
	int const separateIndependents = int(std::count_if(ballot.begin(), ballot.end(), [&](auto const& candidate) {
		return candidate.primary > 0.0 && independentRecipient(candidate);
	}));
	int const groupedCandidates = context.otherCandidates.remaining(separateIndependents,
		share(primaries, EmergingPartyIndex) > 0.0);
	auto findOnBallot = [&](int party) -> Source const* {
		auto const found = std::ranges::find(ballot, party, &Source::id);
		return found != ballot.end() && found->primary > 0.0 ? &*found : nullptr;
	};
	Source const* incumbent = nullptr;
	bool const independentIncumbent = context.incumbentUsesIndependentPreferences;
	if (context.incumbent && (context.leftIndependent || !context.incumbentPairHistory.empty() ||
		(independentIncumbent && independentProximityOddsMultiplier > 1.0))) {
		int const party = context.incumbent->party;
		auto const* candidate = findOnBallot(party);
		if (candidate && candidate->matchesConfiguredIncumbent && (party != roles.independent || namedIndependentMatches)) {
			incumbent = candidate;
		}
	}

	// A left independent uses Labor's position for the ranking weights in
	// this district only. The higher Greens prior also applies, while a
	// replacement candidate uses the ordinary independent settings.
	std::optional<Source> leftIncumbent;
	bool const useLeftPosition = context.leftIndependent && namedIndependentMatches &&
		(!context.incumbent || (incumbent && incumbent->id == independent));
	if (useLeftPosition && incumbent && incumbent->id == roles.independent) {
		auto const labor = std::ranges::find(ballot, roles.labor, &Source::id);
		if (labor != ballot.end()) {
			leftIncumbent = *incumbent;
			leftIncumbent->ideology = labor->ideology;
			incumbent = &*leftIncumbent;
		}
	}
	bool const highIndependentFlow = context.highIndependentFlow || useLeftPosition;
	std::array<Source const*, 3> majorOpponents{};
	if (incumbent) {
		majorOpponents = {findOnBallot(roles.labor), findOnBallot(roles.liberal),
			roles.national ? findOnBallot(*roles.national) : nullptr};
	}
	double const laborToGreensReduction = sensitivity == Sensitivity::ReducedLaborToGreens ?
		context.sensitivity.laborToGreensReduction : 0.0;
	std::vector<SourceProfile> profiles;
	profiles.reserve(onlySource ? 1 : ballot.size());
	for (auto const& rawSource : ballot) {
		auto const& source = leftIncumbent && rawSource.id == leftIncumbent->id ? *leftIncumbent : rawSource;
		if (source.primary == 0.0 || (onlySource && source.id != *onlySource)) continue;
		bool const outsideCoalition = source.id != roles.liberal && source.id != roles.national;

		// This part of the original source follows the observed ranking when
		// the returning candidate and opponent are both on the ballot. The
		// historical weight falls as the donor party's primary odds move away
		// from the old election, in either direction. Compute this from each
		// draw's primary share so that a much larger group of voters can have
		// a different preference pattern.
		double historicalWeight = 0.0;
		std::optional<double> anchoredRate;
		std::optional<SourceProfile> general;
		if (usePairHistory && pairHistoryBlend && incumbent && source.id != incumbent->id) {
			for (auto const* opponent : majorOpponents) {
				if (!opponent || opponent->id == source.id || opponent->id == incumbent->id) continue;
				auto const observation = context.incumbentPairHistory.find({source.id, opponent->id});
				if (observation == context.incumbentPairHistory.end()) continue;
				double const primaryLogOdds = std::log(source.primary / std::max(Roundoff, 1.0 - source.primary));
				double const change = (primaryLogOdds - *observation->second.donorPrimaryLogOdds) /
					pairHistoryBlend->halfWeightLogOddsChange;
				historicalWeight = std::exp2(-change * change);
				if (historicalWeight == 0.0) break;
				// Evaluate the same general ranking without the historical
				// row. The shared draw leaves out local variation, while
				// the central draw supplies the starting preference rate.
				// These are for this ballot and this donor, including its
				// current primary strength and classic TPP assumptions.
				auto generalProfile = [&](ElectionDraw const& draw) {
					auto result = buildSourceProfiles(district, ballot, primaries, leakage,
						nationalIncoming, dual, classicAvailable, classicRates, draw, sensitivity,
						namedIndependentMatches, localSeed, source.id, false);
					return std::move(result.front());
				};
				general = generalProfile(election);
				anchoredRate = observation->second.incumbentShare;
				if (sampleUncertainty && !election.central) {
					auto sharedDraw = election;
					sharedDraw.sharedOnly = true;
					auto centralDraw = election;
					centralDraw.central = true;
					std::array const pair{incumbent->id, opponent->id};
					auto rate = [&](SourceProfile const& profile) {
						return allocateBallots(pair, std::span(&profile, 1), roles, nationalIncoming).front() / profile.primary;
					};
					double centralRate = 0.0;
					// Unknown Liberal advice belongs to the election-wide
					// variation. Average its two directions for the centre.
					if (source.id == roles.liberal || (source.id == roles.national && nationalFollowsElectionLiberalAdvice)) {
						for (bool laborFirst : {false, true}) {
							double const fraction = laborFirst ? unknownLiberalLaborFirst : 1.0 - unknownLiberalLaborFirst;
							if (fraction == 0.0) continue;
							centralDraw.liberalLaborFirst = laborFirst;
							centralRate += fraction * rate(generalProfile(centralDraw));
						}
					}
					else centralRate = rate(generalProfile(centralDraw));
					anchoredRate = anchoredHistoricalRate(*anchoredRate, centralRate,
						rate(generalProfile(sharedDraw)), rate(*general), pairHistoryBlend->matchedDistrictSpread);
				}
				break;
			}
		}
		bool const applyPairHistory = usePairHistory && (!pairHistoryBlend || anchoredRate.has_value());
		std::optional<double> classicRate;
		auto const fittedRate = classicRates.find(source.id);
		if (fittedRate != classicRates.end()) classicRate = fittedRate->second;
		else if (classicRates.empty() && classicAvailable) {
			// Use the original source rates from the accepted FP calculation.
			// The test varies the aggregate correction to those rates on the
			// same election draw, including when the Greens or One Nation
			// profile would otherwise use its own draw for this branch.
			classicRate = source.classicRate;
		}
		auto rankings = coreRankings(source, roles.liberal, classicRate, district, election,
			laborToGreensReduction, localSeed);

		// For a sole independent, the configured Greens comparison places
		// them relative to the major parties in the existing ballot orders.
		// With a second distinct independent, use the same ideology-based
		// weighting for both across all exclusions. Otherwise the leading
		// independent can receive fewer preferences solely because their
		// primary vote put them in the principal independent slot.
		auto const independentMean = independentOverMajor.find(source.id);
		bool const useIndependentMean = independentMean != independentOverMajor.end() &&
			(source.id != roles.greens || !hasAdditionalIndependent);
		bool const rightLaborFlow = source.id == roles.labor &&
			independent == roles.independent && context.rightIndependent;
		std::optional<double> coalitionIndependentOverLabor;
		if (share(primaries, independent) > 0.0 && (useIndependentMean || rightLaborFlow)) {
			// Other independents use the configured Labor–independent
			// comparison. For a named right independent the Labor share ranking
			// them ahead of Liberal starts at 60%, with extra local uncertainty
			// about their appeal. Retain any wider generic district spread.
			double const ahead = rightLaborFlow
				? sampleFallback(RightIndependentLaborPreferenceMean, district, source.id, 207, election, localSeed,
					std::min(RightIndependentLaborDistrictConcentration, genericDistrictConcentration))
				: sampleFallback(independentMean->second, district, source.id, 203, election, localSeed);
			if (source.id == roles.labor && roles.oneNation) {
				double const overOneNation = sampleFallback(0.80, district, source.id, 204, election, localSeed);
				rankings = insertAgainstTwo(rankings, independent, roles.liberal, *roles.oneNation, ahead, overOneNation);
			}
			else if (source.id == roles.greens && context.namedIndependent) {
				double const mean = highIndependentFlow ? 0.50 : 0.25;
				double first = mean;
				if (sampleUncertainty && !election.central) {
					double const sharedVariance = 0.06 * 0.06;
					double const districtSd = highIndependentFlow ? 0.10 : 0.14;
					double const maximum = mean * (1.0 - mean);
					double const common = drawBeta(mean, maximum / sharedVariance - 1.0,
						key(election.seed, -1, source.id, 205));
					first = election.sharedOnly ? common : drawBeta(common, (maximum - sharedVariance) / (districtSd * districtSd) - 1.0,
						key(localSeed, district, source.id, 205));
				}
				double laborBeforeCoalition = 0.0;
				for (auto const& group : rankings) {
					if (position(group, roles.labor) < position(group, roles.liberal)) laborBeforeCoalition += group.fraction;
				}
				rankings = insertGreensIndependent(rankings, independent, roles.labor, roles.liberal,
					laborBeforeCoalition, first, ahead);
			}
			else if (!outsideCoalition) {
				coalitionIndependentOverLabor = ahead;
			}
			else {
				rankings = insertBeside(rankings, independent, roles.liberal, ahead);
			}
		}

		// For an explicitly designated party-labelled candidate, use their
		// own Labor preference estimate against Liberal. For Greens voters,
		// use the local evidence when supplied. The legacy independent-like
		// split gives a default midpoint of 70% against Labor.
		// Place the candidate in the drawn rankings so both estimates can
		// influence earlier exclusions as well as the final count. The core
		// candidates retain their relative order on every ballot.
		if (context.independentLike && share(primaries, context.independentLike->party) > 0.0) {
			auto const& candidate = *context.independentLike;
			if (source.id == roles.labor && candidate.laborOverLiberal) {
				double const ahead = sampleFallback(*candidate.laborOverLiberal, district, source.id, 208, election, localSeed);
				if (roles.oneNation) {
					double const overOneNation = sampleFallback(0.80, district, source.id, 209, election, localSeed);
					rankings = insertAgainstTwo(rankings, candidate.party, roles.liberal, *roles.oneNation, ahead, overOneNation);
				} else {
					rankings = insertBeside(rankings, candidate.party, roles.liberal, ahead);
				}
			}
			else if (source.id == roles.greens) {
				double const overLabor = sampleFallback(candidate.greensOverLabor, district, source.id, 210, election, localSeed);
				if (candidate.greensOverLiberal) {
					double const overLiberal = sampleFallback(*candidate.greensOverLiberal, district, source.id, 211, election, localSeed);
					rankings = insertAgainstTwo(rankings, candidate.party, roles.labor, roles.liberal, overLabor, overLiberal);
				}
				else {
					rankings = insertBeside(rankings, candidate.party, roles.labor, overLabor);
				}
			}
		}

		// A distinct independent's voters can favour another independent with
		// a similar campaign. Historical secondary-independent exclusions in
		// Wannon (2022), Mackellar and Groom (2025) gave the stronger independent
		// about 30–54% of transfers to the main candidates. Use a twofold boost
		// to relative ranking weight as a broad assumption for the four-way
		// count, where an even starting share would rise from 25% to 40%.
		// Extra recipients compete with the existing ranking orders, so the
		// original Labor–Coalition split still holds when only those two remain.
		constexpr double SimilarIndependentWeight = 2.0;
		// Outside ballots usually keep the two Coalition partners together.
		// A measured comparison against one partner needs distinct positions
		// when both stand. Split the shared slot using the same incoming
		// Coalition split as the rest of this draw, then place the incumbent
		// relative to each. Both partners keep their order against Labor.
		bool const explicitCoalitionPartners = applyPairHistory && incumbent && dual && outsideCoalition && (
			context.incumbentPairHistory.contains({source.id, roles.liberal}) ||
			context.incumbentPairHistory.contains({source.id, *roles.national}));
		if (explicitCoalitionPartners) {
			Rankings expanded;
			for (auto const& order : rankings) {
				std::size_t const coalition = position(order, roles.liberal);
				addPlacement(expanded, order, *roles.national, coalition, nationalIncoming);
				addPlacement(expanded, order, *roles.national, coalition + 1, 1.0 - nationalIncoming);
			}
			rankings = std::move(expanded);
		}
		Shares weights;
		for (auto const& rawRecipient : ballot) {
			auto const& recipient = leftIncumbent && rawRecipient.id == leftIncumbent->id ? *leftIncumbent : rawRecipient;
			if (recipient.id == source.id || recipient.id == roles.others ||
				(outsideCoalition && !explicitCoalitionPartners && recipient.id == roles.national)) continue;
			double weight = recipientWeight(source, recipient, district, election, localSeed);
			if (source.id == roles.others && independentRecipient(recipient)) {
				// Lower the candidate's weight in generic Others rankings before
				// any exclusion. Ballots can continue to the next preference,
				// preserving the relative ordering of the other recipients.
				// A zero-primary placeholder still needs a positive numerical
				// weight for reference queries. It cannot win a simulation.
				double const factor = IndependentCandidacy::othersPreferenceWeight(
					recipient.primary, source.primary, groupedCandidates);
				weight = std::max(std::numeric_limits<double>::min(), weight * factor);
			}
			bool const rightIndependentInTransfer = context.rightIndependent &&
				(source.id == independent || recipient.id == independent);
			if (isIndependent(source) && isIndependent(recipient) &&
				source.ideology == recipient.ideology && !rightIndependentInTransfer) {
				weight *= SimilarIndependentWeight;
			}
			weights.emplace(recipient.id, weight);
		}
		// A fitted order can contain a party that is absent from the actual
		// ballot. Its latent place still defines the order of the core, but
		// only candidates with a positive primary can receive votes here.
		for (int const id : rankings.front().order) {
			weights.try_emplace(id, 1.0);
		}
		// A Coalition source can prefer an independent for reasons that
		// differ from its view of Labor. The configured independent–Labor
		// split gives only one comparison. Let the normal preference weights
		// place the independent among the other parties on either side of it.
		if (coalitionIndependentOverLabor) {
			rankings = expandRecipient(rankings, independent, weights,
				RecipientComparison{roles.labor, *coalitionIndependentOverLabor});
		}
		// Coalition ballots usually place their own partner after other
		// preferences have been modelled. A direct comparison with that
		// partner needs their position first, including the share leaking
		// to Labor. Keep the usual placement order for other comparisons.
		std::optional<int> partner;
		if (dual && !outsideCoalition) {
			partner = source.id == roles.liberal ? *roles.national : roles.liberal;
		}
		bool const observesPartner = applyPairHistory && partner && incumbent &&
			context.incumbentPairHistory.contains({source.id, *partner});
		if (observesPartner) {
			rankings = insertOwnCoalitionPartner(rankings, *partner, roles.labor, (source.id == roles.liberal ? leakage.liberal : leakage.national));
		}
		if (incumbent && incumbent->id != source.id && source.id != roles.others) {
			// Others is a changing mix of candidates, so the party-specific
			// observations and the political distance rule do not apply to it.
			applyIncumbentPairPreferences(rankings, weights, district, source, *incumbent,
				majorOpponents, explicitCoalitionPartners, independentIncumbent, election, localSeed, applyPairHistory, anchoredRate);
		}
		if (partner && !observesPartner) {
			rankings = insertOwnCoalitionPartner(rankings, *partner, roles.labor, (source.id == roles.liberal ? leakage.liberal : leakage.national));
		}
		// Others normally uses generic preference weights. A local result
		// can centre its Greens–Labor comparison while leaving the classic
		// Labor–Coalition split fixed. The generic spread represents changes
		// between elections even when the past count itself is precise.
		if (source.id == roles.others && context.othersGreensOverLabor &&
			share(primaries, roles.labor) > 0.0 && share(primaries, roles.greens) > 0.0) {
			double const ahead = sampleFallback(*context.othersGreensOverLabor, district,
				source.id, 212, election, localSeed);
			rankings = expandRecipient(rankings, roles.greens, weights, RecipientComparison{roles.labor, ahead});
		}
		if (!context.historicalTransfers.empty()) {
			applyHistoricalTransfer(rankings, weights, district, source.id, independent, primaries,
				election, namedIndependentMatches, localSeed);
		}
		bool const usesCoalitionSlot = outsideCoalition && !explicitCoalitionPartners;
		BallotRankings prepared{RankingProfile(std::move(rankings), std::move(weights)), usesCoalitionSlot};
		if (general && historicalWeight < 1.0) {
			general->historical = std::move(prepared);
			general->historicalWeight = historicalWeight;
			profiles.push_back(std::move(*general));
		}
		else profiles.push_back({source.id, source.primary, std::move(prepared)});
	}
	return profiles;
}

// Expose the identity only where the live ballot must verify a historical
// match. Ordinary districts do not need a second candidate-name check.
District::NamedIncumbent const* Model::incumbentForPairHistory(int district) const
{
	auto const& context = contexts.at(static_cast<std::size_t>(district));
	return context.incumbentPairHistory.empty() ? nullptr : &context.incumbent.value();
}

bool Model::learnsPair(Pair pair) const
{
	// Pool like contests across districts. Named independent candidates have
	// different campaigns and keep their existing district-specific treatment.
	auto coalition = [&](int id) { return id == roles.liberal || id == roles.national; };
	auto supported = [&](int id) {
		return coalition(id) || id == roles.labor || id == roles.greens || id == roles.oneNation;
	};
	bool const classic = (pair[0] == roles.labor && coalition(pair[1])) ||
		(pair[1] == roles.labor && coalition(pair[0]));
	return pair[0] != pair[1] && supported(pair[0]) && supported(pair[1]) && !classic;
}

std::map<int, std::vector<double>> Model::referenceTransferRates(int district, std::span<Source const> sources,
	std::span<int const> survivors, bool namedIndependentMatches) const
{
	Shares primaries;
	std::map<int, std::vector<double>> rates;
	for (auto const& source : sources) primaries.emplace(source.id,source.primary);
	double const liberal = share(primaries,roles.liberal);
	double const national = roles.national ? share(primaries,*roles.national) : 0.0;
	bool const dual = liberal > 0.0 && national > 0.0;
	CoalitionPreference::Leakage const leakage{};
	// Match the district's known how-to-vote advice. For unknown advice, average
	// the two possible Liberal orders using their pre-election probabilities.
	// Do not force these source rates to explain a live or booth-level TPP.
	for (bool laborFirst : {false,true}) {
		double const weight = laborFirst ? unknownLiberalLaborFirst : 1.0-unknownLiberalLaborFirst;
		if (weight == 0.0) continue;
		ElectionDraw central;
		central.central = true;
		central.liberalLaborFirst = laborFirst;
		auto profiles = buildSourceProfiles(district, sources, primaries, leakage, 0.5, dual, true, {},
			central, Sensitivity::None, namedIndependentMatches);
		for (auto const& profile : profiles) {
			auto votes = allocateBallots(survivors, std::span(&profile, 1), roles, 0.5);
			auto& sourceRates = rates.try_emplace(profile.id, survivors.size(), 0.0).first->second;
			for (std::size_t i = 0; i < votes.size(); ++i) {
				sourceRates[i] += weight * std::clamp(votes[i] / profile.primary, 0.0, 1.0);
			}
		}
	}
	return rates;
}

Shares Model::referencePairRates(int district, std::span<Source const> sources, Pair pair,
	bool namedIndependentMatches) const
{
	Shares rates;
	for (auto const& [source, split] : referenceTransferRates(district, sources, pair, namedIndependentMatches)) {
		rates.emplace(source, split.front());
	}
	return rates;
}

std::map<Pair, Shares> Model::sharedPairRates(int district, std::span<Source const> sources,
	std::span<Pair const> pairs, ElectionDraw election, bool namedIndependentMatches) const
{
	// The input FP mix determines the strength of each original party and which
	// candidates are available. A statewide estimate retains each district's
	// known ticket and geography. Its range describes a common election-wide
	// tendency, so district-specific random departures take their central values.
	election.sharedOnly = true;
	election.averageHistoricalComposition = true;
	if (pairs.empty()) return {};
	Shares primaries;
	for (auto const& source : sources) primaries.emplace(source.id,source.primary);
	double const liberal = share(primaries,roles.liberal);
	double const national = roles.national ? share(primaries,*roles.national) : 0.0;
	CoalitionPreference::Leakage const leakage{};
	auto profiles = buildSourceProfiles(district,sources,primaries,leakage,0.5,liberal > 0 && national > 0,
		true,{},election,Sensitivity::None,namedIndependentMatches);
	std::map<Pair,Shares> rates;
	for (auto const& pair : pairs) {
		auto& split = rates[pair];
		for (auto const& profile : profiles) {
			auto const votes = allocateBallots(pair,std::span(&profile,1),roles,0.5);
			split.emplace(profile.id,std::clamp(votes.front()/profile.primary,0.0,1.0));
		}
	}
	return rates;
}

// Keep the scenario's first preferences, classic TPP and election-wide
// preference effects fixed. Draw a small set of alternative DISTRICT
// rankings and choose one according to how closely it explains the observed
// transfers. A single draw then supplies all exclusions and the final TCP.
// This updates the district only, without feeding the sample back into the
// statewide or cross-district preference estimates.
std::vector<Model::SourceProfile> Model::conditionOnCountObservation(std::vector<SourceProfile> profiles,
	PreparedCountObservation const& observation, int district, std::span<Source const> ballot,
	Shares const& primaries, CoalitionPreference::Leakage leakage, bool dual, bool classicAvailable,
	Shares const& classicRates, double nationalIncoming, ElectionDraw const& election,
	Sensitivity sensitivity, bool namedIndependentMatches, bool& usedSample) const
{
	int const independent = share(primaries, roles.independent) > 0.0 ? roles.independent : roles.emergingIndependent;
	auto ballotId = [&](int id) { return id == roles.independent ? independent : id; };
	std::vector<int> recipients;
	std::vector<double> observed;
	for (auto const& [id, rate] : observation.flows) {
		int const recipient = ballotId(id);
		if (share(primaries, recipient) <= 0.0) return profiles;
		recipients.push_back(recipient);
		observed.push_back(rate);
	}
	std::vector<double> donorWeights;
	donorWeights.reserve(profiles.size());
	double pool = 0.0;
	double matched = 0.0;
	for (auto const& profile : profiles) {
		bool const recipient = std::find(recipients.begin(), recipients.end(), profile.id) != recipients.end();
		double weight = 0.0;
		if (!recipient) {
			if (observation.firstPreferences.empty()) weight = profile.primary;
			else {
				int const id = profile.id == independent ? roles.independent : profile.id;
				weight = share(observation.firstPreferences, id);
				matched += weight;
			}
		}
		donorWeights.push_back(weight);
		pool += weight;
	}
	// If a sampled donor cannot be distinguished on this ballot, the
	// residual would mix preference error with the wrong primary vote mix.
	double const sampled = std::accumulate(observation.firstPreferences.begin(),
		observation.firstPreferences.end(), 0.0, [](double sum, auto const& row) { return sum + row.second; });
	if (pool <= 0.0 || (sampled > 0.0 && matched != sampled)) return profiles;
	if (!sampleUncertainty || election.central) return profiles;
	usedSample = true;

	// Shares sum to one, so their errors move together. Independent errors
	// projected onto the space of zero-sum changes have diagonal variance
	// smaller by (k-1)/k. Restore the configured marginal variance here.
	double const count = static_cast<double>(recipients.size());
	double const variance = observation.variance * count / (count - 1.0);
	auto logWeight = [&](std::vector<SourceProfile> const& draw) {
		std::array<double, 4> predicted{};
		for (std::size_t i = 0; i < draw.size(); ++i) {
			if (donorWeights[i] == 0.0) continue;
			auto const votes = allocateBallots(recipients, std::span(&draw[i], 1), roles, nationalIncoming);
			for (std::size_t j = 0; j < votes.size(); ++j) {
				predicted[j] += (votes[j] / draw[i].primary) * (donorWeights[i] / pool);
			}
		}
		double squaredError = 0.0;
		for (std::size_t j = 0; j < observed.size(); ++j) {
			double const error = predicted[j] - observed[j];
			squaredError += error * error;
		}
		return -0.5 * squaredError / variance;
	};

	// Gumbel noise lets us choose in proportion to the likelihood without
	// storing every candidate draw or exponentiating tiny probabilities.
	// Eight proposals bound the cost to this district and retain uncertainty
	// when the model has limited support for an unusual sample.
	constexpr int Proposals = 8;
	auto const stream = key(election.seed, district, 0, 309);
	auto score = [&](std::vector<SourceProfile> const& draw, int proposal) {
		double const uniform = std::clamp<double>(RandomGenerator::uniform01_from_key(
			RandomGenerator::mixKey(stream, static_cast<std::uint64_t>(proposal))), Roundoff, 1.0 - Roundoff);
		return logWeight(draw) - std::log(-std::log(uniform));
	};
	double best = score(profiles, 0);
	for (int proposal = 1; proposal < Proposals; ++proposal) {
		auto const localSeed = RandomGenerator::mixKey(stream, static_cast<std::uint64_t>(proposal + Proposals));
		auto alternative = buildSourceProfiles(district, ballot, primaries, leakage, nationalIncoming, dual, classicAvailable,
			classicRates, election, sensitivity, namedIndependentMatches, localSeed);
		double const candidate = score(alternative, proposal);
		if (candidate <= best) continue;
		best = candidate;
		profiles = std::move(alternative);
	}
	return profiles;
}

CountResult Model::count(int district, std::span<Source const> sources,
	double laborTpp, double nationalIncoming, ElectionDraw const& election, Sensitivity sensitivity,
	bool reportFinalSources, bool namedIndependentMatches, PreparedCountObservation const* observation,
	std::optional<ThreeCandidateCountContext> countContext,
	std::optional<CoalitionPreference::Leakage> partnerLeakage) const
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
	// The simulation supplies the two original-source rates from its FP draw.
	// Older callers without a split-seat account use the common leakage volume
	// assumed by their own primary calculation.
	CoalitionPreference::Leakage leakage;
	if (partnerLeakage) leakage = *partnerLeakage;
	else if (dual) {
		double const common = MajorPartyFpMath::ClassicPartnerLeakageRate * std::min(liberal,national);
		leakage = {common/liberal,common/national,false};
	}
	double const referenceLeak = dual ? leakage.reference(liberal,national) : 0.;
	if (observation && !observation->candidateTotals.empty() && countContext && classicAvailable
		&& sensitivity == Sensitivity::None) {
		auto joint = countWithThreeCandidateTotals(*observation, *countContext, district, ballot, primaries,
			candidates, laborTpp, dual, leakage, nationalIncoming, election, namedIndependentMatches);
		if (joint) return std::move(*joint);
		// A candidate account that cannot fit these FP draws should not force
		// a pairing or discard the draw. Use the ordinary count in this case.
		result.jointThreeCandidateIncompatible = true;
		observation = nullptr;
	}
	bool const matchTpp = classicAvailable && sensitivity != Sensitivity::WithoutClassicTppMatching;
	Shares classicRates;
	if (matchTpp && !matchClassicTpp(ballot, roles, labor, referenceLeak,
		laborTpp, classicRates, result.classic)) {
		result.status = Status::InfeasibleClassicTpp;
		return result;
	}

	// The same source profiles supply every exclusion and the final pair.
	auto profiles = buildSourceProfiles(district, ballot, primaries, leakage, nationalIncoming,
		dual, classicAvailable, classicRates, election, sensitivity, namedIndependentMatches);
	if (observation) {
		profiles = conditionOnCountObservation(std::move(profiles), *observation, district, ballot,
			primaries, leakage, dual, classicAvailable, classicRates, nationalIncoming,
			election, sensitivity, namedIndependentMatches, result.conditionedOnLiveSample);
	}

	if (matchTpp) {
		auto checkClassicPair = [&](int opponent) {
			std::array const pair{roles.labor, opponent};
			auto const votes = allocateBallots(pair, profiles, roles, nationalIncoming);
			double const expected = dual ? laborTpp-referenceLeak+leakage.contribution(
				opponent == roles.national,liberal,national) : laborTpp;
			result.classic.maximumError = std::max(result.classic.maximumError, std::abs(votes[0] - expected));
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
			double const target = dual ? laborTpp-referenceLeak+
				leakage.contribution(opponent == roles.national,liberal,national) : laborTpp;
			result.final.firstShare = laborFirst ? target : 1.0-target;
		}
	}
	// Live learning remains restricted to supported pairings. A comparison
	// run can inspect any final pair; reading these already-drawn profiles
	// does not change exclusions, random draws or the final vote shares.
	if (reportFinalSources && (learnsPair(result.final.candidates) || !active)) {
		result.finalSources.reserve(profiles.size());
		for (auto const& profile : profiles) {
			auto votes = allocateBallots(candidates,std::span(&profile,1),roles,nationalIncoming);
			result.finalSources.push_back({profile.id,profile.primary,
				std::clamp(votes[0]/profile.primary,0.0,1.0)});
		}
	}
	result.status = Status::Compared;
	return result;
}

// Count an observed three-candidate stage and the notional TPP on one ballot
// account. The published tallies fix who was highest among the three on the
// sampled ballots. Other ballots still need a split among the three. Each
// three-way pile represents ballots with the same highest remaining choice,
// and a draw of its next choice suffices for every possible final two here.
std::optional<CountResult> Model::countWithThreeCandidateTotals(PreparedCountObservation const& observation,
	ThreeCandidateCountContext context, int district, std::span<Source const> ballot,
	Shares const& primaries, std::vector<int> candidates, double laborTpp,
	bool dual, CoalitionPreference::Leakage leakage, double nationalIncoming, ElectionDraw const& election,
	bool namedIndependentMatches) const
{
	auto const& published = observation.candidateTotals;
	if (published.size() != 3 || !published.contains(roles.labor)
		|| !std::isfinite(context.finalFormalVotes) || context.finalFormalVotes <= 0.0) return {};
	// The measured stage identifies the Coalition candidate still in the
	// count. If both partners stood, the excluded partner is already part
	// of the preference pool. A sample with both still in the count cannot
	// identify a single Coalition opponent for the TPP calculation.
	bool const sampledLiberal = published.contains(roles.liberal);
	bool const sampledNational = roles.national && published.contains(*roles.national);
	if (sampledLiberal == sampledNational) return {};
	int const coalition = sampledLiberal ? roles.liberal : *roles.national;
	auto const thirdEntry = std::find_if(published.begin(), published.end(), [&](auto const& row) {
		return row.first != roles.labor && row.first != coalition;
	});
	// A named live independent can occupy the emerging candidate slot in
	// this draw. Read their votes under the original observation's identity.
	int third = thirdEntry->first;
	if (third == roles.independent && share(primaries, third) == 0.0) {
		third = roles.emergingIndependent;
	}
	std::array const sampledRecipients{roles.labor, coalition, thirdEntry->first};
	std::array const recipients{roles.labor, coalition, third};
	std::array<double, 3> fp{}, observed{}, observedTransfers{};
	double sampledTotal = 0.0;
	for (std::size_t i = 0; i < recipients.size(); ++i) {
		int const party = recipients[i];
		fp[i] = share(primaries, party);
		observed[i] = published.at(sampledRecipients[i]) / context.finalFormalVotes;
		double const sampledPrimary =
			observation.candidateFirstPreferences.at(sampledRecipients[i]) / context.finalFormalVotes;
		observedTransfers[i] = observed[i] - sampledPrimary;
		if (fp[i] <= 0.0 || fp[i] + Roundoff < sampledPrimary) return {};
		sampledTotal += observed[i];
	}
	double const minorPool = 1.0 - fp[0] - fp[1] - fp[2];
	double const sampledPool = observation.ballots / context.finalFormalVotes;
	if (sampledTotal > 1.0 + Roundoff || sampledPool > minorPool + Roundoff) return {};

	// The sample says which of three candidates was ranked highest. It
	// cannot establish that all three outlast every other candidate. When
	// the combined outsider vote is large enough to pass one of them, run
	// the ordinary earlier exclusions, including Coalition preference
	// splitting and leakage, before applying these sampled tallies.
	bool const earlierExclusionsUncertain = *std::min_element(fp.begin(), fp.end()) <= minorPool;
	if (candidates.size() > 3 && earlierExclusionsUncertain) {
		Shares originalRates;
		ClassicCheck originalCheck;
		if (!matchClassicTpp(ballot, roles, fp[0], dual ? leakage.reference(share(primaries,roles.liberal),share(primaries,*roles.national)) : 0., laborTpp, originalRates, originalCheck)) return {};
		auto profiles = buildSourceProfiles(district, ballot, primaries, leakage, nationalIncoming, dual, true, originalRates,
			election, Sensitivity::None, namedIndependentMatches);
		while (candidates.size() > 3) {
			auto const totals = allocateBallots(candidates, profiles, roles, nationalIncoming);
			auto const lowest = std::min_element(totals.begin(), totals.end());
			candidates.erase(candidates.begin() + (lowest - totals.begin()));
		}
		if (std::any_of(candidates.begin(), candidates.end(), [&](int party) {
			return std::find(recipients.begin(), recipients.end(), party) == recipients.end();
		}))
			return {};
	}

	// Draw the split of only the outside minor pool. The supplied spread is
	// in percentage points of that pool. A Dirichlet draw divides the pool
	// into three nonnegative fractions that sum to one. Its expected split
	// comes from the sample, while all of the observed transfers stay fixed.
	std::array<double, 3> outsideFlows{};
	double maximumVariance = 0.0;
	for (std::size_t i = 0; i < recipients.size(); ++i) {
		// Half a ballot per destination leaves support for a flow observed
		// as zero in a small sample. It only affects the unsampled ballots.
		double const mean = (observation.ballots * observation.flows.at(sampledRecipients[i]) + 0.5)
			/ (observation.ballots + 1.5);
		outsideFlows[i] = mean;
		maximumVariance = std::max(maximumVariance, mean * (1.0 - mean));
	}
	double const outsidePool = std::max(0.0, minorPool - sampledPool);
	if (outsidePool > 0.0 && maximumVariance > 0.0 && sampleUncertainty && !election.central) {
		double const variance = observation.outsideFlowSd * observation.outsideFlowSd;
		double const concentration = std::max(1.0, maximumVariance / variance - 1.0);
		DrawEngine engine(key(election.seed, district, 0, 310));
		double total = 0.0;
		for (double& rate : outsideFlows) {
			if (rate > 0.0) rate = std::gamma_distribution<double>(rate * concentration)(engine);
			total += rate;
		}
		for (std::size_t i = 0; i < recipients.size(); ++i) {
			outsideFlows[i] = total > 0.0 ? outsideFlows[i] / total : observation.flows.at(sampledRecipients[i]);
		}
	}
	std::array<double, 3> transfers{}, threeWay{};
	for (std::size_t i = 0; i < recipients.size(); ++i) {
		transfers[i] = observedTransfers[i] + outsidePool * outsideFlows[i];
		threeWay[i] = fp[i] + transfers[i];
	}
	if (std::abs(std::accumulate(threeWay.begin(), threeWay.end(), 0.0) - 1.0) > TotalTolerance) return {};

	// All ballots in the Labor or surviving Coalition candidate's pile
	// already rank that major ahead of the other, including transfers from
	// an excluded Coalition partner. The third candidate's pile determines
	// the remaining Labor–Coalition split.
	// Distinguish their own voters from the inherited ballots. For a sample
	// without a local historical prior, use the prepared primary flow and a
	// neutral inherited split. The spreads are provisional modelling choices
	// for election-to-election change, wider when the original mix is unknown.
	auto uncertainRate = [&](double mean, double sd, int stream) {
		if (!sampleUncertainty || election.central || mean == 0.0 || mean == 1.0) return mean;
		double const concentration = std::max(1.0, mean * (1.0 - mean) / (sd * sd) - 1.0);
		return drawBeta(mean, concentration, key(election.seed, district, third, stream));
	};

	CountResult result;
	result.status = Status::Compared;
	result.conditionedOnLiveSample = true;
	result.classic.lower = threeWay[0];
	result.classic.upper = 1.0 - threeWay[1];
	// When measured TPP is present, keep the estimate already conditioned
	// on those votes. Discordant partial counts may reflect a different set
	// of ballots or separate revisions to the published count.
	double const partnerDifference = dual ? leakage.contribution(sampledNational,share(primaries,roles.liberal),
		share(primaries,*roles.national))-leakage.reference(share(primaries,roles.liberal),share(primaries,*roles.national)) : 0.;
	double const priorSampled = context.sampledPairTpp.value_or(laborTpp+partnerDifference);
	double sampledTpp = priorSampled;
	if (!context.hasCountedTpp) {
		auto const thirdSource = std::find_if(ballot.begin(), ballot.end(), [&](Source const& source) { return source.id == third; });
		auto const prior = observation.tppPrior.value_or(ThreeCandidateTppPrior{thirdSource->classicRate, 0.5});
		double const primaryRate = uncertainRate(prior.primaryToLabor, observation.tppPrior ? 0.04 : 0.08, 311);
		double const inheritedRate = uncertainRate(prior.inheritedToLabor, 0.15, 312);
		double const laborTotal = threeWay[0] + fp[2] * primaryRate + transfers[2] * inheritedRate;
		sampledTpp = std::clamp(laborTotal, 0.0, 1.0);
	}
	// The sample updates this particular matchup. Carry only its change
	// back to the reference TPP used in district and statewide reporting.
	result.laborTpp = context.hasCountedReference ? laborTpp : std::clamp(laborTpp+(sampledTpp-priorSampled),0.,1.);
	result.classic.unadjusted = result.laborTpp;
	for (std::size_t i = 0; i < recipients.size(); ++i) {
		result.jointThreeCandidateFlows.emplace(recipients[i], transfers[i] / minorPool);
	}

	auto const excluded = std::min_element(threeWay.begin(), threeWay.end()) - threeWay.begin();
	if (excluded == 2) {
		result.final = roles.labor < coalition ? PairResult{{roles.labor, coalition}, sampledTpp, {}}
			: PairResult{{coalition, roles.labor}, 1.0 - sampledTpp, {}};
		return result;
	}

	// In a non-classic final only the eliminated major's pile needs a next
	// choice. Use its original voters' ranking from the full ballot so a
	// finalist outside the fitted rankings, such as Centre Alliance, can
	// receive votes using the usual political proximity weights.
	int const otherMajor = recipients[1 - excluded];
	std::array const finalists{third, otherMajor};
	int const eliminated = recipients[excluded];
	auto const profiles = buildSourceProfiles(district, ballot, primaries, leakage, nationalIncoming, dual, true, {},
		election, Sensitivity::None, namedIndependentMatches, {}, eliminated);
	double const primaryToThird = allocateBallots(finalists, profiles, roles, nationalIncoming)[0] / fp[excluded];
	double const boundedRate = std::clamp(primaryToThird, 0.0, 1.0);
	// The 3CP tells us that the inherited ballots reached the eliminated
	// candidate. Their next choice is unknown, so allow more variation around
	// the original voters' rate. A measured TCP can refine the final split.
	double const inheritedToThird = uncertainRate(boundedRate, 0.15, 313);
	double const thirdShare = threeWay[2] + fp[excluded] * boundedRate + transfers[excluded] * inheritedToThird;
	result.final = third < otherMajor ? PairResult{{third, otherMajor}, thirdShare, {}}
		: PairResult{{otherMajor, third}, 1.0 - thirdShare, {}};
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
		outcome.firstShareSquaredSum += result.firstShare * result.firstShare;
	};
	addOutcome(reference, 0);
	addOutcome(alternative, 1);
	std::size_t const pairingChanged = reference.candidates != alternative.candidates ? 1 : 0;
	++winnerTransitions[{reference.winner(), alternative.winner()}][pairingChanged];
	if (reference.candidates != alternative.candidates) {
		++changedPair;
	}
	else {
		auto& pair = pairs.at(reference.candidates);
		++pair.samePairDraws;
		if (reference.winner() == reference.candidates[0]) ++pair.samePairFirstWins[0];
		if (alternative.winner() == alternative.candidates[0]) ++pair.samePairFirstWins[1];
		if (winnerChanged) ++pair.samePairWinnerChanges;
		double const change = alternative.firstShare - reference.firstShare;
		pair.samePairShareChange += change;
		pair.samePairAbsoluteChange += std::abs(change);
	}
}

void SourceComparisonSummary::record(Comparison const& comparison)
{
	if (!comparison.reportSources || comparison.legacy.candidates != comparison.model.final.candidates) return;
	auto& pair = pairs[comparison.legacy.candidates];
	if (comparison.legacySources.empty() || comparison.model.finalSources.empty()) {
		++pair.unavailable;
		return;
	}
	using Account = std::map<int, std::array<double, 2>>; // original FP, votes to first finalist
	auto groupSources = [&](std::vector<FinalSource> const& sources) {
		Account account;
		for (auto const& source : sources) {
			int id = source.id;
			if (comparison.combinedCoalitionSources && id == comparison.combinedCoalitionSources->at(1)) {
				id = comparison.combinedCoalitionSources->at(0);
			}
			auto& votes = account[id];
			votes[0] += source.primary;
			votes[1] += source.primary * source.firstRate;
		}
		return account;
	};
	auto const legacy = groupSources(comparison.legacySources);
	auto const ranking = groupSources(comparison.model.finalSources);
	// Ordinary matched CPV accounts sum to one and reproduce their TCP. Do
	// not invent source attribution for shortcuts, a different candidate
	// grouping, or exhausted votes. The pairing/winner comparison still counts
	// those draws; only this explanation is marked unavailable.
	auto accountError = [](Account const& account, double finalShare) {
		double primary = 0.0, first = 0.0;
		for (auto const& [id, votes] : account) {
			(void)id;
			if (!std::isfinite(votes[0]) || !std::isfinite(votes[1]) ||
				votes[0] < 0.0 || votes[1] < 0.0 || votes[1] > votes[0]) return 1.0;
			primary += votes[0];
			first += votes[1];
		}
		return std::max(std::abs(primary - 1.0), std::abs(first - finalShare));
	};
	double error = std::max(accountError(legacy, comparison.legacy.firstShare),
		accountError(ranking, comparison.model.final.firstShare));
	auto checkPrimaries = [&](Account const& first, Account const& second) {
		for (auto const& [id, votes] : first) {
			auto const match = second.find(id);
			double const other = match == second.end() ? 0.0 : match->second[0];
			error = std::max(error, std::abs(votes[0] - other));
		}
	};
	checkPrimaries(legacy, ranking);
	checkPrimaries(ranking, legacy);
	if (error > 1e-7) {
		++pair.unavailable;
		return;
	}
	++pair.draws;
	bool const referenceWins = comparison.legacy.winner() == comparison.legacy.candidates[0];
	if (referenceWins) ++pair.referenceFirstWins;
	if (comparison.model.final.winner() == comparison.model.final.candidates[0]) ++pair.alternativeFirstWins;
	pair.maximumAccountError = std::max(pair.maximumAccountError, error);
	pair.tcpChangeSum += comparison.model.final.firstShare - comparison.legacy.firstShare;
	for (auto const& [id, votes] : legacy) {
		if (votes[0] <= 0.0) continue;
		auto const found = ranking.find(id);
		if (found == ranking.end() || found->second[0] <= 0.0) continue;
		auto& source = pair.sources[id];
		++source.draws;
		source.primarySum += votes[0];
		std::array const firstVotes{votes[1], found->second[1]};
		std::array const primaryVotes{votes[0], found->second[0]};
		// A diagnostic-only replacement answers whether this source's changed
		// transfers would reverse this same final contest, holding all other
		// sources at their old allocations. These effects are not additive:
		// two sources can each suffice to change the winner, or only do so together.
		PairResult const onlyThisSource{comparison.legacy.candidates,
			comparison.legacy.firstShare + firstVotes[1] - firstVotes[0], {}};
		bool const sourceWins = onlyThisSource.winner() == onlyThisSource.candidates[0];
		source.firstWinChangeOnlyThisSource += static_cast<int>(sourceWins) - static_cast<int>(referenceWins);
		for (std::size_t side = 0; side < 2; ++side) {
			double const rate = firstVotes[side] / primaryVotes[side];
			source.rateSum[side] += rate;
			source.rateSquaredSum[side] += rate * rate;
			source.firstVotesSum[side] += firstVotes[side];
		}
	}
}

bool Summary::recordCount(CountResult const& model, int iteration)
{
	if (model.jointThreeCandidateIncompatible) ++jointThreeCandidateIncompatible;
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
	if (model.conditionedOnLiveSample) ++conditionedOnLiveSample;
	if (!model.jointThreeCandidateFlows.empty()) {
		++jointThreeCandidateDraws;
		for (auto const& [party, flow] : model.jointThreeCandidateFlows) jointThreeCandidateFlowSums[party] += flow;
	}
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

	standardVsRanking.record(comparison.legacy, model.final);
	sourceTransfers.record(comparison);
	auto recordTest = [&](std::optional<CountResult> const& alternative, PairComparisonSummary& summary) {
		if (!alternative) return;
		if (alternative->status == Status::Compared) summary.record(model.final, alternative->final);
		else ++summary.failed;
	};
	recordTest(comparison.withoutClassicTppMatching, withoutClassicTppMatching);
	recordTest(comparison.reducedLaborToGreens, reducedLaborToGreens);
}

}
