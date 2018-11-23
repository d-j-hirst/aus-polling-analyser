#pragma once

#include <string>
#include <vector>
#include <list>
#include <fstream>
#include "NewProjectData.h"
#include "Party.h"
#include "Pollster.h"
#include "Poll.h"
#include "Event.h"
#include "Model.h"
#include "Projection.h"
#include "Region.h"
#include "Seat.h"
#include "Simulation.h"
#include "Debug.h"
#include "FileOpeningState.h"

const int PA_MaxPollsters = 100;

// Parent class for the entire polling analysis project.
// Does not "know" about the UI at all.
class PollingProject {
public:

	// Initializes the polling project using the project data
	// selected on the New Project screen.
	PollingProject(NewProjectData& newProjectData);

	// Initializes the polling project by loading from a file.
	PollingProject(std::string pathName);

	// Gets the name of the project.
	std::string getName() { return name; }

	// Gets the name of the project.
	std::string getLastFileName() { return lastFileName; }

	void setOthersPreferenceFlow(float in_othersPreferenceFlow) { othersPreferenceFlow = in_othersPreferenceFlow; }
	float getOthersPreferenceFlow() { return othersPreferenceFlow; }

	void setOthersExhaustRate(float in_othersExhaustRate) { othersExhaustRate = in_othersExhaustRate; }
	float getOthersExhaustRate() { return othersExhaustRate; }

	// Refreshes the calculated 2PPs for all polls.
	void refreshCalc2PP();

	// Adds the party "party".
	void addParty(Party party);

	// Replaces the party with index "partyIndex" by "party".
	void replaceParty(int partyIndex, Party party);

	// Removes the party with index "partyIndex".
	void removeParty(int partyIndex);

	// Returns the party with index "partyIndex".
	Party getParty(int partyIndex) const;

	// Returns a pointer to the party with index "partyIndex".
	Party* getPartyPtr(int partyIndex);

	// Returns a pointer to the party with index "partyIndex".
	Party const* getPartyPtr(int partyIndex) const;

	// Returns the number of parties.
	int getPartyCount() const;

	// Gets the party index from a given pointer.
	int getPartyIndex(Party const* partyPtr);

	// Gets the begin iterator for the pollster list.
	std::list<Party>::const_iterator getPartyBegin() const;

	// Gets the end iterator for the pollster list.
	std::list<Party>::const_iterator getPartyEnd() const;

	// Adds the pollster "pollster".
	void addPollster(Pollster pollster);

	// Replaces the pollster with index "pollsterIndex" by "pollster".
	void replacePollster(int pollsterIndex, Pollster pollster);

	// Removes the pollster with index "pollsterIndex".
	void removePollster(int pollsterIndex);

	// Returns the pollster with index "pollsterIndex".
	Pollster getPollster(int pollsterIndex) const;

	// Returns a pointer to the pollster with index "pollsterIndex".
	Pollster const* getPollsterPtr(int pollsterIndex) const;

	// Gets the pollster index from a given pointer.
	int getPollsterIndex(Pollster const* pollsterPtr);

	// Returns the number of pollsters.
	int getPollsterCount() const;

	// Gets the begin iterator for the pollster list.
	std::list<Pollster>::const_iterator getPollsterBegin() const;

	// Gets the end iterator for the pollster list.
	std::list<Pollster>::const_iterator getPollsterEnd() const;

	// Adds the poll "poll".
	void addPoll(Poll poll);

	// Replaces the poll with index "pollIndex" by "poll".
	void replacePoll(int pollIndex, Poll poll);

	// Removes the poll with index "pollIndex".
	void removePoll(int pollIndex);

	// Returns the poll with index "pollIndex".
	Poll getPoll(int pollIndex) const;

	// Returns a pointer to the poll with index "pollIndex".
	Poll const* getPollPtr(int pollIndex) const;

	// Returns the number of polls.
	int getPollCount() const;

	// Returns the start day for the current visualiser view.
	int getVisStartDay() const;

	// Returns the end day for the current visualiser view.
	int getVisEndDay() const;

	// sets the start and end days for the current visualiser view.
	void setVisualiserBounds(int startDay, int endDay);

	// gets the date in MJD form of the earliest poll
	int getEarliestPollDate() const;

	// gets the date in MJD form of the most recent poll
	int getLatestPollDate() const;

