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
	file << project.models().view(0).getTermCode();
	return "ok";
}
