#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// A timezone-free Gregorian calendar date. Dates in the forecasting pipeline
// represent election days or trend days, so time-of-day and DST are deliberately
// excluded from arithmetic.
class Date {
public:
	Date() = default;

	static std::optional<Date> fromYmd(int year, int month, int day);
	static std::optional<Date> parseIso(std::string_view text);
	static Date fromModifiedJulianDay(int modifiedJulianDay);
	static Date todayLocal();

	bool isValid() const { return serialDay != InvalidSerialDay; }
	std::string formatIso() const;

	int year() const;
	int month() const;
	int day() const;

	Date addDays(int days) const;
	int daysUntil(Date other) const;

	// Modified Julian day at calendar midnight, independent of local timezone.
	int modifiedJulianDay() const;

	// .pol2 files historically stored wxDateTime Julian day numbers. Calendar
	// dates were represented by the UTC instant corresponding to local midnight.
	double toLegacyJulianDay() const;
	static Date fromLegacyJulianDay(double julianDay);

	friend bool operator==(Date lhs, Date rhs) {
		return lhs.serialDay == rhs.serialDay;
	}
	friend bool operator!=(Date lhs, Date rhs) { return !(lhs == rhs); }
	friend bool operator<(Date lhs, Date rhs) {
		return lhs.serialDay < rhs.serialDay;
	}
	friend bool operator>(Date lhs, Date rhs) { return rhs < lhs; }
	friend bool operator<=(Date lhs, Date rhs) { return !(rhs < lhs); }
	friend bool operator>=(Date lhs, Date rhs) { return !(lhs < rhs); }
	friend Date operator+(Date date, int days) { return date.addDays(days); }
	friend Date operator-(Date date, int days) { return date.addDays(-days); }
	friend int operator-(Date lhs, Date rhs) { return rhs.daysUntil(lhs); }

private:
	explicit Date(int serialDay) : serialDay(serialDay) {}

	static constexpr int InvalidSerialDay = -2147483647 - 1;
	int serialDay = InvalidSerialDay;
};

// An absolute point in time, stored at millisecond precision. Timestamps are
// used for run metadata and uploads, not calendar-date arithmetic.
class Timestamp {
public:
	Timestamp() = default;

	static Timestamp now();
	static Timestamp fromUnixMilliseconds(std::int64_t milliseconds);
	static std::optional<Timestamp> parseCompactLocal(std::string_view text);

	bool isValid() const { return unixMillis != InvalidUnixMillis; }
	std::int64_t unixMilliseconds() const { return unixMillis; }

	std::string formatIsoLocal() const;
	std::string formatIsoDateLocal() const;
	std::string formatIsoTimeLocal() const;
	std::string formatIsoUtc() const;
	Date localDate() const;

	double toLegacyJulianDay() const;
	static Timestamp fromLegacyJulianDay(double julianDay);

private:
	explicit Timestamp(std::int64_t unixMillis) : unixMillis(unixMillis) {}

	static constexpr std::int64_t InvalidUnixMillis =
		(-9223372036854775807LL - 1LL);
	std::int64_t unixMillis = InvalidUnixMillis;
};