	// gets the date in MJD form of the earliest thing recorded in the file
	int getEarliestDate() const;

	// gets the date in MJD form of the latest thing recorded in the file
	int getLatestDate() const;

	// converts an MJD date to a wxDateTime date.
	wxDateTime MjdToDate(int mjd) const;

	// Adds the event "event".
	void addEvent(Event event);

	// Replaces the event with index "eventIndex" by "event".
	void replaceEvent(int eventIndex, Event event);

	// Removes the event with index "eventIndex".
	void removeEvent(int eventIndex);

	// Returns the event with index "eventIndex".
	Event getEvent(int eventIndex) const;

	// Returns a pointer to the event with index "eventIndex".
	Event* getEventPtr(int eventIndex);

	// Returns the number of events.
	int getEventCount() const;

	// Adds the model "model".
	void addModel(Model model);

	// Replaces the model with index "modelIndex" by "model".
	void replaceModel(int modelIndex, Model model);

	// Removes the model with index "modelIndex".
	void removeModel(int modelIndex);

	// Extends the model with index "modelIndex" to the latest poll
	void extendModel(int modelIndex);

	// Returns the model with index "modelIndex".
	Model getModel(int modelIndex) const;

	// Returns a pointer to the model with index "modelIndex".
	Model const* getModelPtr(int modelIndex) const;

	// Returns a pointer to the model with index "modelIndex".
	Model* getModelPtr(int modelIndex);

	// Returns the number of models.
	int getModelCount() const;

	// Gets the model index from a given pointer.
	int getModelIndex(Model const* modelPtr);

	// generates a basic model with the standard start and end dates.
	Model generateBaseModel() const;

	// Gets the begin iterator for the model list.
	std::list<Model>::const_iterator getModelBegin() const;

	// Gets the end iterator for the model list.
	std::list<Model>::const_iterator getModelEnd() const;

	// Adds the projection "projection".
	void addProjection(Projection projection);

	// Replaces the projection with index "projectionIndex" by "projection".
	void replaceProjection(int projectionIndex, Projection projection);

	// Removes the projection with index "projectionIndex".
	void removeProjection(int projectionIndex);

	// Returns the projection with index "projectionIndex".
	Projection getProjection(int projectionIndex) const;

	// Returns a pointer to the projection with index "projectionIndex".
	Projection* getProjectionPtr(int projectionIndex);

	// Returns a pointer to the projection with index "projectionIndex".
	Projection const* getProjectionPtr(int projectionIndex) const;

	// Returns the number of projections.
	int getProjectionCount() const;

	// Gets the projection index from a given pointer.
	int getProjectionIndex(Projection const* projectionPtr);

	// Gets the begin iterator for the projection list.
	std::list<Projection>::const_iterator getProjectionBegin() const;

	// Gets the end iterator for the projection list.
	std::list<Projection>::const_iterator getProjectionEnd() const;

	// Adds the region "region".
	void addRegion(Region region);

	// Replaces the region with index "regionIndex" by "region".
	void replaceRegion(int regionIndex, Region region);

	// Removes the region with index "regionIndex".
	void removeRegion(int regionIndex);

	// Returns the region with index "regionIndex".
	Region getRegion(int regionIndex) const;

	// Returns a pointer to the region with index "regionIndex".
	Region* getRegionPtr(int regionIndex);

	// Returns a pointer to the region with index "regionIndex".
	Region const* getRegionPtr(int regionIndex) const;

	// Returns the number of regions.
	int getRegionCount() const;

	// Gets the region index from a given pointer.
	int getRegionIndex(Region const* regionPtr);

	// Calculates the regional swing deviations from the national swings.
	void calculateRegionSwingDeviations();

	// Gets the begin iterator for the region list.
	std::list<Region>::iterator getRegionBegin();

	// Gets the end iterator for the region list.
	std::list<Region>::iterator getRegionEnd();

	// Gets the begin iterator for the region list.
	std::list<Region>::const_iterator getRegionBegin() const;

	// Gets the end iterator for the region list.
	std::list<Region>::const_iterator getRegionEnd() const;

	// Adds the seat "seat".
	void addSeat(Seat seat);

	// Replaces the seat with index "seatIndex" by "seat".
	void replaceSeat(int seatIndex, Seat seat);

