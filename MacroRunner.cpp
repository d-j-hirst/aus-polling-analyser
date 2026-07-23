#include "MacroRunner.h"

#include "Beep.h"
#include "General.h"
#include "Log.h"
#include "PollingProject.h"

#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
	struct Instruction {
		enum class Type {
			CollectPolls,
			DumpModel,
			LoadModel,
			PrepareModel,
			RunModel,
			RunProjection,
			RunSimulation,
			SaveReport,
			ExportReport,
		};

		Type type;
		int id;
		std::string source;
		std::string argument;
	};

	struct LegacyMacroResolution {
		std::vector<Instruction> instructions;
		std::optional<std::string> error;
	};

	std::string trim(std::string const& value)
	{
		auto const first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return {};
		auto const last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	LegacyMacroResolution resolveLegacyNumericTargets(
		std::string const& macro)
	{
		LegacyMacroResolution result;
		auto lines = splitString(macro, ";");
		for (auto const& line : lines) {
			auto const firstSeparator = line.find(':');
			auto const command = firstSeparator == std::string::npos ?
				line : line.substr(0, firstSeparator);
			auto const secondSeparator = firstSeparator == std::string::npos ?
				std::string::npos : line.find(':', firstSeparator + 1);
			bool const hasReportLabel =
				command == "save-report" || command == "export-report";
			if (firstSeparator == std::string::npos ||
				(!hasReportLabel && secondSeparator != std::string::npos) ||
				(hasReportLabel && secondSeparator == std::string::npos)) {
				result.error =
					hasReportLabel ?
						"Error: report command requires a simulation ID and label - " +
							line :
						"Error: not exactly 2 parts to this line - " + line;
				result.instructions.clear();
				return result;
			}
			auto const idText = line.substr(
				firstSeparator + 1,
				(secondSeparator == std::string::npos ?
					line.size() : secondSeparator) -
					firstSeparator - 1);
			auto const argument = hasReportLabel ?
				trim(line.substr(secondSeparator + 1)) : std::string{};
			if (hasReportLabel && argument.empty()) {
				result.error =
					"Error: report label cannot be empty - " + line;
				result.instructions.clear();
				return result;
			}
			int id = -1;
			size_t parsedLength = 0;
			try {
				id = std::stoi(idText, &parsedLength);
			}
			catch (std::invalid_argument const&) {
				result.error =
					"Error: 2nd part did not correspond to an integer - " +
					line;
				result.instructions.clear();
				return result;
			}
			catch (std::out_of_range const&) {
				result.error =
					"Error: ID was outside the supported integer range - " +
					line;
				result.instructions.clear();
				return result;
			}
			if (parsedLength != idText.size()) {
				result.error =
					"Error: 2nd part did not correspond to an integer - " +
					line;
				result.instructions.clear();
				return result;
			}
			if (id < 0) {
				result.error = "Error: Invalid ID - " + line;
				result.instructions.clear();
				return result;
			}

			std::optional<Instruction::Type> type;
			if (command == "collect-polls") {
				type = Instruction::Type::CollectPolls;
			}
			else if (command == "dump-model") {
				type = Instruction::Type::DumpModel;
			}
			else if (command == "load-model") {
				type = Instruction::Type::LoadModel;
			}
			else if (command == "prepare-model") {
				type = Instruction::Type::PrepareModel;
			}
			else if (command == "run-model") {
				type = Instruction::Type::RunModel;
			}
			else if (command == "set-nowcast") {
				result.error =
					"Error: set-nowcast is obsolete. Use a full-election base "
					"projection and set the simulation's Forecast/report mode "
					"to Nowcast - " + line;
				result.instructions.clear();
				return result;
			}
			else if (command == "run-projection") {
				type = Instruction::Type::RunProjection;
			}
			else if (command == "run-simulation") {
				type = Instruction::Type::RunSimulation;
			}
			else if (command == "save-report") {
				type = Instruction::Type::SaveReport;
			}
			else if (command == "export-report") {
				type = Instruction::Type::ExportReport;
			}
			if (!type) {
				result.error = "Error: Invalid instruction - " + line;
				result.instructions.clear();
				return result;
			}
			result.instructions.push_back({ *type, id, line, argument });
		}
		return result;
	}
}

MacroRunner::MacroRunner(PollingProject& project)
	: project_(project)
{
}

