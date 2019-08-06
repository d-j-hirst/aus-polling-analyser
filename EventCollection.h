#pragma once

#include "Event.h"

#include <map>

class PollingProject;

class EventDoesntExistException : public std::runtime_error {
public:
	EventDoesntExistException() : std::runtime_error("") {}
};

class EventCollection {
public:
	// Collection is a map between ID values and events
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with event deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of events is in order they are added.
	typedef std::map<Event::Id, Event> EventContainer;

	// Event index refers to the position of the event in the order of currently existing events
	// Should not be stored persistently as removal of a event will change the indices
	// (use the EventKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	EventCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		EventDoesntExist,
	};

	// Adds the event "event".
	// Throws an exception if the number of events is over the limit, check this beforehand using canAdd();
	void add(Event event);

	// Replaces the event with index "eventIndex" by "event".
	void replace(Event::Id id, Event event);

	// Checks if it is currently possible to add the given event
	// Returns Result::Ok if it's possible and Result::EventDoesntExist if that event doesn't exist
	Result canRemove(Event::Id id);

	// Removes the event with index "eventIndex".
	void remove(Event::Id id);

	// Returns the event with the given id
	Event const& view(Event::Id id) const;

	// Returns the event with index "eventIndex".
	Event const& viewByIndex(Index eventIndex) const { return view(indexToId(eventIndex)); }

	Index idToIndex(Event::Id id) const;
	Event::Id indexToId(Index id) const;

	// Returns the number of events.
	int count() const;

	Event& back() { return std::prev(events.end())->second; }

	// Gets the begin iterator for the pollster list.
	EventContainer::iterator begin() { return events.begin(); }

	// Gets the end iterator for the pollster list.
	EventContainer::iterator end() { return events.end(); }

	// Gets the begin iterator for the pollster list.
	EventContainer::const_iterator cbegin() const { return events.cbegin(); }

	// Gets the end iterator for the pollster list.
	EventContainer::const_iterator cend() const { return events.cend(); }

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	EventContainer events;

	PollingProject& project;
};