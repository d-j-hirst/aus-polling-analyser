#pragma once

#include <string>
#include <algorithm>

class Config {
public:
	Config(std::string const& filename);
	int getModelThreads() const { return modelThreads; }
	int getSimulationThreads() const { return simulationThreads; }
	bool getBeepOnCompletion() const { return beepOnCompletion; }
	void limitBackgroundSimulationThreads(unsigned logicalProcessors) {
		int available = logicalProcessors > 1 ? int(logicalProcessors - 1) : 1;
		simulationThreads = std::max(1, std::min(simulationThreads, available));
	}
private:

	int modelThreads = 1;
	int simulationThreads = 1;
	bool beepOnCompletion = true;
};
