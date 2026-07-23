#pragma once

#include "Simulation.h"

#include <string>
#include <string_view>

// Formats the report fields shown by DisplayFrame without depending on
// wxWidgets, candidate metadata, graphics, or live-election details.
std::string formatCoreReportSummary(
	std::string_view specificationId,
	Simulation const& simulation);

// Report-level overload used by non-GUI renderers and formatting tests.
std::string formatCoreReportSummary(
	std::string_view specificationId,
	std::string_view simulationName,
	Simulation::Report const& report);
