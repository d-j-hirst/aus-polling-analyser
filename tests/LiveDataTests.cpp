#include "../LiveData.h"
#include "../LiveSimulationMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

static_assert(int(LiveData::VoteType::Invalid) == 0);
static_assert(int(LiveData::VoteType::TIO) == 12);
static_assert(int(LiveData::BoothType::Normal) == 0);
static_assert(int(LiveData::BoothType::Invalid) == 6);

int main()
{
	assert(LiveData::voteTypeName(LiveData::VoteType::PrePoll) == "PrePoll");
	assert(LiveData::voteTypeName(LiveData::VoteType::IVote) == "iVote");
	assert(LiveData::boothTypeName(LiveData::BoothType::Ppvc) == "PPVC");

	LiveData::BoothSnapshot snapshot;
	assert(snapshot.boothType == LiveData::BoothType::Invalid);
	assert(snapshot.voteType == LiveData::VoteType::Invalid);

	LiveData::Internals internals;
	assert(internals.projected2pp == 0.0f);
	assert(internals.raw2ppDeviation == 0.0f);

	assert(LiveSimulationMath::evidenceWeight(0.0f) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(
		LiveSimulationMath::EvidenceEpsilon) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(0.000001f) > 0.0f);
	assert(LiveSimulationMath::evidenceWeight(1.0f) == 1.0f);
	assert(LiveSimulationMath::evidenceWeight(0.0f, 14.0f) == 0.0f);
	assert(LiveSimulationMath::evidenceWeight(1.0f, 14.0f) == 1.0f);
	float previousWeight = 0.0f;
	for (int step = 1; step <= 100; ++step) {
		float const weight = LiveSimulationMath::evidenceWeight(
			float(step) * 0.01f);
		assert(weight >= previousWeight);
		previousWeight = weight;
	}

	auto noActivation = LiveSimulationMath::activateFromSource(
		12.0f, 0.0f, 8.0f);
	assert(noActivation.partyShare == 0.0f);
	assert(noActivation.sourceShare == 8.0f);
	assert(!noActivation.requiresNormalisation);

	auto partialActivation = LiveSimulationMath::activateFromSource(
		12.0f, 0.25f, 8.0f);
	assert(std::abs(partialActivation.partyShare - 3.0f) < 0.00001f);
	assert(std::abs(partialActivation.sourceShare - 5.0f) < 0.00001f);
	assert(std::abs(
		partialActivation.partyShare + partialActivation.sourceShare -
		8.0f) < 0.00001f);
	assert(!partialActivation.requiresNormalisation);

	auto limitedActivation = LiveSimulationMath::activateFromSource(
		12.0f, 1.0f, 8.0f);
	assert(limitedActivation.partyShare == 12.0f);
	assert(limitedActivation.sourceShare == 0.0f);
	assert(limitedActivation.requiresNormalisation);

	auto reservedActivation = LiveSimulationMath::activateFromSource(
		12.0f, 1.0f, 8.0f, 2.0f);
	assert(reservedActivation.partyShare == 12.0f);
	assert(reservedActivation.sourceShare == 2.0f);
	assert(reservedActivation.requiresNormalisation);

	float const equalStrengthReserve = LiveSimulationMath::othersReserve(
		10.0f, 4, {2.0f});
	float const strongPartyReserve = LiveSimulationMath::othersReserve(
		10.0f, 4, {6.0f});
	assert(std::abs(equalStrengthReserve - 8.0f) < 0.00001f);
	assert(std::abs(strongPartyReserve - (40.0f / 7.0f)) < 0.00001f);
	assert(strongPartyReserve < equalStrengthReserve);

	std::cout << "Live data tests passed\n";
}
