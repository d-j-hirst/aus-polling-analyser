#include "Log.h"

#include <chrono>

int64_t debugTimerBase = 0;
double debugTimerPeriod = 0.0f;

const std::string LogFileName = "PALog.log";
const double SecondsToMilliseconds = 1000.0;

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
	resetTimer();
	*this << "--- Logging initialized ---\n";
}

void Logger::resetTimer() {
	debugTimerBase = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	debugTimerPeriod = double(std::chrono::high_resolution_clock::period::num) /
		double(std::chrono::high_resolution_clock::period::den);
}

// Prints time since the timer was reset, in milliseconds.
void Logger::logTimeStamp(std::string message) {
	using namespace std;
	int64_t debugTimerNow = std::chrono::high_resolution_clock::now().time_since_epoch().count();
	*this << int(double(debugTimerNow - debugTimerBase) * debugTimerPeriod * SecondsToMilliseconds) << " " << message << "\n";
}