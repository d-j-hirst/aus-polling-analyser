#pragma once

#include <numeric>
#include <sstream>
#include <string>
#include <vector>

struct Region {
	typedef int Id;
	constexpr static Id InvalidId = -1;

	std::string name = "";
	int population = 0;
	float lastElection2pp = 50.0f;
	float sample2pp = 50.0f;
	std::string analysisCode = "";
	float swingDeviation = 0.0f;
	float homeRegionMod = 1.0f; // relative chance that an emerging party will have this as its home state

	std::string textReport() const {
		std::stringstream report;
		report << "Reporting Region: \n";
		report << " Name: " << name << "\n";
		report << " Population: " << population << "\n";
		report << " Last election 2pp: " << lastElection2pp << "\n";
		report << " Sample 2pp: " << sample2pp << "\n";
		report << " Swing Deviation: " << swingDeviation << "\n";
		return report.str();
	}

	Region(std::string name, int population, float lastElection2pp, float sample2pp)
		: name(name), lastElection2pp(lastElection2pp), population(population), sample2pp(sample2pp) {}
	Region() {}
};