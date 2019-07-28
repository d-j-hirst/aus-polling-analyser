#pragma once

#include <string>
#include <vector>
#include <list>
#include <fstream>
#include <unordered_map>

#include "ElectionData.h"
#include "FileOpeningState.h"
#include "NewProjectData.h"
#include "Points.h"

#include "PartyCollection.h"
#include "PollsterCollection.h"
#include "PollCollection.h"
#include "Event.h"
#include "Model.h"
#include "Projection.h"
#include "Region.h"
#include "Seat.h"
#include "Simulation.h"
#include "Result.h"

const int PA_MaxPollsters = 100;


class LatestResultsDataRetriever;
class PreloadDataRetriever;
class PreviousElectionDataRetriever;

// Parent class for the entire polling analysis project.
// Does not "know" about the UI at all.
class PollingProject {
public:

	// Initializes the polling project using the project data
	// selected on the New Project screen.
	PollingProject(NewProjectData& newProjectData);

	// Initializes the polling project by loading from a file.
	PollingProject(std::string pathName);

	void incorporatePreviousElectionResults(PreviousElectionDataRetriever const& dataRetriever);

	void incorporatePreloadData(PreloadDataRetriever const& dataRetriever);

	void incorporateLatestResults(LatestResultsDataRetriever const& dataRetriever);

	// Gets the name of the project.
	std::string getName() { return name; }

	// Gets the name of the project.
	std::string getLastFileName() { return lastFileName; }

	// Refreshes the calculated 2PPs for all polls.
	void refreshCalc2PP();

	PartyCollection& parties() { return partyCollection; }
	PartyCollection const& parties() const { return partyCollection; }

	// If a party is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	PollsterCollection& pollsters() { return pollsterCollection; }
	PollsterCollection const& pollsters() const { return pollsterCollection; }

	// If a pollster is removed, various parts of the project need to be adjusted to account for this.
	void adjustAfterPollsterRemoval(PollsterCollection::Index pollsterIndex, Party::Id pollsterId);

	PollCollection& polls() { return pollCollection; }
	PollCollection const& polls() const { return pollCollection; }

	// Returns the start day for the current visualiser view.
	int getVisStartDay() const;

	// Returns the end day for the current visualiser view.
	int getVisEndDay() const;

	// sets the start and end days for the current visualiser view.
	void setVisualiserBounds(int startDay, int endDay);

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

	// Gets the region index from a given pointer.
	int getSeatIndex(Seat const* seatPtr);

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

	// Adds a result to the front of the results list
	void addResult(Result result);

	// Returns the result with index "resultIndex".
	Result getResult(int resultIndex) const;

	// Returns the number of results.
	int getResultCount() const;

	// Gets the begin iterator for the simulation list.
	std::list<Result>::iterator getResultBegin();

	// Gets the end iterator for the simulation list.
	std::list<Result>::iterator getResultEnd();

	// Each seat has a pointer to the latest live result (if any)
	// This updates these pointers to point to the most recent results.
	void updateLatestResultsForSeats();

	// Gets the booth matching this official ID.
	Results::Booth const& getBooth(int boothId) const;

	Point2Df boothLatitudeRange() const;

	Point2Df boothLongitudeRange() const;

	// Returns the party that this candidate ID refers to.
	// Returns nullptr if candidate did not match any known party
	Party::Id getPartyByCandidate(int candidateId) const;

	// Returns the party that this affiliation ID refers to.
	// Returns nullptr if affiliation did not match any known party
	Party::Id getPartyByAffiliation(int affiliationId) const;

	// Gets data for the candidate this id refers to
	// Returns nullptr if the id does not match a known candidate
	Results::Candidate const* getCandidateById(int candidateId) const;

	// Gets data for the candidate this id refers to
	// Returns nullptr if the id does not match a known candidate
	Results::Affiliation const* getAffiliationById(int affiliationId) const;

	// Gets the affiliation Id for the given candidate (-1 if candidate not found).
	int getCandidateAffiliationId(int candidateId) const;

	// Save this project to the given filename.
	// Returns 0 if successful, and 1 if saving failed.
	int save(std::string filename);

	// Returns whether the project is valid (after opening from a file).
	// If this is false then the project should be closed by
	// calling reset on the smart pointer.
	bool isValid();

	// Invalidates all the projections from a particular model. Used when editing a model.
	void invalidateProjectionsFromModel(Model const* model);

private:

	// Opens the project saved at the given filename.
	// Returns 0 if successful, and 1 if opening failed.
	void open(std::string filename);

	// Opens the project saved at the given filename.
	// Returns false if the end of the file is reached (marked by "#End").
	bool processFileLine(std::string line, FileOpeningState& fos);

	// Removes all the projections from a particular model. Used when deleting a model.
	void removeProjectionsFromModel(Model const* model);

	// If a party is removed, various parts of the project need to be adjusted to deal with this
	void adjustSeatsAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);
	void adjustCandidatesAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);
	void adjustAffiliationsAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	// Makes adjustments after a file has been loaded.
	void finalizeFileLoading();

	// Creates the map between what affiliation numbers and the parties in the project that those
	// affiliation numbers correspond to.
	void collectAffiliations(PreviousElectionDataRetriever const& dataRetriever);

	// Creates the map between candidates and parties that they belong to.
	void collectCandidatesFromPreload(PreloadDataRetriever const& dataRetriever);

	// Adds booth information from preload data to the project. Necessary because some booths may not have
	// existed prior to this election.
	void collectBoothsFromPreload(PreloadDataRetriever const& dataRetriever);

	// Assuming there is live results data added for this seat, calculates the swing to the incumbent here.
	float calculateSwingToIncumbent(Seat const& seat);

	// Assuming there is live results data added for this seat, calculates the % completion of the two-party preferred vote.
	float calculate2cpPercentComplete(Seat const& seat);

	// Assuming there is live results data added for this seat, calculates the % completion of the two-party preferred vote.
	float calculateFpPercentComplete(Seat const& seat);

	// The name of the project.
	std::string name;

	// The last file name the project was saved under.
	// Defaults to "name.pol" if the project has not yet been saved at all.
	std::string lastFileName;

	PartyCollection partyCollection;
	PollsterCollection pollsterCollection;
	PollCollection pollCollection;

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

	// Live election results
	std::list<Result> results;

	// Booth data from a download
	std::unordered_map<int, Results::Booth> booths;

	typedef std::unordered_map<int, Party::Id> IdPartyMap;
	typedef std::unordered_map<int, int> CandidateAffiliationMap;
	typedef std::unordered_map<int, Results::Candidate> CandidateMap;
	typedef std::unordered_map<int, Results::Affiliation> AffiliationMap;
	IdPartyMap affiliationParties;
	IdPartyMap candidateParties;
	CandidateAffiliationMap candidateAffiliations;
	CandidateMap candidates;
	AffiliationMap affiliations;

	static const Party invalidParty;

	bool valid = false;

	// indicates the start day currently shown in the visualiser.
	int visStartDay = -1000000;

	// indicates the end day currently shown in the visualiser.
	int visEndDay = 1000000;
};