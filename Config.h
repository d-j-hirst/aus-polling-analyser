#pragma once

#include <string>

class Config {
public:
	Config(std::string const& filename);
	int getModelThreads() const { return modelThreads; }
	int getSimulationThreads() const { return simulationThreads; }
	bool getBeepOnCompletion() const { return beepOnCompletion; }
private:

	int modelThreads = 1;
	int simulationThreads = 1;
	bool beepOnCompletion = true;
};