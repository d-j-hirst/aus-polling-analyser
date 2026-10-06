#include "LivePreparationBridge.h"

#include "LivePreparation.h"

namespace {
	template <typename Operation>
	void translateLivePreparationException(Operation&& operation)
	{
		try {
			operation();
		}
		// File acquisition, election XML validation and turnout input checks
		// report runtime errors. Translate them at this boundary so preparation
		// can display the specific input problem instead of escaping through the
		// wxWidgets event loop. Logic errors retain their debugger/error path.
		catch (std::runtime_error const& error) {
			throw LivePreparationBridge::Exception(error.what());
		}
	}
}

void LivePreparationBridge::validateAutomaticSetup(
	PollingProject const& project,
	Simulation const& simulation)
{
	translateLivePreparationException([&] {
		LivePreparation::validateAutomaticSetup(project, simulation);
	});
}

void LivePreparationBridge::prepareAutomatic(
	PollingProject& project,
	Simulation& simulation,
	SimulationRun& run)
{
	translateLivePreparationException([&] {
		LivePreparation preparation(project, simulation, run);
		preparation.prepareLiveAutomatic();
	});
}
