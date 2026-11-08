// Exercise candidate exclusions independently of the application, with explicit
// preference accounts whose arithmetic can be checked by hand.
#include "../CoalitionPairing.h"

#include <cassert>
#include <limits>

namespace {

using CoalitionPairing::Decision;
using CoalitionPairing::PartyShare;
using CoalitionPairing::PartyShares;
using CoalitionPairing::Status;
constexpr int Liberal = 1, National = 6, Labor = 0, Greens = 2, Others = -1;

double& share(PartyShares& shares, int party)
{
	auto found = std::find_if(shares.begin(), shares.end(),
		[&](PartyShare const& entry) { return entry.first == party; });
	assert(found != shares.end());
	return found->second;
}

void checkSingleCandidatePaths()
{
	int calls = 0;
	auto sample = [&]() { ++calls; return 0.5; };
	auto allocate = [&](PartyShares&, PartyShares const&) { ++calls; };
	PartyShares const candidates{{Labor, 40.0}, {Liberal, 60.0}};
	for (double nationalFraction : {0.0, 1.0}) {
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			nationalFraction, sample, allocate).status == Status::NotApplicable);
	}
	assert(CoalitionPairing::determine(candidates, {}, Liberal, -1,
		0.5, sample, allocate).status == Status::NotApplicable);
	assert(CoalitionPairing::determine({{Labor, 60.0}, {Greens, 40.0}}, {},
		Liberal, National, 0.5, sample, allocate).status == Status::NotApplicable);
	assert(calls == 0);
}

void checkOwnPrimariesStayWithCandidates()
{
	// LIB 35 + NAT 25 receive nine points, all from Others. At an even
	// incoming split they reach 39.5/29.5, so Labor's 31 eliminates Nationals.
	// Re-dividing the whole Coalition pile into 34.5/34.5 would get this wrong.
	PartyShares const candidates{{Labor, 31.0}, {Liberal, 60.0}};
	PartyShares const excluded{{Others, 9.0}};
	auto allToCoalition = [](PartyShares& targets, PartyShares const& sources) {
		assert(targets.size() == 2);
		for (auto const& source : sources) share(targets, Liberal) += source.second;
	};
	Decision const result = CoalitionPairing::determine(candidates, excluded,
		Liberal, National, 25.0 / 60.0, []() { return 0.5; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	assert(candidates == PartyShares({{Labor, 31.0}, {Liberal, 60.0}}));
	assert(excluded == PartyShares({{Others, 9.0}}));
}

void checkAllCandidatesAndOriginalTransfers()
{
	int samples = 0, allocations = 0;
	PartyShares const candidates{{Labor, 20.0}, {Liberal, 70.0}, {Greens, 8.0}};
	PartyShares const excluded{{Others, 2.0}};
	auto allocate = [&](PartyShares& targets, PartyShares const& sources) {
		++allocations;
		// The allocator must always see one combined Coalition target.
		assert(std::none_of(targets.begin(), targets.end(),
			[](PartyShare const& entry) { return entry.first == National; }));
		if (targets.size() == 3) {
			assert(sources == excluded);
			share(targets, Greens) += 2.0;
		}
		else {
			// Greens held ten points when excluded, but only eight were their
			// own primaries. The two Others points must not be counted twice.
			assert(sources == PartyShares({{Others, 2.0}, {Greens, 8.0}}));
			for (auto const& source : sources) {
				share(targets, Liberal) += source.second * 0.5;
				share(targets, Labor) += source.second * 0.5;
			}
			assert(share(targets, Liberal) == 75.0 && share(targets, Labor) == 25.0);
		}
	};
	auto result = CoalitionPairing::determine(candidates, excluded, Liberal,
		National, 30.0 / 70.0, [&]() { ++samples; return 0.5; }, allocate);
	assert(result.status == Status::BothFinalists);
	assert(allocations == 2 && samples == 1);
}

void checkIdentityAndWeakPartners()
{
	auto allToCoalition = [](PartyShares& targets, PartyShares const& sources) {
		for (auto const& source : sources) share(targets, Liberal) += source.second;
	};
	// Preferences can overturn the primary leader within the Coalition:
	// initial LIB/NAT 20/25 becomes 33.5/26.5, both behind Labor's 40.
	auto result = CoalitionPairing::determine(
		{{Labor, 40.0}, {Liberal, 45.0}}, {{Others, 15.0}},
		Liberal, National, 25.0 / 45.0, []() { return 0.1; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	result = CoalitionPairing::determine(
		{{Labor, 40.0}, {Liberal, 45.0}}, {{Others, 15.0}},
		Liberal, National, 25.0 / 45.0, []() { return 0.9; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == National);

	// A tiny positive partner is examined and excluded without preventing
	// outsiders from continuing. No minimum-share shortcut promotes it.
	int samples = 0;
	auto unchanged = [](PartyShares&, PartyShares const&) {};
	result = CoalitionPairing::determine(
		{{Labor, 30.0}, {Liberal, 40.0}, {Greens, 20.0}, {7, 10.0}}, {},
		Liberal, National, 0.0001, [&]() { ++samples; return 0.5; }, unchanged);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	assert(samples == 1);

	// Both partners can be weak. This hands back to the ordinary count; it
	// does not assert that the surviving Coalition candidate reaches the TCP.
	result = CoalitionPairing::determine(
		{{Labor, 45.0}, {Liberal, 2.0}, {Greens, 53.0}}, {},
		Liberal, National, 0.5, []() { return 0.5; }, unchanged);
	assert(result.status == Status::OnePartnerRemains);
}

void checkSplitDistributionAndInvalidAccounts()
{
	assert(CoalitionPairing::nationalIncomingFraction(0.0) == 0.5);
	double const spread = 1.64485363 * CoalitionPairing::IncomingSplitLogOddsSd;
	assert(std::abs(CoalitionPairing::nationalIncomingFraction(-spread) - 0.25) < 0.0001);
	assert(std::abs(CoalitionPairing::nationalIncomingFraction(spread) - 0.75) < 0.0001);
	auto unchanged = [](PartyShares&, PartyShares const&) {};
	PartyShares const candidates{{Labor, 30.0}, {Liberal, 70.0}};
	double const nan = std::numeric_limits<double>::quiet_NaN();
	for (double invalid : {-0.01, 1.01, nan}) {
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			invalid, []() { return 0.5; }, unchanged).status == Status::Invalid);
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			0.5, [=]() { return invalid; }, unchanged).status == Status::Invalid);
	}
	assert(CoalitionPairing::determine(candidates, {}, Liberal, National, 0.5,
		[]() { return 0.5; }, [](PartyShares& targets, PartyShares const&) {
			share(targets, Liberal) -= 1.0;
		}).status == Status::Invalid);
}

} // namespace

int main()
{
	checkSingleCandidatePaths();
	checkOwnPrimariesStayWithCandidates();
	checkAllCandidatesAndOriginalTransfers();
	checkIdentityAndWeakPartners();
	checkSplitDistributionAndInvalidAccounts();
}
