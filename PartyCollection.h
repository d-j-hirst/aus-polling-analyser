#pragma once

#include "Party.h"

#include <map>

class PollingProject;
class Poll;

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
	typedef std::map<Party::Id, Party> PartyContainer;

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

	// Returns the party with the given abbreviation. Returns -1 if no such party exists.
	int idByAbbreviation(std::string abbreviation) const;

	// Returns the party with the given short code. Returns -1 if no such party exists.
	int indexByShortCode(std::string shortCode) const;

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
	PartyContainer::const_iterator begin() const { return parties.begin(); }

	// Gets the end iterator for the pollster list.
	PartyContainer::const_iterator end() const { return parties.end(); }

	// Gets the begin iterator for the pollster list.
	PartyContainer::const_iterator cbegin() const { return parties.cbegin(); }

	// Gets the end iterator for the pollster list.
	PartyContainer::const_iterator cend() const { return parties.cend(); }

	void setOthersPreferenceFlow(float in_othersPreferenceFlow) { othersPreferenceFlow = in_othersPreferenceFlow; }
	float getOthersPreferenceFlow() { return othersPreferenceFlow; }

	void setOthersExhaustRate(float in_othersExhaustRate) { othersExhaustRate = in_othersExhaustRate; }
	float getOthersExhaustRate() { return othersExhaustRate; }

	// returns true if the two parties given are opposite major parties,
	// or parties that count as if they were those parties
	bool oppositeMajors(Party::Id party1, Party::Id party2) const;

	// recalculates the poll's estimated two-party-preferred based on primary votes.
	// This function will directly edit the poll's data, but does not affect the
	// project's state directly.
	void recalculatePollCalc2PP(Poll& poll) const;

	void logAll() const;
	
private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	// indicates the last-election preference flow from "Others" to the first listed major party.
	float othersPreferenceFlow = 46.5f;

	// indicates the last-election percentage of the "Others" vote that exhausted without reaching a major party.
	float othersExhaustRate = 0.0f;

	PartyContainer parties;

	PollingProject& project;
};