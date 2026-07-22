#pragma once

#include <optional>
#include <string>
#include "Simulation.h"

class PollingProject;

// "Uploads" this report. (Actually saves it in a folder so a python script can upload it).
class ReportUploader {
public:
	ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project);

	// Prepares the JSON consumed by uploads/upload_manager.py.
	// Returns an error message on failure.
	std::optional<std::string> upload();
private:

	Simulation const& simulation;
	PollingProject const& project;
	Simulation::SavedReport const& thisReport;
};
