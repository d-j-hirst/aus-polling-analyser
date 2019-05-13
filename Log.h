#pragma once

#include <fstream>
#include <string>
#include <string_view>
#include <vector>

constexpr int RandomVariable = 3;

class Logger {
public:
	Logger();

	void clear();

	// Message should not include newline characters etc.
	void logTimeStamp(std::string message);

	template<typename T>
	Logger& operator<<(const T& obj);

	template<typename T>
	Logger& operator<<(const std::vector<T>& obj);

	Logger& operator<<(const uint8_t& obj);

	Logger& operator<<(const int8_t& obj);
private:
	void resetLog();
	void resetTimer();

	std::ofstream fileStream_;
};

extern Logger logger;

template<typename T>
inline Logger& Logger::operator<<(const T& obj) {
	fileStream_ << obj;
	fileStream_.flush();
	return *this;
}

template<typename T>
inline Logger& Logger::operator<<(const typename std::vector<T>& obj) {
	fileStream_ << "{";
	bool firstItem = true;
	for (auto const& component : obj) {
		if (!firstItem) fileStream_ << ", ";
		fileStream_ << component;
		firstItem = false;
	}
	fileStream_ << "}";
	fileStream_.flush();
	return *this;
}

inline Logger& Logger::operator<<(const uint8_t& obj) {
	fileStream_ << static_cast<int>(obj);
	fileStream_.flush();
	return *this;
}

inline Logger& Logger::operator<<(const int8_t& obj) {
	fileStream_ << static_cast<int>(obj);
	fileStream_.flush();
	return *this;
}