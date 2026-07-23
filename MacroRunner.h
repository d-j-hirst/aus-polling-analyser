#pragma once

#include "MacroFeedback.h"

#include <optional>
#include <string>

class PollingProject;

class MacroRunner {
public:
	using FeedbackType = MacroFeedbackType;
	using FeedbackFunc = MacroFeedbackFunc;

	MacroRunner(PollingProject& project);
	// Runs semicolon-delimited command:id instructions. Returns the first error,
	// or no value after every instruction succeeds.
	std::optional<std::string> run(
		std::string const& macro,
		FeedbackFunc feedback = [](FeedbackType, std::string) {});
private:
	PollingProject& project_;
};
