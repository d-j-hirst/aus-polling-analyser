#include "ReportUploader.h"

#include <fstream>

ReportUploader::ReportUploader(Simulation::Report const& thisReport)
	: thisReport(thisReport)
{
}

std::string ReportUploader::upload()
{
	std::ofstream file("uploads/latest.dat");
	file << thisReport.partyName.at(0);
	return "ok";
}
