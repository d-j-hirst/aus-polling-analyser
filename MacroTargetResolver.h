#pragma once

#include "ForecastSpecificationRuntimeIds.h"

#include <optional>
#include <string>
#include <vector>

enum class CoreMacroInstructionType {
	PrepareModel,
	LoadModel,
	DumpModel,
	RunModel,
	RunProjection,
	RunSimulation,
	SaveReport,
	ExportReport,
};

struct ResolvedCoreMacroInstruction {
	CoreMacroInstructionType type = CoreMacroInstructionType::RunModel;
	int runtimeId = -1;
	std::string source;
	std::string specificationId;
	std::string argument;
};

struct MacroTargetResolution {
	std::vector<ResolvedCoreMacroInstruction> instructions;
	std::optional<std::string> error;

	bool valid() const { return !error.has_value(); }
};

// Resolves portable forecast-specification IDs for the core forecast commands.
// GUI-only project-management commands deliberately are not part of this
// grammar.
class MacroTargetResolver {
public:
	static MacroTargetResolution resolve(
		std::string const& macro,
		ForecastSpecificationRuntimeIds const& runtimeIds);
};
