#include "OutcomeCollection.h"

void OutcomeCollection::add(Outcome outcome)
{
	outcomes.push_front(outcome);
}

Outcome OutcomeCollection::get(int outcomeIndex) const
{
	auto it = outcomes.begin();
	std::advance(it, outcomeIndex);
	return *it;
}

int OutcomeCollection::count() const
{
	return outcomes.size();
}

std::list<Outcome>::iterator OutcomeCollection::begin()
{
	return outcomes.begin();
}

std::list<Outcome>::iterator OutcomeCollection::end()
{
	return outcomes.end();
}
