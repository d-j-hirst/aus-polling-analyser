#include "PollingProject.h"

#include "ElectionCollection.h"
#include "ForecastSpecificationExport.h"
#include "Log.h"
#include "MacroRunner.h"
#include "NewProjectData.h"
#include "ProjectFiler.h"
#include "ResultCoordinator.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <stdexcept>
#include <utility>

const Party PollingProject::invalidParty = Party("Invalid", 50.0f, 0.0f, "INV", Party::CountAsParty::None);

const std::string ConfigFilename = "config.cfg";

PollingProject::PollingProject()
	: PollingProject(WorkspacePaths::discover())
{}

PollingProject::PollingProject(WorkspacePaths workspacePaths) :
	workspacePaths(std::move(workspacePaths)),
	configObj(this->workspacePaths.resolveString(ConfigFilename)),
	partyCollection(*this),
	pollsterCollection(*this),
	pollCollection(*this),
	modelCollection(*this),
	projectionCollection(*this),
	regionCollection(*this),
	seatCollection(*this),
	simulationCollection(*this),
	resultCoordinator(std::make_unique<ResultCoordinator>(*this)),
	electionCollection(std::make_unique<ElectionCollection>(*this))
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
	: PollingProject(WorkspacePaths::discover(pathName))
{
	lastFileName = std::filesystem::path(pathName).filename().string();
	logger << "Loading project from: " << lastFileName << "\n";
	open(pathName);
}

PollingProject::~PollingProject() = default;

ResultCoordinator& PollingProject::results()
{
	return *resultCoordinator;
}

ResultCoordinator const& PollingProject::results() const
{
	return *resultCoordinator;
}

ElectionCollection& PollingProject::elections()
{
	return *electionCollection;
}

ElectionCollection const& PollingProject::elections() const
{
	return *electionCollection;
}


std::optional<std::string> PollingProject::runMacro(std::string macro, FeedbackFunc feedback)
{
	bool passesValidation = true;
	if (!passesValidation) {
		return "Didn't pass validation!";
	}
	else {
		lastMacro = macro;
		return MacroRunner(*this).run(macro, feedback);
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
	results().adjustAffiliationsAfterPartyRemoval(partyIndex, partyId);
	results().adjustCandidatesAfterPartyRemoval(partyIndex, partyId);
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
		int date = model.getEndDate().modifiedJulianDay();
		if (date > latestDay) latestDay = date;
	}
	for (auto const& [key, projection] : projections()) {
		int date = projection.getSettings().endDate.modifiedJulianDay();
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
	resetSimulationsFromProjection(projectionId);
}

void PollingProject::adjustAfterRegionRemoval(RegionCollection::Index regionIndex, Region::Id regionId)
{
	adjustSeatsAfterRegionRemoval(regionIndex, regionId);
}

PollingProject::SaveResult PollingProject::save(std::string filename)
{
	SaveResult result;
	std::string termCode;
	if (models().count() > 0) {
		termCode = models().viewByIndex(0).getTermCode();
	}
	auto const forecastDirectory = paths().resolve(
		std::filesystem::path("forecasts") / termCode);
	std::error_code pathError;
	bool const forecastDirectoryExists = !termCode.empty() &&
		std::filesystem::is_directory(forecastDirectory, pathError);
	if (pathError) {
		throw std::runtime_error(
			"Could not inspect the forecast configuration directory: " +
			pathError.message());
	}
	if (forecastDirectoryExists) {
		auto forecastExport =
			exportForecastSpecification(*this, forecastDirectory);
		if (!forecastExport.valid()) {
			throw std::runtime_error(
				"Portable forecast configuration validation failed:\n" +
				forecastExport.errorMessage());
		}
	}
	else {
		result.warnings.push_back(
			"Portable forecast configuration was not exported because no "
			"forecasts/" + (termCode.empty() ? std::string("<term-code>") : termCode) +
			" folder exists.");
	}

	ProjectFiler projectFiler(*this);
	projectFiler.save(filename);
	lastFileName = filename;
	return result;
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
	std::vector<Projection::Id> projectionsToRemove;
	for (auto const& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) {
			projectionsToRemove.push_back(key);
		}
	}
	for (auto const projectionId : projectionsToRemove) {
		projections().remove(projectionId);
	}
}

void PollingProject::resetSimulationsFromProjection(Projection::Id projectionId)
{
	Projection::Id const replacementProjection = projections().indexToId(0);
	for (auto& simulationPair : simulations()) {
		auto& simulation = simulationPair.second;
		if (simulation.getSettings().baseProjection == projectionId) {
			auto settings = simulation.getSettings();
			settings.baseProjection = replacementProjection;
			simulation.replaceSettings(std::move(settings));
		}
	}
}

void PollingProject::adjustSeatsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId) {
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		if (seat.incumbent == partyId) seat.incumbent = (seat.challenger ? 0 : 1);
		if (seat.challenger == partyId) seat.challenger = (seat.incumbent ? 0 : 1);
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
