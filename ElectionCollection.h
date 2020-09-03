#pragma once

#include "ElectionData.h"

#include <map>
#include <stdexcept>

class PollingProject;
class Poll;

class ElectionLimitException : public std::runtime_error {
public:
	ElectionLimitException() : std::runtime_error("") {}
};

class RemoveMajorElectionException : public std::runtime_error {
public:
	RemoveMajorElectionException() : std::runtime_error("") {}
};

class ElectionDoesntExistException : public std::runtime_error {
public:
	ElectionDoesntExistException() : std::runtime_error("") {}
};

class ElectionCollection {
public:
	// Collection is a map between ID values and elections
	// Unlike other parts of the project, IDs here ARE preserved between sessions
	// since they are the IDs officially used by the electoral commission.
	// Map must be ordered to ensure order of elections is in order they are added.
	typedef std::map<Results2::Election::Id, Results2::Election> ElectionContainer;

	ElectionCollection(PollingProject& project);

	// Adds the election "election".
	// If an election with the same id already exists
	Results2::Election const& add(Results2::Election election);

	// Returns the election with index "electionIndex".
	Results2::Election const& view(Results2::Election::Id id) const;

	// Returns the election with index "electionIndex".
	Results2::Election const& viewByIndex(int index) const;

	// Returns the number of elections.
	int count() const;

	Results2::Election& back() { return std::prev(elections.end())->second; }

	// Gets the begin iterator for the pollster list.
	ElectionContainer::iterator begin() { return elections.begin(); }

	// Gets the end iterator for the pollster list.
	ElectionContainer::iterator end() { return elections.end(); }

	// Gets the begin iterator for the pollster list.
	ElectionContainer::const_iterator begin() const { return elections.begin(); }

	// Gets the end iterator for the pollster list.
	ElectionContainer::const_iterator end() const { return elections.end(); }

	// Gets the begin iterator for the pollster list.
	ElectionContainer::const_iterator cbegin() const { return elections.cbegin(); }

	// Gets the end iterator for the pollster list.
	ElectionContainer::const_iterator cend() const { return elections.cend(); }

	void logAll() const;

private:

	ElectionContainer elections;

	PollingProject& project;
};