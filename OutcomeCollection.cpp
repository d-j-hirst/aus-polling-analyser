#include "OutcomeCollection.h"

#include "Log.h"

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

void OutcomeCollection::clear()
{
	outcomes.clear();
}

void OutcomeCollection::logAll(SeatCollection const& seats) const
{
	for (auto const& thisOutcome : outcomes) {
		logger << thisOutcome.textReport(seats);
	}
}
