#pragma once

#include "ForecastSpecificationProjectAdapter.h"

class PollingProject;

using ForecastSpecificationImportResult =
	ForecastSpecificationProjectApplyResult;

// Loads the forecast package matching the legacy project's model term code and
// applies its portable configuration in place. Projects without a matching
// package continue to use their .pol2 configuration.
class ForecastSpecificationImporter {
public:
	static ForecastSpecificationImportResult importForProject(
		PollingProject& project);
};
