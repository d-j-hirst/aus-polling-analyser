#pragma once

#include "Party.h"

#include <map>

class PollingProject;

class PartyLimitException : public std::runtime_error {
public:
	PartyLimitException() : std::runtime_error("") {}
};

class RemoveMajorPartyException : public std::runtime_error {
public:
	RemoveMajorPartyException() : std::runtime_error("") {}
};

class PartyDoesntExistException : public std::runtime_error {
public:
	PartyDoesntExistException() : std::runtime_error("") {}
};

class PartyCollection {
public:
	// Collection is a map between ID values and parties
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with party deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of parties is in order they are added.
	typedef int PartyKey;
	typedef std::map<PartyKey, Party> PartyContainer;

	// Party index refers to the position of the party in the order of currently existing parties
	// Should not be stored persistently as removal of a party will change the indices
	// (use the PartyKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	constexpr static int MaxParties = 15;
	constexpr static int NumMajorParties = 2;

	PartyCollection(PollingProject& project);
	
	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		TooManyParties,
		CantRemoveMajorParty,
		PartyDoesntExist,
	};

	// Checks if it is currently possible to add a party
	// Returns Result::Ok if it's possible and Result::TooManyParties if there are too many parties
	Result canAdd();

	// Adds the party "party".
	// Throws an exception if the number of parties is over the limit, check this beforehand using canAdd();
	void add(Party party);

	// Replaces the party with index "partyIndex" by "party".
	void replace(Party::Id id, Party party);

	// Checks if it is currently possible to add the given party
	// Returns Result::Ok if it's possible and Result::TooManyParties if there are too many parties
	Result canRemove(Party::Id id);

	// Removes the party with index "partyIndex".
	void remove(Party::Id id);

	// Returns the party with index "partyIndex".
	Party const& view(Party::Id id) const;

	// Returns the party with index "partyIndex".
	Party const& viewByIndex(Index partyIndex) const { return view(indexToId(partyIndex)); }

	Index idToIndex(Party::Id id) const;
	Party::Id indexToId(Index id) const;

	// Returns the number of parties.
	int count() const;

	Party& back() { return std::prev(parties.end())->second; }

	// Gets the begin iterator for the pollster list.
	PartyContainer::iterator begin() { return parties.begin(); }

	// Gets the end iterator for the pollster list.
	PartyContainer::iterator end() { return parties.end(); }

	// Gets the begin iterator for the pollster list.
	PartyContainer::const_iterator cbegin() const { return parties.cbegin(); }

	// Gets the end iterator for the pollster list.
	PartyContainer::const_iterator cend() const { return parties.cend(); }

	// returns true if the two parties given are opposite major parties,
	// or parties that count as if they were those parties
	bool oppositeMajors(Party::Id party1, Party::Id party2) const;
	
private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	PartyContainer parties;

	PollingProject& project;
};