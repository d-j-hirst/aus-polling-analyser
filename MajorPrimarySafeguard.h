#pragma once

// Reconcile unusually low *pre-count* major-party primaries with uncertain
// preferences in one simulated election. A seat can give Labor more first
// preferences at the same TPP if fewer minor-party ballots ultimately reach
// Labor. Other seats help balance the changes against the statewide FP draw.
// All primaries and changes are percentage points of a district's formal vote.
// Preference rates are fractions of one source party's original ballots.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

namespace MajorPrimarySafeguard {

// The 2022 replay first estimates how the historical Labor–Greens contest
// changes the starting primaries. Applying the low-primary prior to that
// reference would also change the spread used to fit the historical evidence.
// Apply it to the subsequent odds calibration, live baseline and forecast.
// The 2026 SA replay and Victorian forecast both use the safeguard throughout
// calibration so their live starting estimates use the same low-primary rule.
inline constexpr bool enabled(std::string_view election, bool estimatingPairingReference = false)
{
	return election == "2026sa" || election == "2026vic" || (election == "2022vic" && !estimatingPairingReference);
}

// The three separate sources are Greens, One Nation and grouped Others.
// A source absent from the ballot has zero primary and contributes no votes.
using BySource = std::array<double,3>;

struct Source {
	double primary = 0.;
	double rate = 0.;   // Drawn district rate before matching the seat TPP.
	double centre = 0.; // Same draw with only the district-specific noise removed.
	double fitted = 0.; // Rate after matching the seat TPP and partner leakage.
	double requestedExchange = 0., availableExchange = 0.;
	double release = 0., laborFraction = 0., receiveWeight = 0.;
};

struct District {
	double turnout = 0.; // Previous formal votes, also used in statewide FP reconciliation.
	double labor = 0., coalition = 0., leakage = 0., laborTpp = 0.;
	double shoulder = 0., laborNeed = 0., coalitionNeed = 0.;
	std::array<Source,3> sources{};
};

// Major parties have rarely fallen below about 6% in a general election.
// This is a soft historical prior. The shoulder varies between 6 and 12%
// across draws. Below it, the requested increase grows smoothly with the
// shortfall. At the typical shoulder of 9%, a near-zero draw requests about
// six points. Weak support at the last election or a 25–50% independent
// progressively relaxes the prior. A missing historical result supplies no
// such evidence. The SA replay uses the same curve settings as Victoria.
inline double requestedIncrease(double primary, double previous, double independent, double shoulder)
{
	double const local = previous > 0. ? std::min(previous/12.,1.) : 1.;
	double const threshold = shoulder * local * std::clamp((50.-independent)/25.,0.,1.);
	if (primary >= threshold) return 0.;
	double const gap = threshold-primary;
	return .85*gap*gap / (gap+threshold*(2.5/9.));
}

// Transfer the change from one preference rate to another on an odds scale.
// This keeps the seat-wide adjustment common to all donors when removing or
// restoring an individual party's local variation. All three rates are interior.
inline double shiftOdds(double starting, double from, double to)
{
	auto logOdds = [](double p) { return std::log(p/(1.-p)); };
	return 1./(1.+std::exp(-(logOdds(starting)+logOdds(to)-logOdds(from))));
}

// Ask each donor how many *major* primary votes could change sides while its
// local preference rate moves back towards its centre. For example, with no
// partner leakage, reducing a 30%-primary party's flow by ten points makes
// room for three points of Labor FP at the same TPP. A positive exchange
// moves primaries from the Coalition to Labor. A negative one reverses it.
inline void planExchanges(District& district)
{
	double const need = district.laborNeed-district.coalitionNeed;
	double capacity = 0.;
	for (auto& source : district.sources) {
		// Endpoints contain no usable odds information. Leave those rates
		// alone, including a genuine zero or unanimous preference flow.
		if (source.primary <= 0. || std::min({source.rate,1.-source.rate,source.fitted,
			1.-source.fitted,source.centre,1.-source.centre}) < 1e-7) continue;
		double const target = shiftOdds(source.fitted,source.rate,source.centre);
		double const exchange = source.primary*(source.fitted-target)/(1.-district.leakage);
		if (exchange*need > 0.) {
			source.requestedExchange = exchange;
			capacity += std::abs(exchange);
		} else if (district.laborNeed == 0. && district.coalitionNeed == 0.) {
			// Only a district with two comfortable major primaries may
			// compensate. Divide headroom above 12% between the sources
			// so simultaneous exchanges cannot spend the same votes twice.
			double const room = std::max(0.,std::min(district.labor,district.coalition)-12.)/3.;
			source.availableExchange = std::clamp(exchange,-room,room);
		}
	}
	if (capacity == 0.) return;
	double const payingMajor = need > 0. ? district.coalition : district.labor;
	// Stop at the requested relief and leave at least a tenth of the other
	// major's primary in an exceptionally fragmented contest.
	double const scale = std::min({1.,std::abs(need)/capacity,.9*payingMajor/capacity});
	for (auto& source : district.sources) source.requestedExchange *= scale;
}

// Offset each source's exchanges across districts using turnout. Where too
// few districts can compensate, scale the requests down. This changes the
// two major primaries while preserving the statewide primary for each and
// the local primary vote of every minor party.
inline void adjustPreferences(std::vector<District>& districts)
{
	BySource requested{}, availableUp{}, availableDown{};
	for (auto& district : districts) {
		planExchanges(district);
		for (std::size_t j = 0; j < requested.size(); ++j) {
			auto const& source = district.sources[j];
			requested[j] += district.turnout*source.requestedExchange;
			availableUp[j] += district.turnout*std::max(0.,source.availableExchange);
			availableDown[j] += district.turnout*std::max(0.,-source.availableExchange);
		}
	}
	for (auto& district : districts) {
		double netToLabor = 0.;
		for (std::size_t j = 0; j < requested.size(); ++j) {
			auto& source = district.sources[j];
			double const net = requested[j];
			double const reserve = net > 0. ? availableDown[j] : availableUp[j];
			double const scale = net == 0. ? 1. : std::min(1.,reserve/std::abs(net));
			double exchange = source.requestedExchange*scale;
			if (net*source.availableExchange < 0. && reserve > 0.)
				exchange += source.availableExchange*std::min(1.,std::abs(net)/reserve);
			if (exchange == 0.) continue;
			double const revised = source.fitted-exchange*(1.-district.leakage)/source.primary;
			source.rate = shiftOdds(source.rate,source.fitted,revised);
			source.fitted = revised;
			district.labor += exchange;
			district.coalition -= exchange;
			netToLabor += exchange;
		}
		district.laborNeed = std::max(0.,district.laborNeed-std::max(0.,netToLabor));
		district.coalitionNeed = std::max(0.,district.coalitionNeed+std::min(0.,netToLabor));
	}
}

// If the flows can only explain some of the shortfall, convert part of each
// donor primary to major-party primaries. Divide those votes according to
// their effective Labor/Coalition split so the local TPP is still available.
// Use more votes from sources that typically favour the major needing help.
inline void planPrimaryTransfers(District& district)
{
	double const need = district.laborNeed+district.coalitionNeed;
	double laborCapacity = 0., coalitionCapacity = 0.;
	double const room = std::max(0.,std::min(district.labor,district.coalition)-std::max(9.,district.shoulder));
	for (auto& source : district.sources) {
		// Larger donors in districts well clear of both soft thresholds can
		// receive more of that party's primary back from the statewide pool.
		source.receiveWeight = source.primary*room;
		if (need == 0.) continue;
		// With a single Coalition candidate, 40% preferencing Labor
		// converts to 40% Labor FP. With two Coalition candidates
		// some of the new Coalition primaries also reach Labor, so
		// fewer of the converted ballots need to become Labor primaries.
		source.laborFraction = std::clamp((source.fitted-district.leakage)/(1.-district.leakage),0.,1.);
		double const affinity = (district.laborNeed*source.laborFraction +
			district.coalitionNeed*(1.-source.laborFraction))/need;
		source.release = source.primary*affinity;
		laborCapacity += source.release*source.laborFraction;
		coalitionCapacity += source.release*(1.-source.laborFraction);
	}
	double scale = std::min(1.,std::max(laborCapacity > 0. ? district.laborNeed/laborCapacity : 0.,
		coalitionCapacity > 0. ? district.coalitionNeed/coalitionCapacity : 0.));
	// In a split Coalition seat an excluded partner can send even more to
	// Labor than a particular minor party. Converting those minor ballots
	// to Coalition FP then uses some Labor transfers too. Keep the TPP
	// reachable even if almost no such transfers remain in this draw.
	double const laborVotesUsed = laborCapacity+district.leakage*coalitionCapacity;
	if (district.leakage > 0. && laborVotesUsed > 0.) {
		double const transfers = district.laborTpp-district.labor-district.leakage*district.coalition;
		scale = std::min(scale,std::max(0.,transfers)/laborVotesUsed);
	}
	for (auto& source : district.sources) source.release *= scale;
}

struct PrimaryPool {
	BySource released{}, labor{}, receiveWeight{};
};

// Move the same weighted total back to that source in receiving districts.
// Each receiver gives up the statewide Labor/Coalition mix released for that
// source. Its local source rates are fitted to its own TPP by the usual count.
inline void adjustPrimaries(std::vector<District>& districts)
{
	PrimaryPool pool;
	for (auto& district : districts) {
		planPrimaryTransfers(district);
		for (std::size_t j = 0; j < district.sources.size(); ++j) {
			auto const& source = district.sources[j];
			double const votes = district.turnout*source.release;
			pool.released[j] += votes;
			pool.labor[j] += votes*source.laborFraction;
			pool.receiveWeight[j] += district.turnout*source.receiveWeight;
		}
	}
	double feasible = 1.;
	for (auto const& district : districts) {
		double laborCost = 0., coalitionCost = 0.;
		for (std::size_t j = 0; j < district.sources.size(); ++j) {
			if (pool.receiveWeight[j] == 0.) continue;
			double const weight = district.sources[j].receiveWeight/pool.receiveWeight[j];
			laborCost += weight*pool.labor[j];
			coalitionCost += weight*(pool.released[j]-pool.labor[j]);
		}
		// All receivers retain at least the typical 9% shoulder, even when
		// several sources need compensation in the same simulated election.
		if (laborCost > 0.) feasible = std::min(feasible,(district.labor-9.)/laborCost);
		if (coalitionCost > 0.) feasible = std::min(feasible,(district.coalition-9.)/coalitionCost);
	}
	for (auto& district : districts) {
		double laborChange = 0., coalitionChange = 0.;
		for (std::size_t j = 0; j < district.sources.size(); ++j) {
			auto& source = district.sources[j];
			// A party standing only in the affected district cannot
			// give up local FP while retaining its statewide vote.
			if (pool.receiveWeight[j] == 0.) continue;
			double const weight = source.receiveWeight/pool.receiveWeight[j];
			source.primary += feasible*(weight*pool.released[j]-source.release);
			laborChange += feasible*(source.release*source.laborFraction-weight*pool.labor[j]);
			coalitionChange += feasible*(source.release*(1.-source.laborFraction)-weight*(pool.released[j]-pool.labor[j]));
		}
		district.labor += laborChange;
		district.coalition += coalitionChange;
	}
}

}
