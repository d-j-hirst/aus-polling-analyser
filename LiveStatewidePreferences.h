#pragma once

// Report the current statewide split of one party's first preferences between
// two named finalists. The live forecast supplies the district ballots, projected
// FP counts, matched published counts and its fitted preference adjustments.
// This calculation describes those voters even where another TCP is more likely.
// Its estimates are output only and never feed back into a seat's forecast.
#include "LivePreferenceLearning.h"
#include "RandomGenerator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace LiveStatewidePreferences {
using Pair = PreferenceModel::Pair;
using Shares = PreferenceModel::Shares;
using Row = std::array<int,3>; // original party, published destination, other finalist
using Draw = std::map<Row,double>; // percentage of the original party's voters
using Samples = std::map<Row,std::vector<double>>;
using Bands = std::map<Row,std::vector<float>>;

// Use identical iteration keys on successive snapshots of the same size.
// Four thousand draws give roughly four observations in each outer 0.1% tail
// while bounding the extra work. Space them across the run, whose workers
// each own a contiguous block of iteration numbers. Short runs use every draw.
inline constexpr int MaximumDraws = 4096;
inline bool includeDraw(int iteration, int iterations) {
	if (iterations <= MaximumDraws) return true;
	auto const position = static_cast<std::int64_t>(iteration)*MaximumDraws;
	return position/iterations != (position+MaximumDraws)/iterations;
}

inline std::vector<Row> reportRows(PreferenceModel::Roles const& roles, bool victoria) {
	int const labor = roles.labor, liberal = roles.liberal, on = roles.oneNation, greens = roles.greens;
	std::vector<Row> rows{{labor,liberal,on},{liberal,on,labor},{on,labor,liberal}};
	if (victoria) {
		rows.insert(rows.end(),{{greens,labor,liberal},{greens,labor,on},{liberal,labor,greens},
			{on,labor,greens},{labor,greens,liberal}});
		if (roles.national) {
			int const national = *roles.national;
			rows.insert(rows.end(),{{national,on,labor},{labor,national,on},{on,labor,national}});
		}
	}
	return rows;
}

struct CountedBatch {
	Shares donors; // First-preference ballots of the eliminated parties in this batch.
	double first = 0; // Transfers received by the lower-index finalist after their own FP.
	double unknownOrigin = 0; // Counted TCP ballots beyond the available FP account.
};
struct Outstanding {
	Shares donors;
	double classicOffset = 0; // Change in log odds of Labor over the specified Coalition finalist.
	bool reportedFp = false;
};
struct Category {
	LivePreferenceLearning::Adjustment adjustment;
	std::vector<CountedBatch> batches;
	std::vector<Outstanding> outstanding;
	Shares measuredFirst, measuredDonors; // Published source-specific transfers, when supplied.
};
struct Contest {
	Pair pair;
	bool classic = false, learned = false;
	std::vector<Row> rows;
	Shares reference;
	std::map<int,Category> categories;
};
struct District {
	int index = 0;
	bool namedIndependentMatches = true;
	std::vector<PreferenceModel::Source> sources;
	std::vector<Pair> nonClassicPairs;
	std::vector<Contest> contests;
};
struct Election {
	std::vector<District> districts;
	Shares classicReference; // Original election-wide estimates to Labor, fractions.
	// Residual errors in converting Greens and independent FP to classic TPP.
	// Positive means the underlying conversion overestimated Labor. The two
	// errors can be correlated, since the same mixed counts inform both.
	std::array<double,2> classicErrorMean{};
	std::array<std::array<double,2>,2> classicErrorRoot{};
};
inline double votesFor(Shares const& votes, int party) {
	auto found = votes.find(party);
	return found == votes.end() ? 0.0 : found->second;
}

// A published ballot (or the project's confirmed pre-count list) establishes
// who stands. A simulated chance of reaching the TCP has no role in this
// population. Group donors sharing two finalists so a mixed batch is fitted once.
inline std::map<Pair,std::vector<Row>> eligibleContests(std::span<Row const> rows,
	std::set<int> const& roster, Shares const& projected) {
	std::map<Pair,std::vector<Row>> result;
	for (auto const& row : rows) {
		if (!roster.contains(row[0]) || !roster.contains(row[1]) || !roster.contains(row[2])) continue;
		if (votesFor(projected,row[0]) <= 0) continue;
		Pair const pair{std::min(row[1],row[2]),std::max(row[1],row[2])};
		result[pair].push_back(row);
	}
	return result;
}

