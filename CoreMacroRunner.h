#pragma once

#include "ForecastSpecificationRuntimeIds.h"
#include "MacroFeedback.h"

#include <functional>
#include <optional>
#include <string>

class PollingProject;
class Simulation;

// Executes the portable forecast pipeline without project-management or GUI
// commands. All commands and targets are validated before execution begins.
class CoreMacroRunner {
public:
	using FeedbackType = MacroFeedbackType;
	using FeedbackFunc = MacroFeedbackFunc;
	using SimulationCompletedFunc =
		std::function<void(std::string const&, Simulation const&)>;

	explicit CoreMacroRunner(PollingProject& project);

	std::optional<std::string> run(
		std::string const& macro,
		ForecastSpecificationRuntimeIds const& runtimeIds,
		FeedbackFunc feedback = [](FeedbackType, std::string) {},
		SimulationCompletedFunc simulationCompleted = {});

private:
	PollingProject& project_;
};
