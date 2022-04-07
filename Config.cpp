#include "Config.h"

#include "General.h"
#include "Log.h"

#include <fstream>

Config::Config(std::string const& filename)
{
	std::ifstream file(filename);
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, "=");
		if (values.size() != 2) continue;
		try {
			if (values[0] == "iModelThreads") {
				modelThreads = std::stoi(values[1]);
			}
			if (values[0] == "iSimulationThreads") {
				simulationThreads = std::stoi(values[1]);
			}
		} 
		catch (std::invalid_argument) {
			logger << "Invalid config line: " + line;
		}
	} while (true);
}
