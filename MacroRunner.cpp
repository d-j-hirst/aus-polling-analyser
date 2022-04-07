#include "MacroRunner.h"

#include "General.h"
#include "Log.h"
#include "PollingProject.h"

struct Instruction {
	enum class Type {
		None,
		CollectPolls,
		PrepareModel,
		RunModel,
		SetNowcast,
		RunProjection,
		RunSimulation
	};

	Instruction(Type type, int id) : type(type), id(id) {}

	Type type;
	int id;
};

MacroRunner::MacroRunner(PollingProject& project)
	: project_(project)
{
}

std::optional<std::string> MacroRunner::run(std::string macro, bool /*confirmId*/)
{
	logger << "Running macro: reading instructions\n";
	auto lines = splitString(macro, ";");
	std::vector<Instruction> instructions;
	for (auto const& line : lines) {
		auto parts = splitString(line, ":");
		if (parts.size() != 2) return "Error: not exactly 2 parts to this line - " + line;
		int id = -1;
		try {
			id = std::stoi(parts[1]);
		}
		catch (std::invalid_argument) {
			return "Error: 2nd part did not correspond to an integer - " + line;
		}
		if (id < 0) {
			return "Error: Invalid ID - " + line;
		}
		Instruction::Type type = Instruction::Type::None;
		if (parts[0] == "collect-polls") {
			type = Instruction::Type::CollectPolls;
		}
		else if (parts[0] == "prepare-model") {
			type = Instruction::Type::PrepareModel;
		}
		else if (parts[0] == "run-model") {
			type = Instruction::Type::RunModel;
		}
		else if (parts[0] == "set-nowcast") {
			type = Instruction::Type::SetNowcast;
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
		instructions.push_back(Instruction(type, id));
	}
	logger << "Running macro: reading instructions successful, now executing\n";
	for (auto instruction : instructions) {
		if (instruction.type == Instruction::Type::CollectPolls) {
			project_.polls().collectPolls();
		}
		else if (instruction.type == Instruction::Type::PrepareModel) {
			project_.models().access(instruction.id).prepareForRun([](std::string s) {});
		}
		else if (instruction.type == Instruction::Type::RunModel) {
			project_.models().access(instruction.id).loadData([](std::string s) {},
				project_.config().getModelThreads());
		}
		else if (instruction.type == Instruction::Type::SetNowcast) {
			project_.projections().access(instruction.id).setAsNowCast(project_.models());
		}
		else if (instruction.type == Instruction::Type::RunProjection) {
			project_.projections().run(instruction.id);
		}
		else if (instruction.type == Instruction::Type::RunSimulation) {
			project_.simulations().run(instruction.id);
		}
	}



	return std::optional<std::string>();
}
