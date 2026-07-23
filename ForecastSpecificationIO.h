#pragma once

#include "ForecastSpecification.h"

#include <filesystem>
#include <string>
#include <vector>

struct ForecastSpecificationDiagnostic {
	enum class Severity {
		Error,
		Warning,
	};

	Severity severity = Severity::Error;
	std::string location;
	std::string message;
};

struct ForecastSpecificationLoadResult {
	ForecastSpecification specification;
	std::vector<ForecastSpecificationDiagnostic> diagnostics;

	bool valid() const;
};

// Reads a manifest and its package-relative CSV tables, then performs domain,
// cross-reference, and workspace-source validation. All paths in diagnostics
// are lexical paths; loading does not alter the project or the workspace.
ForecastSpecificationLoadResult loadForecastSpecification(
	std::filesystem::path const& manifestPath,
	std::filesystem::path const& workspaceRoot);

// Validates an already constructed specification. manifestDirectory is used
// for package table paths; workspaceRoot is used for referenced analysis data.
// Exporters may disable package-file checks while the tables exist only in
// memory.
std::vector<ForecastSpecificationDiagnostic> validateForecastSpecification(
	ForecastSpecification const& specification,
	std::filesystem::path const& manifestDirectory,
	std::filesystem::path const& workspaceRoot,
	bool validatePackageFiles = true);
