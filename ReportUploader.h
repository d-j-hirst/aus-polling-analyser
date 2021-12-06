#pragma once

#include <string>
#include "Simulation.h"

class PollingProject;

// "Uploads" this report. (Actually saves it in a folder so a python script can upload it).
class ReportUploader {
public:
	ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project);

	// Returns string for success and failure.
	std::string upload();
private:

	Simulation const& simulation;
	PollingProject const& project;
	Simulation::SavedReport const& thisReport;
};