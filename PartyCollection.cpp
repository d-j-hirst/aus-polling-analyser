#include "PartyCollection.h"

#include "PollingProject.h"

#include <exception>

PartyCollection::PartyCollection(PollingProject & project)
	: project(project)
{
}

void PartyCollection::finaliseFileLoading() {
	// Set the two major partyCollection, in case this file comes from a version in which "count-as-party" data was not recorded
	parties[0].countAsParty = Party::CountAsParty::IsPartyOne;
	parties[0].supportsParty = Party::SupportsParty::One;
	parties[1].countAsParty = Party::CountAsParty::IsPartyTwo;
	parties[1].supportsParty = Party::SupportsParty::Two;
}
PartyCollection::Result PartyCollection::canAdd()
{
	return (count() >= MaxParties ? Result::TooManyParties : Result::Ok);
}
void PartyCollection::add(Party party) {
	if (canAdd() != Result::Ok) throw PartyLimitException();
	parties.insert({ nextId, party });
	++nextId;
}

void PartyCollection::replace(Party::Id id, Party party) {
	parties[id] = party;
}

Party const& PartyCollection::view(Party::Id id) const {
	return parties.at(id);
}

PartyCollection::Index PartyCollection::idToIndex(Party::Id id) const
{
	auto foundIt = parties.find(id);
	if (foundIt == parties.end()) return InvalidIndex;
	return std::distance(parties.begin(), foundIt);
}

Party::Id PartyCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Party::InvalidId;
	return std::next(parties.begin(), index)->first;
}

PartyCollection::Result PartyCollection::canRemove(Party::Id id)
{
	if (id < NumMajorParties) return Result::CantRemoveMajorParty;
	auto partyIt = parties.find(id);
	if (partyIt == parties.end()) return Result::PartyDoesntExist;
	return Result::Ok;
}

void PartyCollection::remove(Party::Id id) {
	// A lot of party management is simplified by keeping the first two parties consistent,
	// so we forbid removal of these parties to avoid messier code.
	// If the user wants different top-two parties they can just edit them
	// and having less than two parties doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::CantRemoveMajorParty) throw RemoveMajorPartyException();
	if (removeAllowed == Result::PartyDoesntExist) throw PartyDoesntExistException();
	Index index = idToIndex(id);
	auto partyIt = parties.find(id);
	parties.erase(partyIt);
	project.adjustAfterPartyRemoval(index, id);
}

int PartyCollection::count() const {
	return parties.size();
}

bool PartyCollection::oppositeMajors(Party::Id id1, Party::Id id2) const
{
	return (view(id1).countsAsOne() && view(id2).countsAsTwo()) || (view(id1).countsAsTwo() && view(id2).countsAsOne());
}