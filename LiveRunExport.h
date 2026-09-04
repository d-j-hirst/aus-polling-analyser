#pragma once

#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class PollingProject;
class Simulation;
class SimulationRun;

namespace LiveRunExport {

using FeedbackFunc = std::function<void(std::string)>;
using ActionRequiredFunc = std::function<void(std::string)>;

// Empty string is valid and means export is disabled. Returns an error
// message if the configured folder component is invalid.
inline std::optional<std::string> validateOutputFolder(std::string_view folder)
{
	if (folder.empty()) return std::nullopt;
	bool onlyDots = true;
	for (char character : folder) onlyDots = onlyDots && character == '.';
	if (onlyDots) {
		return "Live diagnostic output folder cannot be \"" +
			std::string(folder) + "\".";
	}
	for (unsigned char character : folder) {
		if (character == '/' || character == '\\') {
			return "Live diagnostic output folder must be a single folder "
				"name, not a path.";
		}
		if (character < 32) {
			return "Live diagnostic output folder contains invalid characters.";
		}
		if (std::string_view("<>:\"|?*").find(character) !=
			std::string_view::npos) {
			return "Live diagnostic output folder contains characters that are "
				"invalid on Windows.";
		}
	}
	if (folder.front() == ' ' || folder.back() == ' ') {
		return "Live diagnostic output folder cannot start or end with a space.";
	}
	if (folder.back() == '.') {
		return "Live diagnostic output folder cannot end with a period.";
	}
	std::string windowsBaseName;
	for (unsigned char character : folder) {
		if (character == '.') break;
		windowsBaseName.push_back(char(std::toupper(character)));
	}
	bool const reservedWindowsName =
		windowsBaseName == "CON" || windowsBaseName == "PRN" ||
		windowsBaseName == "AUX" || windowsBaseName == "NUL" ||
		(windowsBaseName.size() == 4 &&
			(windowsBaseName.substr(0, 3) == "COM" ||
			 windowsBaseName.substr(0, 3) == "LPT") &&
			windowsBaseName[3] >= '1' && windowsBaseName[3] <= '9');
	if (reservedWindowsName) {
		return "Live diagnostic output folder uses a reserved Windows name.";
	}
	return std::nullopt;
}

void exportCompletedAutomaticLiveRun(
	PollingProject& project,
	Simulation const& sim,
	SimulationRun const& run,
	int iterations,
	FeedbackFunc feedback,
	ActionRequiredFunc actionRequired);

}
