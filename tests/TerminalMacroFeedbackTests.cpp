#include "../TerminalMacroFeedback.h"

#include <iostream>
#include <sstream>
#include <string>

namespace {
	bool expect(
		bool condition,
		std::string const& description)
	{
		if (condition) return true;
		std::cerr << "FAILED: " << description << '\n';
		return false;
	}
}

int main()
{
	std::istringstream input("\n");
	std::ostringstream output;
	std::ostringstream errors;
	auto feedback = terminalMacroFeedback(input, output, errors);

	feedback(MacroFeedbackType::Warning, "informational issue");
	feedback(MacroFeedbackType::Warning, "Warning: already prefixed");
	feedback(MacroFeedbackType::ActionRequired, "make the file available");
	feedback(MacroFeedbackType::Fatal, "operation failed");

	bool valid = true;
	valid &= expect(
		output.str() ==
			"Warning: informational issue\n"
			"Warning: already prefixed\n"
			"Action required: make the file available\n"
			"Press Enter to continue...\n",
		"warning and action-required terminal output should be stable");
	valid &= expect(
		errors.str() == "Error: operation failed\n",
		"fatal feedback should be written to the error stream");
	return valid ? 0 : 1;
}
