#include "ForecastSpecificationImport.h"

#include "PollingProject.h"

#include <filesystem>
#include <iterator>
#include <set>
#include <utility>

namespace {
	using Diagnostic = ForecastSpecificationDiagnostic;

	void addError(std::vector<Diagnostic>& diagnostics,
		std::string location, std::string message)
	{
		diagnostics.push_back({ Diagnostic::Severity::Error,
			std::move(location), std::move(message) });
	}
}

ForecastSpecificationImportResult
ForecastSpecificationImporter::importForProject(PollingProject& project)
{
	ForecastSpecificationImportResult result;
	std::set<std::string> matchingTermCodes;
	for (auto const& modelEntry : project.models()) {
		auto const& model = modelEntry.second;
		if (model.getTermCode().empty()) continue;
		auto const manifest = project.paths().resolve(
			std::filesystem::path("forecasts") / model.getTermCode() /
			"forecast.json");
		std::error_code error;
		if (std::filesystem::is_regular_file(manifest, error) && !error) {
			matchingTermCodes.insert(model.getTermCode());
		}
	}
	if (matchingTermCodes.empty()) return result;
	if (matchingTermCodes.size() != 1) {
		addError(result.diagnostics, "forecasts",
			"the project refers to more than one available forecast package");
		return result;
	}

	auto const termCode = *matchingTermCodes.begin();
	auto const manifest = project.paths().resolve(
		std::filesystem::path("forecasts") / termCode / "forecast.json");
	auto loaded = loadForecastSpecification(manifest, project.paths().root());
	result.diagnostics = std::move(loaded.diagnostics);
	if (!loaded.valid()) return result;
	if (loaded.specification.electionCode != termCode) {
		addError(result.diagnostics, manifest.generic_string(),
			"election_code does not match the forecast package directory");
		return result;
	}

	auto applied = ForecastSpecificationProjectAdapter::apply(
		project, loaded.specification);
	result.applied = applied.applied;
	result.runtimeIds = std::move(applied.runtimeIds);
	result.diagnostics.insert(
		result.diagnostics.end(),
		std::make_move_iterator(applied.diagnostics.begin()),
		std::make_move_iterator(applied.diagnostics.end()));
	return result;
}
