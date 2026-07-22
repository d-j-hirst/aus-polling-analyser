#pragma once

#include <stdexcept>
#include <string>

class PollingProject;
class Simulation;
class SimulationRun;

// Keeps the standard simulation path independent of the concrete live-results
// parser and analyser. The implementation is the composition root for live
// preparation and translates its errors into this lightweight interface.
namespace LivePreparationBridge {
	class Exception : public std::runtime_error {
	public:
		explicit Exception(std::string const& what) : std::runtime_error(what) {}
	};

	void validateAutomaticSetup(
		PollingProject const& project,
		Simulation const& simulation);

	void prepareAutomatic(
		PollingProject& project,
		Simulation& simulation,
		SimulationRun& run);
}
