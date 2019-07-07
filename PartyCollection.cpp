#include "PartyCollection.h"

#include "PollingProject.h"

void PartyCollection::add(Party party) {
	parties.push_back(party);
}

void PartyCollection::replace(int partyIndex, Party party) {
	*getPartyPtr(partyIndex) = party;
}

Party const& PartyCollection::view(int partyIndex) const {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return *it;
}

Party* PartyCollection::getPartyPtr(int partyIndex) {
	if (partyIndex < 0) return nullptr;
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return &*it;
}

Party const* PartyCollection::getPartyPtr(int partyIndex) const {
	if (partyIndex < 0) return nullptr;
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return &*it;
}

void PartyCollection::remove(int partyIndex) {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	parties.erase(it);
	project.adjustPollsAfterPartyRemoval(partyIndex);
}

int PartyCollection::count() const {
	return parties.size();
}

int PartyCollection::getPartyIndex(Party const* const partyPtr) {
	int i = 0;
	for (auto it = parties.begin(); it != parties.end(); it++) {
		if (&*it == partyPtr) return i;
		i++;
	}
	return -1;
}