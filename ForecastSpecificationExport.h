#pragma once

#include "ForecastSpecificationIO.h"

#include <filesystem>
#include <vector>

class PollingProject;

struct ForecastSpecificationExportResult {
	ForecastSpecification specification;
	std::vector<ForecastSpecificationDiagnostic> diagnostics;

	bool valid() const;
	std::string errorMessage() const;
};

// Exports only configuration required by the core forecast pipeline. Generated
// output, GUI state, polls, seats, reports, and temporary overrides remain in
// the .pol2 project or their existing canonical analysis files.
ForecastSpecificationExportResult exportForecastSpecification(
	PollingProject const& project,
	std::filesystem::path const& packageDirectory);
