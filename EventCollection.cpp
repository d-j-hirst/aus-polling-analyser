#include "EventCollection.h"

#include "General.h"
#include "PollingProject.h"

#include <exception>

EventCollection::EventCollection(PollingProject & project)
	: project(project)
{
}

void EventCollection::finaliseFileLoading() {
}

void EventCollection::add(Event event) {
	events.insert({ nextId, event });
	++nextId;
}

void EventCollection::replace(Event::Id id, Event event) {
	events[id] = event;
}

Event const& EventCollection::view(Event::Id id) const {
	return events.at(id);
}

EventCollection::Index EventCollection::idToIndex(Event::Id id) const
{
	auto foundIt = events.find(id);
	if (foundIt == events.end()) return InvalidIndex;
	return std::distance(events.begin(), foundIt);
}

Event::Id EventCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Event::InvalidId;
	return std::next(events.begin(), index)->first;
}

EventCollection::Result EventCollection::canRemove(Event::Id id)
{
	auto eventIt = events.find(id);
	if (eventIt == events.end()) return Result::EventDoesntExist;
	return Result::Ok;
}

void EventCollection::remove(Event::Id id) {
	// A lot of event management is simplified by keeping the first two events consistent,
	// so we forbid removal of these events to avoid messier code.
	// If the user wants different top-two events they can just edit them
	// and having less than two events doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::EventDoesntExist) throw EventDoesntExistException();
	auto eventIt = events.find(id);
	events.erase(eventIt);
}

int EventCollection::count() const {
	return events.size();
}

void EventCollection::logAll() const
{
	for (auto const& [key, thisEvent] : events) {
		logger << thisEvent.textReport();
	}
}
