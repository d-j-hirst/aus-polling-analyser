#pragma once

#include <string>
#include "Simulation.h"

// "Uploads" this report. (Actually saves it in a folder so a python script can upload it).
class ReportUploader {
public:
	ReportUploader(Simulation::Report const& thisReport);

	// Returns string for success and failure.
	std::string upload();
private:
	Simulation::Report const& thisReport;
};