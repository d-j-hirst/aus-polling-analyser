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
		if (!line.empty() && line.back() == '\r') line.pop_back();
		auto values = splitString(line, "=");
		if (values.size() != 2) continue;
		try {
			if (values[0] == "iModelThreads") {
				modelThreads = std::stoi(values[1]);
			}
			if (values[0] == "iSimulationThreads") {
				simulationThreads = std::stoi(values[1]);
			}
			if (values[0] == "bBeepOnCompletion") {
				beepOnCompletion = std::stoi(values[1]) == 1;
			}
			if (values[0] == "bLiveDetailedLogging") {
				liveDetailedLogging = std::stoi(values[1]) == 1;
			}
			if (values[0] == "bLiveLegacyTppDiagnostic") {
				liveLegacyTppDiagnostic = std::stoi(values[1]) == 1;
			}
			if (values[0] == "sSimulationAuditDirectory") {
				simulationAuditDirectory = values[1];
			}
		} 
		catch (std::invalid_argument) {
			logger << "Invalid config line: " + line;
		}
	} while (true);
}
