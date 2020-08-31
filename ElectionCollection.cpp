#include "ElectionCollection.h"

#include "Log.h"

ElectionCollection::ElectionCollection(PollingProject& project)
	: project(project)
{
}

Results2::Election const& ElectionCollection::add(Results2::Election election) {
	return elections.insert_or_assign(election.id, election).first->second;
}

Results2::Election const& ElectionCollection::view(Results2::Election::Id id) const {
	return elections.at(id);
}

Results2::Election const& ElectionCollection::viewByIndex(int index) const
{
	return std::next(elections.begin(), index)->second;
}

int ElectionCollection::count() const {
	return elections.size();
}

void ElectionCollection::logAll() const
{
	logger << "Placeholder for logging the election collection\n";
}
