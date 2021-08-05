#pragma once

#include <optional>
#include <string>

class PollingProject;

class MacroRunner {
public:
	MacroRunner(PollingProject& project);
	std::optional<std::string> run(std::string macro, bool confirmId);
private:
	PollingProject& project_;
};