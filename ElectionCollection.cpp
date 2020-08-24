#include "ElectionCollection.h"

ElectionCollection::ElectionCollection(PollingProject& project)
	: project(project)
{
}

Results2::Election const& ElectionCollection::add(Results2::Election election) {
	return elections.insert({ election.id, election }).first->second;
}

Results2::Election const& ElectionCollection::view(Results2::Election::Id id) const {
	return elections.at(id);
}

int ElectionCollection::count() const {
	return elections.size();
}