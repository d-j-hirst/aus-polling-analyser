#include "PollingProject.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"

#include <iomanip>
#include <algorithm>

const Party PollingProject::invalidParty = Party("Invalid", 50.0f, 0.0f, "INV", Party::CountAsParty::None);

PollingProject::PollingProject(NewProjectData& newProjectData) :
		name(newProjectData.projectName),
		lastFileName(newProjectData.projectName + ".pol"),
		valid(true),
		partyCollection(*this)
{
	// The project must always have at least two partyCollection, no matter what. This initializes them with default values.
	partyCollection.add(Party("Labor", 100, 0.0f, "ALP", Party::CountAsParty::IsPartyOne));
	partyCollection.add(Party("Liberals", 0, 0.0f, "LIB", Party::CountAsParty::IsPartyTwo));

	addPollster(Pollster("Default Pollster", 1.0f, 0, true, false));
}

PollingProject::PollingProject(std::string pathName) :
		lastFileName(pathName.substr(pathName.rfind("\\")+1)),
		partyCollection(*this)
{
	logger << lastFileName << "\n";
	open(pathName);
}

void PollingProject::incorporatePreviousElectionResults(PreviousElectionDataRetriever const& dataRetriever)
{
	int seatMatchCount = 0;
	for (auto seatIt = dataRetriever.beginSeats(); seatIt != dataRetriever.endSeats(); ++seatIt) {
		auto seatData = seatIt->second;
		auto matchedSeat = std::find_if(seats.begin(), seats.end(), 
			[seatData](Seat const& seat) { return seat.name == seatData.name || seat.previousName ==seatData.name; });
		if (matchedSeat != seats.end()) {
			matchedSeat->officialId = seatData.officialId;
			matchedSeat->previousResults = seatData;
			++seatMatchCount;
		}
		else {
			logger << "Note - No seat match found for " << seatData.name << ".\n";
			logger << "If this seat was abolished then this is ok, otherwise check the spelling of the existing seat data.\n";
			logger << "Also check that the existing seat data download link is correct and hasn't been set to skip the download.\n";
		}
	}
	logger << seatMatchCount << " seats matched.\n";
	std::copy(dataRetriever.beginBooths(), dataRetriever.endBooths(), std::inserter(booths, booths.end()));
	std::copy(dataRetriever.beginCandidates(), dataRetriever.endCandidates(), std::inserter(candidates, candidates.end()));
	collectAffiliations(dataRetriever);

	candidateParties.insert({ -1, Party::InvalidId });
	for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
		candidates.insert(*candidateIt);
		int affiliationId = candidateIt->second.affiliationId;
		auto affiliationIt = affiliationParties.find(affiliationId);
		if (affiliationIt != affiliationParties.end()) {
			candidateParties.insert({ candidateIt->first, affiliationIt->second });
			candidateAffiliations.insert({ candidateIt->first, affiliationId });
		}
		else {
			// treat unknown party as independent
			candidateParties.insert({ candidateIt->first, affiliationParties[0] });
			candidateAffiliations.insert({ candidateIt->first, -1 });
		}
	}
}

void PollingProject::incorporatePreloadData(PreloadDataRetriever const& dataRetriever)
{
	collectCandidatesFromPreload(dataRetriever);
	collectBoothsFromPreload(dataRetriever);
}

void PollingProject::incorporateLatestResults(LatestResultsDataRetriever const& dataRetriever)
{
	for (auto booth = dataRetriever.beginBooths(); booth != dataRetriever.endBooths(); ++booth) {
		auto const& newBooth = booth->second;

		// Determine which booth (if any) from the previous election this corresponds to
		auto oldBoothIt = booths.find(newBooth.officialId);
		if (oldBoothIt == booths.end()) {
			continue;
		}
		auto& matchedBooth = oldBoothIt->second;
		matchedBooth.fpCandidates = newBooth.fpCandidates; // always record fp candidates regardless of whether booth matching is successful

		// Check if the partyCollection match
		bool allValid = true;
		Party::Id newParty[2] = { candidateParties[newBooth.tcpCandidateId[0]], candidateParties[newBooth.tcpCandidateId[1]] };
		Party::Id oldParty[2] = { affiliationParties[matchedBooth.tcpAffiliationId[0]], affiliationParties[matchedBooth.tcpAffiliationId[1]] };
		for (auto& a : newParty) if (a == Party::InvalidId) { allValid = false; };
		for (auto& a : oldParty) if (a == Party::InvalidId) { allValid = false; };
		bool matchedDirect = newParty[0] == oldParty[0] && (newParty[1] == oldParty[1]) && allValid;
		bool matchedOpposite = newParty[0] == oldParty[1] && (newParty[1] == oldParty[0]) && allValid;
		bool noOldResults = !matchedBooth.hasOldResults(); // no old results, therefore don't need to match for swing purposes, just get the results in whatever order
		bool newResults = newBooth.totalNewTcpVotes();

		if (matchedDirect || matchedOpposite || noOldResults) {
			if (matchedDirect) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
			else if (matchedOpposite) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[1];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[0];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[1];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[0];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
			else if (noOldResults) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
				matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
				matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
		}
		else if (newResults) {
			// Could not match partyCollection are there are some results, wipe previous results
			matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
			matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
			matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
			matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
			matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
			matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
			matchedBooth.tcpVote[0] = 0;
			matchedBooth.tcpVote[1] = 0;
			matchedBooth.newResultsZero = newBooth.newResultsZero;
		}
	}

	for (auto seat = dataRetriever.beginSeats(); seat != dataRetriever.endSeats(); ++seat) {
		auto matchingSeat = std::find_if(seats.begin(), seats.end(), [&](Seat thisSeat)
		{return thisSeat.name == seat->second.name; });
		matchingSeat->latestResults = seat->second;
		for (auto& candidate : matchingSeat->latestResults->finalCandidates) {
			candidate.affiliationId = candidateAffiliations[candidate.candidateId];
		}
		for (auto& candidate : matchingSeat->latestResults->fpCandidates) {
			candidate.affiliationId = candidateAffiliations[candidate.candidateId];
		}
		// *** need something here to check if two-candidate preferred is not recorded because of seat maverick status
		if (matchingSeat->latestResults->total2cpVotes() && matchingSeat->latestResults->classic2pp) {
			Party::Id partyOne = candidateParties[matchingSeat->latestResults->finalCandidates[0].candidateId];
			Party::Id partyTwo = candidateParties[matchingSeat->latestResults->finalCandidates[1].candidateId];
			if (!parties().oppositeMajors(partyOne, partyTwo)) matchingSeat->latestResults->classic2pp = false;
		}
		if (matchingSeat->previousResults.has_value() && matchingSeat->previousResults->total2cpVotes() && matchingSeat->previousResults->classic2pp) {
			Party::Id partyOne = affiliationParties[matchingSeat->previousResults->finalCandidates[0].affiliationId];
			Party::Id partyTwo = affiliationParties[matchingSeat->previousResults->finalCandidates[1].affiliationId];
			if (!parties().oppositeMajors(partyOne, partyTwo)) matchingSeat->previousResults->classic2pp = false;
		}
	}

	// Code below stores information

	updateLatestResultsForSeats(); // only overwrite different results
	wxDateTime dateTime = wxDateTime::Now();
	for (auto& seat : seats) {
		float percentCounted2cp = calculate2cpPercentComplete(seat);
		if (!percentCounted2cp) {
			if (seat.isClassic2pp(true)) continue;
			float percentCountedFp = calculateFpPercentComplete(seat);
			if (!percentCountedFp) continue;
			Result thisResult;
			thisResult.seat = &seat;
			thisResult.percentCounted = percentCountedFp;
			thisResult.updateTime = dateTime;
			if (!seat.latestResult || seat.latestResult->percentCounted != percentCountedFp) {
				addResult(thisResult);
			}
			continue;
		}
		float incumbentSwing = calculateSwingToIncumbent(seat);
		Result thisResult;
		thisResult.seat = &seat;
		thisResult.incumbentSwing = incumbentSwing;
		thisResult.percentCounted = percentCounted2cp;
		thisResult.updateTime = dateTime;
		if (!seat.latestResult || seat.latestResult->percentCounted != percentCounted2cp) {
			addResult(thisResult);
		}
	}
	updateLatestResultsForSeats(); // only overwrite different results
}

