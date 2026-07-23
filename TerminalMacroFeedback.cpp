#include "TerminalMacroFeedback.h"

#include <iostream>
#include <string>
#include <utility>

MacroFeedbackFunc terminalMacroFeedback(
	std::istream& input,
	std::ostream& output,
	std::ostream& errorOutput)
{
	return [&input, &output, &errorOutput](
		MacroFeedbackType type,
		std::string message) {
		switch (type) {
		case MacroFeedbackType::Fatal:
			errorOutput << "Error: " << message << '\n';
			break;
		case MacroFeedbackType::Warning:
			if (message.rfind("Warning:", 0) == 0) {
				output << message << '\n';
			}
			else {
				output << "Warning: " << message << '\n';
			}
			break;
		case MacroFeedbackType::ActionRequired:
			output << "Action required: " << message <<
				"\nPress Enter to continue..." << std::flush;
			{
				std::string response;
				std::getline(input, response);
			}
			output << '\n';
			break;
		}
	};
}
