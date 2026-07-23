#include "CoreMacroRunner.h"

#include "Log.h"
#include "MacroTargetResolver.h"
#include "PollingProject.h"

#include <exception>
#include <utility>
#include <vector>

CoreMacroRunner::CoreMacroRunner(PollingProject& project)
	: project_(project)
{}

std::optional<std::string> CoreMacroRunner::run(
	std::string const& macro,
	ForecastSpecificationRuntimeIds const& runtimeIds,
	FeedbackFunc feedback,
	SimulationCompletedFunc simulationCompleted)
{
	if (!feedback) feedback = [](FeedbackType, std::string) {};
	auto fatal = [&](std::string message) {
		logger << message << '\n';
		feedback(FeedbackType::Fatal, message);
		return std::optional<std::string>(std::move(message));
	};

	logger << "Running core macro: resolving instructions\n";
	auto const resolved = MacroTargetResolver::resolve(macro, runtimeIds);
	if (!resolved.valid()) return fatal(*resolved.error);

	// Validate runtime mappings and CLI capabilities for the whole macro before
	// an earlier command can mutate the project.
		for (auto const& instruction : resolved.instructions) {
			switch (instruction.type) {
			case CoreMacroInstructionType::PrepareModel:
			case CoreMacroInstructionType::LoadModel:
			case CoreMacroInstructionType::DumpModel:
			case CoreMacroInstructionType::RunModel:
				if (project_.models().idToIndex(instruction.runtimeId) ==
				ModelCollection::InvalidIndex) {
				return fatal(
					"Resolved model '" + instruction.specificationId +
					"' is not present in the project. Instruction: " +
					instruction.source);
			}
			break;
		case CoreMacroInstructionType::RunProjection:
			if (project_.projections().idToIndex(instruction.runtimeId) ==
				ProjectionCollection::InvalidIndex) {
				return fatal(
					"Resolved projection '" + instruction.specificationId +
					"' is not present in the project. Instruction: " +
					instruction.source);
			}
			break;
				case CoreMacroInstructionType::RunSimulation:
				case CoreMacroInstructionType::SaveReport:
				case CoreMacroInstructionType::ExportReport:
					if (project_.simulations().idToIndex(instruction.runtimeId) ==
						SimulationCollection::InvalidIndex) {
				return fatal(
					"Resolved simulation '" + instruction.specificationId +
					"' is not present in the project. Instruction: " +
					instruction.source);
			}
				if (instruction.type ==
						CoreMacroInstructionType::RunSimulation &&
					project_.simulations().view(
						instruction.runtimeId).isLive()) {
				return fatal(
					"Unsupported operation: live simulation '" +
					instruction.specificationId +
					"' cannot be run by the command-line forecast runner. "
					"Only projection-based simulations are supported.");
			}
			break;
		}
	}

	logger << "Running core macro: validation successful, now executing\n";
	for (auto const& instruction : resolved.instructions) {
		std::vector<std::string> messages;
		Simulation const* completedSimulation = nullptr;
		auto captureFeedback = [&messages](std::string message) {
			if (!message.empty()) messages.push_back(std::move(message));
		};
		auto actionRequiredFeedback = [&](std::string message) {
			feedback(FeedbackType::ActionRequired, std::move(message));
		};
		auto failureMessage = [&]() {
			std::string message =
				"Error while executing core macro instruction - " +
				instruction.source;
			if (messages.empty()) {
				message +=
					"\nThe command reported failure without further details.";
			}
			else {
				for (auto const& detail : messages) {
					message += "\n" + detail;
				}
			}
			return message;
		};

		try {
			bool succeeded = false;
				logger << "Running core macro instruction: " <<
					instruction.source << '\n';
				switch (instruction.type) {
				case CoreMacroInstructionType::PrepareModel:
					succeeded = project_.models().access(
						instruction.runtimeId).prepareForRun(
							project_.paths(), captureFeedback);
					project_.invalidateProjectionsFromModel(
						instruction.runtimeId);
					break;
				case CoreMacroInstructionType::LoadModel:
					{
						auto& model = project_.models().access(
							instruction.runtimeId);
						auto const filename = project_.paths().resolveString(
							model.generatedDataCacheFilename());
						succeeded = model.loadGeneratedData(
							filename, captureFeedback);
						if (succeeded) {
							project_.invalidateProjectionsFromModel(
								instruction.runtimeId);
						}
						else if (messages.empty()) {
							messages.push_back(
								"Could not load " + filename + ".");
						}
					}
					break;
				case CoreMacroInstructionType::DumpModel:
					{
						auto const& model = project_.models().access(
							instruction.runtimeId);
						auto const filename = project_.paths().resolveString(
							model.generatedDataCacheFilename());
						succeeded = model.dumpGeneratedData(filename);
						if (!succeeded) {
							messages.push_back(
								"Could not write " + filename + ".");
						}
					}
					break;
				case CoreMacroInstructionType::RunModel:
					succeeded = project_.models().access(
					instruction.runtimeId).loadData(
						project_.paths(),
						captureFeedback,
						project_.config().getModelThreads());
				project_.invalidateProjectionsFromModel(instruction.runtimeId);
				break;
			case CoreMacroInstructionType::RunProjection:
				succeeded = project_.projections().run(
					instruction.runtimeId, captureFeedback);
				break;
				case CoreMacroInstructionType::RunSimulation:
					succeeded = project_.simulations().run(
					instruction.runtimeId,
					captureFeedback,
					actionRequiredFeedback);
				if (succeeded) {
					completedSimulation =
						&project_.simulations().view(instruction.runtimeId);
					}
					break;
					case CoreMacroInstructionType::SaveReport:
						project_.simulations().access(
							instruction.runtimeId).saveReport(
								instruction.argument);
						succeeded = true;
						break;
					case CoreMacroInstructionType::ExportReport:
						{
							auto const error =
								project_.simulations().exportReportByLabel(
									instruction.runtimeId,
									instruction.argument);
							succeeded = !error;
							if (error) messages.push_back(*error);
						}
						break;
					}
			if (!succeeded) return fatal(failureMessage());

			for (auto const& message : messages) {
				if (instruction.type ==
					CoreMacroInstructionType::RunSimulation ||
					message.starts_with("Warning:")) {
					feedback(FeedbackType::Warning, message);
				}
			}
			if (completedSimulation && simulationCompleted) {
				simulationCompleted(
					instruction.specificationId, *completedSimulation);
			}
		}
		catch (std::exception const& exception) {
			messages.push_back(exception.what());
			return fatal(failureMessage());
		}
		catch (...) {
			messages.push_back("An unknown exception occurred.");
			return fatal(failureMessage());
		}
	}
	return std::nullopt;
}
