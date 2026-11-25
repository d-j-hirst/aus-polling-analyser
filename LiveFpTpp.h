#pragma once

// Convert one simulated district's accepted primary vote into Labor TPP in a
// compulsory-preference election with one Coalition finalist. Uncounted
// transfers use the draw's preference rates. Counted ballots replace exactly
// the estimate for their own primary voters. The same allocation by source
// is available to the exclusion count and statewide preference table.
#include "LiveStatewidePreferences.h"
#include "LiveSourceSeatTcp.h"

namespace LiveFpTpp {
using LiveStatewidePreferences::Category;
using LiveStatewidePreferences::CountedBatch;
using LiveStatewidePreferences::Shares;
using LiveStatewidePreferences::SourceOdds;
using LiveStatewidePreferences::Totals;
using LiveStatewidePreferences::allocateMeasured;
using LiveStatewidePreferences::votesFor;

struct District {
	Shares covered; // FP voters whose Labor–Coalition destination is already accounted for.
	Category measured; // Observed transfers in each matched or partially matched batch.
	Shares rateChange; // Party-specific changes in the fraction going to Labor after accounting for the broad movement.
	double transferOddsMultiplier = 1.; // Election and regional preference evidence. One leaves the draw unchanged.
	std::optional<LiveSourceSeatTcp::Curve> sourceArea = std::nullopt;
};

struct BoothOrigins {
	Shares fp; // Number of ballots from each FP candidate represented in the guaranteed TCP totals.
	LivePartialCount::Transfers transfers; // Only transfers guaranteed by an official source-by-source count.
};

// Identify the primary voters behind this booth's two guaranteed finalist totals.
// Normally the published TCP belongs to a single FP batch. If those batches do
// not match, addBooth estimates the covered slice. After an FP revision the totals
// can instead include newer ballots: each finalist's own primaries already have
// a known destination, even while the commission is still distributing preferences.
// Keep candidate identities here until the caller groups them into model parties.
inline BoothOrigins boothOrigins(LivePartialCount::Counts const& fp, LivePartialCount::Counts const& tcp,
	LivePartialCount::TcpBasis const* basis, bool revised, Shares const& guaranteed,
	std::optional<LivePartialCount::Evidence> const& earlier = {}) {
	if (!basis || basis->tcp != tcp) {
		// Usually the current FP count is the closest guide. When the exact
		// same two tallies return beside a larger FP count, use the FP batch
		// originally observed with them if it still fits inside today's count.
		// That batch may differ by a few ballots: addBooth still estimates
		// the covered slice and receives no exact candidate transfer table.
		if (!revised && earlier && tcp.size() == 2 && tcp.contains(earlier->first) && tcp.contains(earlier->second)
			&& tcp.at(earlier->first) == earlier->firstTcpVotes && tcp.at(earlier->second) == earlier->secondTcpVotes
			&& LivePartialCount::total(fp) > std::max(LivePartialCount::total(earlier->fp),LivePartialCount::total(tcp))
			&& LivePartialCount::remainingCohort(fp,*earlier))
			return {{earlier->fp.begin(),earlier->fp.end()},{}};
		return {{fp.begin(),fp.end()},{}};
	}
	bool const matched = LivePartialCount::validBasis(*basis);
	if (!revised) {
		if (matched) return {{basis->fp.begin(),basis->fp.end()},basis->transfers};
		return {{fp.begin(),fp.end()},{}};
	}

	BoothOrigins result;
	double guaranteedTransfers = 0.;
	for (auto const& [finalist,ballots] : guaranteed) {
		double const own = LivePartialCount::value(fp,finalist);
		result.fp[finalist] = own;
		guaranteedTransfers += ballots - own;
	}
	double survivingDonors = 0.;
	for (auto const& [source,oldVotes] : basis->fp) {
		if (tcp.contains(source)) continue;
		int const surviving = std::min(oldVotes,fp.at(source));
		if (matched && !basis->transfers.empty()) {
			// A recheck can remove at most this many ballots from either
			// destination. Only the guaranteed remainder of each official
			// transfer enters the count. New donor ballots remain outstanding.
			int const removed = oldVotes - surviving;
			for (auto const& [side,count] : basis->transfers.at(source)) {
				int const known = std::max(0,count-removed);
				result.transfers[source][side] = known;
				result.fp[source] += known;
			}
		} else {
			result.fp[source] = surviving;
			survivingDonors += surviving;
		}
	}
	// When primaries only increased, the entire original matched donor pool
	// is still covered. With an incomplete count or removals we may know only
	// the total guaranteed transfers: assume the surviving old donor mix for
	// that portion. Neither new donor votes nor the booth forecast supply it.
	if (survivingDonors > 0 && guaranteedTransfers < survivingDonors) {
		double const fraction = guaranteedTransfers / survivingDonors;
		for (auto& [source,count] : result.fp)
			if (!tcp.contains(source)) count *= fraction;
	}
	return result;
}

// Freeze the primary mix of this reporting unit's measured TPP once per
// snapshot. A partial or TCP-first batch uses the booth mix where known,
// followed by its projected mix. Nobody in a different TCP is counted here.
inline void addBooth(District& district, Shares const& fp, Shares projected, Shares const& counted,
	LivePartialCount::Transfers const& transfers = {}) {
	for (auto const& [party,n] : fp) projected[party] = std::max(projected[party],n);
	auto available = projected;
	LiveStatewidePreferences::addCounted(district.measured,{0,1},fp,counted,available,transfers);
	for (auto const& [party,n] : projected) district.covered[party] += n - available.at(party);
}

inline double total(Shares const& shares) {
	double sum = 0;
	for (auto const& [party,votes] : shares) sum += votes;
	return sum;
}

// Adjust donor rates together just enough to reproduce the starting TPP.
// Only a mixed total is known. Moving the odds by a common factor preserves
// which sources are more inclined to Labor. Newly appearing sources inherit
// that factor too. An impossible starting primary/TPP combination cannot
// identify a set of flows; its caller retains the separate TPP calculation.
inline bool anchorRates(Shares const& prior, double tpp, Shares& rates) {
	CountedBatch donors;
	donors.first = tpp - votesFor(prior,0);
	for (auto const& [party,n] : prior)
		if (party != 0 && party != 1 && n > 0) donors.donors[party] = n;
	double pool = total(donors.donors);
	constexpr double roundoff = 1e-8; // percentage points
	if (donors.first < -roundoff || donors.first > pool + roundoff) return false;
	if (pool == 0) return true;
	Shares fitted;
	std::vector<SourceOdds> work;
	allocateMeasured(donors,rates,fitted,work);
	// Use the most numerous donor to recover the common odds multiplier for
	// a source absent in the original draw. Present donors have a fitted rate.
	auto reference = std::max_element(donors.donors.begin(),donors.donors.end(),
		[](auto const& a, auto const& b) { return a.second < b.second; });
	double const old = std::clamp(rates.at(reference->first),1e-7,1.-1e-7);
	double const revised = std::clamp(fitted[reference->first]/reference->second,0.,1.);
	for (auto& [party,rate] : rates) {
		if (party == 0 || party == 1) continue;
		auto n = donors.donors.find(party);
		if (n != donors.donors.end()) rate = std::clamp(fitted[party]/n->second,0.,1.);
		else {
			double q = std::clamp(rate,1e-7,1.-1e-7);
			double a = revised*(1-old)*q, b = (1-revised)*old*(1-q);
			rate = a/(a+b);
		}
	}
	return true;
}

struct Result {
	double laborPercent;
	Shares rates;
	std::map<int,Totals> sources;
	double primaryChange = 0, measuredChange = 0, learnedChange = 0; // District percentage points.
	double broadPreferenceChange = 0; // Portion of learnedChange from election and regional evidence.
};

// Match the prepared booth cohorts to this draw's turnout, FP and rechecks.
// A measured vote cannot consume more than the available primary voters of
// its source, or more finalist primaries than that side actually received.
// Any unidentified balance uses the remaining district primary mix. This
// accounting assumption by itself supplies no preference evidence elsewhere.
inline std::optional<Shares> coveredInDraw(Shares const& wanted, Shares const& population,
	double first, double second) {
	double const counted = first+second;
	double const identified = total(wanted);
	double const scale = identified > counted ? counted/identified : 1.;
	Shares result, spare;
	for (auto const& [party,n] : population) {
		double capacity = party == 0 ? std::min(n,first) : party == 1 ? std::min(n,second) : n;
		double used = std::min(capacity,votesFor(wanted,party)*scale);
		result[party] = used;
		spare[party] = capacity-used;
	}
	double left = counted-total(result), room = total(spare);
	if (left > room+1e-7) return {};
	if (left > 0 && room > 0)
		for (auto const& [party,n] : spare) result[party] += n*std::min(1.,left/room);
	return result;
}

// Each covered batch fixes its Labor total. Official donor-specific flows
// are preserved where available; otherwise apportion that batch according
// to this draw's relative rates. If a simulated recheck changes a finalist
// total or the available origins, refit just the covered donors to the new
// measured total. Original and revised counts never accumulate.
inline Shares countedTransfers(Category const& measured, Shares const& rates, Shares const& covered,
	double laborVotes) {
	Shares first = measured.measuredFirst, donors = measured.measuredDonors;
	std::vector<SourceOdds> work;
	for (auto const& batch : measured.batches) {
		allocateMeasured(batch,rates,first,work);
		for (auto const& [party,n] : batch.donors) donors[party] += n;
	}
	CountedBatch updated;
	updated.first = laborVotes - votesFor(covered,0);
	Shares observedRates = rates;
	bool changed = std::abs(total(first)-updated.first) > 1e-7;
	for (auto const& [party,n] : covered) {
		if (party == 0 || party == 1) continue;
		double base = votesFor(donors,party);
		if (std::abs(base-n) > 1e-7) changed = true;
		if (n <= 0) continue;
		updated.donors[party] = n;
		if (base > 0) observedRates[party] = std::clamp(votesFor(first,party)/base,0.,1.);
	}
	if (changed) {
		first.clear();
		allocateMeasured(updated,observedRates,first,work);
	}
	return first;
}

// Primary inputs are percentages of formal votes. Count inputs are the
// current scenario's ballots. Positive flow changes mean more preferences
// to Labor, and apply only to outstanding voters of the affected source.
// Summing changes from the prior preserves an exactly unchanged draw without
// a special election-wide switch for the first reporting booth.
inline std::optional<Result> project(District const& district, Shares const& prior, double priorTpp,
	Shares const& current, Shares rates, double turnout, double laborVotes, double coalitionVotes) {
	if (!(turnout > 0) || laborVotes < 0 || coalitionVotes < 0) return {};
	double counted = laborVotes + coalitionVotes;
	turnout = std::max(turnout,counted);
	rates[0] = 1.;
	rates[1] = 0.;
	if (!anchorRates(prior,priorTpp,rates)) return {};
	double movement = 0;
	Shares population;
	for (auto const& [party,rate] : rates) {
		double share = votesFor(current,party);
		movement += (share-votesFor(prior,party))*rate;
		population[party] = share*turnout*.01;
	}
	auto covered = coveredInDraw(district.covered,population,laborVotes,coalitionVotes);
	if (!covered) return {};
	auto first = countedTransfers(district.measured,rates,*covered,laborVotes);
	double measuredDifference = laborVotes, learnedDifference = 0, broadDifference = 0;
	Result result{priorTpp+movement,rates,{}};
	for (auto const& [party,n] : population) {
		double used = covered->at(party), rate = rates.at(party);
		double outstanding = std::max(0.,n-used);
		// The starting rate belongs to this draw and already agrees with its
		// original TPP. Matched counts can change the odds of a minor-party
		// voter preferring Labor over the Coalition. For example, doubling
		// equal odds changes a 50% rate to two in three. Apply that evidence
		// only to voters whose preferences are still unknown, followed by
		// the party-specific change estimated after the broad movement.
		double broadRate = rate;
		if (district.transferOddsMultiplier != 1. && rate > 0. && rate < 1.) {
			double const laborWeight = rate * district.transferOddsMultiplier;
			broadRate = laborWeight / (laborWeight + (1. - rate));
		}
		double revised = std::clamp(broadRate+votesFor(district.rateChange,party),0.,1.);
		measuredDifference -= used*rate;
		broadDifference += outstanding*(broadRate-rate);
		learnedDifference += outstanding*(revised-rate);
		double toLabor = party == 0 ? n : party == 1 ? 0. : votesFor(first,party)+outstanding*revised;
		result.sources[party] = {toLabor,n};
		if (n > 0) result.rates[party] = std::clamp(toLabor/n,0.,1.);
	}
	result.primaryChange = movement;
	result.measuredChange = 100*measuredDifference/turnout;
	result.learnedChange = 100*learnedDifference/turnout;
	result.broadPreferenceChange = 100*broadDifference/turnout;
	result.laborPercent += result.measuredChange + result.learnedChange;
	if (district.sourceArea && counted<turnout) {
		// The transferred booths supply a change in TCP for this geography.
		// On this draw the FP shares are already set. Honour the new estimate
		// by moving only unresolved donor preferences, in proportion to those
		// still available on the losing side. Both the ranked count and the
		// statewide flow report then receive the same revised allocation.
		double const change = district.sourceArea->change(result.laborPercent)*turnout*.01;
		Shares room;
		for (auto const& [party,n] : population) {
			if (party==0 || party==1) continue;
			double remaining = std::max(0.,n-covered->at(party));
			double labor = result.sources.at(party).first-votesFor(first,party);
			room[party] = std::clamp(change>0 ? remaining-labor : labor,0.,remaining);
		}
		double const available = total(room);
		if (available>0 && change!=0) {
			double fraction = std::clamp(change/available,-1.,1.);
			for (auto const& [party,capacity] : room) {
				auto& source = result.sources.at(party);
				source.first += fraction*capacity;
				if (population.at(party)>0) result.rates[party] = std::clamp(source.first/population.at(party),0.,1.);
			}
			result.laborPercent += 100*fraction*available/turnout;
			result.learnedChange += 100*fraction*available/turnout;
		}
	}
	if (counted == turnout) result.laborPercent = 100*laborVotes/turnout;
	return result;
}
}