// Allocate the two counted finalist totals to their original FP groups. Exact
// matched FP uses all the known ballots. For a partial TCP without a matching
// FP batch, the counted voters use the observed FP mix, then the projected
// mix if more voters are needed. Each party can contribute at most its still
// available ballots. An excess with no identifiable source can satisfy the
// published totals but never enters a named party's reported denominator.
inline void addCounted(Category& category, Pair pair, Shares const& fp,
	Shares const& tcp, Shares& available, LivePartialCount::Transfers const& transfers = {}) {
	double const firstTotal = votesFor(tcp,pair[0]), secondTotal = votesFor(tcp,pair[1]);
	double const counted = firstTotal + secondTotal;
	if (counted <= 0) return;
	Shares used;
	double left = counted;
	auto take = [&](Shares const& population) {
		Shares capacity;
		double total = 0;
		for (auto const& [party,votes] : population) {
			double amount = std::min(votes,votesFor(available,party));
			if (party == pair[0]) amount = std::min(amount,firstTotal - votesFor(used,party));
			if (party == pair[1]) amount = std::min(amount,secondTotal - votesFor(used,party));
			if (amount > 0) { capacity.emplace(party,amount); total += amount; }
		}
		if (total == 0) return;
		double const fraction = std::min(left/total,1.0);
		for (auto const& [party,amount] : capacity) {
			used[party] += fraction*amount;
			available.at(party) -= fraction*amount;
		}
		left = std::max(0.0,left - fraction*total);
	};
	take(fp);
	if (left > 1e-9) take(available);
	CountedBatch batch;
	batch.first = firstTotal - votesFor(used,pair[0]);
	batch.unknownOrigin = left;
	for (auto const& [party,amount] : used) {
		if (party == pair[0] || party == pair[1] || amount <= 0) continue;
		// The caller passes an official transfer table only for an exact,
		// reconciled batch. Such a source needs no estimated allocation.
		if (auto row = transfers.find(party); row != transfers.end()) {
			double const direct = LivePartialCount::value(row->second,pair[0]);
			category.measuredDonors[party] += amount;
			category.measuredFirst[party] += direct;
			batch.first -= direct;
		}
		else batch.donors.emplace(party,amount);
	}
	if (!batch.donors.empty()) category.batches.push_back(std::move(batch));
}

// A booth can have an older matched FP/TCP batch, fresh FP and a later TCP
// increment. Consume the matched portion first and only then attribute the
// increment. An accepted old pair can still constrain this particular
// hypothetical contest after the commission starts counting a different pair.
inline void addUnit(Contest& contest, int code, Shares projected, Shares reported, Shares currentTcp,
	LivePartialCount::TcpBasis const* matched, double pendingOffset, double futureOffset) {
	auto& category = contest.categories[code];
	auto const pair = contest.pair;
	bool const current = currentTcp.size() == 2 && currentTcp.contains(pair[0]) && currentTcp.contains(pair[1]) &&
		currentTcp.at(pair[0])+currentTcp.at(pair[1]) > 0;
	if (!current) currentTcp.clear();
	if (matched) {
		bool unchangedPrimaries = true;
		for (auto const& [party,votes] : matched->fp)
			if (votes > votesFor(reported,party) || votes > votesFor(projected,party)+1e-7) unchangedPrimaries = false;
		bool compatible = matched->tcp.size() == 2 && matched->tcp.contains(pair[0]) && matched->tcp.contains(pair[1]);
		for (auto const& [party,votes] : matched->tcp)
			if (current && votes > votesFor(currentTcp,party)) compatible = false;
		if (compatible) {
			Shares fp(matched->fp.begin(),matched->fp.end()), tcp(matched->tcp.begin(),matched->tcp.end());
			auto before = projected;
			// Accepted history also keeps near matches and minor FP rechecks.
			// Their finalist totals remain evidence. Only a still-intact FP
			// batch can identify specific parties' transfers exactly.
			if (unchangedPrimaries && LivePartialCount::validBasis(*matched))
				addCounted(category,pair,fp,tcp,projected,matched->transfers);
			else addCounted(category,pair,fp,tcp,projected);
			for (auto& [party,votes] : reported)
				votes = std::max(0.0,votes-(votesFor(before,party)-votesFor(projected,party)));
			if (current) for (auto const& [party,votes] : tcp) currentTcp[party] -= votes;
		}
	}
	// For a partial count keep the same assumed source mix in both halves:
	// whatever has been assigned to the TCP is no longer awaiting preferences.
	if (!currentTcp.empty()) {
		auto before = projected;
		addCounted(category,pair,reported,currentTcp,projected);
		for (auto& [party,votes] : reported)
			votes = std::max(0.0,votes-(votesFor(before,party)-votesFor(projected,party)));
	}
	Outstanding pending{{},pendingOffset,true}, future{{},futureOffset,false};
	std::set<int> included;
	for (auto const& row : contest.rows) {
		int const source = row[0];
		if (!included.insert(source).second) continue;
		double const remaining = votesFor(projected,source);
		if (remaining <= 0) continue;
		if (!contest.classic) {
			// All unallocated voters of this party in the category use the
			// same learned transfer rate. Store their weight once per category.
			if (category.outstanding.empty()) category.outstanding.emplace_back();
			category.outstanding.front().donors[source] += remaining;
			continue;
		}
		double const received = std::min(remaining,votesFor(reported,source));
		if (received > 0) pending.donors[source] = received;
		if (remaining > received) future.donors[source] = remaining-received;
	}
	if (!pending.donors.empty()) category.outstanding.push_back(std::move(pending));
	if (!future.donors.empty()) category.outstanding.push_back(std::move(future));
}

