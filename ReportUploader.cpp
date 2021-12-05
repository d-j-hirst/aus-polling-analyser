#include "ReportUploader.h"

#include "PollingProject.h"

#include <fstream>

ReportUploader::ReportUploader(Simulation::Report const& thisReport, PollingProject const& project)
	: thisReport(thisReport), project(project)
{
}

std::string ReportUploader::upload()
{
	std::ofstream file("uploads/latest.dat");
	// *** This should be a temporary hack, replace with a global project setting
	// once creating the UI for that can be justified
	file << project.models().view(0).getTermCode() << "\n";
	file << project.getElectionName() << "\n";
	return "ok";
}
