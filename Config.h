#pragma once

#include <string>

class Config {
public:
	Config(std::string const& filename);
	int getModelThreads() const { return modelThreads; }
	int getSimulationThreads() const { return simulationThreads; }
private:

	int modelThreads = 1;
	int simulationThreads = 1;
};