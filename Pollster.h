#pragma once

#include <string>
#include <sstream>

struct Pollster {
	typedef int Id;
	constexpr static Id InvalidId = -1;
	constexpr static Id DefaultId = 0;

	std::string name = "";
	unsigned long colour = 0;
	Pollster(std::string name, unsigned long colour)
		: name(name), colour(colour) {}
	Pollster() {}

	std::string textReport() const {
		std::stringstream report;
		report << std::boolalpha;
		report << "Reporting Pollster: \n";
		report << " Name: " << name << "\n";
		report << " Colour: " << colour << "\n";
		return report.str();
	}
};