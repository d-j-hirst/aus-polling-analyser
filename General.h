#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

inline std::string formatFloat(float floatToFormat, int numDigits, bool addPlusToPositives = false, float nullValue = std::numeric_limits<float>::lowest()) {
	if (floatToFormat == nullValue) return "";
	double floatTimesDigits = double(floatToFormat) * pow(10.0f, numDigits);
	double rounded = round(floatTimesDigits) * pow(0.1f, numDigits);
	std::stringstream ss;
	if (addPlusToPositives && !std::signbit(rounded)) ss << "+";
	ss << std::noshowpoint << std::defaultfloat << rounded;
	std::string preliminaryString = ss.str();
	if (numDigits > 0) {
		auto charIt = std::find(preliminaryString.begin(), preliminaryString.end(), '.');
		if (charIt == preliminaryString.end()) preliminaryString += ".";
		for (int i = 0; i < numDigits; ++i) {
			auto subCharIt = std::find(preliminaryString.begin(), preliminaryString.end(), '.');
			for (int j = 0; j <= i; ++j) ++subCharIt;
			if (subCharIt == preliminaryString.end()) preliminaryString += "0";
		}
	}
	return preliminaryString;
}

inline void transferFileToString(std::ifstream& file, std::string& string) {
	file.seekg(0, std::ios::end);
	size_t size = size_t(file.tellg());
	string = std::string(size, ' ');
	file.seekg(0);
	file.read(&string[0], size);
}

inline std::vector<std::string> splitString(std::string s, std::string const& delimiter) {
	std::vector<std::string> tokens;
	size_t pos = 0;
	while ((pos = s.find(delimiter)) != std::string::npos) {
		tokens.emplace_back(s.substr(0, pos));
		s.erase(0, pos + delimiter.length());
	}
	tokens.emplace_back(s);
	return tokens;
}

// Splits a string into an vector of float values according to the provided delimiter
// Throws std::invalid_argument if a value cannot be converted to a float
inline std::vector<float> splitStringF(std::string s, std::string const& delimiter) {
	auto stringTokens = splitString(s, delimiter);
	std::vector<float> floats;
	std::transform(stringTokens.begin(), stringTokens.end(), std::back_inserter(floats),
		[](std::string s) {return std::stof(s); });
	return floats;
}

// Converts from a modified julian date number to a regular julian date number
inline double mjdToJdn(double mjd) {
	return mjd + 2400000.5;
}

// converts an MJD date to a wxDateTime date.
inline wxDateTime mjdToDate(int mjd) {
	if (mjd <= -1000000) return wxInvalidDateTime;
	wxDateTime tempDate = wxDateTime(double(mjd) + 2400000.5);
	tempDate.SetHour(18);
	return tempDate;
}

inline int dateToIntMjd(wxDateTime date) {
	return int(floor(date.GetModifiedJulianDayNumber()));
}

inline std::string boolToStr(bool b) {
	std::ostringstream ss;
	ss << std::boolalpha << b;
	return ss.str();
}

template<class InputFirstIt, class InputSecondIt, class OutputIt, class BinaryOperation>
OutputIt transform_combine(InputFirstIt first1, InputFirstIt last1, InputSecondIt first2, OutputIt d_first,
	BinaryOperation binary_op)
{
	while (first1 != last1) {
		*d_first++ = binary_op(*first1++, *first2++);
	}
	return d_first;
}

// Take a regular vote share (in the range 0.0f to 100.0f) and transform it using
// the logit transform on the scale -infinity to +infinity
inline float transformVoteShare(float voteShare) {
	return std::log((voteShare * 0.01f) / (1.0f - voteShare * 0.01f)) * 25.0f + 50.0f;
}

// Take a transformed vote share
inline float detransformVoteShare(float transformedVoteShare) {
	return 100.0f / (1.0f + std::exp(-0.04f * (transformedVoteShare - 50.0f)));
}

// Take a transformed vote share
inline float logitDeriv(float startingPoint) {
	return 25.0f / startingPoint + 0.25f / (1.0f - 0.01f * startingPoint);
}

constexpr float DefaultLogitDerivLimit = 4.0f;

inline float limitedLogitDeriv(float startingPoint, float limit = DefaultLogitDerivLimit) {
	return std::clamp(logitDeriv(startingPoint), 0.0f, limit);
}