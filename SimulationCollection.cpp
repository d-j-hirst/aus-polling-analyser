#include "SimulationCollection.h"

#include "General.h"
#include "PollingProject.h"

#include <algorithm>
#include <exception>
#include <optional>

namespace {
	std::optional<Timestamp> parseLiveReportDate(std::string const& dateCode)
	{
		return Timestamp::parseCompactLocal(dateCode);
	}
}

SimulationCollection::SimulationCollection(PollingProject & project)
	: project(project)
{
}

void SimulationCollection::finaliseFileLoading() {
}

void SimulationCollection::add(Simulation simulation) {
	simulations.insert({ nextId, simulation });
	++nextId;
}

void SimulationCollection::replace(Simulation::Id id, Simulation simulation) {
	simulations[id] = simulation;
	logger << simulation.getSettings().name;
	logger << simulations[id].getSettings().name;
}

Simulation const& SimulationCollection::view(Simulation::Id id) const {
	return simulations.at(id);
}

SimulationCollection::Index SimulationCollection::idToIndex(Simulation::Id id) const
{
	auto foundIt = simulations.find(id);
	if (foundIt == simulations.end()) return InvalidIndex;
	return std::distance(simulations.begin(), foundIt);
}

Simulation::Id SimulationCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Simulation::InvalidId;
	return std::next(simulations.begin(), index)->first;
}

SimulationCollection::Result SimulationCollection::canRemove(Simulation::Id id)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) return Result::SimulationDoesntExist;
	return Result::Ok;
}

void SimulationCollection::remove(Simulation::Id id) {
	// A lot of simulation management is simplified by keeping the first two simulations consistent,
	// so we forbid removal of these simulations to avoid messier code.
	// If the user wants different top-two simulations they can just edit them
	// and having less than two simulations doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::SimulationDoesntExist) throw SimulationDoesntExistException();
	auto simulationIt = simulations.find(id);
	simulations.erase(simulationIt);
}

bool SimulationCollection::run(
	Simulation::Id id,
	SimulationRun::FeedbackFunc feedback,
	SimulationRun::ActionRequiredFunc actionRequired)
{
	if (!actionRequired) actionRequired = feedback;
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		feedback("The requested simulation does not exist.");
		return false;
	}
	Simulation& simulation = simulationIt->second;
	return simulation.run(project, feedback, actionRequired);
}

Simulation& SimulationCollection::access(Simulation::Id id)
{
	return simulations.at(id);
}

int SimulationCollection::count() const {
	return simulations.size();
}

std::optional<std::string> SimulationCollection::uploadToServer(
	Simulation::Id id, int reportIndex)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		return "Could not prepare report upload: the selected simulation does not exist.";
	}
	auto const& reports = simulationIt->second.viewSavedReports();
	if (reportIndex == -1 && simulationIt->second.isLive()) {
		Simulation::SavedReport sReport;
		sReport.report = simulationIt->second.getLatestReport();
		sReport.label = "New results";
		sReport.dateSaved = Timestamp::now();
		if (!sReport.report.dateCode.empty()) {
			auto const reportDate =
				parseLiveReportDate(sReport.report.dateCode);
			if (!reportDate) {
				return "Could not prepare report upload: the live report has an "
					"invalid date code (expected YYYYMMDDHHMMSS).";
			}
			sReport.dateSaved = *reportDate;
		}
		auto reportUploader = ReportUploader(sReport, simulationIt->second, project);
		return reportUploader.upload();
	}
	if (reportIndex <= -1 || reportIndex >= int(reports.size())) {
		return "Could not prepare report upload: the selected report does not exist.";
	}
	auto const& report = reports[reportIndex];
	auto reportUploader = ReportUploader(report, simulationIt->second, project);
	return reportUploader.upload();
}

void SimulationCollection::deleteReport(Simulation::Id id, int reportIndex)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) throw SimulationDoesntExistException();
	if (reportIndex <= -1 || reportIndex >= int(simulationIt->second.viewSavedReports().size())) {
		logger << "Invalid report!\n";
		return;
	}
	simulationIt->second.deleteReport(reportIndex);
}

void SimulationCollection::deleteAllReports(Simulation::Id id)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) throw SimulationDoesntExistException();
	while (simulationIt->second.viewSavedReports().size() > 0) {
		simulationIt->second.deleteReport(0);
	}
}

void SimulationCollection::startLoadingSimulation()
{
	loadingSimulation.emplace(Simulation::Settings());
}

void SimulationCollection::finaliseLoadedSimulation()
{
	if (!loadingSimulation.has_value()) return;
	add(Simulation(loadingSimulation.value()));
	loadingSimulation.reset();
}

void SimulationCollection::logAll(ProjectionCollection const& projections) const
{
	for (auto const& [key, thisSimulation] : simulations) {
		logger << thisSimulation.textReport(projections);
	}
}