// Each mixed count fixes the TOTAL received, while its individual donors may
// still have different transfer rates. Multiply every donor's odds by the same
// factor until their allocations sum to that total. This preserves their
// relative preference strengths on every draw. A sole donor is measured exactly.
struct SourceOdds { int party; double votes, odds; };
inline void allocateMeasured(CountedBatch const& batch, Shares const& rates,
	Shares& first, std::vector<SourceOdds>& work) {
	work.clear();
	double total = batch.unknownOrigin;
	for (auto const& [party,amount] : batch.donors) {
		double rate = std::clamp(rates.at(party),1e-7,1.0-1e-7);
		work.push_back({party,amount,rate/(1.0-rate)});
		total += amount;
	}
	double const target = std::clamp(batch.first,0.0,total);
	if (batch.donors.size() == 1 && batch.unknownOrigin == 0) {
		first[batch.donors.begin()->first] += target;
		return;
	}
	if (target == 0 || target == total) {
		if (target > 0) for (auto const& source : work) first[source.party] += source.votes;
		return;
	}
	// Express the common odds factor as x/(1-x), so zero and one bracket
	// every feasible answer. Newton steps speed up ordinary mixed counts;
	// bisection keeps an extreme near-unanimous result inside the bracket.
	double low = 0, high = 1, x = .5;
	for (int step = 0; step < 60; ++step) {
		double fitted = batch.unknownOrigin*x, slope = batch.unknownOrigin;
		for (auto const& source : work) {
			double const denominator = 1.0-x + source.odds*x;
			fitted += source.votes*source.odds*x/denominator;
			slope += source.votes*source.odds/(denominator*denominator);
		}
		if (std::abs(fitted-target) < 1e-11*total) break;
		if (fitted < target) low = x;
		else high = x;
		double const next = x - (fitted-target)/slope;
		x = next > low && next < high ? next : .5*(low+high);
	}
	for (auto const& source : work)
		first[source.party] += source.votes*source.odds*x/(1.0-x+source.odds*x);
}

struct Totals { double first = 0, donors = 0; };
using Accounts = std::map<Row,Totals>;

// Keep a single election draw for all districts and categories. A measured
// booth can constrain its own mixture exactly and also inform the forecast
// for outstanding voters elsewhere. Each actual donor ballot enters the
// statewide vote account once for this hypothetical pair.
inline void addContest(Contest const& contest, Shares const& sampled, std::uint64_t seed, Accounts& sums,
	Shares const& classicError = {}) {
	std::vector<SourceOdds> work;
	for (auto const& [code,category] : contest.categories) {
		auto const& fit = category.adjustment;
		Shares rates = sampled;
		if (contest.learned) {
			double variation = 0;
			if (code != LivePreferenceLearning::Ordinary) {
				auto pairKey = RandomGenerator::combinePartyIds(contest.pair[0],contest.pair[1]);
				auto key = RandomGenerator::mixKey(RandomGenerator::mixKey(seed,pairKey),static_cast<std::uint64_t>(code));
				variation = RandomGenerator::normal_from_key(key,0,1) * LivePreferenceLearning::sharedCategorySd(fit);
			}
			for (auto& [party,rate] : rates) {
				auto base = contest.reference.find(party);
				double centre = base == contest.reference.end() ? rate : base->second;
				rate = LivePreferenceLearning::transferRate(rate,centre,fit,variation);
			}
		}
		Shares countedFirst = category.measuredFirst, countedDonors = category.measuredDonors;
		auto measuredRates = rates;
		if (contest.classic) for (auto const& [party,error] : classicError) {
			auto found = measuredRates.find(party);
			if (found != measuredRates.end()) found->second = std::clamp(found->second-error,0.0,1.0);
		}
		for (auto const& batch : category.batches) {
			allocateMeasured(batch,measuredRates,countedFirst,work);
			for (auto const& [party,amount] : batch.donors) countedDonors[party] += amount;
		}
		for (auto const& row : contest.rows) {
			int const source = row[0];
			double first = votesFor(countedFirst,source), donors = votesFor(countedDonors,source);
			for (auto const& remaining : category.outstanding) {
				double const amount = votesFor(remaining.donors,source);
				if (amount <= 0) continue;
				double rate = rates.at(source);
				if (contest.classic && remaining.classicOffset != 0)
					rate = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(rate)+remaining.classicOffset);
				if (contest.classic && remaining.reportedFp)
					rate = std::clamp(rate-votesFor(classicError,source),0.0,1.0);
				first += amount*rate;
				donors += amount;
			}
			auto& total = sums[row];
			total.first += row[1] == contest.pair[0] ? first : donors-first;
			total.donors += donors;
		}
	}
}

