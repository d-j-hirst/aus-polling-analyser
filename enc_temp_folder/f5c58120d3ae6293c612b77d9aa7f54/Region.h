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
	float swingDeviation = 0.0f;

	float simulationSwing = 0.0f;
	float localModifierAverage = 0.0f;
	float additionalUncertainty = 0.0f;
	int seatCount = 0;

	float liveSwing = 0.0f;
	float livePercentCounted = 0.0f;
	int classicSeatCount = 0;

	std::vector<int> partyLeading;
	// party first, then region
	std::vector<std::vector<int>> partyWins;
	
	// Get the number of seats in this region in which non-major parties are leading
	int getOthersLeading() const {
		if (partyLeading.size() < 3) return 0;
		return std::accumulate(partyLeading.begin() + 2, partyLeading.end(), 0);
	}

	std::string textReport() const {
		std::stringstream report;
		report << "Reporting Party: \n";
		report << " Name: " << name << "\n";
		report << " Population: " << population << "\n";
		report << " Last election 2pp: " << lastElection2pp << "\n";
		report << " Sample 2pp: " << sample2pp << "\n";
		report << " Swing Deviation: " << swingDeviation << "\n";
		report << " Additional Uncertainty: " << additionalUncertainty << "\n";
		return report.str();
	}

	Region(std::string name, int population, float lastElection2pp, float sample2pp)
		: name(name), lastElection2pp(lastElection2pp), population(population), sample2pp(sample2pp) {}
	Region() {}
};