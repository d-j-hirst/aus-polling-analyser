#include "PollingProject.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "MacroRunner.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"
#include "ProjectFiler.h"

#include <algorithm>
#include <iomanip>

const Party PollingProject::invalidParty = Party("Invalid", 50.0f, 0.0f, "INV", Party::CountAsParty::None);

const std::string ConfigFilename = "config.cfg";

PollingProject::PollingProject() :
	partyCollection(*this),
	pollsterCollection(*this),
	pollCollection(*this),
	modelCollection(*this),
	projectionCollection(*this),
	regionCollection(*this),
	seatCollection(*this),
	simulationCollection(*this),
	electionCollection(*this),
	resultCoordinator(*this),
	configObj(ConfigFilename)
{}

PollingProject::PollingProject(NewProjectData& newProjectData)
	: PollingProject()
{
	name = newProjectData.projectName,
	lastFileName = newProjectData.projectName + ".pol",
	// The project must always have at least two parties, no matter what. This initializes them with default values.
	partyCollection.add(Party("Labor", 100, 0.0f, "ALP", Party::CountAsParty::IsPartyOne));
	partyCollection.add(Party("Liberals", 0, 0.0f, "LIB", Party::CountAsParty::IsPartyTwo));
	pollsterCollection.add(Pollster("Default Pollster", 0));
	valid = true;
}

PollingProject::PollingProject(std::string pathName)
	: PollingProject()
{
	lastFileName = pathName.substr(pathName.rfind("\\") + 1);
	logger << "Loading project from: " << lastFileName << "\n";
	open(pathName);
}


std::optional<std::string> PollingProject::runMacro(std::string macro)
{
	bool passesValidation = true;
	if (!passesValidation) {
		return "Didn't pass validation!";
	}
	else {
		lastMacro = macro;
		return MacroRunner(*this).run(macro, true);
	}
}

void PollingProject::updateMacro(std::string macro)
{
	lastMacro = macro;
}

void PollingProject::refreshCalc2PP() {
	for (auto it = polls().begin(); it != polls().end(); it++)
		partyCollection.recalculatePollCalc2PP(it->second);
}

void PollingProject::adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId)
{
	polls().adjustAfterPartyRemoval(partyIndex, partyId);
	adjustSeatsAfterPartyRemoval(partyIndex, partyId);
	resultCoordinator.adjustAffiliationsAfterPartyRemoval(partyIndex, partyId);
	resultCoordinator.adjustCandidatesAfterPartyRemoval(partyIndex, partyId);
}

void PollingProject::adjustAfterPollsterRemoval(PollsterCollection::Index /*pollsterIndex*/, Party::Id pollsterId)
{
	polls().removePollsFromPollster(pollsterId);
}

int PollingProject::getEarliestDate() const {
	int earliestDay = polls().getEarliestDate();
	return earliestDay;
}

int PollingProject::getLatestDate() const {
	int latestDay = polls().getLatestDate();
	for (auto const& [key, model] : models()) {
		int date = int(floor(model.getEndDate().GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	for (auto const& [key, projection] : projections()) {
		int date = int(floor(projection.getSettings().endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

void PollingProject::adjustAfterModelRemoval(ModelCollection::Index, StanModel::Id modelId)
{
	removeProjectionsFromModel(modelId);
}

void PollingProject::adjustAfterProjectionRemoval(ProjectionCollection::Index, Projection::Id projectionId)
{
	removeSimulationsFromProjection(projectionId);
}

void PollingProject::adjustAfterRegionRemoval(RegionCollection::Index regionIndex, Region::Id regionId)
{
	adjustSeatsAfterRegionRemoval(regionIndex, regionId);
}

void PollingProject::save(std::string filename)
{
	ProjectFiler projectFiler(*this);
	lastFileName = filename;
	projectFiler.save(filename);
}

bool PollingProject::isValid() {
	return valid;
}

void PollingProject::invalidateProjectionsFromModel(StanModel::Id modelId) {
	for (auto& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) projection.invalidate();
	}
}

void PollingProject::open(std::string filename)
{
	ProjectFiler projectFiler(*this);
	projectFiler.open(filename);
}

void PollingProject::removeProjectionsFromModel(StanModel::Id modelId) {
	for (auto const& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) projections().remove(key);
	}
}

void PollingProject::removeSimulationsFromProjection(Projection::Id projectionId)
{
	for (int i = 0; i < simulations().count(); i++) {
		Simulation const& simulation = simulations().viewByIndex(i);
		if (simulation.getSettings().baseProjection == projectionId) {
			simulations().remove(simulations().indexToId(i));
			i--;
		}
	}
}

void PollingProject::adjustSeatsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId) {
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		if (seat.incumbent == partyId) seat.incumbent = (seat.challenger ? 0 : 1);
		if (seat.challenger == partyId) seat.challenger = (seat.incumbent ? 0 : 1);
		if (seat.challenger2 == partyId) seat.challenger2 = 0;
	}
}

void PollingProject::adjustSeatsAfterRegionRemoval(RegionCollection::Index, Party::Id regionId)
{
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		if (seat.region == regionId) seat.region = regions().indexToId(0);
	}
}

void PollingProject::finalizeFileLoading() {
	partyCollection.finaliseFileLoading();
}