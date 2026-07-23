#include "PollingProject.h"

#include "ElectionCollection.h"
#include "ForecastSpecificationExport.h"
#include "Log.h"
#include "MacroRunner.h"
#include "NewProjectData.h"
#include "ProjectFiler.h"
#include "ResultCoordinator.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

PollingProject::PollingProject()
	: PollingProject(WorkspacePaths::discover())
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

ResultCoordinator& PollingProject::results()
{
	if (!resultCoordinator) {
		resultCoordinator = std::make_shared<ResultCoordinator>(*this);
		legacyPartyRemovalHandler = [this](
			PartyCollection::Index partyIndex, Party::Id partyId) {
			resultCoordinator->adjustAffiliationsAfterPartyRemoval(
				partyIndex, partyId);
			resultCoordinator->adjustCandidatesAfterPartyRemoval(
				partyIndex, partyId);
		};
	}
	return *resultCoordinator;
}

ResultCoordinator const& PollingProject::results() const
{
	return const_cast<PollingProject*>(this)->results();
}

ElectionCollection& PollingProject::elections()
{
	if (!electionCollection) {
		electionCollection = std::make_shared<ElectionCollection>(*this);
	}
	return *electionCollection;
}

ElectionCollection const& PollingProject::elections() const
{
	return const_cast<PollingProject*>(this)->elections();
}


std::optional<std::string> PollingProject::runMacro(
	std::string macro,
	MacroFeedbackFunc feedback)
{
	bool passesValidation = true;
	if (!passesValidation) {
		std::string const message = "Didn't pass validation!";
		feedback(MacroFeedbackType::Fatal, message);
		return message;
	}
	else {
		lastMacro = macro;
		return MacroRunner(*this).run(macro, std::move(feedback));
	}
}

void PollingProject::updateMacro(std::string macro)
{
	lastMacro = macro;
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

void PollingProject::open(std::string filename)
{
	ProjectFiler projectFiler(*this);
	projectFiler.open(filename);
}
