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
	fileStream_.close();
	fileStream_.open(LogFileName);
}

void Logger::resetLog() {
	*this << "--- Logging initialized ---\n";
}