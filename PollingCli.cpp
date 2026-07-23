#include "CoreMacroRunner.h"
#include "CoreReportSummary.h"
#include "ForecastSpecificationIO.h"
#include "ForecastSpecificationProjectAdapter.h"
#include "Log.h"
#include "TerminalMacroFeedback.h"
#include "WorkspacePaths.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {
	struct Options {
		std::optional<std::string> term;
		std::optional<std::filesystem::path> forecast;
		std::optional<std::string> macro;
		bool showHelp = false;
		bool noLog = false;
	};

	void printUsage(std::ostream& output)
	{
		output <<
			"Usage:\n"
			"  polling-cli --term <election-code> --macro <instructions>\n"
			"  polling-cli --forecast <forecast.json> --macro <instructions>\n"
			"\n"
			"Options:\n"
			"  --term <code>       Election code. Uses "
			"forecasts/<code>/forecast.json by default.\n"
			"  --forecast <path>   Override the forecast manifest path. "
			"--term is optional.\n"
			"  --macro <commands>  Semicolon-delimited core macro commands. "
			"See README.md for the command grammar.\n"
			"  --no-log            Disable PALog.log output.\n"
			"  --help              Show this help text.\n";
	}

	std::optional<std::string> assignValue(
		int& argument,
		int argumentCount,
		char const* const arguments[],
		std::string_view option,
		std::optional<std::string>& destination)
	{
		if (destination) {
			return "Option " + std::string(option) +
				" may only be supplied once.";
		}
		if (argument + 1 >= argumentCount) {
			return "Option " + std::string(option) + " requires a value.";
		}
		destination = arguments[++argument];
		if (destination->empty()) {
			return "Option " + std::string(option) +
				" requires a non-empty value.";
		}
		return std::nullopt;
	}

	std::optional<std::string> parseOptions(
		int argumentCount,
		char const* const arguments[],
		Options& options)
	{
		std::optional<std::string> forecast;
		for (int argument = 1; argument < argumentCount; ++argument) {
			std::string_view const option(arguments[argument]);
			std::optional<std::string> error;
			if (option == "--help" || option == "-h") {
				options.showHelp = true;
			}
			else if (option == "--term") {
				error = assignValue(argument, argumentCount, arguments,
					option, options.term);
			}
			else if (option == "--forecast") {
				error = assignValue(argument, argumentCount, arguments,
					option, forecast);
			}
			else if (option == "--macro") {
				error = assignValue(argument, argumentCount, arguments,
					option, options.macro);
			}
			else if (option == "--no-log") {
				options.noLog = true;
			}
			else {
				return "Unknown argument: " + std::string(option);
			}
			if (error) return error;
		}

		if (forecast) options.forecast = *forecast;
		if (options.showHelp) return std::nullopt;
		if (!options.term && !options.forecast) {
			return "Either --term or --forecast must be supplied.";
		}
		if (!options.macro) return "Option --macro is required.";
		return std::nullopt;
	}

	void printDiagnostic(ForecastSpecificationDiagnostic const& diagnostic)
	{
		auto& output =
			diagnostic.severity ==
				ForecastSpecificationDiagnostic::Severity::Error ?
			std::cerr : std::cout;
		output <<
			(diagnostic.severity ==
				ForecastSpecificationDiagnostic::Severity::Error ?
				"Error" : "Warning") <<
			": " << diagnostic.location << ": " <<
			diagnostic.message << '\n';
	}

	int run(Options const& options)
	{
		auto const workspaceHint =
			options.forecast.value_or(std::filesystem::path());
		auto const workspacePaths = WorkspacePaths::discover(workspaceHint);
		std::filesystem::path forecastPath;
		if (options.forecast) {
			forecastPath = *options.forecast;
		}
		else {
			forecastPath = workspacePaths.resolve(
				std::filesystem::path("forecasts") / *options.term /
				"forecast.json");
		}

		auto loaded =
			loadForecastSpecification(forecastPath, workspacePaths.root());
		for (auto const& diagnostic : loaded.diagnostics) {
			printDiagnostic(diagnostic);
		}
		if (!loaded.valid()) return 1;

		if (options.term &&
			loaded.specification.electionCode != *options.term) {
			std::cerr <<
				"Error: forecast election code '" <<
				loaded.specification.electionCode <<
				"' does not match --term '" << *options.term << "'.\n";
			return 1;
		}

		auto constructed = ForecastSpecificationProjectAdapter::construct(
			loaded.specification, workspacePaths);
		for (auto const& diagnostic : constructed.diagnostics) {
			printDiagnostic(diagnostic);
		}
		if (!constructed.valid()) return 1;

		std::error_code pathError;
		auto displayPath =
			std::filesystem::absolute(forecastPath, pathError);
		if (pathError) displayPath = forecastPath;
		std::cout << "Election: " << loaded.specification.electionName <<
			" (" << loaded.specification.electionCode << ")\n";
		std::cout << "Forecast: " <<
			displayPath.lexically_normal().string() << '\n';
		std::cout << "Macro: " << *options.macro << '\n' << std::flush;

		auto const error = CoreMacroRunner(*constructed.project).run(
			*options.macro,
			constructed.runtimeIds,
			terminalMacroFeedback(std::cin, std::cout, std::cerr),
			[](std::string const& specificationId,
				Simulation const& simulation) {
				std::cout <<
					formatCoreReportSummary(
						specificationId, simulation) <<
					std::flush;
			});
		if (error) return 1;

		std::cout << "Macro completed successfully.\n";
		return 0;
	}
}

int main(int argumentCount, char const* const arguments[])
{
	Options options;
	if (auto const error = parseOptions(
		argumentCount, arguments, options)) {
		std::cerr << "Error: " << *error << "\n\n";
		printUsage(std::cerr);
		return 2;
	}
	if (options.noLog) logger.setEnabled(false);
	if (options.showHelp) {
		printUsage(std::cout);
		return 0;
	}

	try {
		return run(options);
	}
	catch (std::exception const& exception) {
		std::cerr << "Error: " << exception.what() << '\n';
		return 1;
	}
	catch (...) {
		std::cerr << "Error: an unknown exception occurred.\n";
		return 1;
	}
}
