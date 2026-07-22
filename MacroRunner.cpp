#include "MacroRunner.h"

#include "Beep.h"
#include "General.h"
#include "Log.h"
#include "PollingProject.h"

#include <cctype>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
	struct Instruction {
		enum class Type {
			None,
			CollectPolls,
			DumpModel,
			LoadModel,
			PrepareModel,
			RunModel,
			RunProjection,
			RunSimulation
		};

		Instruction(Type type, int id, std::string source) :
			type(type), id(id), source(std::move(source)) {}

		Type type;
		int id;
		std::string source;
	};

	std::string modelCacheFilename(StanModel const& model)
	{
		auto const termCode = model.getTermCode();
		if (termCode.empty()) {
			throw std::logic_error(
				"The model needs an election term code before its cache can be used.");
		}
		for (unsigned char character : termCode) {
			if (!std::isalnum(character) && character != '-' && character != '_') {
				throw std::logic_error(
					"The model term code contains a character that cannot be used "
					"in a cache filename: " + termCode);
			}
		}
		return "model-" + termCode + ".bin";
	}
}

MacroRunner::MacroRunner(PollingProject& project)
	: project_(project)
{
}

std::optional<std::string> MacroRunner::run(
	std::string const& macro, MacroRunner::FeedbackFunc feedback)
{
	if (!feedback) feedback = [](std::string) {};
	logger << "Running macro: reading instructions\n";
	auto lines = splitString(macro, ";");
	std::vector<Instruction> instructions;
	for (auto const& line : lines) {
		auto parts = splitString(line, ":");
		if (parts.size() != 2) return "Error: not exactly 2 parts to this line - " + line;
		int id = -1;
		size_t parsedLength = 0;
		try {
			id = std::stoi(parts[1], &parsedLength);
		}
		catch (std::invalid_argument const&) {
			return "Error: 2nd part did not correspond to an integer - " + line;
		}
		catch (std::out_of_range const&) {
			return "Error: ID was outside the supported integer range - " + line;
		}
		if (parsedLength != parts[1].size()) {
			return "Error: 2nd part did not correspond to an integer - " + line;
		}
		if (id < 0) {
			return "Error: Invalid ID - " + line;
		}
		Instruction::Type type = Instruction::Type::None;
		if (parts[0] == "collect-polls") {
			type = Instruction::Type::CollectPolls;
		}
		else if (parts[0] == "dump-model") {
			type = Instruction::Type::DumpModel;
		}
		else if (parts[0] == "load-model") {
			type = Instruction::Type::LoadModel;
		}
		else if (parts[0] == "prepare-model") {
			type = Instruction::Type::PrepareModel;
		}
		else if (parts[0] == "run-model") {
			type = Instruction::Type::RunModel;
		}
		else if (parts[0] == "set-nowcast") {
			return
				"Error: set-nowcast is obsolete. Use a full-election base "
				"projection and set the simulation's Forecast/report mode to "
				"Nowcast - " + line;
		}
		else if (parts[0] == "run-projection") {
			type = Instruction::Type::RunProjection;
		}
		else if (parts[0] == "run-simulation") {
			type = Instruction::Type::RunSimulation;
		}
		if (type == Instruction::Type::None) {
			return "Error: Invalid instruction - " + line;
		}
		instructions.emplace_back(type, id, line);
	}

	// Validate every reference before executing anything so a later typo cannot
	// leave the project after only part of the macro has run.
	for (auto const& instruction : instructions) {
		bool const needsModel =
			instruction.type == Instruction::Type::DumpModel ||
			instruction.type == Instruction::Type::LoadModel ||
			instruction.type == Instruction::Type::PrepareModel ||
			instruction.type == Instruction::Type::RunModel;
		bool const needsProjection =
			instruction.type == Instruction::Type::RunProjection;
		bool const needsSimulation =
			instruction.type == Instruction::Type::RunSimulation;
		if (needsModel && project_.models().idToIndex(instruction.id) ==
			ModelCollection::InvalidIndex) {
			return "Error: Model ID does not exist - " + instruction.source;
		}
		if (needsProjection && project_.projections().idToIndex(instruction.id) ==
			ProjectionCollection::InvalidIndex) {
			return "Error: Projection ID does not exist - " + instruction.source;
		}
		if (needsSimulation && project_.simulations().idToIndex(instruction.id) ==
			SimulationCollection::InvalidIndex) {
			return "Error: Simulation ID does not exist - " + instruction.source;
		}
	}

	logger << "Running macro: reading instructions successful, now executing\n";
	for (auto const& instruction : instructions) {
		std::vector<std::string> messages;
		auto captureFeedback = [&messages](std::string message) {
			messages.push_back(std::move(message));
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
				// Poll collection has no target, but retains the conventional :0 suffix
				// so all macro instructions continue to use the same grammar.
				succeeded = project_.polls().collectPolls(
					[](std::string, std::string defaultValue) {
						return defaultValue;
					},
					captureFeedback);
				break;
			case Instruction::Type::DumpModel:
				{
					auto const& model = project_.models().access(instruction.id);
					auto const filename = project_.paths().resolveString(
						modelCacheFilename(model));
					succeeded = model.dumpGeneratedData(filename);
					if (!succeeded) {
						messages.push_back("Could not write " + filename + ".");
					}
				}
				break;
			case Instruction::Type::LoadModel:
				{
					auto& model = project_.models().access(instruction.id);
					auto const filename = project_.paths().resolveString(
						modelCacheFilename(model));
					succeeded = model.loadGeneratedData(
						filename, captureFeedback);
					if (succeeded) {
						project_.invalidateProjectionsFromModel(instruction.id);
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
				succeeded = project_.models().access(instruction.id).loadData(
					project_.paths(), captureFeedback,
					project_.config().getModelThreads());
				project_.invalidateProjectionsFromModel(instruction.id);
				break;
			case Instruction::Type::RunProjection:
				succeeded = project_.projections().run(
					instruction.id, captureFeedback);
				break;
			case Instruction::Type::RunSimulation:
				// Simulation feedback can be interactive, such as asking the user to
				// close an output file before retrying, so it must not be deferred.
				succeeded = project_.simulations().run(instruction.id, feedback);
				break;
			case Instruction::Type::None:
				succeeded = false;
				messages.push_back("The parsed instruction had no executable type.");
				break;
			}
			if (!succeeded) return failureMessage();
		}
		catch (std::exception const& e) {
			messages.push_back(e.what());
			return failureMessage();
		}
		catch (...) {
			messages.push_back("An unknown exception occurred.");
			return failureMessage();
		}
	}

	if (project_.config().getBeepOnCompletion()) beep();

	return std::nullopt;
}
