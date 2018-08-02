#pragma once

#include <string>
#include <numeric>

struct Region {
	std::string name = "";
	int population = 0;
	float lastElection2pp = 50.0f;
	float sample2pp = 50.0f;
	float swingDeviation = 0.0f;

	float simulationSwing = 0.0f;
	float localModifierAverage = 0.0f;
	float additionalUncertainty = 0.0f;
	int seatCount = 0;
	std::vector<int> partyLeading;
	// party first, then region
	std::vector<std::vector<int>> partyWins;

	int getOthersLeading() const {
		if (partyLeading.size() < 3) return 0.0f;
		return std::accumulate(partyLeading.begin() + 2, partyLeading.end(), 0);
	}

	Region(std::string name, int population, float lastElection2pp, float sample2pp)
		: name(name), lastElection2pp(lastElection2pp), population(population), sample2pp(sample2pp) {}
	Region() {}
};