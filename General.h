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
#include <array>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

inline std::string formatFloat(float floatToFormat, int numDigits, bool addPlusToPositives = false, float nullValue = std::numeric_limits<float>::lowest()) {
	if (floatToFormat == nullValue) return "";
	if (std::isnan(floatToFormat)) return "none";
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

// Splits a string into an vector of int values according to the provided delimiter
// Throws std::invalid_argument if a value cannot be converted to a int
inline std::vector<int> splitStringI(std::string s, std::string const& delimiter) {
	auto stringTokens = splitString(s, delimiter);
	std::vector<int> ints;
	std::transform(stringTokens.begin(), stringTokens.end(), std::back_inserter(ints),
		[](std::string s) {return std::stoi(s); });
	return ints;
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
OutputIt transform_combine(InputFirstIt begin1, InputFirstIt end1, InputSecondIt begin2, OutputIt d_begin,
	BinaryOperation binary_op)
{
	while (begin1 != end1) {
		*d_begin++ = binary_op(*begin1++, *begin2++);
	}
	return d_begin;
}

// Take a regular vote share (in the range 0 to 100) and transform it using
// the logit transform on the scale -infinity to +infinity
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
inline T transformVoteShare(T voteShare) {
	return std::log((voteShare * T(0.01)) / (T(1.0) - voteShare * T(0.01))) * T(25.0);
}

// Take a transformed vote share and detransform it back to a regular vote share
// on the scale 0-100
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
inline T detransformVoteShare(T transformedVoteShare) {
	return T(100.0) / (T(1.0) + std::exp(-T(0.04) * transformedVoteShare));
}

// Get the derivative of the logit function for a given regular vote share
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
inline T logitDeriv(T startingPoint) {
	return T(25.0) / startingPoint + T(0.25) / (T(1.0) - T(0.01) * startingPoint);
}

constexpr double DefaultLogitDerivLimit = 4.0;

template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	inline T limitedLogitDeriv(T startingPoint, T limit = T(DefaultLogitDerivLimit)) {
	return std::clamp(logitDeriv(startingPoint), T(0.0), limit);
}

// Takes a regular vote share and adjusts it by transforming it and applying a
// swing proportional to the derivative, then detransforming it to yield a new
// vote share after the swing
// Useful as it prevents the result from exceeding (0, 100) and results in
// "flattening" of the swing towards the bounds which may be desirable in many cases
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	inline T basicTransformedSwing(T startingPoint, T swing, T limit = T(DefaultLogitDerivLimit)) {
	T transformed = transformVoteShare(startingPoint);
	T deriv = limitedLogitDeriv(startingPoint, limit);
	T projection = transformed + deriv * swing;
	return detransformVoteShare(projection);
}

// Takes a regular vote share and adjusts it by transforming it and applying a
// swing proportional to the derivative, then detransforming it to yield a new
// vote share after the swing. This method uses a predictor-corrector method
// to better estimate the derivative.
// Useful for situations where bounding the value between (0, 100) is desirable,
// but with less flattening.
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	inline T predictorCorrectorTransformedSwing(T startingPoint, T swing, T limit = T(DefaultLogitDerivLimit)) {
	T transformed = transformVoteShare(startingPoint);
	T deriv = limitedLogitDeriv(startingPoint, limit);
	T projection = transformed + deriv * swing;
	T tempDetransformed = detransformVoteShare(projection);
	T projectedDeriv = limitedLogitDeriv(tempDetransformed, limit);
	T averagedDeriv = (deriv + projectedDeriv) * T(0.5);
	T correctedProjection = transformed + averagedDeriv * swing;
	return detransformVoteShare(correctedProjection);
}

// from https://stackoverflow.com/questions/11809502/which-is-better-way-to-calculate-ncr
// Some risk of overflow here, but should be fine for smallish values
inline long long nCr(long long n, long long r) {
	if (r > n - r) r = n - r; // because C(n, r) == C(n, n - r)
	long long ans = 1;
	long long i;

	for (i = 1; i <= r; i++) {
		ans *= n - r + i;
		ans /= i;
	}

	return ans;
}

template<typename T>
bool contains(std::vector<T> const& vec, T find) {
	return std::find(vec.begin(), vec.end(), find) != vec.end();
}

template<typename T>
T mix(T lower, T upper, T upperFactor) {
	return upper * upperFactor + lower * (T(1.0) - upperFactor);
}

// find value for "key" within "map" giving "defaultVal" if not found.
template<typename T, typename U, typename V>
U getAt(T map, U key, V defaultVal) {
	auto it = map.find(key);
	if (it == map.end()) return defaultVal;
	return it->second;
}

namespace detail
{
	template <typename T, std::size_t...Is>
	std::array<T, sizeof...(Is)> make_array(const T& value, std::index_sequence<Is...>)
	{
		return { { (static_cast<void>(Is), value)... } };
	}
}

template <std::size_t N, typename T>
std::array<T, N> make_array(const T& value)
{
	return detail::make_array(value, std::make_index_sequence<N>());
}

template<typename T>
bool isBoundedBy(const T& val, const T& lower, const T& upper) {
	return lower <= val && val <= upper;
}

// Efficient (for small e) method for raising a float to an integer power
inline float myPow(float b, unsigned int e) {
	{
		if (e == 0)
			return 1;
		else if (e % 2 == 0)
			return myPow(b, e / 2) * myPow(b, e / 2);
		else
			return b * myPow(b, e / 2) * myPow(b, e / 2);
	}
};