void PollingProject::refreshCalc2PP() {
	for (auto it = polls.begin(); it != polls.end(); it++)
		recalculatePollCalc2PP(*it);
}

void PollingProject::adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId)
{
	adjustPollsAfterPartyRemoval(partyIndex, partyId);
	adjustSeatsAfterPartyRemoval(partyIndex, partyId);
	adjustAffiliationsAfterPartyRemoval(partyIndex, partyId);
	adjustCandidatesAfterPartyRemoval(partyIndex, partyId);
}

void PollingProject::addPollster(Pollster pollster) {
	pollsters.push_back(pollster);
}

void PollingProject::replacePollster(int pollsterIndex, Pollster pollster) {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	*it = pollster;
}

Pollster PollingProject::getPollster(int pollsterIndex) const {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	return *it;
}

Pollster const* PollingProject::getPollsterPtr(int pollsterIndex) const {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	return &*it;
}

int PollingProject::getPollsterIndex(Pollster const* const pollsterPtr) {
	int i = 0;
	for (auto it = pollsters.begin(); it != pollsters.end(); it++) {
		if (&*it == pollsterPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::removePollster(int pollsterIndex) {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	removePollsFromPollster(std::addressof(*it));
	pollsters.erase(it);
}

int PollingProject::getPollsterCount() const {
	return pollsters.size();
}

std::list<Pollster>::const_iterator PollingProject::getPollsterBegin() const {
	return pollsters.begin();
}

std::list<Pollster>::const_iterator PollingProject::getPollsterEnd() const {
	return pollsters.end();
}

void PollingProject::addPoll(Poll poll) {
	polls.push_back(poll);
}

void PollingProject::replacePoll(int pollIndex, Poll poll) {
	polls[pollIndex] = poll;
}

Poll PollingProject::getPoll(int pollIndex) const {
	return polls[pollIndex];
}

Poll const* PollingProject::getPollPtr(int pollIndex) const {
	return &polls[pollIndex];
}

void PollingProject::removePoll(int pollIndex) {
	auto it = polls.begin();
	for (int i = 0; i < pollIndex; i++) it++;
	polls.erase(it);
}

int PollingProject::getPollCount() const {
	return polls.size();
}

int PollingProject::getVisStartDay() const {
	return visStartDay;
}

int PollingProject::getVisEndDay() const {
	return visEndDay;
}

void PollingProject::setVisualiserBounds(int startDay, int endDay) {
	startDay = std::max(getEarliestDate(), startDay);
	endDay = std::min(getLatestDate(), endDay);
	visStartDay = startDay; visEndDay = endDay;
}

int PollingProject::getEarliestPollDate() const {
	if (!getPollCount()) return -100000000;
	int earliestDay = 1000000000;
	for (int i = 0; i < getPollCount(); ++i) {
		int date = int(floor(getPoll(i).date.GetModifiedJulianDayNumber()));
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollingProject::getLatestPollDate() const {
	if (!getPollCount()) return -100000000;
	int latestDay = -1000000000;
	for (int i = 0; i < getPollCount(); ++i) {
		int date = int(floor(getPoll(i).date.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

int PollingProject::getEarliestDate() const {
	int earliestDay = getEarliestPollDate();
	for (int i = 0; i < getModelCount(); ++i) {
		int date = int(floor(getModel(i).endDate.GetModifiedJulianDayNumber()));
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollingProject::getLatestDate() const {
	int latestDay = getLatestPollDate();
	for (int i = 0; i < getModelCount(); ++i) {
		int date = int(floor(getModel(i).endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	for (int i = 0; i < getProjectionCount(); ++i) {
		int date = int(floor(getProjection(i).endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

wxDateTime PollingProject::MjdToDate(int mjd) const {
	if (mjd <= -1000000) return wxInvalidDateTime;
	wxDateTime tempDate = wxDateTime(double(mjd) + 2400000.5);
	tempDate.SetHour(18);
	return tempDate;
}

// generates a basic model with the standard start and end dates.
Model PollingProject::generateBaseModel() const {
	Model tempModel = Model();
	tempModel.startDate = MjdToDate(getEarliestPollDate());
	tempModel.endDate = MjdToDate(getLatestPollDate());
	return tempModel;
}

void PollingProject::addEvent(Event event) {
	events.push_back(event);
}

void PollingProject::replaceEvent(int eventIndex, Event event) {
	events[eventIndex] = event;
}

Event PollingProject::getEvent(int eventIndex) const {
	return events[eventIndex];
}

Event* PollingProject::getEventPtr(int eventIndex) {
	return &events[eventIndex];
}

void PollingProject::removeEvent(int eventIndex) {
	auto it = events.begin();
	for (int i = 0; i < eventIndex; i++) it++;
	events.erase(it);
}

int PollingProject::getEventCount() const {
	return events.size();
}

void PollingProject::addModel(Model model) {
	models.push_back(model);
}

void PollingProject::replaceModel(int modelIndex, Model model) {
	invalidateProjectionsFromModel(getModelPtr(modelIndex));
	*getModelPtr(modelIndex) = model;
}

Model PollingProject::getModel(int modelIndex) const {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return *it;
}

Model const* PollingProject::getModelPtr(int modelIndex) const {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return &*it;
}

Model* PollingProject::getModelPtr(int modelIndex) {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return &*it;
}

void PollingProject::removeModel(int modelIndex) {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	removeProjectionsFromModel(std::addressof(*it));
	models.erase(it);
}

void PollingProject::extendModel(int modelIndex) {
	int latestMjd = getLatestPollDate();
	if (latestMjd < getModelPtr(modelIndex)->endDate.GetMJD()) return;
	getModelPtr(modelIndex)->endDate = MjdToDate(latestMjd);
}

int PollingProject::getModelCount() const {
	return models.size();
}

std::list<Model>::const_iterator PollingProject::getModelBegin() const {
	return models.begin();
}

std::list<Model>::const_iterator PollingProject::getModelEnd() const {
	return models.end();
}

int PollingProject::getModelIndex(Model const* const modelPtr) {
	int i = 0;
	for (auto it = models.begin(); it != models.end(); it++) {
		if (&*it == modelPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::addProjection(Projection projection) {
	projections.push_back(projection);
}

void PollingProject::replaceProjection(int projectionIndex, Projection projection) {
	*getProjectionPtr(projectionIndex) = projection;
}

Projection PollingProject::getProjection(int projectionIndex) const {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return *it;
}

Projection* PollingProject::getProjectionPtr(int projectionIndex) {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return &*it;
}

Projection const* PollingProject::getProjectionPtr(int projectionIndex) const {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return &*it;
}

void PollingProject::removeProjection(int projectionIndex) {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	projections.erase(it);
}

int PollingProject::getProjectionCount() const {
	return projections.size();
}

std::list<Projection>::const_iterator PollingProject::getProjectionBegin() const {
	return projections.begin();
}

std::list<Projection>::const_iterator PollingProject::getProjectionEnd() const {
	return projections.end();
}

int PollingProject::getProjectionIndex(Projection const* const projectionPtr) {
	int i = 0;
	for (auto it = projections.begin(); it != projections.end(); it++) {
		if (&*it == projectionPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::addRegion(Region region) {
	regions.push_back(region);
	calculateRegionSwingDeviations();
}

void PollingProject::replaceRegion(int regionIndex, Region region) {
	*getRegionPtr(regionIndex) = region;
	calculateRegionSwingDeviations();
}

Region PollingProject::getRegion(int regionIndex) const {
	return *getRegionPtr(regionIndex);
}

Region* PollingProject::getRegionPtr(int regionIndex) {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	return &*it;
}

Region const* PollingProject::getRegionPtr(int regionIndex) const {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	return &*it;
}

void PollingProject::removeRegion(int regionIndex) {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	regions.erase(it);
	calculateRegionSwingDeviations();
}

int PollingProject::getRegionCount() const {
	return regions.size();
}

int PollingProject::getRegionIndex(Region const* const regionPtr) {
	int i = 0;
	for (auto it = regions.begin(); it != regions.end(); it++) {
		if (&*it == regionPtr) return i;
		i++;
	}
	return -1;
}

std::list<Region>::iterator PollingProject::getRegionBegin() {
	return regions.begin();
}

std::list<Region>::iterator PollingProject::getRegionEnd() {
	return regions.end();
}

std::list<Region>::const_iterator PollingProject::getRegionBegin() const {
	return regions.begin();
}

std::list<Region>::const_iterator PollingProject::getRegionEnd() const {
	return regions.end();
}

void PollingProject::calculateRegionSwingDeviations() {
	int totalPopulation = 0;
	float total2pp = 0.0f;
	float totalOld2pp = 0.0f;
	for (Region const& thisRegion : regions) {
		totalPopulation += thisRegion.population;
		total2pp += float(thisRegion.population) * thisRegion.sample2pp;
		totalOld2pp += float(thisRegion.population) * thisRegion.lastElection2pp;
	}
	total2pp /= float(totalPopulation);
	totalOld2pp /= float(totalPopulation);
	for (Region& thisRegion : regions) {
		thisRegion.swingDeviation = (thisRegion.sample2pp - thisRegion.lastElection2pp) - (total2pp - totalOld2pp);
	}
}

void PollingProject::addSeat(Seat seat) {
	seats.push_back(seat);
}

void PollingProject::replaceSeat(int seatIndex, Seat seat) {
	*getSeatPtr(seatIndex) = seat;
}

Seat PollingProject::getSeat(int seatIndex) const {
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	return *it;
}

Seat* PollingProject::getSeatPtr(int seatIndex) {
	if (seatIndex < 0) return nullptr;
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	return &*it;
}

void PollingProject::removeSeat(int seatIndex) {
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	seats.erase(it);
}

int PollingProject::getSeatCount() const {
	return seats.size();
}

int PollingProject::getSeatIndex(Seat const* seatPtr)
{
	int i = 0;
	for (auto it = seats.begin(); it != seats.end(); ++it) {
		if (&*it == seatPtr) return i;
		i++;
	}
	return -1;
}

std::list<Seat>::iterator PollingProject::getSeatBegin() {
	return seats.begin();
}

std::list<Seat>::iterator PollingProject::getSeatEnd() {
	return seats.end();
}

Seat * PollingProject::getSeatPtrByName(std::string seatName)
{
	auto seatIt = std::find_if(seats.begin(), seats.end(),
		[seatName](Seat const& s) {return s.name == seatName; });
	if (seatIt != seats.end()) return &*seatIt;
	return nullptr;
}

void PollingProject::addSimulation(Simulation simulation) {
	simulations.push_back(simulation);
}

void PollingProject::replaceSimulation(int simulationIndex, Simulation simulation) {
	*getSimulationPtr(simulationIndex) = simulation;
}

Simulation PollingProject::getSimulation(int simulationIndex) const {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return *it;
}

Simulation* PollingProject::getSimulationPtr(int simulationIndex) {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return &*it;
}

Simulation const* PollingProject::getSimulationPtr(int simulationIndex) const {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return &*it;
}

void PollingProject::removeSimulation(int simulationIndex) {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	simulations.erase(it);
}

int PollingProject::getSimulationCount() const {
	return simulations.size();
}

std::list<Simulation>::const_iterator PollingProject::getSimulationBegin() const {
	return simulations.begin();
}

std::list<Simulation>::const_iterator PollingProject::getSimulationEnd() const {
	return simulations.end();
}

void PollingProject::addResult(Result result)
{
	results.push_front(result);
}

Result PollingProject::getResult(int resultIndex) const
{
	auto it = results.begin();
	std::advance(it, resultIndex);
	return *it;
}

int PollingProject::getResultCount() const
{
	return results.size();
}

std::list<Result>::iterator PollingProject::getResultBegin()
{
	return results.begin();
}

std::list<Result>::iterator PollingProject::getResultEnd()
{
	return results.end();
}

Results::Booth const& PollingProject::getBooth(int boothId) const
{
	return booths.at(boothId);
}

Point2Df PollingProject::boothLatitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool latitudeInitiated = false;
	float minLatitude = 0.0f;
	float maxLatitude = 0.0f;
	for (auto const& booth : booths) {
		float latitude = booth.second.coords.latitude;
		if (std::abs(latitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (!latitudeInitiated) {
			minLatitude = latitude;
			maxLatitude = latitude;
			latitudeInitiated = true;
		}
		minLatitude = std::min(minLatitude, latitude);
		maxLatitude = std::max(maxLatitude, latitude);
	}
	if (!latitudeInitiated) return { 0.0f, 0.0f };
	return { minLatitude, maxLatitude };
}

Point2Df PollingProject::boothLongitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool longitudeInitiated = false;
	float minLongitude = 0.0f;
	float maxLongitude = 0.0f;
	for (auto const& booth : booths) {
		float longitude = booth.second.coords.longitude;
		if (std::abs(longitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (longitude > 180.0f) {
			logger << booth.second.name << " - bad longitude - " << longitude << "\n";
		}
		if (!longitudeInitiated) {
			minLongitude = longitude;
			maxLongitude = longitude;
			longitudeInitiated = true;
		}
		minLongitude = std::min(minLongitude, longitude);
		maxLongitude = std::max(maxLongitude, longitude);
	}
	if (!longitudeInitiated) return { 0.0f, 0.0f };
	return { minLongitude, maxLongitude };
}

Party::Id PollingProject::getPartyByCandidate(int candidateId) const
{
	auto candidateIt = candidateParties.find(candidateId);
	if (candidateIt == candidateParties.end()) return Party::InvalidId;
	return candidateIt->second;
}

Party::Id PollingProject::getPartyByAffiliation(int affiliationId) const
{
	auto affiliationIt = affiliationParties.find(affiliationId);
	if (affiliationIt == affiliationParties.end()) return Party::InvalidId;
	return affiliationIt->second;
}

Results::Candidate const * PollingProject::getCandidateById(int candidateId) const
{
	auto candidateIt = candidates.find(candidateId);
	if (candidateIt == candidates.end()) return nullptr;
	return &candidateIt->second;
}

Results::Affiliation const * PollingProject::getAffiliationById(int affiliationId) const
{
	auto affiliationIt = affiliations.find(affiliationId);
	if (affiliationIt == affiliations.end()) return nullptr;
	return &affiliationIt->second;
}

int PollingProject::getCandidateAffiliationId(int candidateId) const
{
	auto affiliationIt = candidateAffiliations.find(candidateId);
	if (affiliationIt == candidateAffiliations.end()) return -1;
	return affiliationIt->second;
}

void PollingProject::updateLatestResultsForSeats() {
	for (auto& thisResult : results) {
		if (!thisResult.seat->latestResult) thisResult.seat->latestResult = &thisResult;
		else if (thisResult.seat->latestResult->updateTime < thisResult.updateTime) thisResult.seat->latestResult = &thisResult;
	}
}

int PollingProject::save(std::string filename) {
	std::ofstream os = std::ofstream(filename, std::ios_base::trunc);
	os << std::setprecision(12);
	if (!os) return 1;
	os << "#Project" << "\n";
	os << "name=" << name << "\n";
	os << "opre=" << othersPreferenceFlow << "\n";
	os << "oexh=" << othersExhaustRate << "\n";
	os << "#partyCollection" << "\n";
	for (auto const& partyPair : partyCollection) {
		Party const& thisParty = partyPair.second;
		os << "@Party" << "\n";
		os << "name=" << thisParty.name << "\n";
		os << "pref=" << thisParty.preferenceShare << "\n";
		os << "exha=" << thisParty.exhaustRate << "\n";
		os << "abbr=" << thisParty.abbreviation << "\n";
		os << "cap =" << int(thisParty.countAsParty) << "\n";
		os << "supp=" << int(thisParty.supportsParty) << "\n";
		os << "bcmt=" << thisParty.boothColourMult << "\n";
		os << "ideo=" << thisParty.ideology << "\n";
		os << "cons=" << thisParty.consistency << "\n";
		for (std::string officialCode : thisParty.officialCodes) {
			os << "code=" << officialCode << "\n";
		}
		os << "colr=" << thisParty.colour.r << "\n";
		os << "colg=" << thisParty.colour.g << "\n";
		os << "colb=" << thisParty.colour.b << "\n";
	}
	os << "#Pollsters" << "\n";
	for (auto it = pollsters.begin(); it != pollsters.end(); ++it) {
		os << "@Pollster" << "\n";
		os << "name=" << it->name << "\n";
		os << "weig=" << it->weight << "\n";
		os << "colr=" << it->colour << "\n";
		os << "cali=" << int(it->useForCalibration) << "\n";
		os << "igin=" << int(it->ignoreInitially) << "\n";
	}
	os << "#Polls" << "\n";
	for (auto it = polls.begin(); it != polls.end(); ++it) {
		os << "@Poll" << "\n";
		os << "poll=" << getPollsterIndex(it->pollster) << "\n";
		os << "year=" << it->date.GetYear() << "\n";
		os << "mont=" << it->date.GetMonth() << "\n";
		os << "day =" << it->date.GetDay() << "\n";
		os << "prev=" << it->reported2pp << "\n";
		os << "resp=" << it->respondent2pp << "\n";
		os << "calc=" << it->calc2pp << "\n";
		for (int i = 0; i < partyCollection.count(); i++) {
			os << "py" << (i<10 ? "0" : "") << i << "=" << it->primary[i] << "\n";
		}
		os << "py15=" << it->primary[PartyCollection::MaxParties] << "\n";
	}
	os << "#Events" << "\n";
	for (auto const& thisEvent : events) {
		os << "@Event" << "\n";
		os << "name=" << thisEvent.name << "\n";
		os << "type=" << thisEvent.eventType << "\n";
		os << "date=" << thisEvent.date.GetJulianDayNumber() << "\n";
		os << "vote=" << thisEvent.vote << "\n";
	}
	os << "#Models" << "\n";
	for (auto const& thisModel : models) {
		os << "@Model" << "\n";
		os << "name=" << thisModel.name << "\n";
		os << "iter=" << thisModel.numIterations << "\n";
		os << "trnd=" << thisModel.trendTimeScoreMultiplier << "\n";
		os << "hsm =" << thisModel.houseEffectTimeScoreMultiplier << "\n";
		os << "cfpb=" << thisModel.calibrationFirstPartyBias << "\n";
		os << "fstd=" << thisModel.finalStandardDeviation << "\n";
		os << "strt=" << thisModel.startDate.GetJulianDayNumber() << "\n";
		os << "end =" << thisModel.endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisModel.lastUpdated.GetJulianDayNumber() << "\n";
		for (auto const& thisDay : thisModel.day) {
			os << "$Day" << "\n";
			os << "mtnd=" << thisDay.trend2pp << "\n";
			for (int pollsterIndex = 0; pollsterIndex < int(thisDay.houseEffect.size()); ++pollsterIndex) {
				os << "he" << pollsterIndex << (pollsterIndex < 10 ? " " : "") << "=" <<
					thisDay.houseEffect[pollsterIndex] << "\n";
			}
		}
	}
	os << "#Projections" << "\n";
	for (auto const& thisProjection : projections) {
		os << "@Projection" << "\n";
		os << "name=" << thisProjection.name << "\n";
		os << "iter=" << thisProjection.numIterations << "\n";
		os << "base=" << getModelIndex(thisProjection.baseModel) << "\n";
		os << "end =" << thisProjection.endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisProjection.lastUpdated.GetJulianDayNumber() << "\n";
		os << "dlyc=" << thisProjection.dailyChange << "\n";
		os << "inic=" << thisProjection.initialStdDev << "\n";
		os << "vtls=" << thisProjection.leaderVoteLoss << "\n";
		os << "nele=" << thisProjection.numElections << "\n";
		for (int dayIndex = 0; dayIndex < int(thisProjection.meanProjection.size()); ++dayIndex) {
			os << "mean=" << thisProjection.meanProjection[dayIndex] << "\n";
			os << "stdv=" << thisProjection.sdProjection[dayIndex] << "\n";
		}
	}
	os << "#Regions" << "\n";
	for (auto const& thisRegion : regions) {
		os << "@Region" << "\n";
		os << "name=" << thisRegion.name << "\n";
		os << "popn=" << thisRegion.population << "\n";
		os << "lele=" << thisRegion.lastElection2pp << "\n";
		os << "samp=" << thisRegion.sample2pp << "\n";
		os << "swng=" << thisRegion.swingDeviation << "\n";
		os << "addu=" << thisRegion.additionalUncertainty << "\n";
	}
	os << "#Seats" << "\n";
	for (auto const& thisSeat : seats) {
		os << "@Seat" << "\n";
		os << "name=" << thisSeat.name << "\n";
		os << "pvnm=" << thisSeat.previousName << "\n";
		os << "incu=" << partyCollection.idToIndex(thisSeat.incumbent) << "\n";
		os << "chal=" << partyCollection.idToIndex(thisSeat.challenger) << "\n";
		os << "cha2=" << partyCollection.idToIndex(thisSeat.challenger2) << "\n";
		os << "regn=" << getRegionIndex(thisSeat.region) << "\n";
		os << "marg=" << thisSeat.margin << "\n";
		os << "lmod=" << thisSeat.localModifier << "\n";
		os << "iodd=" << thisSeat.incumbentOdds << "\n";
		os << "codd=" << thisSeat.challengerOdds << "\n";
		os << "c2od=" << thisSeat.challenger2Odds << "\n";
		os << "winp=" << thisSeat.incumbentWinPercent << "\n";
		os << "tipp=" << thisSeat.tippingPointPercent << "\n";
		os << "sma =" << thisSeat.simulatedMarginAverage << "\n";
		os << "lp1 =" << partyCollection.idToIndex(thisSeat.livePartyOne) << "\n";
		os << "lp2 =" << partyCollection.idToIndex(thisSeat.livePartyTwo) << "\n";
		os << "lp3 =" << partyCollection.idToIndex(thisSeat.livePartyThree) << "\n";
		os << "p2pr=" << thisSeat.partyTwoProb << "\n";
		os << "p3pr=" << thisSeat.partyThreeProb << "\n";
		os << "over=" << int(thisSeat.overrideBettingOdds) << "\n";
	}
	os << "#Simulations" << "\n";
	for (auto const& thisSimulation : simulations) {
		os << "@Simulation" << "\n";
		os << "name=" << thisSimulation.name << "\n";
		os << "iter=" << thisSimulation.numIterations << "\n";
		os << "base=" << getProjectionIndex(thisSimulation.baseProjection) << "\n";
		os << "prev=" << thisSimulation.prevElection2pp << "\n";
		os << "stsd=" << thisSimulation.stateSD << "\n";
		os << "stde=" << thisSimulation.stateDecay << "\n";
		os << "live=" << int(thisSimulation.live) << "\n";
	}
	os << "#Results" << "\n";
	for (auto const& thisResult : results) {
		os << "@Result" << "\n";
		os << "seat=" << getSeatIndex(thisResult.seat) << "\n";
		os << "swng=" << thisResult.incumbentSwing << "\n";
		os << "cnt =" << thisResult.percentCounted << "\n";
		os << "btin=" << thisResult.boothsIn << "\n";
		os << "btto=" << thisResult.totalBooths << "\n";
		os << "updt=" << thisResult.updateTime.GetJulianDayNumber() << "\n";
	}
	os << "#End";
	os.close();
	return 0; // success
}

bool PollingProject::isValid() {
	return valid;
}

void PollingProject::recalculatePollCalc2PP(Poll& poll) const {
	int npartyCollection = partyCollection.count();
	float sum2PP = 0.0f;
	float sumPrimaries = 0.0f;
	for (int i = 0; i < npartyCollection; i++) {
		if (poll.primary[i] < 0) continue;
		sum2PP += poll.primary[i] * partyCollection.viewByIndex(i).preferenceShare * (1.0f - partyCollection.viewByIndex(i).exhaustRate * 0.01f);
		sumPrimaries += poll.primary[i] * (1.0f - partyCollection.viewByIndex(i).exhaustRate * 0.01f);
	}
	if (poll.primary[PartyCollection::MaxParties] > 0) {
		sum2PP += poll.primary[PartyCollection::MaxParties] * othersPreferenceFlow * (1.0f - othersExhaustRate * 0.01f);
		sumPrimaries += poll.primary[PartyCollection::MaxParties] * (1.0f - othersExhaustRate * 0.01f);
	}
	poll.calc2pp = sum2PP / sumPrimaries + 0.14f; // the last 0.14f accounts for
												  // leakage in Lib-Nat contests
}

void PollingProject::invalidateProjectionsFromModel(Model const* model) {
	for (auto& thisProjection : projections) {
		if (thisProjection.baseModel == model) { thisProjection.lastUpdated = wxInvalidDateTime; }
	}
}

void PollingProject::open(std::string filename) {
	valid = true;
	std::ifstream is = std::ifstream(filename);
	if (!is) { valid = false; return; }

	FileOpeningState fos;

	while (is) {
		std::string s;
		std::getline(is, s);
		if(!processFileLine(s, fos)) break;
	}

	is.close();

	results.clear(); // *** remove!

	finalizeFileLoading();
}

bool PollingProject::processFileLine(std::string line, FileOpeningState& fos) {

	// Section changes
	if (!line.compare("#Project")) {
		fos.section = FileSection_Project;
		return true;
	}
	else if (!line.compare("#Parties")) {
		fos.section = FileSection_Parties;
		return true;
	}
	else if (!line.compare("#Pollsters")) {
		fos.section = FileSection_Pollsters;
		return true;
	}
	else if (!line.compare("#Polls")) {
		fos.section = FileSection_Polls;
		return true;
	}
	else if (!line.compare("#Events")) {
		fos.section = FileSection_Events;
		return true;
	}
	else if (!line.compare("#Models")) {
		fos.section = FileSection_Models;
		return true;
	}
	else if (!line.compare("#Projections")) {
		fos.section = FileSection_Projections;
		return true;
	}
	else if (!line.compare("#Regions")) {
		fos.section = FileSection_Regions;
		return true;
	}
	else if (!line.compare("#Seats")) {
		fos.section = FileSection_Seats;
		return true;
	}
	else if (!line.compare("#Simulations")) {
		fos.section = FileSection_Simulations;
		return true;
	}
	else if (!line.compare("#Results")) {
		fos.section = FileSection_Results;
		return true;
	}
	else if (!line.compare("#End")) {
		return false;
	}

	// New item changes
	if (fos.section == FileSection_Parties) {
		if (!line.compare("@Party")) {
			partyCollection.add(Party());
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!line.compare("@Pollster")) {
			pollsters.push_back(Pollster());
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!line.compare("@Poll")) {
			polls.push_back(Poll());
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!line.compare("@Event")) {
			events.push_back(Event());
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!line.compare("@Model")) {
			models.push_back(Model());
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!line.compare("@Projection")) {
			projections.push_back(Projection());
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!line.compare("@Region")) {
			regions.push_back(Region());
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!line.compare("@Seat")) {
			seats.push_back(Seat());
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!line.compare("@Simulation")) {
			simulations.push_back(Simulation());
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!line.compare("@Result")) {
			results.push_back(Result());
			return true;
		}
	}

	// Values
	if (fos.section == FileSection_Project) {
		if (!line.substr(0, 5).compare("name=")) {
			name = line.substr(5);
			return true;
		}
		if (!line.substr(0, 5).compare("opre=")) {
			othersPreferenceFlow = std::stof(line.substr(5));
			return true;
		}
		if (!line.substr(0, 5).compare("oexh=")) {
			othersExhaustRate = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Parties) {
		if (!partyCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			partyCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pref=")) {
			partyCollection.back().preferenceShare = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("exha=")) {
			partyCollection.back().exhaustRate = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("abbr=")) {
			partyCollection.back().abbreviation = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("cap =")) {
			partyCollection.back().countAsParty = Party::CountAsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("supp=")) {
			partyCollection.back().supportsParty = Party::SupportsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("ideo=")) {
			partyCollection.back().ideology = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cons=")) {
			partyCollection.back().consistency = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("bcmt=")) {
			partyCollection.back().boothColourMult = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("code=")) {
			partyCollection.back().officialCodes.push_back(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			partyCollection.back().colour.r = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colg=")) {
			partyCollection.back().colour.g = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colb=")) {
			partyCollection.back().colour.b = std::stoi(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!pollsters.size()) return true; //prevent crash from mixed-up data.
		auto it = pollsters.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("weig=")) {
			it->weight = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			it->colour = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cali=")) {
			it->useForCalibration = std::stoi(line.substr(5)) != 0;
			return true;
		}
		else if (!line.substr(0, 5).compare("igin=")) {
			it->ignoreInitially = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!polls.size()) return true; //prevent crash from mixed-up data.
		auto it = polls.end();
		it--;
		if (!line.substr(0, 5).compare("poll=")) {
			it->pollster = getPollsterPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("year=")) {
			it->date.SetYear(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("mont=")) {
			it->date.SetMonth((wxDateTime::Month)std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("day =")) {
			it->date.SetDay(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			it->reported2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("resp=")) {
			it->respondent2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("calc=")) {
			it->calc2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("py")) {
			int primaryIndex = std::stoi(line.substr(2, 2));
			it->primary[primaryIndex] = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!events.size()) return true; //prevent crash from mixed-up data.
		auto it = events.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("type=")) {
			it->eventType = EventType(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("date=")) {
			it->date = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("vote=")) {
			it->vote = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!models.size()) return true; //prevent crash from mixed-up data.
		auto it = models.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("trnd=")) {
			it->trendTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("hsm =")) {
			it->houseEffectTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cfpb=")) {
			it->calibrationFirstPartyBias = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("fstd=")) {
			it->finalStandardDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("strt=")) {
			it->startDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			it->endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 4).compare("$Day")) {
			it->day.push_back(ModelTimePoint(getPollsterCount()));
			return true;
		}
		else if (!line.substr(0, 5).compare("mtnd=")) {
			if (!it->day.size()) return true;
			it->day.back().trend2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("he") && !line.substr(4, 1).compare("=")) {
			int pollsterIndex = std::stoi(line.substr(2, 2));
			if (pollsterIndex < getPollsterCount()) {
				it->day.back().houseEffect[pollsterIndex] = std::stof(line.substr(5));
			}
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!projections.size()) return true; //prevent crash from mixed-up data.
		auto it = projections.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			it->baseModel = getModelPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			it->endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("dlyc=")) {
			it->dailyChange = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("inic=")) {
			it->initialStdDev = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("vtls=")) {
			it->leaderVoteLoss = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("nele=")) {
			it->numElections = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("mean=")) {
			it->meanProjection.push_back(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("stdv=")) {
			it->sdProjection.push_back(std::stod(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!regions.size()) return true; //prevent crash from mixed-up data.
		auto it = regions.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("popn=")) {
			it->population = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lele=")) {
			it->lastElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("samp=")) {
			it->sample2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			it->swingDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("addu=")) {
			it->additionalUncertainty = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!seats.size()) return true; //prevent crash from mixed-up data.
		auto it = seats.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pvnm=")) {
			it->previousName = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("incu=")) {
			it->incumbent = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("chal=")) {
			it->challenger = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cha2=")) {
			it->challenger2 = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("regn=")) {
			it->region = getRegionPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("marg=")) {
			it->margin = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lmod=")) {
			it->localModifier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("iodd=")) {
			it->incumbentOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("codd=")) {
			it->challengerOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("c2od=")) {
			it->challenger2Odds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("winp=")) {
			it->incumbentWinPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("tipp=")) {
			it->tippingPointPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("sma =")) {
			it->simulatedMarginAverage = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp1 =")) {
			it->livePartyOne = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp2 =")) {
			it->livePartyTwo = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp3 =")) {
			it->livePartyThree = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p2pr=")) {
			it->partyTwoProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p3pr=")) {
			it->partyThreeProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("over=")) {
			it->overrideBettingOdds = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!simulations.size()) return true; //prevent crash from mixed-up data.
		auto it = simulations.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			it->baseProjection = getProjectionPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			it->prevElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stsd=")) {
			it->stateSD = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stde=")) {
			it->stateDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("live=")) {
			it->live = Simulation::Mode(std::stoi(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!results.size()) return true; //prevent crash from mixed-up data.
		auto it = results.end();
		it--;
		if (!line.substr(0, 5).compare("seat=")) {
			it->seat = getSeatPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			it->incumbentSwing = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cnt =")) {
			it->percentCounted = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("btin=")) {
			it->boothsIn = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("btto=")) {
			it->totalBooths = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->updateTime = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
	}

	return true;
}

void PollingProject::removePollsFromPollster(Pollster const* pollster) {
	for (int i = 0; i < getPollCount(); i++) {
		Poll* poll = &polls[i];
		if (poll->pollster == pollster) { removePoll(i); i--; }
	}
}

void PollingProject::removeProjectionsFromModel(Model const* model) {
	for (int i = 0; i < getProjectionCount(); i++) {
		Projection* projection = getProjectionPtr(i);
		if (projection->baseModel == model) { removeProjection(i); i--; }
	}
}

void PollingProject::adjustPollsAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id) {
	for (int i = 0; i < getPollCount(); i++) {
		Poll* poll = &polls[i];
		for (int j = partyIndex; j < partyCollection.count(); j++)
			poll->primary[j] = poll->primary[j + 1];
		poll->primary[partyCollection.count()] = -1;
	}
}

void PollingProject::adjustSeatsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId) {
	for (int i = 0; i < getSeatCount(); i++) {
		Seat* seat = getSeatPtr(i);
		if (seat->incumbent == partyId) seat->incumbent = (seat->challenger ? 0 : 1);
		if (seat->challenger == partyId) seat->challenger = (seat->incumbent ? 0 : 1);
		if (seat->challenger2 == partyId) seat->challenger2 = 0;
	}
}

void PollingProject::adjustCandidatesAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& candidate : candidateParties) {
		if (candidate.second == partyId) {
			candidate.second = -1;
		}
	}
}

void PollingProject::adjustAffiliationsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& affiliation : affiliationParties) {
		if (affiliation.second == partyId) affiliation.second = -1;
	}
}

void PollingProject::finalizeFileLoading() {
	// sets the correct effective start/end dates
	for (auto& model : models) {
		model.updateEffectiveDates(MjdToDate(getEarliestPollDate()), MjdToDate(getLatestPollDate()));
	}

	partyCollection.finaliseFileLoading();

	setVisualiserBounds(visStartDay, visEndDay);
}

void PollingProject::collectAffiliations(PreviousElectionDataRetriever const & dataRetriever)
{
	affiliationParties.insert({-1, Party::InvalidId});
	affiliations.insert({ -1, {"Invalid"} });
	for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
		affiliations.insert({ affiliationIt->first, affiliationIt->second });
		// Don't bother doing any string comparisons if this affiliation is already recorded
		if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
			for (auto const& party : partyCollection) {
				for (auto partyCode : party.second.officialCodes) {
					if (affiliationIt->second.shortCode == partyCode) {
						affiliationParties.insert({ affiliationIt->first, party.first });
					}
				}
			}
		}
	}
}

void PollingProject::collectCandidatesFromPreload(PreloadDataRetriever const & dataRetriever)
{
	affiliationParties.insert({ -1, Party::InvalidId });
	for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
		affiliations.insert({ affiliationIt->first, affiliationIt->second });
		// Don't bother doing any string comparisons if this affiliation is already recorded
		if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
			for (auto const& party : partyCollection) {
				for (auto partyCode : party.second.officialCodes) {
					if (affiliationIt->second.shortCode == partyCode) {
						affiliationParties.insert({ affiliationIt->first, party.first });
					}
				}
			}
		}
	}

	candidateParties.insert({ -1, Party::InvalidId });
	for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
		candidates.insert(*candidateIt);
		int affiliationId = candidateIt->second.affiliationId;
		candidateAffiliations.insert({ candidateIt->first, affiliationId });
		auto affiliationIt = affiliationParties.find(affiliationId);
		if (affiliationIt != affiliationParties.end()) {
			candidateParties.insert({ candidateIt->first, affiliationIt->second });
		}
		else {
			// treat unknown party as independent
			candidateParties.insert({ candidateIt->first, affiliationParties[0] });
		}
	}
}

void PollingProject::collectBoothsFromPreload(PreloadDataRetriever const & dataRetriever)
{
	for (auto boothIt = dataRetriever.beginBooths(); boothIt != dataRetriever.endBooths(); ++boothIt) {
		auto foundBooth = booths.find(boothIt->first);
		if (foundBooth == booths.end()) {
			booths.insert({ boothIt->first, boothIt->second });
		}
		else {
			foundBooth->second.coords = boothIt->second.coords;
			foundBooth->second.name = boothIt->second.name;
		}
	}
}

float PollingProject::calculateSwingToIncumbent(Seat const & seat)
{
	std::array<int, 2> seatTotalVotes = { 0, 0 };
	std::array<int, 2> seatTotalVotesOld = { 0, 0 };
	for (auto booth : seat.latestResults->booths) {
		Results::Booth thisBooth = booths[booth];
		int totalOld = thisBooth.tcpVote[0] + thisBooth.tcpVote[1];
		int totalNew = thisBooth.newTcpVote[0] + thisBooth.newTcpVote[1];
		if (totalOld && totalNew) {
			bool matchedSame = (affiliationParties[thisBooth.tcpAffiliationId[0]] == candidateParties[seat.latestResults->finalCandidates[0].candidateId]);
			if (matchedSame) {
				seatTotalVotes[0] += thisBooth.newTcpVote[0];
				seatTotalVotes[1] += thisBooth.newTcpVote[1];
				seatTotalVotesOld[0] += thisBooth.tcpVote[0];
				seatTotalVotesOld[1] += thisBooth.tcpVote[1];
			}
			else {
				seatTotalVotes[0] += thisBooth.newTcpVote[1];
				seatTotalVotes[1] += thisBooth.newTcpVote[0];
				seatTotalVotesOld[0] += thisBooth.tcpVote[1];
				seatTotalVotesOld[1] += thisBooth.tcpVote[0];
			}
		}
	}

	int totalOldSeat = seatTotalVotesOld[0] + seatTotalVotesOld[1];
	int totalNewSeat = seatTotalVotes[0] + seatTotalVotes[1];

	if (totalOldSeat && totalNewSeat) {
		float swing = (float(seatTotalVotes[0]) / float(totalNewSeat) -
			float(seatTotalVotesOld[0]) / float(totalOldSeat)) * 100.0f;
		float swingToIncumbent = swing * (seat.incumbent == candidateParties[seat.latestResults->finalCandidates[0].candidateId] ? 1 : -1);
		return swingToIncumbent;
	}
	return 0;
}

float PollingProject::calculate2cpPercentComplete(Seat const & seat)
{
	if (seat.latestResults->enrolment <= 0) return 0;
	int totalVotes = 0;
	for (auto booth : seat.latestResults->booths) {
		Results::Booth thisBooth = booths[booth];
		totalVotes += thisBooth.newTcpVote[0];
		totalVotes += thisBooth.newTcpVote[1];
	}

	totalVotes += seat.latestResults->declarationVotes();

	return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}

float PollingProject::calculateFpPercentComplete(Seat const & seat)
{
	if (seat.latestResults->enrolment <= 0) return 0;
	int totalVotes = seat.latestResults->totalFpVotes();

	return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}