inline Draw estimate(Election const& inputs, PreferenceModel::Model const& model,
	PreferenceModel::Model::ElectionDraw const& election, Shares const& classicPercent) {
	Accounts sums;
	auto const& roles = model.partyRoles();
	// The TPP model has two separately measured common errors for converting
	// reported primaries. Sample that same joint distribution once across
	// districts. No such party-specific residual is identified for One Nation.
	constexpr std::uint64_t conversionKey = 0x5357466c6f775450ULL;
	auto key = RandomGenerator::mixKey(election.seed,conversionKey);
	std::array<double,2> errors = inputs.classicErrorMean;
	for (size_t i = 0; i < errors.size(); ++i) {
		double z = RandomGenerator::normal_from_key(RandomGenerator::mixKey(key,i),0,1);
		for (size_t j = i; j < errors.size(); ++j) errors[j] += inputs.classicErrorRoot[j][i]*z;
	}
	for (auto const& district : inputs.districts) {
		auto sources = district.sources;
		// Classic TPP uses the accepted forecast's sampled flows to Labor.
		// Preserve the district's prepared difference from the statewide centre
		// (e.g. a returning independent) when applying that common draw.
		for (auto& source : sources) {
			auto draw = classicPercent.find(source.id);
			auto base = inputs.classicReference.find(source.id);
			if (draw == classicPercent.end()) draw = classicPercent.find(roles.others);
			if (base == inputs.classicReference.end()) base = inputs.classicReference.find(roles.others);
			if (draw == classicPercent.end() || base == inputs.classicReference.end() ||
				source.id == roles.labor || source.id == roles.liberal || source.id == roles.national) continue;
			double const offset = LivePartialCount::logOdds(source.classicRate) - LivePartialCount::logOdds(base->second);
			source.classicRate = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(draw->second*.01)+offset);
		}
		auto sampled = model.sharedPairRates(district.index,sources,district.nonClassicPairs,election,district.namedIndependentMatches);
		for (auto const& contest : district.contests) {
			if (!contest.classic) {
				addContest(contest,sampled.at(contest.pair),election.seed,sums);
				continue;
			}
			Shares rates;
			Shares classicError;
			for (auto const& source : sources) {
				double rate = source.classicRate;
				if ((source.id == roles.national || source.id == roles.liberal) && source.id != contest.pair[1])
					rate = contest.reference.at(source.id);
				rates.emplace(source.id,rate);
				if (source.id == roles.greens) classicError.emplace(source.id,errors[0]);
				if (source.id == roles.independent || source.additionalIndependent)
					classicError.emplace(source.id,errors[1]);
			}
			addContest(contest,rates,election.seed,sums,classicError);
		}
	}
	Draw result;
	for (auto const& [row,total] : sums) {
		// Withhold an inconsistent report before a non-finite value can enter
		// a percentile. Only floating-point rounding can legitimately take a
		// finalist marginally outside the source party's total.
		double const tolerance = 1e-9*std::max(1.0,total.donors);
		if (!std::isfinite(total.donors) || !std::isfinite(total.first) || total.donors < 0 ||
			total.first < -tolerance || total.first > total.donors+tolerance)
			throw std::domain_error("Statewide preference allocation exceeds the source vote account.");
		if (total.donors == 0) continue;
		result.emplace(row,std::clamp(100.0*total.first/total.donors,0.0,100.0));
	}
	return result;
}

// Sort complete statewide draws before reading the website's percentile
// bands. Averaging district interval endpoints would discard their dependence.
inline Bands percentiles(Samples& samples, std::span<float const> thresholds) {
	Bands result;
	for (auto& [row,values] : samples) {
		if (values.empty()) continue;
		std::sort(values.begin(),values.end());
		for (float p : thresholds) {
			double const index = p*.01*(values.size()-1);
			double const value = std::lerp(values[static_cast<size_t>(std::floor(index))],
				values[static_cast<size_t>(std::ceil(index))],index-std::floor(index));
			result[row].push_back(static_cast<float>(value));
		}
	}
	return result;
}
}
