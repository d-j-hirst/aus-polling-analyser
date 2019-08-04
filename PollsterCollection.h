#pragma once

#include "Pollster.h"

#include <map>

class PollingProject;

class RemoveRequiredPollsterException : public std::runtime_error {
public:
	RemoveRequiredPollsterException() : std::runtime_error("") {}
};

class PollsterDoesntExistException : public std::runtime_error {
public:
	PollsterDoesntExistException() : std::runtime_error("") {}
};

class PollsterCollection {
public:
	// Collection is a map between ID values and pollsters
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with pollster deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of pollsters is in order they are added.
	typedef std::map<Pollster::Id, Pollster> PollsterContainer;

	// Pollster index refers to the position of the pollster in the order of currently existing pollsters
	// Should not be stored persistently as removal of a pollster will change the indices
	// (use the PollsterKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	constexpr static int NumRequiredPollsters = 1;

	PollsterCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		CantRemoveRequiredPollster,
		PollsterDoesntExist,
	};

	// Adds the pollster "pollster".
	// Throws an exception if the number of pollsters is over the limit, check this beforehand using canAdd();
	void add(Pollster pollster);

	// Replaces the pollster with index "pollsterIndex" by "pollster".
	void replace(Pollster::Id id, Pollster pollster);

	// Checks if it is currently possible to add the given pollster
	// Returns Result::Ok if it's possible and Result::TooManyPollsters if there are too many pollsters
	Result canRemove(Pollster::Id id);

	// Removes the pollster with index "pollsterIndex".
	void remove(Pollster::Id id);

	// Returns the pollster with index "pollsterIndex".
	Pollster const& view(Pollster::Id id) const;

	// Returns the pollster with index "pollsterIndex".
	Pollster const& viewByIndex(Index pollsterIndex) const { return view(indexToId(pollsterIndex)); }

	Index idToIndex(Pollster::Id id) const;
	Pollster::Id indexToId(Index id) const;

	// Returns the number of pollsters.
	int count() const;

	Pollster& back() { return std::prev(pollsters.end())->second; }

	// Gets the begin iterator for the pollster list.
	PollsterContainer::iterator begin() { return pollsters.begin(); }

	// Gets the end iterator for the pollster list.
	PollsterContainer::iterator end() { return pollsters.end(); }

	// Gets the begin iterator for the pollster list.
	PollsterContainer::const_iterator cbegin() const { return pollsters.cbegin(); }

	// Gets the end iterator for the pollster list.
	PollsterContainer::const_iterator cend() const { return pollsters.cend(); }

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	PollsterContainer pollsters;

	PollingProject& project;
};