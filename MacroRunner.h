#pragma once

#include <functional>
#include <optional>
#include <string>

class PollingProject;

class MacroRunner {
public:
	typedef std::function<void(std::string)> FeedbackFunc;

	MacroRunner(PollingProject& project);
	std::optional<std::string> run(std::string macro, FeedbackFunc feedback = [](std::string) {});
private:
	PollingProject& project_;
};