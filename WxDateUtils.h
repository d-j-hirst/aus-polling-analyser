#pragma once

#include "Date.h"

#include <wx/datetime.h>

// Temporary wxWidgets date adapters. Core date handling will move to portable
// calendar types; keeping these here prevents unrelated helpers from depending
// on wxWidgets in the meantime.
inline wxDateTime mjdToDate(int mjd)
{
	if (mjd <= -1000000) return wxInvalidDateTime;
	Date const date = Date::fromModifiedJulianDay(mjd);
	if (!date.isValid()) return wxInvalidDateTime;
	return wxDateTime(date.day(),
		static_cast<wxDateTime::Month>(date.month() - 1), date.year());
}

inline int dateToIntMjd(wxDateTime const& date)
{
	if (!date.IsValid()) return -100000000;
	auto const portableDate = Date::fromYmd(
		date.GetYear(), int(date.GetMonth()) + 1, date.GetDay());
	return portableDate ? portableDate->modifiedJulianDay() : -100000000;
}

inline int dateToIntMjd(Date date)
{
	return date.isValid() ? date.modifiedJulianDay() : -100000000;
}

inline wxDateTime toWxDate(Date date)
{
	if (!date.isValid()) return wxInvalidDateTime;
	return wxDateTime(date.day(),
		static_cast<wxDateTime::Month>(date.month() - 1), date.year());
}

inline Date fromWxDate(wxDateTime const& date)
{
	if (!date.IsValid()) return {};
	return Date::fromYmd(date.GetYear(), int(date.GetMonth()) + 1, date.GetDay())
		.value_or(Date{});
}
