#pragma once

#include <string>
#include <sstream>

struct Pollster {
	typedef int Id;
	constexpr static Id InvalidId = -1;
	constexpr static Id DefaultId = 0;

	std::string name = "";
	float weight = 1.0f;
	unsigned long colour = 0;
	bool useForCalibration = false;
	bool ignoreInitially = false;
	Pollster(std::string name, float weight, unsigned long colour, bool useForCalibration, bool ignoreInitially)
		: name(name), weight(weight), colour(colour), useForCalibration(useForCalibration),
		ignoreInitially(ignoreInitially) {}
	Pollster() {}

	std::string textReport() const {
		std::stringstream report;
		report << std::boolalpha;
		report << "Reporting Pollster: \n";
		report << " Name: " << name << "\n";
		report << " Weight: " << weight << "\n";
		report << " Colour: " << colour << "\n";
		report << " Use for calibration: " << useForCalibration << "\n";
		report << " Ignore initially: " << ignoreInitially << "\n";
		return report.str();
	}
};