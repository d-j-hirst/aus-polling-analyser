#pragma once

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string formatFloat(float floatToFormat, int numDigits, bool addPlusToPositives = false) {
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