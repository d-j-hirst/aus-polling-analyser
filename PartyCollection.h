#pragma once

#include "Party.h"

#include <list>

class PollingProject;

class PartyCollection {
public:
	PartyCollection(PollingProject& project) : project(project) {}

	// Adds the party "party".
	void addParty(Party party);

	// Replaces the party with index "partyIndex" by "party".
	void replaceParty(int partyIndex, Party party);

	// Removes the party with index "partyIndex".
	void removeParty(int partyIndex);

	// Returns the party with index "partyIndex".
	Party getParty(int partyIndex) const;

	Party const* partyOne() const { return &*parties.begin(); }
	Party const* partyTwo() const { return &*std::next(parties.begin()); }

	// Returns a pointer to the party with index "partyIndex".
	Party* getPartyPtr(int partyIndex);

	// Returns a pointer to the party with index "partyIndex".
	Party const* getPartyPtr(int partyIndex) const;

	// Returns the number of parties.
	int getPartyCount() const;

	// Gets the party index from a given pointer.
	int getPartyIndex(Party const* partyPtr);

	Party& back() { return parties.back(); }

	// Gets the begin iterator for the pollster list.
	std::list<Party>::iterator begin() { return parties.begin(); }

	// Gets the end iterator for the pollster list.
	std::list<Party>::iterator end() { return parties.end(); }

	// Gets the begin iterator for the pollster list.
	std::list<Party>::const_iterator cbegin() const { return parties.cbegin(); }

	// Gets the end iterator for the pollster list.
	std::list<Party>::const_iterator cend() const { return parties.cend(); }

private:

	std::list<Party> parties;

	PollingProject& project;
};