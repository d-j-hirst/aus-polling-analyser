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
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with election deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of elections is in order they are added.
	typedef std::map<Results2::Election::Id, Results2::Election> ElectionContainer;

	ElectionCollection(PollingProject& project);

	enum class Result {
		Ok,
		TooManyElections,
		CantRemoveMajorElection,
		ElectionDoesntExist,
	};

	// Adds the election "election".
	// If an election with the same id already exists
	Results2::Election const& add(Results2::Election election);

	// Removes the election with index "electionIndex".
	void remove(Results2::Election::Id id);

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
	ElectionContainer::const_iterator cbegin() const { return elections.cbegin(); }

	// Gets the end iterator for the pollster list.
	ElectionContainer::const_iterator cend() const { return elections.cend(); }

	void logAll() const;

private:

	ElectionContainer elections;

	PollingProject& project;
};