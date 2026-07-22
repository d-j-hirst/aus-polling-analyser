#include "../Date.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
	auto const leapDay = Date::parseIso("2024-02-29");
	assert(leapDay);
	assert(leapDay->formatIso() == "2024-02-29");
	assert(!Date::parseIso("2023-02-29"));
	assert(!Date::parseIso("2024-2-29"));
	assert(!Date::parseIso("2024-13-01"));

	auto const endOfYear = Date::parseIso("2023-12-31").value();
	assert((endOfYear + 1).formatIso() == "2024-01-01");
	assert((leapDay.value() + 1).formatIso() == "2024-03-01");
	assert(Date::parseIso("2024-03-10").value() - leapDay.value() == 10);
	assert(Date::parseIso("1970-01-01")->modifiedJulianDay() == 40587);

	for (char const* value : {"1900-01-01", "1970-01-01", "2000-02-29", "2028-11-25"}) {
		auto const original = Date::parseIso(value).value();
		auto const restored = Date::fromLegacyJulianDay(
			original.toLegacyJulianDay());
		assert(restored == original);
	}

	auto const epoch = Timestamp::fromUnixMilliseconds(0);
	assert(epoch.formatIsoUtc() == "1970-01-01T00:00:00");
	assert(std::abs(epoch.toLegacyJulianDay() - 2440587.5) < 1e-9);
	assert(Timestamp::fromLegacyJulianDay(2440587.5).unixMilliseconds() == 0);
	assert(!Timestamp::fromLegacyJulianDay(-1.0e9).isValid());

	auto const localTimestamp = Timestamp::parseCompactLocal("20260323121118");
	assert(localTimestamp);
	assert(localTimestamp->formatIsoLocal() == "2026-03-23T12:11:18");
	assert(!Timestamp::parseCompactLocal("20260230121118"));
	auto const restoredTimestamp = Timestamp::fromLegacyJulianDay(
		localTimestamp->toLegacyJulianDay());
	assert(std::llabs(restoredTimestamp.unixMilliseconds() -
		localTimestamp->unixMilliseconds()) <= 1);

	std::cout << "Date tests passed\n";
}
