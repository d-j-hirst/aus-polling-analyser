#pragma once

#include <optional>
#include <string>
#include "Simulation.h"

class PollingProject;

// Serializes a report for a separate upload client. No network access occurs
// here; uploads/upload_manager.py handles authenticated submission.
class ReportUploader {
public:
	ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project);

	// Prepares the JSON consumed by uploads/upload_manager.py.
	// Returns an error message on failure.
	std::optional<std::string> exportReport();
private:

	Simulation const& simulation;
	PollingProject const& project;
	Simulation::SavedReport const& thisReport;
};
