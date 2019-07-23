#pragma once

#include <string>

struct Pollster {
	typedef int Id;
	constexpr static Id InvalidId = -1;

	std::string name = "";
	float weight = 1.0f;
	unsigned long colour = 0;
	bool useForCalibration = false;
	bool ignoreInitially = false;
	Pollster(std::string name, float weight, unsigned long colour, bool useForCalibration, bool ignoreInitially)
		: name(name), weight(weight), colour(colour), useForCalibration(useForCalibration),
		ignoreInitially(ignoreInitially) {}
	Pollster() {}
};