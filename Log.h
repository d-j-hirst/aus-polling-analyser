#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

// For debugging convenience only - do not leave in stable code
#define PA_LOG_VAR(a) logger << a << " - " << #a << "\n"

class Logger {
public:
	Logger();

	void clear();

	void setFlushFlag(bool val) { std::lock_guard lock(mutex_); doFlush_ = val; }
	void setEnabled(bool enabled);
	bool isEnabled() const { std::lock_guard lock(mutex_); return enabled_; }

	template<typename T>
	auto operator<<(const T& obj)
		-> decltype(std::ofstream() << obj, *this)&;

	template<typename T,
		std::enable_if_t<std::is_enum<T>::value, int> = 0>
	auto operator<<(const T& obj)
		-> decltype(std::ofstream() << static_cast<int>(obj), *this)&;
	
	template<typename T>
	auto operator<<(const T& obj)
		-> decltype(obj.FormatISODate(), *this)&;

	template<typename T>
	Logger& operator<<(const std::vector<T>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::set<T, U>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::map<T, U>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::unordered_map<T, U>& obj);

	template<typename T, int X>
	Logger& operator<<(const std::array<T, X>& obj);

	template<typename T, typename U>
	Logger& operator<<(const std::pair<T, U>& obj);

	template<typename T, typename U, typename V>
	Logger& operator<<(const std::tuple<T, U, V>& obj);

	template<typename T>
	Logger& operator<<(const std::optional<T>& obj);

	Logger& operator<<(const uint8_t& obj);

	Logger& operator<<(const int8_t& obj);
private:
	void resetLog();

	void flushIf() {
		if (enabled_ && doFlush_) fileStream_.flush();
	}

	// Container insertions call scalar insertions while holding this lock.
	mutable std::recursive_mutex mutex_;
	bool enabled_ = true;
	bool doFlush_ = true;

	std::ofstream fileStream_;
};

extern Logger logger;

template<typename T>
auto Logger::operator<<(const T& obj)
-> decltype(std::ofstream() << obj, *this)&
{
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	fileStream_ << obj;
	flushIf();
	return *this;
}

template<typename T,
	std::enable_if_t<std::is_enum<T>::value, int>>
auto Logger::operator<<(const T& obj)
-> decltype(std::ofstream() << static_cast<int>(obj), *this)&
{
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	fileStream_ << static_cast<int>(obj);
	flushIf();
	return *this;
}

template<typename T>
auto Logger::operator<<(const T& obj)
-> decltype(obj.FormatISODate(), *this)&
{
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	fileStream_ << obj.FormatISODate();
	flushIf();
	return *this;
}

template<typename T>
inline Logger& Logger::operator<<(const typename std::vector<T>& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
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
inline Logger& Logger::operator<<(const typename std::set<T, U>& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	fileStream_ << "{";
	bool firstItem = true;
	for (auto const& val : obj) {
		if (!firstItem) fileStream_ << ", ";
		*this << val;
		firstItem = false;
	}
	fileStream_ << "}";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T, typename U>
inline Logger& Logger::operator<<(const typename std::map<T, U>& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
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

template<typename T, typename U>
inline Logger& Logger::operator<<(const typename std::unordered_map<T, U>& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
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
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
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
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	*this << "[" << obj.first << ", " << obj.second << "]";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T, typename U, typename V>
inline Logger& Logger::operator<<(const std::tuple<T, U, V>& obj)
{
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	bool prevFlush = doFlush_;
	setFlushFlag(false);
	*this << "[" << std::get<0>(obj) << ", " << std::get<1>(obj) << ", " << std::get<2>(obj) << "]";
	setFlushFlag(prevFlush);
	flushIf();
	return *this;
}

template<typename T>
inline Logger& Logger::operator<<(const std::optional<T>& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	if (obj) {
		*this << obj.value();
	} else {
		*this << "empty";
	}
	return *this;
}

inline Logger& Logger::operator<<(const uint8_t& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	fileStream_ << static_cast<int>(obj);
	flushIf();
	return *this;
}

inline Logger& Logger::operator<<(const int8_t& obj) {
	std::lock_guard lock(mutex_);
	if (!enabled_) return *this;
	fileStream_ << static_cast<int>(obj);
	flushIf();
	return *this;
}