std::optional<std::string> MacroRunner::run(
	std::string const& macro,
	MacroRunner::FeedbackFunc feedback)
{
	if (!feedback) feedback = [](FeedbackType, std::string) {};
	auto fatal = [&](std::string message) {
		feedback(FeedbackType::Fatal, message);
		return std::optional<std::string>(std::move(message));
	};
	logger << "Running macro: reading instructions\n";
	auto resolved = resolveLegacyNumericTargets(macro);
	if (resolved.error) {
		return fatal(*resolved.error);
	}
	// Validate every reference before executing anything so a later typo cannot
	// leave the project after only part of the macro has run.
	for (auto const& instruction : resolved.instructions) {
		bool const needsModel =
			instruction.type == Instruction::Type::DumpModel ||
			instruction.type == Instruction::Type::LoadModel ||
			instruction.type == Instruction::Type::PrepareModel ||
			instruction.type == Instruction::Type::RunModel;
		bool const needsProjection =
			instruction.type == Instruction::Type::RunProjection;
			bool const needsSimulation =
				instruction.type == Instruction::Type::RunSimulation ||
				instruction.type == Instruction::Type::SaveReport ||
				instruction.type == Instruction::Type::ExportReport;
		if (needsModel && project_.models().idToIndex(instruction.id) ==
			ModelCollection::InvalidIndex) {
			return fatal("Error: Model ID does not exist - " + instruction.source);
		}
		if (needsProjection &&
			project_.projections().idToIndex(instruction.id) ==
			ProjectionCollection::InvalidIndex) {
			return fatal("Error: Projection ID does not exist - " + instruction.source);
		}
		if (needsSimulation &&
			project_.simulations().idToIndex(instruction.id) ==
			SimulationCollection::InvalidIndex) {
			return fatal("Error: Simulation ID does not exist - " + instruction.source);
		}
	}

	logger << "Running macro: reading instructions successful, now executing\n";
	for (auto const& instruction : resolved.instructions) {
		std::vector<std::string> messages;
		auto captureFeedback = [&messages](std::string message) {
			if (!message.empty()) messages.push_back(std::move(message));
		};
		auto captureSimulationFeedback = [&](std::string message) {
			if (message.empty()) return;
			messages.push_back(std::move(message));
		};
		auto actionRequiredFeedback = [&](std::string message) {
			feedback(FeedbackType::ActionRequired, std::move(message));
		};
		auto failureMessage = [&]() {
			std::string message = "Error while executing macro instruction - " +
				instruction.source;
			if (messages.empty()) {
				message += "\nThe command reported failure without further details.";
			}
			else {
				for (auto const& detail : messages) message += "\n" + detail;
			}
			logger << message << "\n";
			return message;
		};

		try {
			bool succeeded = true;
			logger << "Running macro instruction: " << instruction.source << "\n";
			switch (instruction.type) {
			case Instruction::Type::CollectPolls:
				// Poll collection has no target, but retains the conventional :0
				// suffix so legacy instructions keep a consistent grammar.
				succeeded = project_.polls().collectPolls(
					[](std::string, std::string defaultValue) {
						return defaultValue;
					},
					captureFeedback);
				break;
				case Instruction::Type::DumpModel:
					{
						auto const& model =
							project_.models().access(instruction.id);
						auto const filename = project_.paths().resolveString(
							model.generatedDataCacheFilename());
						succeeded = model.dumpGeneratedData(filename);
					if (!succeeded) {
						messages.push_back("Could not write " + filename + ".");
					}
				}
				break;
				case Instruction::Type::LoadModel:
					{
						auto& model =
							project_.models().access(instruction.id);
						auto const filename = project_.paths().resolveString(
							model.generatedDataCacheFilename());
					succeeded = model.loadGeneratedData(
						filename, captureFeedback);
					if (succeeded) {
						project_.invalidateProjectionsFromModel(
							instruction.id);
					}
					else if (messages.empty()) {
						messages.push_back("Could not load " + filename + ".");
					}
				}
				break;
			case Instruction::Type::PrepareModel:
				succeeded = project_.models().access(instruction.id)
					.prepareForRun(project_.paths(), captureFeedback);
				project_.invalidateProjectionsFromModel(instruction.id);
				break;
			case Instruction::Type::RunModel:
				succeeded = project_.models().access(
					instruction.id).loadData(
					project_.paths(), captureFeedback,
					project_.config().getModelThreads());
				project_.invalidateProjectionsFromModel(instruction.id);
				break;
				case Instruction::Type::RunProjection:
					succeeded = project_.projections().run(
						instruction.id, captureFeedback);
					break;
				case Instruction::Type::RunSimulation:
					succeeded = project_.simulations().run(
						instruction.id, captureSimulationFeedback,
						actionRequiredFeedback);
					break;
				case Instruction::Type::SaveReport:
					project_.simulations().access(
						instruction.id).saveReport(instruction.argument);
					break;
				case Instruction::Type::ExportReport:
					{
						auto const error =
							project_.simulations().exportReportByLabel(
								instruction.id, instruction.argument);
						succeeded = !error;
						if (error) messages.push_back(*error);
					}
					break;
				}
			if (!succeeded) return fatal(failureMessage());

			// Most component feedback describes a failed operation and is consumed by
			// failureMessage(). Successful simulation feedback consists of warnings or
			// live-result notices; explicit "Warning:" messages from other commands are
			// warnings as well.
			for (auto const& message : messages) {
				if (instruction.type == Instruction::Type::RunSimulation ||
					message.starts_with("Warning:")) {
					feedback(FeedbackType::Warning, message);
				}
			}
		}
		catch (std::exception const& e) {
			messages.push_back(e.what());
			return fatal(failureMessage());
		}
		catch (...) {
			messages.push_back("An unknown exception occurred.");
			return fatal(failureMessage());
		}
	}

	if (project_.config().getBeepOnCompletion()) beep();

	return std::nullopt;
}
