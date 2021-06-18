#pragma once

#include "Outcome.h"

#include <map>

class PollingProject;

class OutcomeCollection {
public:

	// Adds a result to the front of the results list
	void add(Outcome result);

	// Returns the result with index "resultIndex".
	Outcome get(int resultIndex) const;

	// Returns the number of results.
	int count() const;

	// Gets the begin iterator for the simulation list.
	std::list<Outcome>::iterator begin();

	// Gets the end iterator for the simulation list.
	std::list<Outcome>::iterator end();

	void clear();

	void logAll(SeatCollection const& seats) const;

private:

	// Live election results
	std::list<Outcome> outcomes;
};