	// Removes the seat with index "seatIndex".
	void removeSeat(int seatIndex);

	// Returns the seat with index "seatIndex".
	Seat getSeat(int seatIndex) const;

	// Returns a pointer to the seat with index "seatIndex".
	Seat* getSeatPtr(int seatIndex);

	// Returns the number of seats.
	int getSeatCount() const;

	// Gets the begin iterator for the seat list.
	std::list<Seat>::iterator getSeatBegin();

	// Gets the end iterator for the seat list.
	std::list<Seat>::iterator getSeatEnd();

	// Gets a pointer to the first seat found with this name. Returns null if no seat matches.
	Seat* getSeatPtrByName(std::string name);

	// Adds the simulation "simulation".
	void addSimulation(Simulation simulation);

	// Replaces the simulation with index "simulationIndex" by "simulation".
	void replaceSimulation(int simulationIndex, Simulation simulation);

	// Removes the simulation with index "simulationIndex".
	void removeSimulation(int simulationIndex);

	// Returns the simulation with index "simulationIndex".
	Simulation getSimulation(int simulationIndex) const;

	// Returns a pointer to the simulation with index "simulationIndex".
	Simulation* getSimulationPtr(int simulationIndex);

	// Returns a pointer to the simulation with index "simulationIndex".
	Simulation const* getSimulationPtr(int simulationIndex) const;

	// Returns the number of simulations.
	int getSimulationCount() const;

	// Gets the begin iterator for the simulation list.
	std::list<Simulation>::const_iterator getSimulationBegin() const;

	// Gets the end iterator for the simulation list.
	std::list<Simulation>::const_iterator getSimulationEnd() const;

	// Save this project to the given filename.
	// Returns 0 if successful, and 1 if saving failed.
	int save(std::string filename);

	// Returns whether the project is valid (after opening from a file).
	// If this is false then the project should be closed by
	// calling reset on the smart pointer.
	bool isValid();

	// recalculates the poll's estimated two-party-preferred based on primary votes.
	// This function will directly edit the poll's data, but does not affect the
	// project's state directly.
	void recalculatePollCalc2PP(Poll& poll) const;

	// Invalidates all the projections from a particular model. Used when editing a model.
	void invalidateProjectionsFromModel(Model const* model);

private:

	// Opens the project saved at the given filename.
	// Returns 0 if successful, and 1 if opening failed.
	void open(std::string filename);

	// Opens the project saved at the given filename.
	// Returns false if the end of the file is reached (marked by "#End").
	bool processFileLine(std::string line, FileOpeningState& fos);

	// If a party is removed, all the polls need to be adjusted to account for this.
	void adjustPollsAfterPartyRemoval(int partyIndex);

	// Removes all the polls from a particular pollster. Used when deleting a pollster.
	void removePollsFromPollster(Pollster const* pollster);

	// Removes all the projections from a particular model. Used when deleting a model.
	void removeProjectionsFromModel(Model const* model);

	// Makes adjustments after a file has been loaded.
	void finalizeFileLoading();

	// The name of the project.
	std::string name;

	// The last file name the project was saved under.
	// Defaults to "name.pol" if the project has not yet been saved at all.
	std::string lastFileName;

	// Vector containing the data for political parties.
	std::list<Party> parties;

	// List containing the data for pollsters.
	// Polls have pointers to individual pollsters,
	// so a list is used to avoid dangling pointers.
	std::list<Pollster> pollsters;

	// Vector containing the data for polls.
	std::vector<Poll> polls;

	// Vector containing the data for events.
	std::vector<Event> events;

	// Vector containing the data for models.
	std::list<Model> models;

	// Vector containing the data for projections.
	std::list<Projection> projections;

	// Vector containing the data for regions.
	std::list<Region> regions;

	// Vector containing the data for seats.
	std::list<Seat> seats;

	// Vector containing the data for simulations.
	std::list<Simulation> simulations;

	bool valid = false;

	// indicates the last-election preference flow from "Others" to the first listed major party.
	float othersPreferenceFlow = 46.5f;

	// indicates the last-election preference flow from "Others" to the first listed major party.
	float othersExhaustRate = 0.0f;

	// indicates the start day currently shown in the visualiser.
	int visStartDay = -1000000;

	// indicates the end day currently shown in the visualiser.
	int visEndDay = 1000000;
};