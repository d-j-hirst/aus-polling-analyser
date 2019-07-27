#pragma once

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