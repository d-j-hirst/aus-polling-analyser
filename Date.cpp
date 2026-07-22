#include "Date.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>

namespace {
	constexpr std::int64_t MillisecondsPerSecond = 1000;
	constexpr std::int64_t MillisecondsPerDay = 86400000;
	constexpr double UnixEpochJulianDay = 2440587.5;
	constexpr int UnixEpochModifiedJulianDay = 40587;
	constexpr double InvalidLegacyJulianThreshold = -1000000.0;

	struct Ymd {
		int year;
		int month;
		int day;
	};

	bool isLeapYear(int year)
	{
		return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	}

	int daysInMonth(int year, int month)
	{
		constexpr std::array<int, 12> MonthLengths = {
			31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
		};
		if (month < 1 || month > 12) return 0;
		return MonthLengths[month - 1] +
			(month == 2 && isLeapYear(year) ? 1 : 0);
	}

	// These civil-calendar conversions use 1970-01-01 as day zero and avoid
	// timezone APIs entirely. They are valid across the full supported year range.
	std::int64_t daysFromCivil(int year, unsigned month, unsigned day)
	{
		year -= month <= 2;
		int const era = (year >= 0 ? year : year - 399) / 400;
		unsigned const yearOfEra = unsigned(year - era * 400);
		unsigned const dayOfYear =
			(153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
		unsigned const dayOfEra =
			yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
		return std::int64_t(era) * 146097 + dayOfEra - 719468;
	}

	Ymd civilFromDays(std::int64_t serialDay)
	{
		serialDay += 719468;
		std::int64_t const era =
			(serialDay >= 0 ? serialDay : serialDay - 146096) / 146097;
		unsigned const dayOfEra = unsigned(serialDay - era * 146097);
		unsigned const yearOfEra =
			(dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 -
				dayOfEra / 146096) / 365;
		int year = int(yearOfEra) + int(era) * 400;
		unsigned const dayOfYear =
			dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
		unsigned const monthPrime = (5 * dayOfYear + 2) / 153;
		unsigned const day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;
		unsigned const month = monthPrime + (monthPrime < 10 ? 3 : -9);
		year += month <= 2;
		return {year, int(month), int(day)};
	}

	bool parseDigits(std::string_view text, int& value)
	{
		value = 0;
		if (text.empty()) return false;
		for (char const character : text) {
			if (character < '0' || character > '9') return false;
			value = value * 10 + character - '0';
		}
		return true;
	}

	std::string formatDateTime(std::tm const& value, bool includeTime)
	{
		char buffer[20] = {};
		if (includeTime) {
			std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
				value.tm_year + 1900, value.tm_mon + 1, value.tm_mday,
				value.tm_hour, value.tm_min, value.tm_sec);
		}
		else {
			std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
				value.tm_year + 1900, value.tm_mon + 1, value.tm_mday);
		}
		return buffer;
	}

	bool localTime(std::time_t value, std::tm& result)
	{
#ifdef _WIN32
		return localtime_s(&result, &value) == 0;
#else
		return localtime_r(&value, &result) != nullptr;
#endif
	}

	bool utcTime(std::time_t value, std::tm& result)
	{
#ifdef _WIN32
		return gmtime_s(&result, &value) == 0;
#else
		return gmtime_r(&value, &result) != nullptr;
#endif
	}

	std::time_t unixSeconds(std::int64_t unixMillis)
	{
		// Integer division truncates towards zero, so adjust negative timestamps
		// to preserve floor semantics before conversion to time_t.
		std::int64_t seconds = unixMillis / MillisecondsPerSecond;
		if (unixMillis < 0 && unixMillis % MillisecondsPerSecond) --seconds;
		return static_cast<std::time_t>(seconds);
	}

	double invalidLegacyJulianDay()
	{
		return double(std::numeric_limits<std::int64_t>::min()) /
			double(MillisecondsPerDay) + UnixEpochJulianDay;
	}
}

std::optional<Date> Date::fromYmd(int year, int month, int day)
{
	if (year < 1 || year > 9999 || month < 1 || month > 12 ||
		day < 1 || day > daysInMonth(year, month)) {
		return std::nullopt;
	}
	auto const serial = daysFromCivil(year, unsigned(month), unsigned(day));
	if (serial < std::numeric_limits<int>::min() ||
		serial > std::numeric_limits<int>::max()) {
		return std::nullopt;
	}
	return Date(int(serial));
}

std::optional<Date> Date::parseIso(std::string_view text)
{
	if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
		return std::nullopt;
	}
	int year = 0;
	int month = 0;
	int day = 0;
	if (!parseDigits(text.substr(0, 4), year) ||
		!parseDigits(text.substr(5, 2), month) ||
		!parseDigits(text.substr(8, 2), day)) {
		return std::nullopt;
	}
	return fromYmd(year, month, day);
}

Date Date::fromModifiedJulianDay(int modifiedJulianDay)
{
	auto const serial = std::int64_t(modifiedJulianDay) -
		UnixEpochModifiedJulianDay;
	if (serial < std::numeric_limits<int>::min() + 1LL ||
		serial > std::numeric_limits<int>::max()) {
		return {};
	}
	auto const value = civilFromDays(serial);
	if (value.year < 1 || value.year > 9999) return {};
	return Date(int(serial));
}

Date Date::todayLocal()
{
	auto const currentTime = std::chrono::system_clock::to_time_t(
		std::chrono::system_clock::now());
	std::tm local = {};
	if (!localTime(currentTime, local)) return {};
	return fromYmd(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)
		.value_or(Date{});
}

