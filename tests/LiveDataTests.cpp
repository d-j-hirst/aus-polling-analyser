#include "../LiveData.h"

#include <cassert>
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

	std::cout << "Live data tests passed\n";
}
