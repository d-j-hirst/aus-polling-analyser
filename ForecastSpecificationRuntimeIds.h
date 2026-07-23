#pragma once

#include <map>
#include <string>

// Forecast specification IDs are stable across processes and project edits;
// collection IDs are only valid within the current PollingProject instance.
struct ForecastSpecificationRuntimeIds {
	std::map<std::string, int> parties;
	std::map<std::string, int> regions;
	std::map<std::string, int> models;
	std::map<std::string, int> projections;
	std::map<std::string, int> simulations;
};
