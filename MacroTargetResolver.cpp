#include "MacroTargetResolver.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
	enum class TargetType {
		Model,
		Projection,
		Simulation,
	};

	struct ParsedCommand {
		CoreMacroInstructionType type;
		TargetType targetType;
		bool targetRequired;
		bool argumentRequired;
	};

	std::string trim(std::string_view value)
	{
		auto const first = std::find_if_not(value.begin(), value.end(),
			[](unsigned char character) { return std::isspace(character); });
		auto const last = std::find_if_not(value.rbegin(), value.rend(),
			[](unsigned char character) { return std::isspace(character); }).base();
		if (first >= last) return {};
		return std::string(first, last);
	}

	std::string lowerCase(std::string_view value)
	{
		std::string lowered;
		lowered.reserve(value.size());
		for (unsigned char character : value) {
			lowered.push_back(static_cast<char>(std::tolower(character)));
		}
		return lowered;
	}

	std::string stripPunctuation(std::string_view value)
	{
		std::string stripped;
		stripped.reserve(value.size());
		for (unsigned char character : value) {
			if (std::isalnum(character)) {
				stripped.push_back(static_cast<char>(std::tolower(character)));
			}
		}
		return stripped;
	}

	std::string joinIds(std::vector<std::string> const& ids)
	{
		if (ids.empty()) return "(none)";
		std::ostringstream output;
		for (std::size_t index = 0; index < ids.size(); ++index) {
			if (index) output << ", ";
			output << ids[index];
		}
		return output.str();
	}

	std::vector<std::string> allIds(std::map<std::string, int> const& ids)
	{
		std::vector<std::string> result;
		result.reserve(ids.size());
		for (auto const& [id, runtimeId] : ids) {
			static_cast<void>(runtimeId);
			result.push_back(id);
		}
		return result;
	}

	std::optional<ParsedCommand> parseCommand(std::string const& command)
	{
		if (command == "run-model") {
			return ParsedCommand{
				CoreMacroInstructionType::RunModel, TargetType::Model, false,
				false };
		}
		if (command == "prepare-model") {
			return ParsedCommand{
				CoreMacroInstructionType::PrepareModel, TargetType::Model, false,
				false };
		}
		if (command == "load-model") {
			return ParsedCommand{
				CoreMacroInstructionType::LoadModel, TargetType::Model, false,
				false };
		}
		if (command == "dump-model") {
			return ParsedCommand{
				CoreMacroInstructionType::DumpModel, TargetType::Model, false,
				false };
		}
		if (command == "run-projection") {
			return ParsedCommand{
				CoreMacroInstructionType::RunProjection, TargetType::Projection,
				false, false };
		}
		if (command == "run-simulation") {
			return ParsedCommand{
				CoreMacroInstructionType::RunSimulation, TargetType::Simulation,
				true, false };
		}
		if (command == "save-report") {
			return ParsedCommand{
				CoreMacroInstructionType::SaveReport, TargetType::Simulation,
				true, true };
		}
		if (command == "export-report") {
			return ParsedCommand{
				CoreMacroInstructionType::ExportReport, TargetType::Simulation,
				true, true };
		}
		return std::nullopt;
	}

	std::map<std::string, int> const& idsFor(
		TargetType type,
		ForecastSpecificationRuntimeIds const& runtimeIds)
	{
		switch (type) {
		case TargetType::Model:
			return runtimeIds.models;
		case TargetType::Projection:
			return runtimeIds.projections;
		case TargetType::Simulation:
			return runtimeIds.simulations;
		}
		static std::map<std::string, int> const empty;
		return empty;
	}

	std::string targetName(TargetType type)
	{
		switch (type) {
		case TargetType::Model:
			return "model";
		case TargetType::Projection:
			return "projection";
		case TargetType::Simulation:
			return "simulation";
		}
		return "instruction";
	}

	struct TargetResolution {
		int runtimeId = -1;
		std::string specificationId;
		std::optional<std::string> error;
	};

	TargetResolution resolveTarget(
		std::string const& target,
		bool targetRequired,
		TargetType type,
		std::map<std::string, int> const& ids)
	{
		auto const available = allIds(ids);
		auto const kind = targetName(type);
		if (target.empty()) {
			if (!targetRequired && ids.size() == 1) {
				auto const& [id, runtimeId] = *ids.begin();
				return { runtimeId, id, std::nullopt };
			}
			std::string message = targetRequired ?
				"An explicit " + kind + " target is required." :
				"The " + kind + " target may be omitted only when exactly one " +
				kind + " is configured.";
			message += " Available " + kind + " IDs: " + joinIds(available);
			return { -1, {}, std::move(message) };
		}

		auto const loweredTarget = lowerCase(target);
		std::vector<std::pair<std::string, int>> exactMatches;
		for (auto const& [id, runtimeId] : ids) {
			if (lowerCase(id) == loweredTarget) {
				exactMatches.emplace_back(id, runtimeId);
			}
		}
		if (exactMatches.size() == 1) {
			return {
				exactMatches.front().second,
				exactMatches.front().first,
				std::nullopt
			};
		}
		if (exactMatches.size() > 1) {
			std::vector<std::string> matchingIds;
			for (auto const& [id, runtimeId] : exactMatches) {
				static_cast<void>(runtimeId);
				matchingIds.push_back(id);
			}
			return { -1, {},
				"Ambiguous " + kind + " target '" + target +
				"'. Matching IDs: " + joinIds(matchingIds) };
		}

		auto const strippedTarget = stripPunctuation(target);
		std::vector<std::pair<std::string, int>> partialMatches;
		for (auto const& [id, runtimeId] : ids) {
			bool const originalMatch =
				lowerCase(id).find(loweredTarget) != std::string::npos;
			bool const strippedMatch = !strippedTarget.empty() &&
				stripPunctuation(id).find(strippedTarget) != std::string::npos;
			if (originalMatch || strippedMatch) {
				partialMatches.emplace_back(id, runtimeId);
			}
		}
		if (partialMatches.size() == 1) {
			return {
				partialMatches.front().second,
				partialMatches.front().first,
				std::nullopt
			};
		}
		if (partialMatches.size() > 1) {
			std::vector<std::string> matchingIds;
			for (auto const& [id, runtimeId] : partialMatches) {
				static_cast<void>(runtimeId);
				matchingIds.push_back(id);
			}
			return { -1, {},
				"Ambiguous " + kind + " target '" + target +
				"'. Matching IDs: " + joinIds(matchingIds) };
		}
		return { -1, {},
			"No " + kind + " ID matches '" + target +
			"'. Available IDs: " + joinIds(available) };
	}
}