std::string Date::formatIso() const
{
	if (!isValid()) return {};
	auto const value = civilFromDays(serialDay);
	char buffer[11] = {};
	std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
		value.year, value.month, value.day);
	return buffer;
}

int Date::year() const
{
	return isValid() ? civilFromDays(serialDay).year : 0;
}

int Date::month() const
{
	return isValid() ? civilFromDays(serialDay).month : 0;
}

int Date::day() const
{
	return isValid() ? civilFromDays(serialDay).day : 0;
}

Date Date::addDays(int days) const
{
	if (!isValid()) return {};
	auto const result = std::int64_t(serialDay) + days;
	if (result < std::numeric_limits<int>::min() + 1LL ||
		result > std::numeric_limits<int>::max()) {
		return {};
	}
	auto const value = civilFromDays(result);
	if (value.year < 1 || value.year > 9999) return {};
	return Date(int(result));
}

int Date::daysUntil(Date other) const
{
	if (!isValid() || !other.isValid()) return 0;
	auto const difference = std::int64_t(other.serialDay) - serialDay;
	if (difference < std::numeric_limits<int>::min()) {
		return std::numeric_limits<int>::min();
	}
	if (difference > std::numeric_limits<int>::max()) {
		return std::numeric_limits<int>::max();
	}
	return int(difference);
}

int Date::modifiedJulianDay() const
{
	return isValid() ? serialDay + UnixEpochModifiedJulianDay : InvalidSerialDay;
}

double Date::toLegacyJulianDay() const
{
	if (!isValid()) return invalidLegacyJulianDay();
	std::tm local = {};
	local.tm_year = year() - 1900;
	local.tm_mon = month() - 1;
	local.tm_mday = day();
	local.tm_isdst = -1;
	auto const time = std::mktime(&local);
	if (time == std::time_t(-1)) return invalidLegacyJulianDay();
	return double(time) / 86400.0 + UnixEpochJulianDay;
}

Date Date::fromLegacyJulianDay(double julianDay)
{
	return Timestamp::fromLegacyJulianDay(julianDay).localDate();
}

Timestamp Timestamp::now()
{
	auto const milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	return Timestamp(milliseconds);
}

Timestamp Timestamp::fromUnixMilliseconds(std::int64_t milliseconds)
{
	if (milliseconds == InvalidUnixMillis) return {};
	return Timestamp(milliseconds);
}

std::optional<Timestamp> Timestamp::parseCompactLocal(std::string_view text)
{
	if (text.size() != 14) return std::nullopt;
	int parts[6] = {};
	constexpr std::array<std::pair<size_t, size_t>, 6> Ranges = {{
		{0, 4}, {4, 2}, {6, 2}, {8, 2}, {10, 2}, {12, 2}
	}};
	for (size_t index = 0; index < Ranges.size(); ++index) {
		if (!parseDigits(text.substr(Ranges[index].first, Ranges[index].second),
			parts[index])) {
			return std::nullopt;
		}
	}
	if (!Date::fromYmd(parts[0], parts[1], parts[2]) ||
		parts[3] > 23 || parts[4] > 59 || parts[5] > 59) {
		return std::nullopt;
	}
	std::tm local = {};
	local.tm_year = parts[0] - 1900;
	local.tm_mon = parts[1] - 1;
	local.tm_mday = parts[2];
	local.tm_hour = parts[3];
	local.tm_min = parts[4];
	local.tm_sec = parts[5];
	local.tm_isdst = -1;
	auto const time = std::mktime(&local);
	std::tm checked = {};
	if (!localTime(time, checked) || checked.tm_year != parts[0] - 1900 ||
		checked.tm_mon != parts[1] - 1 || checked.tm_mday != parts[2] ||
		checked.tm_hour != parts[3] || checked.tm_min != parts[4] ||
		checked.tm_sec != parts[5]) {
		return std::nullopt;
	}
	return Timestamp(std::int64_t(time) * MillisecondsPerSecond);
}

std::string Timestamp::formatIsoLocal() const
{
	if (!isValid()) return {};
	std::tm local = {};
	if (!localTime(unixSeconds(unixMillis), local)) return {};
	return formatDateTime(local, true);
}

std::string Timestamp::formatIsoDateLocal() const
{
	if (!isValid()) return {};
	std::tm local = {};
	if (!localTime(unixSeconds(unixMillis), local)) return {};
	return formatDateTime(local, false);
}

std::string Timestamp::formatIsoUtc() const
{
	if (!isValid()) return {};
	std::tm utc = {};
	if (!utcTime(unixSeconds(unixMillis), utc)) return {};
	return formatDateTime(utc, true);
}

Date Timestamp::localDate() const
{
	if (!isValid()) return {};
	std::tm local = {};
	if (!localTime(unixSeconds(unixMillis), local)) return {};
	return Date::fromYmd(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)
		.value_or(Date{});
}

double Timestamp::toLegacyJulianDay() const
{
	if (!isValid()) return invalidLegacyJulianDay();
	return double(unixMillis) / double(MillisecondsPerDay) +
		UnixEpochJulianDay;
}

Timestamp Timestamp::fromLegacyJulianDay(double julianDay)
{
	if (!std::isfinite(julianDay) || julianDay <= InvalidLegacyJulianThreshold) {
		return {};
	}
	double const milliseconds =
		(julianDay - UnixEpochJulianDay) * double(MillisecondsPerDay);
	if (!std::isfinite(milliseconds) ||
		milliseconds < double(std::numeric_limits<std::int64_t>::min() + 1LL) ||
		milliseconds > double(std::numeric_limits<std::int64_t>::max())) {
		return {};
	}
	return Timestamp(std::int64_t(std::llround(milliseconds)));
}
