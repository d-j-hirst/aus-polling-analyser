#pragma once

#include <functional>
#include <optional>
#include <string>

class PollingProject;

class MacroRunner {
public:
	typedef std::function<void(std::string)> FeedbackFunc;

	MacroRunner(PollingProject& project);
	// Runs semicolon-delimited command:id instructions. Returns the first error,
	// or no value after every instruction succeeds.
	std::optional<std::string> run(
		std::string const& macro,
		FeedbackFunc feedback = [](std::string) {});
private:
	PollingProject& project_;
};
