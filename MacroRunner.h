#pragma once

#include <functional>
#include <optional>
#include <string>

class PollingProject;

class MacroRunner {
public:
	enum class FeedbackType {
		Fatal,
		ActionRequired,
		Warning
	};
	using FeedbackFunc =
		std::function<void(FeedbackType, std::string)>;

	MacroRunner(PollingProject& project);
	// Runs semicolon-delimited command:id instructions. Returns the first error,
	// or no value after every instruction succeeds.
	std::optional<std::string> run(
		std::string const& macro,
		FeedbackFunc feedback = [](FeedbackType, std::string) {});
private:
	PollingProject& project_;
};
