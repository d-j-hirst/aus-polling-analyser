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

float& share(PartyShares& shares, int party)
{
	auto found = std::find_if(shares.begin(), shares.end(),
		[&](PartyShare const& entry) { return entry.first == party; });
	assert(found != shares.end());
	return found->second;
}

void checkSingleCandidatePaths()
{
	int calls = 0;
	auto sample = [&]() { ++calls; return 0.5f; };
	auto allocate = [&](PartyShares&, PartyShares const&) { ++calls; };
	PartyShares const candidates{{Labor, 40.0f}, {Liberal, 60.0f}};
	for (float nationalFraction : {0.0f, 1.0f}) {
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			nationalFraction, sample, allocate).status == Status::NotApplicable);
	}
	assert(CoalitionPairing::determine(candidates, {}, Liberal, -1,
		0.5f, sample, allocate).status == Status::NotApplicable);
	assert(CoalitionPairing::determine({{Labor, 60.0f}, {Greens, 40.0f}}, {},
		Liberal, National, 0.5f, sample, allocate).status == Status::NotApplicable);
	assert(calls == 0);
}

void checkOwnPrimariesStayWithCandidates()
{
	// LIB 35 + NAT 25 receive nine points, all from Others. At an even
	// incoming split they reach 39.5/29.5, so Labor's 31 eliminates Nationals.
	// Re-dividing the whole Coalition pile into 34.5/34.5 would get this wrong.
	PartyShares const candidates{{Labor, 31.0f}, {Liberal, 60.0f}};
	PartyShares const excluded{{Others, 9.0f}};
	auto allToCoalition = [](PartyShares& targets, PartyShares const& sources) {
		assert(targets.size() == 2);
		for (auto const& source : sources) share(targets, Liberal) += source.second;
	};
	Decision const result = CoalitionPairing::determine(candidates, excluded,
		Liberal, National, 25.0f / 60.0f, []() { return 0.5f; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	assert(candidates == PartyShares({{Labor, 31.0f}, {Liberal, 60.0f}}));
	assert(excluded == PartyShares({{Others, 9.0f}}));
}

void checkAllCandidatesAndOriginalTransfers()
{
	int samples = 0, allocations = 0;
	PartyShares const candidates{{Labor, 20.0f}, {Liberal, 70.0f}, {Greens, 8.0f}};
	PartyShares const excluded{{Others, 2.0f}};
	auto allocate = [&](PartyShares& targets, PartyShares const& sources) {
		++allocations;
		// The allocator must always see one combined Coalition target.
		assert(std::none_of(targets.begin(), targets.end(),
			[](PartyShare const& entry) { return entry.first == National; }));
		if (targets.size() == 3) {
			assert(sources == excluded);
			share(targets, Greens) += 2.0f;
		}
		else {
			// Greens held ten points when excluded, but only eight were their
			// own primaries. The two Others points must not be counted twice.
			assert(sources == PartyShares({{Others, 2.0f}, {Greens, 8.0f}}));
			for (auto const& source : sources) {
				share(targets, Liberal) += source.second * 0.5f;
				share(targets, Labor) += source.second * 0.5f;
			}
			assert(share(targets, Liberal) == 75.0f && share(targets, Labor) == 25.0f);
		}
	};
	auto result = CoalitionPairing::determine(candidates, excluded, Liberal,
		National, 30.0f / 70.0f, [&]() { ++samples; return 0.5f; }, allocate);
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
		{{Labor, 40.0f}, {Liberal, 45.0f}}, {{Others, 15.0f}},
		Liberal, National, 25.0f / 45.0f, []() { return 0.1f; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	result = CoalitionPairing::determine(
		{{Labor, 40.0f}, {Liberal, 45.0f}}, {{Others, 15.0f}},
		Liberal, National, 25.0f / 45.0f, []() { return 0.9f; }, allToCoalition);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == National);

	// A tiny positive partner is examined and excluded without preventing
	// outsiders from continuing. No minimum-share shortcut promotes it.
	int samples = 0;
	auto unchanged = [](PartyShares&, PartyShares const&) {};
	result = CoalitionPairing::determine(
		{{Labor, 30.0f}, {Liberal, 40.0f}, {Greens, 20.0f}, {7, 10.0f}}, {},
		Liberal, National, 0.0001f, [&]() { ++samples; return 0.5f; }, unchanged);
	assert(result.status == Status::OnePartnerRemains && result.survivingParty == Liberal);
	assert(samples == 1);

	// Both partners can be weak. This hands back to the ordinary count; it
	// does not assert that the surviving Coalition candidate reaches the TCP.
	result = CoalitionPairing::determine(
		{{Labor, 45.0f}, {Liberal, 2.0f}, {Greens, 53.0f}}, {},
		Liberal, National, 0.5f, []() { return 0.5f; }, unchanged);
	assert(result.status == Status::OnePartnerRemains);
}

void checkSplitDistributionAndInvalidAccounts()
{
	assert(CoalitionPairing::nationalIncomingFraction(0.0f) == 0.5f);
	float const spread = 1.64485363f * CoalitionPairing::IncomingSplitLogOddsSd;
	assert(std::abs(CoalitionPairing::nationalIncomingFraction(-spread) - 0.25f) < 0.0001f);
	assert(std::abs(CoalitionPairing::nationalIncomingFraction(spread) - 0.75f) < 0.0001f);
	auto unchanged = [](PartyShares&, PartyShares const&) {};
	PartyShares const candidates{{Labor, 30.0f}, {Liberal, 70.0f}};
	float const nan = std::numeric_limits<float>::quiet_NaN();
	for (float invalid : {-0.01f, 1.01f, nan}) {
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			invalid, []() { return 0.5f; }, unchanged).status == Status::Invalid);
		assert(CoalitionPairing::determine(candidates, {}, Liberal, National,
			0.5f, [=]() { return invalid; }, unchanged).status == Status::Invalid);
	}
	assert(CoalitionPairing::determine(candidates, {}, Liberal, National, 0.5f,
		[]() { return 0.5f; }, [](PartyShares& targets, PartyShares const&) {
			share(targets, Liberal) -= 1.0f;
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
