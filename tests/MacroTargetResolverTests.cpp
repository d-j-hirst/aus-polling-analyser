#include "../MacroTargetResolver.h"

#include <iostream>
#include <string>

namespace {
	bool expect(bool condition, std::string const& description)
	{
		if (condition) return true;
		std::cerr << "FAILED: " << description << '\n';
		return false;
	}

	bool contains(std::optional<std::string> const& value,
		std::string const& text)
	{
		return value && value->find(text) != std::string::npos;
	}
}

int main()
{
	ForecastSpecificationRuntimeIds ids;
	ids.models = { { "standard-model", 11 } };
	ids.projections = {
		{ "projection-2028-election-projection", 21 }
	};
	ids.simulations = {
		{ "live-simulation", 31 },
		{ "now-cast-simulation", 32 },
		{ "standard-simulation", 33 }
	};

	bool valid = true;
	auto resolved = MacroTargetResolver::resolve(
		"run-model;run-projection;run-simulation:nowcast", ids);
	valid &= expect(resolved.valid(),
		"the documented macro should resolve");
	valid &= expect(resolved.instructions.size() == 3,
		"the documented macro should contain three instructions");
	if (resolved.instructions.size() == 3) {
		valid &= expect(resolved.instructions[0].runtimeId == 11,
			"an omitted unique model should resolve");
		valid &= expect(resolved.instructions[1].runtimeId == 21,
			"an omitted unique projection should resolve");
		valid &= expect(resolved.instructions[2].runtimeId == 32,
			"punctuation-stripped partial simulation matching should resolve");
		valid &= expect(
			resolved.instructions[2].specificationId ==
				"now-cast-simulation",
			"the resolved stable ID should be retained");
	}

	resolved = MacroTargetResolver::resolve(
		"run-model:STANDARD;run-projection:2028-ELECTION;"
		"run-simulation:NOW-CAST-SIMULATION",
		ids);
	valid &= expect(resolved.valid(),
		"matching should be case-insensitive");

	auto exactIds = ids;
	exactIds.simulations["nowcast"] = 34;
	resolved = MacroTargetResolver::resolve(
		"run-simulation:nowcast", exactIds);
	valid &= expect(resolved.valid() &&
		resolved.instructions.front().runtimeId == 34,
		"an exact ID should take precedence over partial matches");

	resolved = MacroTargetResolver::resolve(
		"run-simulation:simulation", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "live-simulation") &&
		contains(resolved.error, "now-cast-simulation") &&
		contains(resolved.error, "standard-simulation"),
		"ambiguous matches should list matching IDs");

	resolved = MacroTargetResolver::resolve(
		"run-simulation:missing", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "Available IDs:") &&
		contains(resolved.error, "live-simulation"),
		"missing matches should list available IDs");

	resolved = MacroTargetResolver::resolve(
		"run-simulation", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "explicit simulation target"),
		"simulations should always require an explicit target");

	auto multipleModels = ids;
	multipleModels.models["alternate-model"] = 12;
	resolved = MacroTargetResolver::resolve(
		"run-model;run-simulation:nowcast", multipleModels);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "alternate-model") &&
		contains(resolved.error, "standard-model"),
		"an omitted model should fail when multiple models exist");

	resolved = MacroTargetResolver::resolve(
		"run-model;run-projection;run-simulation:missing", ids);
	valid &= expect(!resolved.valid() && resolved.instructions.empty(),
		"a later invalid target should prevent the entire macro executing");

	resolved = MacroTargetResolver::resolve(
		"prepare-model;dump-model;load-model;run-model", ids);
	valid &= expect(resolved.valid() &&
		resolved.instructions.size() == 4 &&
		resolved.instructions[0].type ==
			CoreMacroInstructionType::PrepareModel &&
		resolved.instructions[1].type ==
			CoreMacroInstructionType::DumpModel &&
		resolved.instructions[2].type ==
			CoreMacroInstructionType::LoadModel &&
		resolved.instructions[3].type ==
			CoreMacroInstructionType::RunModel,
		"model preparation and cache commands should resolve the unique model");

	resolved = MacroTargetResolver::resolve(
		"save-report:nowcast: Updated forecast: evening run ", ids);
	valid &= expect(resolved.valid() &&
		resolved.instructions.size() == 1 &&
		resolved.instructions[0].type ==
			CoreMacroInstructionType::SaveReport &&
		resolved.instructions[0].runtimeId == 32 &&
		resolved.instructions[0].argument ==
			"Updated forecast: evening run",
		"save-report should resolve its simulation and retain the report label");

	resolved = MacroTargetResolver::resolve(
		"save-report:nowcast", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "requires both a simulation target"),
		"save-report should require a report label");

	resolved = MacroTargetResolver::resolve(
		"save-report:nowcast:   ", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "Report label cannot be empty"),
		"save-report should reject a blank report label");

	resolved = MacroTargetResolver::resolve(
		"export-report:nowcast: Updated forecast: evening run ", ids);
	valid &= expect(resolved.valid() &&
		resolved.instructions.size() == 1 &&
		resolved.instructions[0].type ==
			CoreMacroInstructionType::ExportReport &&
		resolved.instructions[0].runtimeId == 32 &&
		resolved.instructions[0].argument ==
			"Updated forecast: evening run",
		"export-report should resolve a simulation and retain the saved label");

	resolved = MacroTargetResolver::resolve("collect-polls", ids);
	valid &= expect(!resolved.valid() &&
		contains(resolved.error, "Supported instructions: prepare-model") &&
		contains(resolved.error, "save-report") &&
		contains(resolved.error, "export-report"),
		"non-core commands should be rejected and list the core grammar");

	return valid ? 0 : 1;
}
