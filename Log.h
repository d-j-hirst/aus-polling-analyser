#pragma once

#include <fstream>
#include <map>
#include <string>
#include <vector>

class Logger {
public:
	Logger();

	void clear();

	void setFlushFlag(bool val) { doFlush_ = val; }

	template<typename T>
	Logger& operator<<(const T& obj);

	template<typename T>
	Logger& operator<<(const std::vector<T>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::map<T, U>& obj);

	template<typename T, int X>
	Logger& operator<<(const std::array<T, X>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::pair<T, U>& obj);

	Logger& operator<<(const uint8_t& obj);

	Logger& operator<<(const int8_t& obj);
private:
	void resetLog();

	void flushIf() { if (doFlush_) fileStream_.flush(); }

	bool doFlush_ = true;

	std::ofstream fileStream_;
};

extern Logger logger;

template<typename T>
inline Logger& Logger::operator<<(const T& obj) {
	fileStream_ << obj;
	flushIf();
	return *this;
}

template<typename T>
inline Logger& Logger::operator<<(const typename std::vector<T>& obj) {
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	fileStream_ << "[";
	bool firstItem = true;
	for (auto const& component : obj) {
		if (!firstItem) fileStream_ << ", ";
		*this << component;
		firstItem = false;
	}
	fileStream_ << "]";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T, typename U>
inline Logger& Logger::operator<<(const typename std::map<T, U>& obj) {
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	fileStream_ << "{";
	bool firstItem = true;
	for (auto const& [key, val] : obj) {
		if (!firstItem) fileStream_ << ", ";
		*this << key << ": " << val;
		firstItem = false;
	}
	fileStream_ << "}";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T, int X>
inline Logger& Logger::operator<<(const std::array<T, X>& obj)
{
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	fileStream_ << "[";
	bool firstItem = true;
	for (auto const& component : obj) {
		if (!firstItem) fileStream_ << ", ";
		*this << component;
		firstItem = false;
	}
	fileStream_ << "]";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T, typename U>
inline Logger& Logger::operator<<(const std::pair<T, U>& obj)
{
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	*this << "[" << obj.first << ", " << obj.second << "]";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

inline Logger& Logger::operator<<(const uint8_t& obj) {
	fileStream_ << static_cast<int>(obj);
	flushIf();
	return *this;
}

inline Logger& Logger::operator<<(const int8_t& obj) {
	fileStream_ << static_cast<int>(obj);
	flushIf();
	return *this;
}