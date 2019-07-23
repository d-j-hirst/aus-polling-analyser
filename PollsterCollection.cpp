#include "PollsterCollection.h"

#include "PollingProject.h"

#include <exception>

PollsterCollection::PollsterCollection(PollingProject& project)
	: project(project)
{
}

void PollsterCollection::finaliseFileLoading() {
}

void PollsterCollection::add(Pollster pollster) {
	pollsters.insert({ nextId, pollster });
	++nextId;
}

void PollsterCollection::replace(Pollster::Id id, Pollster pollster) {
	pollsters[id] = pollster;
}

Pollster const& PollsterCollection::view(Pollster::Id id) const {
	return pollsters.at(id);
}

PollsterCollection::Index PollsterCollection::idToIndex(Pollster::Id id) const
{
	auto foundIt = pollsters.find(id);
	if (foundIt == pollsters.end()) return InvalidIndex;
	return std::distance(pollsters.begin(), foundIt);
}

Pollster::Id PollsterCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Pollster::InvalidId;
	return std::next(pollsters.begin(), index)->first;
}

PollsterCollection::Result PollsterCollection::canRemove(Pollster::Id id)
{
	if (count() <= NumRequiredPollsters) return Result::CantRemoveRequiredPollster;
	auto pollsterIt = pollsters.find(id);
	if (pollsterIt == pollsters.end()) return Result::PollsterDoesntExist;
	return Result::Ok;
}

void PollsterCollection::remove(Pollster::Id id) {
	// A lot of pollster management is simplified by keeping the first two pollsters consistent,
	// so we forbid removal of these pollsters to avoid messier code.
	// If the user wants different top-two pollsters they can just edit them
	// and having less than two pollsters doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::CantRemoveRequiredPollster) throw RemoveRequiredPollsterException();
	if (removeAllowed == Result::PollsterDoesntExist) throw PollsterDoesntExistException();
	Index index = idToIndex(id);
	auto pollsterIt = pollsters.find(id);
	pollsters.erase(pollsterIt);
	project.adjustAfterPollsterRemoval(index, id);
}

int PollsterCollection::count() const {
	return pollsters.size();
}
