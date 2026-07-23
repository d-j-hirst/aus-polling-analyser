#include "LivePreparationBridge.h"

namespace {
	[[noreturn]] void rejectLivePreparation()
	{
		throw LivePreparationBridge::Exception(
			"Live simulations are not supported by the command-line forecast "
			"runner.");
	}
}

void LivePreparationBridge::validateAutomaticSetup(
	PollingProject const&,
	Simulation const&)
{
	rejectLivePreparation();
}

void LivePreparationBridge::prepareAutomatic(
	PollingProject&,
	Simulation&,
	SimulationRun&)
{
	rejectLivePreparation();
}
