#include "CoreMacroRunner.h"
#include "ForecastSpecificationIO.h"
#include "ForecastSpecificationProjectAdapter.h"
#include "PollingProject.h"
#include "WorkspacePaths.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char const* argv[])
{
	if (argc < 3) {
		std::cerr <<
			"Usage: cli-boundary-tests <workspace> <forecast.json>...\n";
		return 2;
	}

	WorkspacePaths const paths(argv[1]);
	bool valid = true;
	bool checkedLiveRejection = false;
	for (int argument = 2; argument < argc; ++argument) {
		auto loaded = loadForecastSpecification(argv[argument], paths.root());
		if (!loaded.valid()) {
			for (auto const& diagnostic : loaded.diagnostics) {
				std::cerr << argv[argument] << ": " << diagnostic.location <<
					": " << diagnostic.message << '\n';
			}
			valid = false;
			continue;
		}

		auto constructed = ForecastSpecificationProjectAdapter::construct(
			loaded.specification, paths);
		if (!constructed.valid()) {
			std::cerr << argv[argument] << ": " <<
				constructed.errorMessage() << '\n';
			valid = false;
			continue;
		}

		for (auto const& simulation : loaded.specification.simulations) {
			if (simulation.mode ==
				ForecastSpecification::SimulationMode::Projection) {
				continue;
			}

			auto const modelRuntimeId =
				constructed.runtimeIds.models.begin()->second;
			bool const modelWasReady =
				constructed.project->models().view(
					modelRuntimeId).isReadyForProjection();
			std::vector<std::pair<
				CoreMacroRunner::FeedbackType, std::string>> feedback;
			auto const error = CoreMacroRunner(*constructed.project).run(
				"prepare-model;run-simulation:" + simulation.id,
				constructed.runtimeIds,
				[&feedback](
					CoreMacroRunner::FeedbackType type,
					std::string message) {
					feedback.emplace_back(type, std::move(message));
				});
			if (!error ||
				error->find("Unsupported operation: live simulation") ==
					std::string::npos ||
				feedback.size() != 1 ||
				feedback.front().first !=
					CoreMacroRunner::FeedbackType::Fatal ||
				constructed.project->models().view(
					modelRuntimeId).isReadyForProjection() != modelWasReady) {
				std::cerr << argv[argument] <<
					": CLI did not reject live simulation " <<
					simulation.id <<
					" with fatal feedback before executing prepare-model\n";
				valid = false;
			}
			checkedLiveRejection = true;
			break;
		}
	}

	if (!checkedLiveRejection) {
		std::cerr <<
			"No configured live simulation was available for the rejection "
			"test\n";
		return 1;
	}
	return valid ? 0 : 1;
}
