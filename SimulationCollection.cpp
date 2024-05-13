#include "SimulationCollection.h"

#include "General.h"
#include "PollingProject.h"

#include <exception>

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

void SimulationCollection::run(Simulation::Id id, SimulationRun::FeedbackFunc feedback)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) throw SimulationDoesntExistException();
	Simulation& simulation = simulationIt->second;
	simulation.run(project, feedback);
}

Simulation& SimulationCollection::access(Simulation::Id id)
{
	return simulations.at(id);
}

int SimulationCollection::count() const {
	return simulations.size();
}

void SimulationCollection::uploadToServer(Simulation::Id id, int reportIndex)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) throw SimulationDoesntExistException();
	auto const& reports = simulationIt->second.viewSavedReports();
	if (reportIndex == -1 && simulationIt->second.isLive()) {
		Simulation::SavedReport sReport;
		sReport.report = simulationIt->second.getLatestReport();
		sReport.label = "New results";
		sReport.dateSaved = wxDateTime::Now();
		if (sReport.report.dateCode.size()) {
			int year = std::stoi(sReport.report.dateCode.substr(0, 4));
			int month = std::stoi(sReport.report.dateCode.substr(4, 2));
			int day = std::stoi(sReport.report.dateCode.substr(6, 2));
			int hour = std::stoi(sReport.report.dateCode.substr(8, 2));
			int minute = std::stoi(sReport.report.dateCode.substr(10, 2));
			int second = std::stoi(sReport.report.dateCode.substr(12, 2));
			sReport.dateSaved = wxDateTime(day, wxDateTime::Month(month - 1), year, hour, minute, second);
		}
		auto reportUploader = ReportUploader(sReport, simulationIt->second, project);
		reportUploader.upload();
		return;
	}
	if (reportIndex <= -1 || reportIndex >= int(reports.size())) {
		logger << "Invalid report!\n";
		return;
	}
	auto const& report = reports[reportIndex];
	auto reportUploader = ReportUploader(report, simulationIt->second, project);
	reportUploader.upload();
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
