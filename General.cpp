#include "General.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

std::string formatFloat(float floatToFormat, int numDigits) {
	double floatTimesDigits = double(floatToFormat) * pow(10.0f, numDigits);
	double rounded = round(floatTimesDigits) * pow(0.1f, numDigits);
	std::stringstream ss;
	ss << std::noshowpoint << std::defaultfloat << rounded;
	std::string preliminaryString = ss.str();
	if (numDigits > 0) {
		auto charIt = std::find(preliminaryString.begin(), preliminaryString.end(), '.');
		if (charIt == preliminaryString.end()) preliminaryString += ".";
		for (int i = 0; i < numDigits; ++i) {
			auto charIt = std::find(preliminaryString.begin(), preliminaryString.end(), '.');
			for (int j = 0; j <= i; ++j) ++charIt;
			if (charIt == preliminaryString.end()) preliminaryString += "0";
		}
	}
	return preliminaryString;
}