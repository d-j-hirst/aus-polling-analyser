#include "Log.h"

#include <chrono>

const std::string LogFileName = "PALog.log";

// This is exposed by the "Log.h" file as an external variable
Logger logger;

Logger::Logger()
	: fileStream_(LogFileName)
{
	resetLog();
}

void Logger::clear() {
	std::lock_guard lock(mutex_);
	if (!enabled_) return;
	fileStream_.close();
	fileStream_.open(LogFileName);
}

void Logger::setEnabled(bool enabled)
{
	std::lock_guard lock(mutex_);
	if (enabled_ == enabled) return;
	enabled_ = enabled;
	if (!enabled_) {
		fileStream_.close();
		return;
	}
	fileStream_.open(LogFileName);
	resetLog();
}

void Logger::resetLog() {
	*this << "--- Logging initialized ---\n";
}