MacroTargetResolution MacroTargetResolver::resolve(
	std::string const& macro,
	ForecastSpecificationRuntimeIds const& runtimeIds)
{
	MacroTargetResolution result;
	std::size_t instructionStart = 0;
	while (instructionStart <= macro.size()) {
		auto const separator = macro.find(';', instructionStart);
		auto const instructionEnd =
			separator == std::string::npos ? macro.size() : separator;
		auto const source = trim(std::string_view(macro).substr(
			instructionStart, instructionEnd - instructionStart));
		if (source.empty()) {
			result.error = "Macro contains an empty instruction.";
			result.instructions.clear();
			return result;
		}

			auto const colon = source.find(':');
			auto const command = trim(std::string_view(source).substr(0, colon));
			auto const parsed = parseCommand(command);
			if (!parsed) {
				result.error =
					"Unsupported core macro instruction '" + command +
					"'. Supported instructions: prepare-model, load-model, "
					"dump-model, run-model, run-projection, run-simulation, "
					"save-report, export-report. Instruction: " + source;
				result.instructions.clear();
				return result;
			}

			auto const argumentSeparator = colon == std::string::npos ?
				std::string::npos : source.find(':', colon + 1);
			if (!parsed->argumentRequired &&
				argumentSeparator != std::string::npos) {
				result.error =
					"Macro instruction contains more than one ':' separator: " +
					source;
				result.instructions.clear();
				return result;
			}
			auto const targetEnd = argumentSeparator == std::string::npos ?
				source.size() : argumentSeparator;
			auto const target = colon == std::string::npos ?
				std::string{} :
				trim(std::string_view(source).substr(
					colon + 1, targetEnd - colon - 1));
			std::string argument;
			if (parsed->argumentRequired) {
				if (argumentSeparator == std::string::npos) {
					result.error =
						"Instruction requires both a simulation target and a "
						"report label: " + source;
					result.instructions.clear();
					return result;
				}
				argument = trim(
					std::string_view(source).substr(argumentSeparator + 1));
				if (argument.empty()) {
					result.error =
						"Report label cannot be empty. Instruction: " + source;
					result.instructions.clear();
					return result;
				}
			}

			auto const resolved = resolveTarget(
				target,
				parsed->targetRequired,
			parsed->targetType,
			idsFor(parsed->targetType, runtimeIds));
		if (resolved.error) {
			result.error = *resolved.error + " Instruction: " + source;
			result.instructions.clear();
			return result;
		}
		result.instructions.push_back({
			parsed->type,
				resolved.runtimeId,
				source,
				resolved.specificationId,
				std::move(argument)
			});

		if (separator == std::string::npos) break;
		instructionStart = separator + 1;
	}
	return result;
}
