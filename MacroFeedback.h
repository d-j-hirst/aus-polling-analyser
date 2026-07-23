#pragma once

#include <functional>
#include <string>

enum class MacroFeedbackType {
	Fatal,
	ActionRequired,
	Warning,
};

using MacroFeedbackFunc =
	std::function<void(MacroFeedbackType, std::string)>;
