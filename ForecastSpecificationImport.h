#pragma once

#include "ForecastSpecificationIO.h"

#include <vector>

class PollingProject;

struct ForecastSpecificationImportResult {
	bool applied = false;
	std::vector<ForecastSpecificationDiagnostic> diagnostics;

	bool valid() const;
	std::string errorMessage() const;
};

// Loads the forecast package matching the legacy project's model term code and
// applies its portable configuration in place. Projects without a matching
// package continue to use their .pol2 configuration.
class ForecastSpecificationImporter {
public:
	static ForecastSpecificationImportResult importForProject(
		PollingProject& project);
};
