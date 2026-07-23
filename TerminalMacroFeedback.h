#pragma once

#include "MacroFeedback.h"

#include <iosfwd>

// Adapts core macro feedback to terminal streams without platform APIs.
MacroFeedbackFunc terminalMacroFeedback(
	std::istream& input,
	std::ostream& output,
	std::ostream& errorOutput);
