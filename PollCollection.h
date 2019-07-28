#pragma once

#include "PartyCollection.h"
#include "Poll.h"

#include <map>

class PollingProject;

class PollDoesntExistException : public std::runtime_error {
public:
	PollDoesntExistException() : std::runtime_error("") {}
};

class PollCollection {
public:
	// Collection is a map between ID values and polls
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with poll deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of polls is in order they are added.
	typedef int PollKey;
	typedef std::map<PollKey, Poll> PollContainer;

	// Poll index refers to the position of the poll in the order of currently existing polls
	// Should not be stored persistently as removal of a poll will change the indices
	// (use the PollKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	constexpr static int NumRequiredPolls = 1;

	PollCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		CantRemoveRequiredPoll,
		PollDoesntExist,
	};

	// Adds the poll "poll".
	// Throws an exception if the number of polls is over the limit, check this beforehand using canAdd();
	void add(Poll poll);

	// Replaces the poll with index "pollIndex" by "poll".
	void replace(Poll::Id id, Poll poll);

	// Checks if it is currently possible to add the given poll
	// Returns Result::Ok if it's possible and Result::TooManyPolls if there are too many polls
	Result canRemove(Poll::Id id);

	// Removes the poll with index "pollIndex".
	void remove(Poll::Id id);

	// Returns the poll with index "pollIndex".
	Poll const& view(Poll::Id id) const;

	// Returns the poll with index "pollIndex".
	Poll const& viewByIndex(Index pollIndex) const { return view(indexToId(pollIndex)); }

	Index idToIndex(Poll::Id id) const;
	Poll::Id indexToId(Index id) const;

	// Returns the number of polls.
	int count() const;

	// gets the date in MJD form of the earliest poll
	int getEarliestDate() const;

	// gets the date in MJD form of the most recent poll
	int getLatestDate() const;

	// Removes all the polls from a particular pollster. Used when deleting a pollster.
	void removePollsFromPollster(Pollster::Id pollster);

	// If a party is removed, polls need to be adjusted to deal with this
	void adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	Poll& back() { return std::prev(polls.end())->second; }

	// Gets the begin iterator for the poll list.
	PollContainer::iterator begin() { return polls.begin(); }

	// Gets the end iterator for the poll list.
	PollContainer::iterator end() { return polls.end(); }

	// Gets the begin iterator for the poll list.
	PollContainer::const_iterator cbegin() const { return polls.cbegin(); }

	// Gets the end iterator for the poll list.
	PollContainer::const_iterator cend() const { return polls.cend(); }

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	PollContainer polls;

	PollingProject& project;
};