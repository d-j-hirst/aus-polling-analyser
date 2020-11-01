#pragma once

#include "Simulation.h"

#include <map>

class PollingProject;
class ProjectionCollection;

class SimulationDoesntExistException : public std::runtime_error {
public:
	SimulationDoesntExistException() : std::runtime_error("") {}
};

class SimulationCollection {
public:
	// Collection is a map between ID values and simulations
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with simulation deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of simulations is in order they are added.
	typedef std::map<Simulation::Id, Simulation> SimulationContainer;

	// Simulation index refers to the position of the simulation in the order of currently existing simulations
	// Should not be stored persistently as removal of a simulation will change the indices
	// (use the SimulationKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	SimulationCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		SimulationDoesntExist,
	};

	// Adds the simulation "simulation".
	// Throws an exception if the number of simulations is over the limit, check this beforehand using canAdd();
	void add(Simulation simulation);

	// Replaces the simulation with index "simulationIndex" by "simulation".
	void replace(Simulation::Id id, Simulation simulation);

	// Checks if it is currently possible to add the given simulation
	// Returns Result::Ok if it's possible and Result::SimulationDoesntExist if that simulation doesn't exist
	Result canRemove(Simulation::Id id);

	// Removes the simulation with index "simulationIndex".
	void remove(Simulation::Id id);

	void run(Simulation::Id id, SimulationRun::FeedbackFunc feedback = [](std::string) {});

	// Returns access to the simulation with the given id
	Simulation& access(Simulation::Id id);

	// Returns the simulation with the given id
	Simulation const& view(Simulation::Id id) const;

	// Returns the simulation with index "simulationIndex".
	Simulation const& viewByIndex(Index simulationIndex) const { return view(indexToId(simulationIndex)); }

	Index idToIndex(Simulation::Id id) const;
	Simulation::Id indexToId(Index id) const;

	// Returns the number of simulations.
	int count() const;

	void startLoadingSimulation();

	void finaliseLoadedSimulation();

	void logAll(ProjectionCollection const& projections) const;

	// Gets the begin iterator for the pollster list.
	SimulationContainer::iterator begin() { return simulations.begin(); }
	SimulationContainer::const_iterator begin() const { return simulations.cbegin(); }

	// Gets the end iterator for the pollster list.
	SimulationContainer::iterator end() { return simulations.end(); }
	SimulationContainer::const_iterator end() const { return simulations.cend(); }

	// Gets the begin iterator for the pollster list.
	SimulationContainer::const_iterator cbegin() const { return simulations.cbegin(); }

	// Gets the end iterator for the pollster list.
	SimulationContainer::const_iterator cend() const { return simulations.cend(); }

	std::optional<Simulation::Settings> loadingSimulation;

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	SimulationContainer simulations;

	PollingProject& project;
};