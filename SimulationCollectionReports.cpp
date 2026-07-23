#include "SimulationCollection.h"

#include "Log.h"
#include "PollingProject.h"
#include "ReportUploader.h"

#include <sstream>

namespace {
	std::optional<Timestamp> parseLiveReportDate(std::string const& dateCode)
	{
		return Timestamp::parseCompactLocal(dateCode);
	}

	std::string availableReportLabels(Simulation::SavedReports const& reports)
	{
		if (reports.empty()) return "(none)";
		std::ostringstream labels;
		for (std::size_t index = 0; index < reports.size(); ++index) {
			if (index) labels << ", ";
			labels << "'" << reports[index].label << "'";
		}
		return labels.str();
	}
}

std::optional<std::string> SimulationCollection::uploadToServer(
	Simulation::Id id, int reportIndex)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		return "Could not prepare report upload: the selected simulation "
			"does not exist.";
	}
	auto const& reports = simulationIt->second.viewSavedReports();
	if (reportIndex == -1 && simulationIt->second.isLive()) {
		Simulation::SavedReport savedReport;
		savedReport.report = simulationIt->second.getLatestReport();
		savedReport.label = "New results";
		savedReport.dateSaved = Timestamp::now();
		if (!savedReport.report.dateCode.empty()) {
			auto const reportDate =
				parseLiveReportDate(savedReport.report.dateCode);
			if (!reportDate) {
				return "Could not prepare report upload: the live report has "
					"an invalid date code (expected YYYYMMDDHHMMSS).";
			}
			savedReport.dateSaved = *reportDate;
		}
			auto reportUploader =
				ReportUploader(savedReport, simulationIt->second, project);
			return reportUploader.exportReport();
	}
	if (reportIndex <= -1 || reportIndex >= int(reports.size())) {
		return "Could not prepare report upload: the selected report does "
			"not exist.";
	}
	auto const& report = reports[reportIndex];
	auto reportUploader =
		ReportUploader(report, simulationIt->second, project);
	return reportUploader.exportReport();
}

std::optional<std::string> SimulationCollection::exportReportByLabel(
	Simulation::Id id, std::string const& reportLabel)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		return "Could not export report: the selected simulation does not exist.";
	}

	auto const& reports = simulationIt->second.viewSavedReports();
	Simulation::SavedReport const* matchingReport = nullptr;
	for (auto const& report : reports) {
		if (report.label != reportLabel) continue;
		if (matchingReport) {
			return "Could not export report: multiple saved reports are labelled '" +
				reportLabel + "'. Report labels must be unique for macro export.";
		}
		matchingReport = &report;
	}
	if (!matchingReport) {
		return "Could not export report: no saved report is labelled '" +
			reportLabel + "'. Available labels: " +
			availableReportLabels(reports) + ".";
	}

	auto reportUploader =
		ReportUploader(*matchingReport, simulationIt->second, project);
	return reportUploader.exportReport();
}

void SimulationCollection::deleteReport(
	Simulation::Id id, int reportIndex)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		throw SimulationDoesntExistException();
	}
	if (reportIndex <= -1 ||
		reportIndex >=
			int(simulationIt->second.viewSavedReports().size())) {
		logger << "Invalid report!\n";
		return;
	}
	simulationIt->second.deleteReport(reportIndex);
}

void SimulationCollection::deleteAllReports(Simulation::Id id)
{
	auto simulationIt = simulations.find(id);
	if (simulationIt == simulations.end()) {
		throw SimulationDoesntExistException();
	}
	while (!simulationIt->second.viewSavedReports().empty()) {
		simulationIt->second.deleteReport(0);
	}
}
