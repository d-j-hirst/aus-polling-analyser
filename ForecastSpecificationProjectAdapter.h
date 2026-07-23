#pragma once

#include "ForecastSpecificationIO.h"
#include "ForecastSpecificationRuntimeIds.h"

#include <memory>
#include <string>
#include <vector>

class PollingProject;
class WorkspacePaths;

struct ForecastSpecificationProjectApplyResult {
	bool applied = false;
	ForecastSpecificationRuntimeIds runtimeIds;
	std::vector<ForecastSpecificationDiagnostic> diagnostics;

	bool valid() const;
	std::string errorMessage() const;
};

struct ForecastSpecificationProjectConstructionResult {
	ForecastSpecificationProjectConstructionResult();
	~ForecastSpecificationProjectConstructionResult();
	ForecastSpecificationProjectConstructionResult(
		ForecastSpecificationProjectConstructionResult&&) noexcept;
	ForecastSpecificationProjectConstructionResult& operator=(
		ForecastSpecificationProjectConstructionResult&&) noexcept;

	ForecastSpecificationProjectConstructionResult(
		ForecastSpecificationProjectConstructionResult const&) = delete;
	ForecastSpecificationProjectConstructionResult& operator=(
		ForecastSpecificationProjectConstructionResult const&) = delete;

	std::unique_ptr<PollingProject> project;
	ForecastSpecificationRuntimeIds runtimeIds;
	std::vector<ForecastSpecificationDiagnostic> diagnostics;

	bool valid() const;
	std::string errorMessage() const;
};

// Converts the portable DTO into the collections used by the forecast
// pipeline. Callers must pass a specification that has already passed
// loadForecastSpecification/validateForecastSpecification.
class ForecastSpecificationProjectAdapter {
public:
	static ForecastSpecificationProjectApplyResult apply(
		PollingProject& project,
		ForecastSpecification const& specification);

	static ForecastSpecificationProjectConstructionResult construct(
		ForecastSpecification const& specification,
		WorkspacePaths const& workspacePaths);
};
