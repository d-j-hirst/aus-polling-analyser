#include "ReportUploader.h"

#include "PollingProject.h"

#include <fstream>

ReportUploader::ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project)
	: project(project), simulation(simulation), thisReport(thisReport)
{
}

std::string ReportUploader::upload()
{
	std::ofstream file("uploads/latest.dat");
	// *** This should be a temporary hack, replace with a global project setting
	// once creating the UI for that can be justified
	file << project.models().view(0).getTermCode() << "\n";
	file << project.getElectionName() << "\n";
	file << thisReport.label << "\n";
	file << thisReport.dateSaved.ToUTC().FormatISOCombined() << "\n";
	std::string modeString;
	switch (simulation.getSettings().reportMode) {
	case Simulation::Settings::ReportMode::RegularForecast: modeString = "RF"; break;
	case Simulation::Settings::ReportMode::LiveForecast: modeString = "LF"; break;
	case Simulation::Settings::ReportMode::Nowcast: modeString = "NC"; break;
	}
	file << modeString << "\n";
	file << thisReport.report.getPartyOverallWinPercent(Simulation::MajorParty::One) << "\n";
	file << thisReport.report.getPartyOverallWinPercent(Simulation::MajorParty::Two) << "\n";
	file << thisReport.report.getOthersOverallWinPercent() << "\n";
	return "ok";
}
