#pragma once

#include <string>
#include <vector>
#include <list>
#include <fstream>
#include <optional>
#include <unordered_map>

#include "Config.h"
#include "ElectionData.h"
#include "FileOpeningState.h"
#include "NewProjectData.h"
#include "Points.h"

#include "ElectionCollection.h"
#include "ModelCollection.h"
#include "OutcomeCollection.h"
#include "PartyCollection.h"
#include "PollCollection.h"
#include "PollsterCollection.h"
#include "ProjectionCollection.h"
#include "RegionCollection.h"
#include "ResultCoordinator.h"
#include "SeatCollection.h"
#include "SimulationCollection.h"
#include "Projection.h"
#include "Region.h"
#include "Seat.h"
#include "Simulation.h"
#include "Outcome.h"

const int PA_MaxPollsters = 100;

class LatestResultsDataRetriever;
class PreloadDataRetriever;
class PreviousElectionDataRetriever;

// Parent class for the entire polling analysis project.
// Does not "know" about the UI at all.
class PollingProject {
public:
	friend class ProjectFiler;

	// Initializes the polling project using the project data
	// selected on the New Project screen.
	PollingProject(NewProjectData& newProjectData);

	// Initializes the polling project by loading from a file.
	PollingProject(std::string pathName);

	// Gets the name of the project.
	std::string getName() { return name; }

	// Gets the file name that the project was last saved under.
	std::string getLastFileName() { return lastFileName; }

	// Gets the full text string for the last macro to be run.
	std::string getLastMacro() { return lastMacro; }

	std::string getElectionName() const { return electionName; }

	void setElectionName(std::string newName) { electionName = newName; }

	// Runs the given macro. Returns true if the macro verified successfully, 
	std::optional<std::string> runMacro(std::string macro);

	// Update the macro to the given value without running it.
	void updateMacro(std::string macro);

	// Refreshes the calculated 2PPs for all polls.
	void refreshCalc2PP();

	Config const& config() const { return configObj; }

	PartyCollection& parties() { return partyCollection; }
	PartyCollection const& parties() const { return partyCollection; }

	// If a party is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	PollsterCollection& pollsters() { return pollsterCollection; }
	PollsterCollection const& pollsters() const { return pollsterCollection; }

	// If a pollster is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterPollsterRemoval(PollsterCollection::Index pollsterIndex, Pollster::Id pollsterId);

	PollCollection& polls() { return pollCollection; }
	PollCollection const& polls() const { return pollCollection; }

	// gets the date in MJD form of the earliest thing recorded in the file
	int getEarliestDate() const;

	// gets the date in MJD form of the latest thing recorded in the file
	int getLatestDate() const;

	ModelCollection& models() { return modelCollection; }
	ModelCollection const& models() const { return modelCollection; }

	// If a model is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterModelRemoval(ModelCollection::Index modelIndex, StanModel::Id modelId);

	ProjectionCollection& projections() { return projectionCollection; }
	ProjectionCollection const& projections() const { return projectionCollection; }

	// If a projection is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterProjectionRemoval(ProjectionCollection::Index projectionIndex, Projection::Id projectionId);

	RegionCollection& regions() { return regionCollection; }
	RegionCollection const& regions() const { return regionCollection; }

	// If a region is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterRegionRemoval(RegionCollection::Index regionIndex, Region::Id regionId);

	SeatCollection& seats() { return seatCollection; }
	SeatCollection const& seats() const { return seatCollection; }

	SimulationCollection& simulations() { return simulationCollection; }
	SimulationCollection const& simulations() const { return simulationCollection; }

	ResultCoordinator& results() { return resultCoordinator; }
	ResultCoordinator const& results() const { return resultCoordinator; }

	OutcomeCollection& outcomes() { return outcomeCollection; }
	OutcomeCollection const& outcomes() const { return outcomeCollection; }

	ElectionCollection& elections() { return electionCollection; }
	ElectionCollection const& elections() const { return electionCollection; }

	// Save this project to the given filename.
	void save(std::string filename);

	// Returns whether the project is valid (after opening from a file).
	// If this is false then the project should be closed by
	// calling reset on the smart pointer.
	bool isValid();

	// Invalidates all the projections from a particular model. Used when editing a model.
	void invalidateProjectionsFromModel(StanModel::Id modelId);

private:

	// Basic constructor that handles everything required by the project regardless of whether
	// it is a new or existing project.
	PollingProject();

	// Opens the project saved at the given filename.
	// Returns 0 if successful, and 1 if opening failed.
	void open(std::string filename);

	// Removes all the projections from a particular model. Used when deleting a model.
	void removeProjectionsFromModel(StanModel::Id modelId);

	// Removes all the simulations from a particular projection. Used when deleting a projection.
	void removeSimulationsFromProjection(Projection::Id projectionId);

	// If a party is removed, various parts of the project need to be adjusted to deal with this
	void adjustSeatsAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	// If a region is removed, seats pointing to it need to have their region reset to the default
	void adjustSeatsAfterRegionRemoval(RegionCollection::Index regionIndex, Party::Id regionId);

	// Makes adjustments after a file has been loaded.
	void finalizeFileLoading();

	// The name of the project.
	std::string name;

	// Name of the election ("e.g. 2022 Federal Election")
	std::string electionName;

	// The last file name the project was saved under.
	// Defaults to "name.pol" if the project has not yet been saved at all.
	std::string lastFileName;
	
	// Stores full text of the last macro to be run.
	std::string lastMacro;

	Config configObj;

	PartyCollection partyCollection;
	PollsterCollection pollsterCollection;
	PollCollection pollCollection;
	ModelCollection modelCollection;
	ProjectionCollection projectionCollection;
	RegionCollection regionCollection;
	SeatCollection seatCollection;
	SimulationCollection simulationCollection;
	ResultCoordinator resultCoordinator;
	OutcomeCollection outcomeCollection;
	ElectionCollection electionCollection;

	static const Party invalidParty;

	bool valid = false;
};