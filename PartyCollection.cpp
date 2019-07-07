#include "PartyCollection.h"

#include "PollingProject.h"

void PartyCollection::addParty(Party party) {
	parties.push_back(party);
}

void PartyCollection::replaceParty(int partyIndex, Party party) {
	*getPartyPtr(partyIndex) = party;
}

Party PartyCollection::getParty(int partyIndex) const {
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

void PartyCollection::removeParty(int partyIndex) {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	parties.erase(it);
	project.adjustPollsAfterPartyRemoval(partyIndex);
}

int PartyCollection::getPartyCount() const {
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