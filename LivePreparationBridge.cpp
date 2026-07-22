#include "LivePreparationBridge.h"

#include "LivePreparation.h"

namespace {
	template <typename Operation>
	void translateLivePreparationException(Operation&& operation)
	{
		try {
			operation();
		}
		catch (LivePreparation::Exception const& error) {
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
